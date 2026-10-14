#include "app/library.h"

#include "core/packinfo.h"
#include "core/storydb.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QRegularExpression>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QtConcurrent>

int LibraryItem::ageValue() const
{
    const auto m = QRegularExpression(R"(^(\d+))").match(age);
    return m.hasMatch() ? m.captured(1).toInt() : 99;
}

QString Library::dataDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/lunii-natif";
    QDir().mkpath(d);
    return d;
}

QString Library::thumbDir()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/lunii-natif/thumbs";
    QDir().mkpath(d);
    return d;
}

Library::Library(QObject *parent) : QObject(parent)
{
    openDb();
    load();
    m_sources = QSettings().value("library/sources").toStringList();
    // les modifications de dossiers arrivent souvent en rafale (copie de plusieurs fichiers)
    m_rescanDelay.setSingleShot(true);
    m_rescanDelay.setInterval(1500);
    connect(&m_rescanDelay, &QTimer::timeout, this, &Library::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_rescanDelay, qOverload<>(&QTimer::start));
    updateWatcher();
}

Library::~Library()
{
    m_db.close();
}

void Library::openDb()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "library");
    m_db.setDatabaseName(dataDir() + "/library.sqlite");
    if (!m_db.open())
        qWarning("Bibliothèque : %s", qPrintable(m_db.lastError().text()));
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec("CREATE TABLE IF NOT EXISTS packs (path TEXT PRIMARY KEY, source TEXT, mtime INTEGER, size INTEGER,"
           " type INTEGER, uuid TEXT, title TEXT, description TEXT, author TEXT, age TEXT, official INTEGER,"
           " night INTEGER, nodes INTEGER, duration INTEGER, thumb TEXT, added INTEGER)");
    q.exec("CREATE TABLE IF NOT EXISTS collections (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE)");
    q.exec("CREATE TABLE IF NOT EXISTS collection_items (collection INTEGER, key TEXT, PRIMARY KEY(collection, key))");
    q.exec("CREATE TABLE IF NOT EXISTS tags (key TEXT, tag TEXT, PRIMARY KEY(key, tag))");
}

void Library::load()
{
    m_items.clear();
    QSqlQuery q("SELECT path, source, mtime, size, type, uuid, title, description, author, age, official, night,"
                " nodes, duration, thumb, added FROM packs",
                m_db);
    while (q.next()) {
        LibraryItem it;
        it.path = q.value(0).toString();
        it.source = q.value(1).toString();
        it.mtime = q.value(2).toLongLong();
        it.size = q.value(3).toLongLong();
        it.type = q.value(4).toInt();
        it.uuid = q.value(5).toString();
        it.title = q.value(6).toString();
        it.description = q.value(7).toString();
        it.author = q.value(8).toString();
        it.age = q.value(9).toString();
        it.official = q.value(10).toBool();
        it.nightMode = q.value(11).toBool();
        it.nodes = q.value(12).toInt();
        it.durationSec = q.value(13).toLongLong();
        it.thumb = q.value(14).toString();
        it.added = q.value(15).toLongLong();
        m_items << it;
    }
    m_tags.clear();
    QSqlQuery t("SELECT key, tag FROM tags ORDER BY tag", m_db);
    while (t.next())
        m_tags[t.value(0).toString()] << t.value(1).toString();
    for (LibraryItem &it : m_items)
        it.tags = m_tags.value(it.key());

    m_collections.clear();
    m_collectionItems.clear();
    QSqlQuery c("SELECT id, name FROM collections ORDER BY name COLLATE NOCASE", m_db);
    while (c.next())
        m_collections << Collection{c.value(0).toInt(), c.value(1).toString()};
    QSqlQuery ci("SELECT collection, key FROM collection_items", m_db);
    while (ci.next())
        m_collectionItems[ci.value(0).toInt()].insert(ci.value(1).toString());
}

void Library::store(const LibraryItem &it)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO packs VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const QVariant &v : {QVariant(it.path), QVariant(it.source), QVariant(it.mtime), QVariant(it.size),
                              QVariant(it.type), QVariant(it.uuid), QVariant(it.title), QVariant(it.description),
                              QVariant(it.author), QVariant(it.age), QVariant(it.official), QVariant(it.nightMode),
                              QVariant(it.nodes), QVariant(it.durationSec), QVariant(it.thumb), QVariant(it.added)})
        q.addBindValue(v);
    q.exec();
}

