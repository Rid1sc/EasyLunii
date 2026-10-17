// Plugin EasyLunii « Podcasts » : recherche (annuaire Apple Podcasts), flux RSS, téléchargement des épisodes.
// Protocole : voir PLUGINS.md. Usage : easylunii-podcast search|open|fetch, requête JSON sur l'entrée standard.
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <cstdio>

static void emitLine(const QJsonObject &o)
{
    const QByteArray line = QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n";
    fwrite(line.constData(), 1, size_t(line.size()), stdout);
    fflush(stdout);
}

static int fail(const QString &message)
{
    emitLine({{"error", message}});
    return 1;
}

static QNetworkAccessManager *net()
{
    static QNetworkAccessManager *n = new QNetworkAccessManager;
    return n;
}

static QNetworkRequest request(const QUrl &url)
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader,
                QStringLiteral("EasyLunii/%1 (podcast plugin)").arg(qEnvironmentVariable("EASYLUNII_VERSION", "1")));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(60000);
    return r;
}

// GET bloquant ; si « file » est fourni, écrit au fil de l'eau et signale la progression
static QByteArray get(const QUrl &url, QString *error, QFile *file = nullptr, const QString &label = {})
{
    QNetworkReply *r = net()->get(request(url));
    QEventLoop loop;
    QByteArray data;
    qint64 last = 0;
    if (file) {
        QObject::connect(r, &QNetworkReply::readyRead, [&]() { file->write(r->readAll()); });
        QObject::connect(r, &QNetworkReply::downloadProgress, [&](qint64 got, qint64 total) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (total > 0 && now - last > 300) {
                last = now;
                emitLine({{"progress", double(got) / double(total)},
                          {"message", QStringLiteral("%1 : %2 / %3 Mo").arg(label).arg(got / 1048576.0, 0, 'f', 1).arg(total / 1048576.0, 0, 'f', 1)}});
            }
        });
    }
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (r->error() != QNetworkReply::NoError) {
        *error = QStringLiteral("%1 (%2)").arg(r->errorString(), url.toString());
        r->deleteLater();
        return {};
    }
    if (file)
        file->write(r->readAll());
    else
        data = r->readAll();
    r->deleteLater();
    return data;
}

static QString plainText(QString html)
{
    html.replace(QRegularExpression("<br\\s*/?>|</p>", QRegularExpression::CaseInsensitiveOption), "\n");
    html.remove(QRegularExpression("<[^>]*>"));
    html.replace("&amp;", "&").replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&#39;", "'").replace("&nbsp;", " ");
    return html.trimmed();
}

static int parseDuration(const QString &s)
{
    const QStringList p = s.trimmed().split(':');
    int total = 0;
    for (const QString &x : p)
        total = total * 60 + x.toInt();
    return total;
}

// ---------------------------------------------------------------- recherche

static int search(const QJsonObject &req)
{
    QUrl url("https://itunes.apple.com/search");
    QUrlQuery q;
    q.addQueryItem("media", "podcast");
    q.addQueryItem("entity", "podcast");
    q.addQueryItem("country", "fr");
    q.addQueryItem("limit", "40");
    q.addQueryItem("term", req.value("query").toString());
    url.setQuery(q);
    QString err;
    const QByteArray d = get(url, &err);
    if (!err.isEmpty())
        return fail(err);
    QJsonArray items;
    for (const QJsonValue &v : QJsonDocument::fromJson(d).object().value("results").toArray()) {
        const QJsonObject r = v.toObject();
        const QString feed = r.value("feedUrl").toString();
        QString subtitle = r.value("artistName").toString();
        if (feed.isEmpty())
            subtitle += QStringLiteral(" — flux non publié dans l'annuaire : collez le lien de la page de l'émission");
        items.append(QJsonObject{{"id", feed.isEmpty() ? "itunes:" + QString::number(r.value("collectionId").toInteger()) : feed},
                                 {"kind", "collection"},
                                 {"title", r.value("collectionName").toString()},
                                 {"subtitle", subtitle},
                                 {"description", QStringLiteral("%1 — %2 épisodes").arg(r.value("primaryGenreName").toString()).arg(r.value("trackCount").toInt())},
                                 {"image", r.value("artworkUrl600").toString()},
                                 {"url", feed}});
    }
    emitLine({{"result", QJsonObject{{"items", items}}}});
    return 0;
}

// ---------------------------------------------------------------- flux RSS

