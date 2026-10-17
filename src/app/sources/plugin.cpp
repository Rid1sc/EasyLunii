#include "app/sources/plugin.h"

#include "core/convert.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>

QStringList PluginInfo::searchPaths()
{
    const QString app = QCoreApplication::applicationDirPath();
    return {app + "/plugins", QDir::cleanPath(app + "/../share/easylunii/plugins"),
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugins"};
}

PluginInfo PluginInfo::load(const QString &dir, QString *error)
{
    PluginInfo p;
    QFile f(dir + "/plugin.json");
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("plugin.json introuvable");
        return {};
    }
    QJsonParseError pe;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll(), &pe).object();
    if (pe.error != QJsonParseError::NoError || o.value("id").toString().isEmpty()) {
        if (error)
            *error = QObject::tr("plugin.json invalide : %1").arg(pe.errorString());
        return {};
    }
    p.id = o.value("id").toString();
    p.name = o.value("name").toString(p.id);
    p.version = o.value("version").toString();
    p.description = o.value("description").toString();
    p.dir = dir;
    QString exe = o.value("executable").toString();
#ifdef Q_OS_WIN
    if (!exe.endsWith(".exe", Qt::CaseInsensitive) && !exe.endsWith(".bat", Qt::CaseInsensitive))
        exe += ".exe";
#endif
    p.executable = QDir(dir).absoluteFilePath(exe);
    p.search = o.value("search").toBool();
    p.link = o.value("link").toBool();
    p.searchHint = o.value("searchHint").toString();
    p.linkHint = o.value("linkHint").toString();
    if (o.contains("icon"))
        p.icon = QDir(dir).absoluteFilePath(o.value("icon").toString());
    if (exe.isEmpty() || !QFileInfo(p.executable).isExecutable()) {
        if (error)
            *error = QObject::tr("exécutable introuvable : %1").arg(p.executable);
        return {};
    }
    return p;
}

QList<PluginInfo> PluginInfo::discover()
{
    QList<PluginInfo> out;
    QStringList seen;
    for (const QString &root : searchPaths()) {
        for (const QFileInfo &d : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const PluginInfo p = load(d.absoluteFilePath());
            if (!p.id.isEmpty() && !seen.contains(p.id)) {   // le premier trouvé l'emporte
                seen << p.id;
                out << p;
            }
        }
    }
    return out;
}

SourceItem SourceItem::fromJson(const QJsonObject &o)
{
    SourceItem i;
    i.raw = o;
    i.id = o.value("id").toString();
    i.kind = o.value("kind").toString("audio");
    i.title = o.value("title").toString();
    i.subtitle = o.value("subtitle").toString();
    i.description = o.value("description").toString();
    i.image = o.value("image").toString();
    i.url = o.value("url").toString();
    i.audioUrl = o.value("audioUrl").toString();
    i.date = o.value("date").toString();
    i.duration = o.value("duration").toInt();
    const QJsonObject g = o.value("group").toObject();
    i.groupTitle = g.value("title").toString();
    i.groupPart = g.value("part").toInt();
    return i;
}

QString PluginRunner::cacheDir(const QString &pluginId)
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/sources/" + pluginId;
}

QString PluginRunner::dataDir(const QString &pluginId)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/plugin-data/" + pluginId;
}