void Library::addSource(const QString &dir)
{
    const QString d = QDir(dir).absolutePath();
    if (m_sources.contains(d))
        return;
    m_sources << d;
    QSettings().setValue("library/sources", m_sources);
    updateWatcher();
    emit sourcesChanged();
    rescan();
}

void Library::removeSource(const QString &dir)
{
    if (!m_sources.removeOne(dir))
        return;
    QSettings().setValue("library/sources", m_sources);
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM packs WHERE source = ?");
    q.addBindValue(dir);
    q.exec();
    m_items.erase(std::remove_if(m_items.begin(), m_items.end(), [&](const LibraryItem &i) { return i.source == dir; }),
                  m_items.end());
    updateWatcher();
    emit sourcesChanged();
    emit itemsChanged();
}

void Library::updateWatcher()
{
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    // surveillance des dossiers et de leurs sous-dossiers (peu nombreux en pratique)
    QStringList dirs;
    for (const QString &s : m_sources) {
        dirs << s;
        QDirIterator it(s, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext() && dirs.size() < 2000)
            dirs << it.next();
    }
    if (!dirs.isEmpty())
        m_watcher.addPaths(dirs);
}

int Library::indexOfPath(const QString &path) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].path == path)
            return i;
    return -1;
}

int Library::indexOfUuid(const QString &uuid) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (!uuid.isEmpty() && m_items[i].uuid == uuid)
            return i;
    return -1;
}

// ------------------------------------------------------------------ analyse

struct ScanResult {
    QVector<LibraryItem> found;
    QStringList removed;
};

