#include "app/librarymodel.h"

#include "app/library.h"

#include <QApplication>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QUrl>

QString formatDuration(qint64 sec)
{
    if (sec <= 0)
        return {};
    if (sec >= 3600)
        return QString("%1 h %2").arg(sec / 3600).arg((sec % 3600) / 60, 2, 10, QChar('0'));
    return QString("%1 min").arg(qMax<qint64>(1, sec / 60));
}

QString formatSize(qint64 b)
{
    if (b >= 1024 * 1024 * 1024)
        return QString::number(b / 1024.0 / 1024 / 1024, 'f', 1) + " Go";
    if (b >= 1024 * 1024)
        return QString::number(b / 1024.0 / 1024, 'f', 0) + " Mo";
    return QString::number(qMax<qint64>(1, b / 1024)) + " Ko";
}

// ------------------------------------------------------------------ modèle

LibraryModel::LibraryModel(Library *lib, QObject *parent) : QAbstractListModel(parent), m_lib(lib)
{
    connect(lib, &Library::itemsChanged, this, &LibraryModel::rebuild);
    rebuild();
}

void LibraryModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    const auto &items = m_lib->items();
    for (int i = 0; i < items.size(); ++i)
        if (items[i].isPack())
            m_rows << i;
    endResetModel();
}

void LibraryModel::setDeviceUuids(const QSet<QString> &uuids)
{
    if (uuids == m_onDevice)
        return;
    m_onDevice = uuids;
    if (!m_rows.isEmpty())
        emit dataChanged(index(0), index(int(m_rows.size()) - 1), {OnDeviceRole});
}

int LibraryModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const LibraryItem &it = m_lib->items().at(m_rows[index.row()]);
    switch (role) {
    case Qt::DisplayRole: return it.title;
    case Qt::ToolTipRole: return QString("%1\n%2").arg(it.title, it.path);
    case PathRole: return it.path;
    case UuidRole: return it.uuid;
    case KeyRole: return it.key();
    case AgeRole: return it.age;
    case AgeValueRole: return it.ageValue();
    case OfficialRole: return it.official;
    case OnDeviceRole: return !it.uuid.isEmpty() && m_onDevice.contains(it.uuid);
    case SizeRole: return it.size;
    case DurationRole: return it.durationSec;
    case TypeRole: return it.type;
    case TagsRole: return it.tags;
    case AddedRole: return it.added;
    case SourceRole: return it.source;
    case DescriptionRole: return it.description;
    case AuthorRole: return it.author;
    case NightRole: return it.nightMode;
    case ThumbRole: return it.thumb;
    case NodesRole: return it.nodes;
    }
    return {};
}

Qt::ItemFlags LibraryModel::flags(const QModelIndex &index) const
{
    return QAbstractListModel::flags(index) | Qt::ItemIsDragEnabled;
}

QStringList LibraryModel::mimeTypes() const
{
    return {mime::libraryPaths, "text/uri-list"};
}

QMimeData *LibraryModel::mimeData(const QModelIndexList &indexes) const
{
    auto *md = new QMimeData;
    QStringList paths;
    QList<QUrl> urls;
    for (const QModelIndex &i : indexes) {
        const QString p = i.data(PathRole).toString();
        if (!paths.contains(p)) {
            paths << p;
            urls << QUrl::fromLocalFile(p);
        }
    }
    md->setData(mime::libraryPaths, paths.join('\n').toUtf8());
    md->setUrls(urls);   // glisser vers le gestionnaire de fichiers
    return md;
}

// ------------------------------------------------------------------ filtre

LibraryFilter::LibraryFilter(Library *lib, QObject *parent) : QSortFilterProxyModel(parent), m_lib(lib)
{
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setSortLocaleAware(true);
    setDynamicSortFilter(true);
    connect(lib, &Library::collectionsChanged, this, [this]() {
        if (m_scope == CollectionScope)
            invalidateFilter();
    });
}

void LibraryFilter::setText(const QString &t)
{
    m_text = t.trimmed();
    invalidateFilter();
}

void LibraryFilter::setMaxAge(int age)
{
    m_maxAge = age;
    invalidateFilter();
}

void LibraryFilter::setScope(Scope s, const QVariant &arg)
{
    m_scope = s;
    m_arg = arg;
    invalidateFilter();
}

void LibraryFilter::setSortKey(Sort s)
{
    m_sort = s;
    invalidate();
    sort(0, s == ByAdded || s == BySize || s == ByDuration ? Qt::DescendingOrder : Qt::AscendingOrder);
}