QJsonObject PluginRunner::call(const QString &command, const QJsonObject &args, const Progress &progress,
                               const QAtomicInt *cancel)
{
    m_error.clear();
    QProcess p;
    p.setWorkingDirectory(m_p.dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString data = dataDir(m_p.id);
    QDir().mkpath(data);
    env.insert("EASYLUNII_PLUGIN_DATA", data);
    env.insert("EASYLUNII_FFMPEG", lunii::ffmpegPath());
    env.insert("EASYLUNII_VERSION", QCoreApplication::applicationVersion());
    p.setProcessEnvironment(env);
    p.start(m_p.executable, {command});
    if (!p.waitForStarted(10000)) {
        m_error = QObject::tr("Impossible de lancer le plugin %1 : %2").arg(m_p.name, p.errorString());
        return {};
    }
    p.write(QJsonDocument(args).toJson(QJsonDocument::Compact) + "\n");
    p.closeWriteChannel();

    QJsonObject result;
    QByteArray buffer;
    bool done = false;
    auto parseLines = [&]() {
        int nl;
        while ((nl = buffer.indexOf('\n')) >= 0) {
            const QByteArray line = buffer.left(nl).trimmed();
            buffer.remove(0, nl + 1);
            if (line.isEmpty())
                continue;
            const QJsonObject o = QJsonDocument::fromJson(line).object();
            if (o.contains("result")) {
                result = o.value("result").toObject();
                done = true;
            } else if (o.contains("error")) {
                m_error = o.value("error").toString();
                done = true;
            } else if (o.contains("progress") || o.contains("message")) {
                if (progress)
                    progress(o.value("progress").toDouble(-1), o.value("message").toString());
            }
        }
    };
    while (p.state() != QProcess::NotRunning) {
        if (cancel && cancel->loadRelaxed()) {
            p.kill();
            p.waitForFinished(3000);
            m_error = QObject::tr("Annulé");
            return {};
        }
        p.waitForReadyRead(200);
        buffer += p.readAllStandardOutput();
        parseLines();
    }
    buffer += p.readAllStandardOutput() + "\n";
    parseLines();
    if (!done && m_error.isEmpty()) {
        const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
        m_error = QObject::tr("Le plugin %1 n'a pas répondu%2").arg(m_p.name, err.isEmpty() ? QString() : " : " + err.right(400));
    }
    return m_error.isEmpty() ? result : QJsonObject();
}

static QList<SourceItem> itemsOf(const QJsonObject &r)
{
    QList<SourceItem> out;
    for (const QJsonValue &v : r.value("items").toArray())
        out << SourceItem::fromJson(v.toObject());
    return out;
}

QList<SourceItem> PluginRunner::search(const QString &query)
{
    return itemsOf(call("search", {{"query", query}}));
}

QList<SourceItem> PluginRunner::open(const QString &url, QString *title)
{
    const QJsonObject r = call("open", {{"url", url}});
    if (title)
        *title = r.value("title").toString();
    return itemsOf(r);
}

FetchedAudio PluginRunner::fetch(const SourceItem &item, const Progress &progress, const QAtomicInt *cancel)
{
    m_error.clear();
    const QString cache = cacheDir(m_p.id);
    const QString key = QCryptographicHash::hash(item.id.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString metaPath = cache + "/" + key + ".json";
    // déjà en cache ?
    QFile meta(metaPath);
    if (meta.open(QIODevice::ReadOnly)) {
        const QJsonObject m = QJsonDocument::fromJson(meta.readAll()).object();
        FetchedAudio f{cache + "/" + m.value("audio").toString(), m.value("image").toString(), m.value("title").toString()};
        if (!f.image.isEmpty())
            f.image = cache + "/" + f.image;
        if (QFileInfo(f.audio).isFile())
            return f;
        meta.close();
    }
    QDir().mkpath(cache);
    QTemporaryDir tmp(cache + "/fetch-XXXXXX");
    const QJsonObject r = call("fetch", {{"item", item.raw}, {"dir", tmp.path()}}, progress, cancel);
    if (r.isEmpty()) {
        if (m_error.isEmpty())
            m_error = QObject::tr("Réponse vide du plugin");
        return {};
    }
    const QString audio = r.value("audio").toString();
    if (!QFileInfo(audio).isFile()) {
        m_error = QObject::tr("Le plugin n'a pas fourni de fichier audio");
        return {};
    }
    // rangement dans le cache : <clé>.<ext>
    auto keep = [&](const QString &src, const QString &suffix) -> QString {
        if (src.isEmpty() || !QFileInfo(src).isFile())
            return {};
        const QString name = key + suffix + "." + QFileInfo(src).suffix().toLower();
        QFile::remove(cache + "/" + name);
        return QFile::rename(src, cache + "/" + name) || QFile::copy(src, cache + "/" + name) ? name : QString();
    };
    const QString a = keep(audio, ""), img = keep(r.value("image").toString(), "-image");
    if (a.isEmpty()) {
        m_error = QObject::tr("Impossible de ranger le fichier dans le cache");
        return {};
    }
    const QString title = r.value("title").toString(item.title);
    if (meta.open(QIODevice::WriteOnly | QIODevice::Truncate))
        meta.write(QJsonDocument(QJsonObject{{"audio", a}, {"image", img}, {"title", title}, {"id", item.id}}).toJson());
    return {cache + "/" + a, img.isEmpty() ? QString() : cache + "/" + img, title};
}