static QString makeThumb(const QString &path, const lunii::PackInfo &pi)
{
    QByteArray data;
    if (pi.official && !pi.uuid.isNull())
        data = lunii::StoryDb::instance().picture(pi.uuid, true);   // jaquette couleur officielle
    if (data.isEmpty())
        data = pi.cover;
    if (data.isEmpty())
        data = lunii::StoryDb::instance().picture(pi.uuid, false);  // vignette déposée par un import STUdio
    QImage img;
    if (data.isEmpty() || !img.loadFromData(data))
        return {};
    img = img.scaled(320, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QString name = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex() + ".png";
    const QString file = Library::thumbDir() + "/" + name;
    return img.save(file, "PNG") ? file : QString();
}

void Library::rescan()
{
    if (m_scanning) {
        m_rescanPending = true;
        return;
    }
    m_scanning = true;
    QHash<QString, QPair<qint64, qint64>> known;
    for (const LibraryItem &it : m_items)
        known.insert(it.path, {it.mtime, it.size});
    const QStringList sources = m_sources;

    auto *w = new QFutureWatcher<ScanResult>(this);
    connect(w, &QFutureWatcher<ScanResult>::finished, this, [this, w]() {
        const ScanResult r = w->result();
        w->deleteLater();
        applyScan(r.found, r.removed);
        m_scanning = false;
        emit scanFinished();
        if (m_rescanPending) {
            m_rescanPending = false;
            rescan();
        }
    });
    w->setFuture(QtConcurrent::run([this, known, sources]() {
        ScanResult r;
        QSet<QString> seen;
        QList<QPair<QString, QString>> todo;   // chemin, source
        for (const QString &src : sources) {
            QDirIterator it(src, {"*.zip", "*.7z", "*.pk"}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString p = it.next();
                seen.insert(p);
                const QFileInfo fi(p);
                const auto k = known.constFind(p);
                if (k == known.constEnd() || k->first != fi.lastModified().toSecsSinceEpoch() || k->second != fi.size())
                    todo << qMakePair(p, src);
            }
        }
        for (auto it = known.constBegin(); it != known.constEnd(); ++it)
            if (!seen.contains(it.key()))
                r.removed << it.key();
        QVector<LibraryItem> batch;
        QElapsedTimer flush;
        flush.start();
        for (int i = 0; i < todo.size(); ++i) {
            QMetaObject::invokeMethod(this, [this, i, n = todo.size()]() { emit scanProgress(i, int(n)); },
                                      Qt::QueuedConnection);
            // résultats partiels : la grille se remplit pendant l'analyse
            if (!batch.isEmpty() && flush.elapsed() > 700) {
                QMetaObject::invokeMethod(this, [this, batch]() { applyScan(batch, {}); }, Qt::QueuedConnection);
                batch.clear();
                flush.restart();
            }
            const QString &p = todo[i].first;
            const QFileInfo fi(p);
            const lunii::PackInfo pi = lunii::inspectPack(p);
            LibraryItem it;
            it.path = p;
            it.source = todo[i].second;
            it.mtime = fi.lastModified().toSecsSinceEpoch();
            it.size = fi.size();
            it.added = QDateTime::currentSecsSinceEpoch();
            if (pi.valid) {
                it.type = int(pi.type);
                it.uuid = pi.uuid.isNull() ? QString() : lunii::uuidStr(pi.uuid);
                it.title = pi.title;
                it.description = pi.description;
                it.author = pi.author;
                it.age = pi.age;
                it.official = pi.official;
                it.nightMode = pi.nightMode;
                it.nodes = pi.nodeCount;
                it.durationSec = pi.durationSec;
                it.thumb = makeThumb(p, pi);
            }
            batch << it;   // les archives qui ne sont pas des histoires sont mémorisées (type 0) pour ne pas être relues
        }
        r.found = batch;
        return r;
    }));
}

void Library::applyScan(const QVector<LibraryItem> &found, const QStringList &removed)
{
    m_db.transaction();
    for (const QString &p : removed) {
        QSqlQuery q(m_db);
        q.prepare("DELETE FROM packs WHERE path = ?");
        q.addBindValue(p);
        q.exec();
        const int i = indexOfPath(p);
        if (i >= 0)
            m_items.removeAt(i);
    }
    for (LibraryItem it : found) {
        const int i = indexOfPath(it.path);
        if (i >= 0)
            it.added = m_items[i].added;
        it.tags = m_tags.value(it.key());
        store(it);
        if (i >= 0)
            m_items[i] = it;
        else
            m_items << it;
    }
    m_db.commit();
    if (!found.isEmpty() || !removed.isEmpty())
        emit itemsChanged();
}

// ------------------------------------------------------------------ collections et étiquettes

int Library::createCollection(const QString &name)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO collections (name) VALUES (?)");
    q.addBindValue(name);
    if (!q.exec())
        return 0;
    const int id = q.lastInsertId().toInt();
    m_collections << Collection{id, name};
    std::sort(m_collections.begin(), m_collections.end(),
              [](const Collection &a, const Collection &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
    emit collectionsChanged();
    return id;
}

void Library::renameCollection(int id, const QString &name)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE collections SET name = ? WHERE id = ?");
    q.addBindValue(name);
    q.addBindValue(id);
    if (!q.exec())
        return;
    for (Collection &c : m_collections)
        if (c.id == id)
            c.name = name;
    emit collectionsChanged();
}

void Library::deleteCollection(int id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM collections WHERE id = ?");
    q.addBindValue(id);
    q.exec();
    q.prepare("DELETE FROM collection_items WHERE collection = ?");
    q.addBindValue(id);
    q.exec();
    m_collections.erase(std::remove_if(m_collections.begin(), m_collections.end(),
                                       [id](const Collection &c) { return c.id == id; }),
                        m_collections.end());
    m_collectionItems.remove(id);
    emit collectionsChanged();
}

void Library::addToCollection(int id, const QStringList &keys)
{
    m_db.transaction();
    for (const QString &k : keys) {
        QSqlQuery q(m_db);
        q.prepare("INSERT OR IGNORE INTO collection_items VALUES (?, ?)");
        q.addBindValue(id);
        q.addBindValue(k);
        q.exec();
        m_collectionItems[id].insert(k);
    }
    m_db.commit();
    emit collectionsChanged();
}

void Library::removeFromCollection(int id, const QStringList &keys)
{
    m_db.transaction();
    for (const QString &k : keys) {
        QSqlQuery q(m_db);
        q.prepare("DELETE FROM collection_items WHERE collection = ? AND key = ?");
        q.addBindValue(id);
        q.addBindValue(k);
        q.exec();
        m_collectionItems[id].remove(k);
    }
    m_db.commit();
    emit collectionsChanged();
}

QStringList Library::allTags() const
{
    QSet<QString> s;
    for (const QStringList &l : m_tags)
        for (const QString &t : l)
            s.insert(t);
    QStringList out(s.begin(), s.end());
    out.sort(Qt::CaseInsensitive);
    return out;
}

void Library::setTags(const QString &key, const QStringList &tags)
{
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM tags WHERE key = ?");
    q.addBindValue(key);
    q.exec();
    for (const QString &t : tags) {
        QSqlQuery i(m_db);
        i.prepare("INSERT OR IGNORE INTO tags VALUES (?, ?)");
        i.addBindValue(key);
        i.addBindValue(t);
        i.exec();
    }
    m_db.commit();
    m_tags[key] = tags;
    for (LibraryItem &it : m_items)
        if (it.key() == key)
            it.tags = tags;
    emit itemsChanged();
}
