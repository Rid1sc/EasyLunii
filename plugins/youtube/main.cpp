// Plugin EasyLunii « YouTube » : recherche, playlists et son des vidéos, via yt-dlp.
// Autonome : yt-dlp et, si besoin, QuickJS sont téléchargés au premier usage depuis leurs publications GitHub
// officielles (empreinte SHA-256 vérifiée) dans le dossier de données du plugin.
// Protocole : voir PLUGINS.md. Usage : easylunii-youtube search|open|fetch, requête JSON sur l'entrée standard.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cstdio>
#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif

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

static void progress(double fraction, const QString &message)
{
    emitLine({{"progress", fraction}, {"message", message}});
}

static QNetworkAccessManager *net()
{
    static QNetworkAccessManager *n = new QNetworkAccessManager;
    return n;
}

static QByteArray get(const QUrl &url, QString *error, const QString &label = {})
{
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("EasyLunii-youtube-plugin"));
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(120000);
    QNetworkReply *reply = net()->get(r);
    QEventLoop loop;
    qint64 last = 0;
    if (!label.isEmpty())
        QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 got, qint64 total) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (total > 0 && now - last > 400) {
                last = now;
                progress(double(got) / double(total), QStringLiteral("%1 : %2 %").arg(label).arg(100 * got / total));
            }
        });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    QByteArray data;
    if (reply->error() != QNetworkReply::NoError)
        *error = QStringLiteral("%1 (%2)").arg(reply->errorString(), url.toString());
    else
        data = reply->readAll();
    reply->deleteLater();
    return data;
}

// ---------------------------------------------------------------- outils (yt-dlp, QuickJS)

static QString dataDir()
{
    QString d = qEnvironmentVariable("EASYLUNII_PLUGIN_DATA");
    if (d.isEmpty())
        d = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/easylunii/plugin-data/youtube";
    QDir().mkpath(d);
    return d;
}

// Télécharge le fichier « asset » de la dernière publication GitHub de « repo », empreinte SHA-256 vérifiée
static bool installFromGitHub(const QString &repo, const QString &asset, const QString &dest, const QString &label, QString *error)
{
    const QByteArray rel = get(QUrl("https://api.github.com/repos/" + repo + "/releases/latest"), error);
    if (!error->isEmpty())
        return false;
    QString url, digest;
    for (const QJsonValue &v : QJsonDocument::fromJson(rel).object().value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        if (a.value("name").toString() == asset) {
            url = a.value("browser_download_url").toString();
            digest = a.value("digest").toString();
        }
    }
    if (url.isEmpty() || !digest.startsWith("sha256:")) {
        *error = QStringLiteral("%1 : fichier %2 ou empreinte introuvable dans la publication").arg(repo, asset);
        return false;
    }
    const QByteArray bin = get(QUrl(url), error, label);
    if (!error->isEmpty())
        return false;
    if (QCryptographicHash::hash(bin, QCryptographicHash::Sha256).toHex() != digest.mid(7).toLatin1()) {
        *error = QStringLiteral("%1 : empreinte SHA-256 incorrecte, fichier refusé").arg(asset);
        return false;
    }
    QFile f(dest + ".part");
    if (!f.open(QIODevice::WriteOnly) || f.write(bin) != bin.size()) {
        *error = QStringLiteral("Écriture impossible : %1").arg(dest);
        return false;
    }
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    QFile::remove(dest);
    return QFile::rename(dest + ".part", dest);
}

static void touchStamp()
{
    QFile stamp(dataDir() + "/updated");
    if (stamp.open(QIODevice::WriteOnly))
        stamp.write(QDateTime::currentDateTime().toString(Qt::ISODate).toLatin1());
}

static QString ytdlp(QString *error)
{
#ifdef Q_OS_WIN
    const QString asset = "yt-dlp.exe", path = dataDir() + "/yt-dlp.exe";
#else
    const QString asset = "yt-dlp_linux", path = dataDir() + "/yt-dlp";
#endif
    if (!QFileInfo(path).isExecutable()) {
        progress(-1, QStringLiteral("Premier usage : installation de yt-dlp…"));
        if (!installFromGitHub("yt-dlp/yt-dlp", asset, path, "yt-dlp", error))
            return {};
        touchStamp();
    } else if (QFileInfo(dataDir() + "/updated").lastModified().daysTo(QDateTime::currentDateTime()) >= 7) {
        // YouTube change souvent : mise à jour hebdomadaire (la version autonome se met à jour elle-même)
        progress(-1, QStringLiteral("Mise à jour de yt-dlp…"));
        QProcess::execute(path, {"-U"});
        touchStamp();
    }
    return path;
}

