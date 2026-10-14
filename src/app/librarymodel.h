// Modèle, filtre et rendu de la grille de la bibliothèque.
#pragma once

#include <QAbstractListModel>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>

class Library;

namespace mime {
inline const char *libraryPaths = "application/x-easylunii-library";   // chemins d'archives, un par ligne
inline const char *deviceStories = "application/x-easylunii-device";   // UUID d'histoires de la Lunii
}

class LibraryModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        PathRole = Qt::UserRole + 1, UuidRole, KeyRole, AgeRole, AgeValueRole, OfficialRole, OnDeviceRole,
        SizeRole, DurationRole, TypeRole, TagsRole, AddedRole, SourceRole, DescriptionRole, AuthorRole,
        NightRole, ThumbRole, NodesRole
    };
    explicit LibraryModel(Library *lib, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;

    void setDeviceUuids(const QSet<QString> &uuids);
    const QSet<QString> &deviceUuids() const { return m_onDevice; }

private:
    void rebuild();
    Library *m_lib;
    QVector<int> m_rows;          // index dans Library::items() des archives qui sont des histoires
    QSet<QString> m_onDevice;
};

class LibraryFilter : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    enum Scope { All, Official, Custom, OnDevice, NotOnDevice, Source, CollectionScope, Tag };
    enum Sort { ByTitle, ByAge, ByAdded, BySize, ByDuration };

    explicit LibraryFilter(Library *lib, QObject *parent = nullptr);
    void setText(const QString &t);
    void setMaxAge(int age);                      // -1 = tous
    void setScope(Scope s, const QVariant &arg = {});
    void setSortKey(Sort s);

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
    bool lessThan(const QModelIndex &a, const QModelIndex &b) const override;

private:
    Library *m_lib;
    QString m_text;
    int m_maxAge = -1;
    Scope m_scope = All;
    QVariant m_arg;
    Sort m_sort = ByTitle;
};

class LibraryDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void setGrid(bool grid) { m_grid = grid; }
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;
    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &idx) const override;

    static QPixmap thumbnail(const QString &file, int side);

private:
    bool m_grid = true;
};

QString formatDuration(qint64 sec);
QString formatSize(qint64 bytes);