bool LibraryFilter::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex i = sourceModel()->index(row, 0, parent);
    switch (m_scope) {
    case All: break;
    case Official: if (!i.data(LibraryModel::OfficialRole).toBool()) return false; break;
    case Custom: if (i.data(LibraryModel::OfficialRole).toBool()) return false; break;
    case OnDevice: if (!i.data(LibraryModel::OnDeviceRole).toBool()) return false; break;
    case NotOnDevice: if (i.data(LibraryModel::OnDeviceRole).toBool()) return false; break;
    case Source: if (i.data(LibraryModel::SourceRole).toString() != m_arg.toString()) return false; break;
    case CollectionScope:
        if (!m_lib->collectionKeys(m_arg.toInt()).contains(i.data(LibraryModel::KeyRole).toString()))
            return false;
        break;
    case Tag: if (!i.data(LibraryModel::TagsRole).toStringList().contains(m_arg.toString())) return false; break;
    }
    if (m_maxAge >= 0 && i.data(LibraryModel::AgeValueRole).toInt() > m_maxAge)
        return false;
    if (m_text.isEmpty())
        return true;
    const QString hay = i.data(Qt::DisplayRole).toString() + ' ' + i.data(LibraryModel::DescriptionRole).toString()
                      + ' ' + i.data(LibraryModel::AuthorRole).toString() + ' '
                      + i.data(LibraryModel::TagsRole).toStringList().join(' ') + ' '
                      + i.data(LibraryModel::PathRole).toString();
    // recherche insensible aux accents et à la casse, tous les mots doivent être présents
    auto fold = [](QString s) {
        s = s.normalized(QString::NormalizationForm_D);
        s.removeIf([](QChar c) { return c.category() == QChar::Mark_NonSpacing; });
        return s.toCaseFolded();
    };
    const QString h = fold(hay);
    for (const QString &w : fold(m_text).split(' ', Qt::SkipEmptyParts))
        if (!h.contains(w))
            return false;
    return true;
}

bool LibraryFilter::lessThan(const QModelIndex &a, const QModelIndex &b) const
{
    auto title = [](const QModelIndex &i) { return i.data(Qt::DisplayRole).toString(); };
    switch (m_sort) {
    case ByAge: {
        const int x = a.data(LibraryModel::AgeValueRole).toInt(), y = b.data(LibraryModel::AgeValueRole).toInt();
        if (x != y)
            return x < y;
        break;
    }
    case ByAdded: return a.data(LibraryModel::AddedRole).toLongLong() < b.data(LibraryModel::AddedRole).toLongLong();
    case BySize: return a.data(LibraryModel::SizeRole).toLongLong() < b.data(LibraryModel::SizeRole).toLongLong();
    case ByDuration: return a.data(LibraryModel::DurationRole).toLongLong() < b.data(LibraryModel::DurationRole).toLongLong();
    case ByTitle: break;
    }
    return QString::localeAwareCompare(title(a), title(b)) < 0;
}

// ------------------------------------------------------------------ rendu

