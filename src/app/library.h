// Bibliothèque : dossiers surveillés, index SQLite, collections et étiquettes.
#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QTimer>
#include <QVector>

struct LibraryItem {
    QString path;
    QString source;          // dossier surveillé d'origine
    qint64 mtime = 0;
    qint64 size = 0;
    int type = 0;            // lunii::PackType
    QString uuid;            // canonique, majuscules ; vide si inconnu
    QString title;
    QString description;
    QString author;
    QString age;             // "3+"
    bool official = false;
    bool nightMode = false;
    int nodes = 0;
    qint64 durationSec = 0;
    QString thumb;           // vignette en cache (PNG)
    qint64 added = 0;
    QStringList tags;

    bool isPack() const { return type != 0; }
    QString key() const { return uuid.isEmpty() ? path : uuid; }   // identité pour collections/étiquettes
    int ageValue() const;
};

struct Collection {
    int id = 0;
    QString name;
};

class Library : public QObject
{
    Q_OBJECT
public:
    explicit Library(QObject *parent = nullptr);
    ~Library() override;

    static QString dataDir();
    static QString thumbDir();

    QStringList sources() const { return m_sources; }
    void addSource(const QString &dir);
    void removeSource(const QString &dir);

    const QVector<LibraryItem> &items() const { return m_items; }
    int indexOfPath(const QString &path) const;
    int indexOfUuid(const QString &uuid) const;

    QList<Collection> collections() const { return m_collections; }
    int createCollection(const QString &name);
    void renameCollection(int id, const QString &name);
    void deleteCollection(int id);
    QSet<QString> collectionKeys(int id) const { return m_collectionItems.value(id); }
    void addToCollection(int id, const QStringList &keys);
    void removeFromCollection(int id, const QStringList &keys);

    QStringList allTags() const;
    void setTags(const QString &key, const QStringList &tags);

    bool isScanning() const { return m_scanning; }

public slots:
    void rescan();

signals:
    void scanProgress(int done, int total);
    void scanFinished();
    void itemsChanged();         // ajout/suppression/modification d'éléments
    void collectionsChanged();
    void sourcesChanged();

private:
    void openDb();
    void load();
    void store(const LibraryItem &it);
    void updateWatcher();
    void applyScan(const QVector<LibraryItem> &found, const QStringList &removed);

    QSqlDatabase m_db;
    QStringList m_sources;
    QVector<LibraryItem> m_items;
    QList<Collection> m_collections;
    QHash<int, QSet<QString>> m_collectionItems;
    QHash<QString, QStringList> m_tags;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanDelay;
    bool m_scanning = false;
    bool m_rescanPending = false;
};