// Moteur JavaScript demandé par yt-dlp pour YouTube : deno ou node 22+ s'ils sont installés, sinon QuickJS
static QStringList jsRuntimeArgs(QString *error)
{
    if (!QStandardPaths::findExecutable("deno").isEmpty())
        return {};   // activé par défaut dans yt-dlp
    const QString node = QStandardPaths::findExecutable("node");
    if (!node.isEmpty()) {
        QProcess p;
        p.start(node, {"--version"});
        p.waitForFinished(5000);
        const int major = QString::fromLatin1(p.readAllStandardOutput()).mid(1).section('.', 0, 0).toInt();
        if (major >= 22)
            return {"--js-runtimes", "node:" + node};
    }
#ifdef Q_OS_WIN
    const QString asset = "qjs-windows-x86_64.exe", path = dataDir() + "/qjs.exe";
#else
    const QString asset = "qjs-linux-x86_64", path = dataDir() + "/qjs";
#endif
    if (!QFileInfo(path).isExecutable()) {
        progress(-1, QStringLiteral("Premier usage : installation de QuickJS…"));
        if (!installFromGitHub("quickjs-ng/quickjs", asset, path, "QuickJS", error))
            return {};
    }
    return {"--js-runtimes", "quickjs:" + path};
}

// Lance yt-dlp ; chaque ligne de sortie standard passe par « onLine »
static bool runYtdlp(const QStringList &args, QByteArray *out, QString *error, const std::function<void(const QString &)> &onLine = {})
{
    const QString exe = ytdlp(error);
    if (exe.isEmpty())
        return false;
    QStringList all = jsRuntimeArgs(error);
    if (!error->isEmpty())
        return false;
    all << "--no-warnings" << "--ignore-config";
    // options personnelles : <données du plugin>/yt-dlp.conf, une ou plusieurs options par ligne
    // (ex. « --cookies-from-browser firefox » si YouTube demande une connexion)
    QFile conf(dataDir() + "/yt-dlp.conf");
    if (conf.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const QString &line : QString::fromUtf8(conf.readAll()).split('\n'))
            if (!line.trimmed().isEmpty() && !line.trimmed().startsWith('#'))
                all << QProcess::splitCommand(line.trimmed());
    all << args;
    QProcess p;
#ifdef Q_OS_LINUX
    // yt-dlp s'arrête avec le plugin (annulation dans EasyLunii)
    p.setChildProcessModifier([]() { prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif
    p.start(exe, all);
    if (!p.waitForStarted()) {
        *error = QStringLiteral("yt-dlp ne démarre pas : %1").arg(p.errorString());
        return false;
    }
    QByteArray buf;
    while (p.state() != QProcess::NotRunning) {
        p.waitForReadyRead(500);
        const QByteArray chunk = p.readAllStandardOutput();
        if (out)
            out->append(chunk);
        if (onLine) {
            buf += chunk;
            int nl;
            while ((nl = buf.indexOf('\n')) >= 0) {
                onLine(QString::fromUtf8(buf.left(nl)).trimmed());
                buf.remove(0, nl + 1);
            }
        }
    }
    if (out)
        out->append(p.readAllStandardOutput());
    if (p.exitCode() != 0) {
        const QStringList errs = QString::fromUtf8(p.readAllStandardError()).trimmed().split('\n');
        *error = errs.isEmpty() ? QStringLiteral("yt-dlp a échoué") : errs.last();
        if (error->contains("not a bot") || error->contains("Sign in to confirm") || error->contains("This video is not available"))
            *error = QStringLiteral("YouTube bloque le téléchargement depuis cette connexion (contrôle anti-robot). "
                                    "Réessayez plus tard, ou ajoutez la ligne « --cookies-from-browser firefox » "
                                    "(ou chrome…) dans %1 pour utiliser votre session YouTube. Détail : %2")
                         .arg(QDir::toNativeSeparators(dataDir() + "/yt-dlp.conf"), errs.last().left(160));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- éléments

// « Azuro et la sorcière, Pt. 1 : Le secret d'Azuro » -> série « Azuro et la sorcière », partie 1
static void seriesOf(const QString &title, QJsonObject &item)
{
    static const QRegularExpression pt(R"(^(.*?)[\s,:–—-]*\b(?:pt|part|partie|[ée]pisode|ep|chapitre)\.?\s*(\d{1,2})\b)",
                                       QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression frac(R"(^(.*?)[\s,:(–—-]*(\d{1,2})\s*/\s*\d{1,2}\)?)");
    QRegularExpressionMatch m = pt.match(title);
    if (!m.hasMatch())
        m = frac.match(title);
    if (m.hasMatch() && !m.captured(1).trimmed().isEmpty() && m.captured(2).toInt() > 0)
        item.insert("group", QJsonObject{{"title", m.captured(1).trimmed()}, {"part", m.captured(2).toInt()}});
}

static QJsonObject itemOf(const QJsonObject &e)
{
    const QString id = e.value("id").toString();
    QString url = e.value("webpage_url").toString(e.value("url").toString());
    if (!url.startsWith("http"))
        url = "https://www.youtube.com/watch?v=" + id;
    QJsonObject item{{"id", "youtube:" + id},
                     {"kind", "audio"},
                     {"title", e.value("title").toString()},
                     {"subtitle", e.value("channel").toString(e.value("uploader").toString())},
                     {"description", e.value("description").toString().left(800)},
                     {"duration", int(e.value("duration").toDouble())},
                     {"image", "https://i.ytimg.com/vi/" + id + "/hqdefault.jpg"},
                     {"url", url},
                     {"videoId", id}};
    const QString date = e.value("upload_date").toString();
    if (date.size() == 8)
        item.insert("date", date.left(4) + "-" + date.mid(4, 2) + "-" + date.mid(6, 2));
    seriesOf(item.value("title").toString(), item);
    return item;
}

static int search(const QJsonObject &req)
{
    QByteArray out;
    QString err;
    if (!runYtdlp({"--flat-playlist", "-J", "ytsearch25:" + req.value("query").toString()}, &out, &err))
        return fail(err);
    QJsonArray items;
    for (const QJsonValue &v : QJsonDocument::fromJson(out).object().value("entries").toArray())
        if (v.toObject().value("id").toString().size() == 11)   // vidéos seulement
            items.append(itemOf(v.toObject()));
    emitLine({{"result", QJsonObject{{"items", items}}}});
    return 0;
}

static int open(const QJsonObject &req)
{
    QByteArray out;
    QString err;
    if (!runYtdlp({"--flat-playlist", "-J", req.value("url").toString()}, &out, &err))
        return fail(err);
    const QJsonObject o = QJsonDocument::fromJson(out).object();
    QJsonArray items;
    if (o.value("_type").toString() == "playlist") {
        for (const QJsonValue &v : o.value("entries").toArray())
            if (!v.toObject().value("id").toString().isEmpty())
                items.append(itemOf(v.toObject()));
    } else {
        items.append(itemOf(o));
    }
    emitLine({{"result", QJsonObject{{"title", o.value("title").toString()}, {"items", items}}}});
    return 0;
}

// Miniature 16:9 avec bandes unies sur les côtés (jaquette carrée au centre) : on retire les bandes
static QImage withoutSideBars(const QImage &src)
{
    if (src.isNull() || src.width() <= src.height())
        return src;
    auto uniform = [&](int x) {
        const QColor ref = src.pixelColor(x, 0);
        for (int y = 0; y < src.height(); y += 4) {
            const QColor c = src.pixelColor(x, y);
            if (qAbs(c.red() - ref.red()) + qAbs(c.green() - ref.green()) + qAbs(c.blue() - ref.blue()) > 40)
                return false;
        }
        return true;
    };
    int left = 0, right = src.width() - 1;
    while (left < src.width() / 3 && uniform(left))
        ++left;
    while (right > src.width() * 2 / 3 && uniform(right))
        --right;
    return (left > 4 && right < src.width() - 5) ? src.copy(left, 0, right - left + 1, src.height()) : src;
}

static int fetch(const QJsonObject &req)
{
    const QJsonObject item = req.value("item").toObject();
    const QString dir = req.value("dir").toString();
    const QString url = item.value("url").toString();
    if (url.isEmpty() || dir.isEmpty())
        return fail(QStringLiteral("Requête incomplète"));
    QString err;
    static const QRegularExpression pct(R"(\[download\]\s+([\d.]+)%)");
    const QString title = item.value("title").toString();
    // son seul si YouTube le propose ; sinon (bridage fréquent) le meilleur format avec du son, dont EasyLunii
    // n'extrait que l'audio
    if (!runYtdlp({"-f", "bestaudio[ext=m4a]/bestaudio/best[acodec!=none]/best", "--no-playlist", "--newline", "-o",
                   dir + "/audio.%(ext)s", url},
                  nullptr, &err, [&](const QString &line) {
                      const auto m = pct.match(line);
                      if (m.hasMatch())
                          progress(m.captured(1).toDouble() / 100.0, QStringLiteral("%1 : %2 %").arg(title, m.captured(1)));
                  }))
        return fail(err);
    QString audio;
    for (const QFileInfo &f : QDir(dir).entryInfoList({"audio.*"}, QDir::Files))
        if (!f.fileName().endsWith(".part"))
            audio = f.absoluteFilePath();
    if (audio.isEmpty())
        return fail(QStringLiteral("yt-dlp n'a produit aucun fichier"));
    QJsonObject result{{"audio", audio}, {"title", title}};
    // jaquette : meilleure miniature disponible, bandes latérales retirées
    const QString vid = item.value("videoId").toString();
    for (const char *name : {"maxresdefault.jpg", "sddefault.jpg", "hqdefault.jpg"}) {
        QString ierr;
        const QByteArray img = get(QUrl("https://i.ytimg.com/vi/" + vid + "/" + name), &ierr);
        QImage im;
        if (!ierr.isEmpty() || !im.loadFromData(img))
            continue;
        const QString path = dir + "/image.jpg";
        if (withoutSideBars(im).save(path, "JPG", 92)) {
            result.insert("image", path);
            break;
        }
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