QPixmap LibraryDelegate::thumbnail(const QString &file, int side)
{
    if (file.isEmpty())
        return {};
    const QString key = QString("%1@%2").arg(file).arg(side);
    QPixmap pm;
    if (QPixmapCache::find(key, &pm))
        return pm;
    QImage img(file);
    if (img.isNull())
        return {};
    const qreal dpr = qApp->devicePixelRatio();
    pm = QPixmap::fromImage(img.scaled(int(side * dpr), int(side * dpr), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    pm.setDevicePixelRatio(dpr);
    QPixmapCache::insert(key, pm);
    return pm;
}

static const int CELL_W = 172, CELL_H = 218, COVER = 156;

QSize LibraryDelegate::sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &) const
{
    if (m_grid)
        return {CELL_W, CELL_H};
    return {opt.rect.width(), 56};
}

static void drawBadge(QPainter *p, const QRect &r, const QString &text, const QColor &bg, const QColor &fg)
{
    p->setPen(Qt::NoPen);
    p->setBrush(bg);
    p->drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
    p->setPen(fg);
    p->drawText(r, Qt::AlignCenter, text);
}

void LibraryDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QPalette &pal = opt.palette;
    const bool selected = opt.state & QStyle::State_Selected;
    const bool hover = opt.state & QStyle::State_MouseOver;
    const QColor accent = pal.color(QPalette::Highlight);

    const QRect r = opt.rect.adjusted(4, 4, -4, -4);
    if (selected || hover) {
        QColor bg = accent;
        bg.setAlpha(selected ? 70 : 30);
        p->setPen(selected ? QPen(accent, 1.5) : Qt::NoPen);
        p->setBrush(bg);
        p->drawRoundedRect(r, 8, 8);
    }

    const QString title = idx.data(Qt::DisplayRole).toString();
    const QString age = idx.data(LibraryModel::AgeRole).toString();
    const bool onDevice = idx.data(LibraryModel::OnDeviceRole).toBool();
    const bool night = idx.data(LibraryModel::NightRole).toBool();
    const int side = m_grid ? COVER : 44;
    const QPixmap pm = thumbnail(idx.data(LibraryModel::ThumbRole).toString(), side);

    QRect cover = m_grid ? QRect(r.left() + (r.width() - side) / 2, r.top() + 6, side, side)
                         : QRect(r.left() + 6, r.top() + (r.height() - side) / 2, side, side);
    QPainterPath clip;
    clip.addRoundedRect(cover, 6, 6);
    p->setClipPath(clip);
    p->fillRect(cover, pal.color(QPalette::AlternateBase));
    if (!pm.isNull()) {
        const QSize s = pm.deviceIndependentSize().toSize();
        p->drawPixmap(cover.left() + (side - s.width()) / 2, cover.top() + (side - s.height()) / 2, pm);
    } else {
        p->setPen(pal.color(QPalette::PlaceholderText));
        QFont f = opt.font;
        f.setPointSizeF(f.pointSizeF() * (m_grid ? 2.4 : 1.2));
        p->setFont(f);
        p->drawText(cover, Qt::AlignCenter, title.left(1).toUpper());
    }
    p->setClipping(false);

    QFont small = opt.font;
    small.setPointSizeF(small.pointSizeF() * 0.82);
    small.setBold(true);
    p->setFont(small);
    if (m_grid) {
        if (!age.isEmpty())
            drawBadge(p, QRect(cover.left() + 6, cover.top() + 6, 30, 18), age, QColor(0, 0, 0, 160), Qt::white);
        if (onDevice)
            drawBadge(p, QRect(cover.right() - 25, cover.top() + 6, 20, 18), QString::fromUtf8("✓"),
                      QColor(38, 166, 154), Qt::white);
        if (night)
            drawBadge(p, QRect(cover.right() - 25, cover.bottom() - 23, 20, 18), QString::fromUtf8("☾"),
                      QColor(0, 0, 0, 160), Qt::white);
        QFont tf = opt.font;
        p->setFont(tf);
        p->setPen(pal.color(selected ? QPalette::HighlightedText : QPalette::Text));
        const QRect box(r.left() + 6, cover.bottom() + 6, r.width() - 12, r.bottom() - cover.bottom() - 8);
        // deux lignes maximum : coupure au dernier espace qui tient, élision de la fin
        const QFontMetrics fm(tf);
        int cut = int(title.size());
        while (cut > 0 && fm.horizontalAdvance(title.left(cut)) > box.width()) {
            const int sp = title.lastIndexOf(' ', cut - 1);
            cut = sp > 0 ? sp : cut - 1;
        }
        const QString line2 = fm.elidedText(title.mid(cut).trimmed(), Qt::ElideRight, box.width());
        p->drawText(QRect(box.left(), box.top(), box.width(), fm.height()), Qt::AlignHCenter, title.left(cut));
        if (!line2.isEmpty())
            p->drawText(QRect(box.left(), box.top() + fm.height(), box.width(), fm.height()), Qt::AlignHCenter, line2);
    } else {
        QFont tf = opt.font;
        tf.setBold(true);
        p->setFont(tf);
        p->setPen(pal.color(selected ? QPalette::HighlightedText : QPalette::Text));
        const int x = cover.right() + 12;
        const QRect box(x, r.top() + 6, r.right() - x - 140, r.height() / 2 - 4);
        p->drawText(box, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(tf).elidedText(title, Qt::ElideRight, box.width()));
        p->setFont(opt.font);
        p->setPen(pal.color(QPalette::PlaceholderText));
        QStringList info;
        if (!age.isEmpty())
            info << age;
        const QString dur = formatDuration(idx.data(LibraryModel::DurationRole).toLongLong());
        if (!dur.isEmpty())
            info << dur;
        info << formatSize(idx.data(LibraryModel::SizeRole).toLongLong());
        const QStringList tags = idx.data(LibraryModel::TagsRole).toStringList();
        if (!tags.isEmpty())
            info << tags.join(", ");
        p->drawText(QRect(x, r.top() + r.height() / 2, box.width(), r.height() / 2 - 4), Qt::AlignLeft | Qt::AlignVCenter,
                    info.join("  ·  "));
        if (onDevice) {
            p->setFont(small);
            drawBadge(p, QRect(r.right() - 120, r.top() + (r.height() - 20) / 2, 110, 20), tr("sur la Lunii"),
                      QColor(38, 166, 154), Qt::white);
        }
    }
    p->restore();
}
