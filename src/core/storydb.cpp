#include "core/storydb.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace lunii {

static const char *OFFICIAL_TOKEN_URL = "https://server-auth-prod.lunii.com/guest/create";
static const char *OFFICIAL_DB_URL = "https://server-data-prod.lunii.com/v2/packs";
static const char *PICTURE_BASE_URL = "https://storage.googleapis.com/lunii-data-prod";

QString configDir()
{
    return QDir::homePath() + "/.lunii-qt";
}

QString cacheDir()
{
    return configDir() + "/cache";
}

QString uuidStr(const QUuid &u)
{
    return u.toString(QUuid::WithoutBraces).toUpper();
}

QString shortUuid(const QUuid &u)
{
    return QString::fromLatin1(u.toRfc4122().toHex()).right(8).toUpper();
}

StoryDb &StoryDb::instance()
{
    static StoryDb db;
    return db;
}

// GET bloquant avec délai max
static QByteArray httpGet(const QString &url, const QList<QPair<QByteArray, QByteArray>> &headers,
                          int timeoutMs, int *status = nullptr)
{
    QNetworkAccessManager nam;
    QNetworkRequest req{QUrl(url)};
    for (const auto &h : headers)
        req.setRawHeader(h.first, h.second);
    QNetworkReply *rep = nam.get(req);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, rep, &QNetworkReply::abort);
    QObject::connect(rep, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    if (status)
        *status = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray body = rep->error() == QNetworkReply::NoError ? rep->readAll() : QByteArray();
    rep->deleteLater();
    return body;
}

static QHash<QString, QJsonObject> readDb(const QString &path)
{
    QHash<QString, QJsonObject> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        const QString u = o.value("uuid").toString().toUpper();
        if (!u.isEmpty())
            out.insert(u, o);
    }
    return out;
}

bool StoryDb::load(bool refreshOfficial)
{
    QDir().mkpath(configDir());
    const QString officialPath = configDir() + "/official.db";
    bool ok = true;

    if (refreshOfficial || !QFile::exists(officialPath)) {
        int st = 0;
        const QByteArray tok = httpGet(OFFICIAL_TOKEN_URL, {}, 30000, &st);
        const QString token = QJsonDocument::fromJson(tok).object().value("response").toObject()
                                  .value("token").toObject().value("server").toString();
        if (st == 200 && !token.isEmpty()) {
            const QByteArray db = httpGet(OFFICIAL_DB_URL,
                                          {{"Application-Sender", "luniistore_desktop"},
                                           {"Accept", "application/json"},
                                           {"X-AUTH-TOKEN", token.toUtf8()}},
                                          30000, &st);
            const QJsonObject resp = QJsonDocument::fromJson(db).object().value("response").toObject();
            if (st == 200 && !resp.isEmpty()) {
                QFile f(officialPath);
                if (f.open(QIODevice::WriteOnly))
                    f.write(QJsonDocument(resp).toJson(QJsonDocument::Compact));
            } else {
                ok = false;
            }
        } else {
            ok = false;
        }
    }

    auto official = readDb(officialPath);
    QMutexLocker l(&m_lock);
    m_official = std::move(official);
    l.unlock();
    loadThirdParty();
    return ok;
}

void StoryDb::loadThirdParty()
{
    const QString path = configDir() + "/third-party.db";
    if (!QFile::exists(path))
        QFile::copy(":/json/unofficial.json", path), QFile(path).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    auto tp = readDb(path);
    QMutexLocker l(&m_lock);
    m_thirdParty = std::move(tp);
}

QJsonObject StoryDb::entry(const QUuid &u, bool *official) const
{
    const QString k = uuidStr(u);
    QMutexLocker l(&m_lock);
    if (auto it = m_official.constFind(k); it != m_official.constEnd()) {
        if (official)
            *official = true;
        return *it;
    }
    if (official)
        *official = false;
    return m_thirdParty.value(k);
}

// Infos localisées : première langue de locales_available (comme Lunii.QT)
static QJsonObject localized(const QJsonObject &e)
{
    const QJsonObject locales = e.value("locales_available").toObject();
    const QJsonObject infos = e.value("localized_infos").toObject();
    if (locales.isEmpty() || infos.isEmpty())
        return {};
    return infos.value(locales.keys().first()).toObject();
}