// Série annoncée dans le titre : « … (2/4) », « Épisode 2/4 : … », « … 2 sur 4 »
static void seriesOf(const QString &title, QJsonObject &item)
{
    static const QRegularExpression re(R"((?:[ée]pisode|partie|ep\.?|chapitre)?\s*(\d{1,2})\s*(?:/|sur)\s*(\d{1,2}))",
                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(title);
    if (!m.hasMatch() || m.captured(1).toInt() < 1 || m.captured(1).toInt() > m.captured(2).toInt())
        return;
    QString base = QString(title).remove(m.capturedStart(), m.capturedLength());
    base.remove(QRegularExpression(R"(\(\s*\)|\[\s*\])"));
    base = base.trimmed();
    base.remove(QRegularExpression(R"(^[\s:,–—-]+|[\s:,–—-]+$)"));
    item.insert("group", QJsonObject{{"title", base.trimmed()}, {"part", m.captured(1).toInt()}});
}

static QJsonObject parseFeed(const QByteArray &xml, QString *error)
{
    QXmlStreamReader x(xml);
    QString channelTitle, channelImage, channelDesc;
    QJsonArray items;
    QJsonObject item;
    bool inItem = false, inChannelImage = false;
    while (!x.atEnd()) {
        x.readNext();
        const QString name = x.qualifiedName().toString();
        if (x.isStartElement()) {
            if (name == "item" || name == "entry") {
                inItem = true;
                item = {{"kind", "audio"}};
            } else if (name == "image" && !inItem) {
                inChannelImage = true;
            } else if (name == "itunes:image") {
                const QString href = x.attributes().value("href").toString();
                if (inItem)
                    item.insert("image", href);
                else
                    channelImage = href;   // prioritaire sur <image><url>
            } else if (name == "url" && inChannelImage && channelImage.isEmpty()) {
                channelImage = x.readElementText().trimmed();
            } else if (name == "enclosure" && inItem) {
                item.insert("audioUrl", x.attributes().value("url").toString());
                item.insert("mime", x.attributes().value("type").toString());
            } else if (name == "title") {
                const QString t = x.readElementText().trimmed();
                if (inItem)
                    item.insert("title", t);
                else if (channelTitle.isEmpty() && !inChannelImage)
                    channelTitle = t;
            } else if (name == "itunes:title" && inItem) {
                item.insert("itunesTitle", x.readElementText().trimmed());
            } else if (name == "guid" || (name == "id" && inItem)) {
                if (inItem)
                    item.insert("id", x.readElementText().trimmed());
            } else if (name == "pubDate" && inItem) {
                const QDateTime d = QDateTime::fromString(x.readElementText().trimmed(), Qt::RFC2822Date);
                if (d.isValid())
                    item.insert("date", d.toString(Qt::ISODate));
            } else if (name == "itunes:duration" && inItem) {
                item.insert("duration", parseDuration(x.readElementText()));
            } else if ((name == "description" || name == "itunes:summary" || name == "content:encoded") && inItem) {
                const QString d = plainText(x.readElementText());
                if (!item.contains("description") || d.size() > item.value("description").toString().size())
                    item.insert("description", d);
            } else if (name == "description" && !inItem && channelDesc.isEmpty()) {
                channelDesc = plainText(x.readElementText());
            }
        } else if (x.isEndElement()) {
            if ((name == "item" || name == "entry") && inItem) {
                inItem = false;
                if (item.value("audioUrl").toString().isEmpty())
                    continue;   // pas un épisode audio
                if (item.value("id").toString().isEmpty())
                    item.insert("id", item.value("audioUrl"));
                if (item.contains("itunesTitle"))
                    item.insert("title", item.take("itunesTitle"));
                if (!item.contains("image"))
                    item.insert("image", channelImage);
                // durée absente : « durée : 00:08:38 » en tête de description (Radio France)
                static const QRegularExpression dur(R"(dur[ée]e\s*:\s*(\d+:\d{2}(?::\d{2})?))", QRegularExpression::CaseInsensitiveOption);
                if (item.value("duration").toInt() == 0) {
                    const auto m = dur.match(item.value("description").toString());
                    if (m.hasMatch())
                        item.insert("duration", parseDuration(m.captured(1)));
                }
                item.insert("subtitle", channelTitle);
                seriesOf(item.value("title").toString(), item);
                items.append(item);
            } else if (name == "image") {
                inChannelImage = false;
            }
        }
    }
    if (x.hasError() && items.isEmpty()) {
        *error = QStringLiteral("Flux illisible : %1").arg(x.errorString());
        return {};
    }
    return {{"title", channelTitle}, {"image", channelImage}, {"description", channelDesc}, {"items", items}};
}

// Lien d'une page : <link rel="alternate" type="application/rss+xml" href="…">
static QString discoverFeed(const QByteArray &html, const QUrl &base)
{
    static const QRegularExpression link(R"(<link\b[^>]*>)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression href(R"(href\s*=\s*["']([^"']+)["'])", QRegularExpression::CaseInsensitiveOption);
    auto it = link.globalMatch(QString::fromUtf8(html));
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        if (!tag.contains("application/rss+xml", Qt::CaseInsensitive) && !tag.contains("application/atom+xml", Qt::CaseInsensitive))
            continue;
        const auto h = href.match(tag);
        if (h.hasMatch())
            return base.resolved(QUrl(h.captured(1).replace("&amp;", "&"))).toString();
    }
    return {};
}

static int open(const QJsonObject &req)
{
    QString url = req.value("url").toString().trimmed();
    if (url.isEmpty())
        return fail(QStringLiteral("Le flux de ce podcast n'est pas publié dans l'annuaire : ouvrez la page de l'émission "
                                   "sur le site de la radio et collez son adresse."));
    if (url.startsWith("www."))
        url.prepend("https://");
    QString err;
    // résultat de recherche (« itunes:123456 ») ou page Apple Podcasts (…/id123456) : adresse du flux par l'annuaire
    static const QRegularExpression apple(R"(^itunes:(\d+)$|podcasts\.apple\.com/.*/id(\d+))");
    if (const auto m = apple.match(url); m.hasMatch()) {
        const QString id = m.captured(1).isEmpty() ? m.captured(2) : m.captured(1);
        const QByteArray d = get(QUrl("https://itunes.apple.com/lookup?id=" + id), &err);
        if (!err.isEmpty())
            return fail(err);
        const QJsonObject r = QJsonDocument::fromJson(d).object().value("results").toArray().first().toObject();
        const QString feed = r.value("feedUrl").toString();
        if (feed.isEmpty())
            return fail(QStringLiteral("« %1 » (%2) ne publie pas son flux dans l'annuaire d'Apple. Ouvrez la page de l'émission "
                                       "sur le site de la radio (pour Radio France : radiofrance.fr, page du podcast), "
                                       "copiez son adresse et collez-la ici.")
                            .arg(r.value("collectionName").toString(), r.value("artistName").toString()));
        url = feed;
    }
    QByteArray d = get(QUrl(url), &err);
    if (!err.isEmpty())
        return fail(err);
    if (!d.left(2000).contains("<rss") && !d.left(2000).contains("<feed")) {
        const QString feed = discoverFeed(d, QUrl(url));
        if (feed.isEmpty())
            return fail(QStringLiteral("Aucun flux de podcast trouvé sur cette page."));
        d = get(QUrl(feed), &err);
        if (!err.isEmpty())
            return fail(err);
    }
    const QJsonObject r = parseFeed(d, &err);
    if (!err.isEmpty())
        return fail(err);
    emitLine({{"result", r}});
    return 0;
}

// ---------------------------------------------------------------- téléchargement

static QString extensionOf(const QString &url, const QString &mime)
{
    const QString suffix = QFileInfo(QUrl(url).path()).suffix().toLower();
    if (QStringList{"mp3", "m4a", "aac", "ogg", "oga", "opus", "wav", "flac", "mp4", "m4b"}.contains(suffix))
        return suffix;
    if (mime.contains("mpeg"))
        return "mp3";
    if (mime.contains("m4a") || mime.contains("mp4") || mime.contains("aac"))
        return "m4a";
    if (mime.contains("ogg"))
        return "ogg";
    return "mp3";
}

static int fetch(const QJsonObject &req)
{
    const QJsonObject item = req.value("item").toObject();
    const QString dir = req.value("dir").toString();
    const QString audioUrl = item.value("audioUrl").toString();
    if (audioUrl.isEmpty() || dir.isEmpty())
        return fail(QStringLiteral("Requête incomplète"));
    QString err;
    const QString audio = dir + "/audio." + extensionOf(audioUrl, item.value("mime").toString());
    QFile f(audio);
    if (!f.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("Écriture impossible : %1").arg(audio));
    get(QUrl(audioUrl), &err, &f, item.value("title").toString());
    f.close();
    if (!err.isEmpty()) {
        QFile::remove(audio);
        return fail(err);
    }
    QJsonObject result{{"audio", audio}, {"title", item.value("title").toString()}};
    const QString imageUrl = item.value("image").toString();
    if (!imageUrl.isEmpty()) {
        QString ierr;
        const QByteArray img = get(QUrl(imageUrl), &ierr);
        const QString path = dir + "/image.jpg";
        QFile fi(path);
        if (ierr.isEmpty() && !img.isEmpty() && fi.open(QIODevice::WriteOnly) && fi.write(img) == img.size())
            result.insert("image", path);
    }
    emitLine({{"result", result}});
    return 0;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2)
        return fail(QStringLiteral("Commande attendue : search, open ou fetch"));
    QFile in;
    if (!in.open(stdin, QIODevice::ReadOnly))
        return fail(QStringLiteral("Entrée standard illisible"));
    const QJsonObject req = QJsonDocument::fromJson(in.readAll()).object();
    const QString cmd = QString::fromLocal8Bit(argv[1]);
    if (cmd == "search")
        return search(req);
    if (cmd == "open")
        return open(req);
    if (cmd == "fetch")
        return fetch(req);
    return fail(QStringLiteral("Commande inconnue : %1").arg(cmd));
}