bool StoryDb::isOfficial(const QUuid &u) const
{
    QMutexLocker l(&m_lock);
    return m_official.contains(uuidStr(u));
}

bool StoryDb::isKnown(const QUuid &u) const
{
    QMutexLocker l(&m_lock);
    const QString k = uuidStr(u);
    return m_official.contains(k) || m_thirdParty.contains(k);
}

QString StoryDb::title(const QUuid &u) const
{
    const QJsonObject e = entry(u);
    const QJsonObject loc = localized(e);
    if (!loc.isEmpty())
        return loc.value("title").toString();
    return e.value("title").toString();
}

QString StoryDb::description(const QUuid &u) const
{
    const QJsonObject e = entry(u);
    const QJsonObject loc = localized(e);
    QString d = loc.isEmpty() ? e.value("description").toString() : loc.value("description").toString();
    // retrait des balises HTML de la description officielle
    d.remove(QRegularExpression("<[^>]*>"));
    return d.trimmed();
}

QString StoryDb::author(const QUuid &u) const
{
    const QJsonObject e = entry(u);
    const QJsonObject authors = e.value("authors").toObject();
    if (!authors.isEmpty())
        return authors.value(authors.keys().first()).toObject().value("name").toString();
    return e.value("author").toString();
}

QString StoryDb::pictureUrl(const QUuid &u) const
{
    bool official = false;
    const QJsonObject e = entry(u, &official);
    if (!official)
        return {};
    const QString img = localized(e).value("image").toObject().value("image_url").toString();
    return img.isEmpty() ? QString() : QString(PICTURE_BASE_URL) + img;
}

QString StoryDb::ageLabel(const QUuid &u) const
{
    bool official = false;
    const QJsonObject e = entry(u, &official);
    if (!official)
        return {};
    const QJsonValue age = e.value("age_min");
    if (age.isDouble())
        return QString::number(age.toInt()) + "+";
    return {};
}

qint64 StoryDb::durationSec(const QUuid &u) const
{
    // champ "duration" de la base officielle, en millisecondes
    return qint64(entry(u).value("duration").toDouble()) / 1000;
}

QByteArray StoryDb::picture(const QUuid &u, bool download) const
{
    QDir().mkpath(cacheDir());
    const QString file = cacheDir() + "/" + uuidStr(u);
    QFile f(file);
    if (f.open(QIODevice::ReadOnly))
        return f.readAll();
    if (!download)
        return {};
    const QString url = pictureUrl(u);
    if (url.isEmpty())
        return {};
    int st = 0;
    const QByteArray data = httpGet(url, {}, 5000, &st);
    if (st == 200 && !data.isEmpty() && f.open(QIODevice::WriteOnly))
        f.write(data);
    return data;
}

void StoryDb::addThirdParty(const QUuid &u, const QString &title, const QString &desc)
{
    const QString path = configDir() + "/third-party.db";
    QJsonObject root;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
    }
    const QString key = QString::fromLatin1(u.toRfc4122().toHex());
    root.insert(key, QJsonObject{{"uuid", u.toString(QUuid::WithoutBraces)},
                                 {"title", title},
                                 {"description", desc}});
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    f.close();
    loadThirdParty();
}

void StoryDb::addThumb(const QUuid &u, const QByteArray &data)
{
    QDir().mkpath(cacheDir());
    QFile f(cacheDir() + "/" + uuidStr(u));
    if (!f.exists() && f.open(QIODevice::WriteOnly))
        f.write(data);
}

QByteArray StoryDb::thirdPartyMeta(const QUuid &u) const
{
    QMutexLocker l(&m_lock);
    const auto it = m_thirdParty.constFind(uuidStr(u));
    if (it == m_thirdParty.constEnd())
        return {};
    return QJsonDocument(*it).toJson(QJsonDocument::Compact);
}

QString StoryDb::findFullUuid(const QString &part) const
{
    const QString p = part.toUpper();
    QMutexLocker l(&m_lock);
    for (const auto *db : {&m_official, &m_thirdParty})
        for (auto it = db->constBegin(); it != db->constEnd(); ++it)
            if (it.key().contains(p))
                return it.key();
    return {};
}

} // namespace lunii
