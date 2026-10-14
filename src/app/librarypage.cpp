#include "app/librarypage.h"

#include "app/devicepanel.h"
#include "app/jobqueue.h"
#include "app/library.h"
#include "app/librarymodel.h"
#include "core/device.h"
#include "core/packtype.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

enum SideKind { KindScope = 1, KindSource, KindCollection, KindTag, KindAddSource, KindAddCollection };
static const int KindRole = Qt::UserRole + 1, ArgRole = Qt::UserRole + 2;

// ------------------------------------------------------------------ barre latérale

void SideBar::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(mime::libraryPaths) || e->mimeData()->hasFormat(mime::deviceStories))
        e->acceptProposedAction();
}

void SideBar::dragMoveEvent(QDragMoveEvent *e)
{
    QTreeWidgetItem *it = itemAt(e->position().toPoint());
    const bool toCollection = it && it->data(0, KindRole).toInt() == KindCollection
                           && e->mimeData()->hasFormat(mime::libraryPaths);
    const bool toLibrary = e->mimeData()->hasFormat(mime::deviceStories);
    if (toCollection || toLibrary)
        e->acceptProposedAction();
    else
        e->ignore();
}

void SideBar::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat(mime::deviceStories)) {
        emit deviceStoriesDropped(QString::fromUtf8(e->mimeData()->data(mime::deviceStories)).split('\n', Qt::SkipEmptyParts));
        e->acceptProposedAction();
        return;
    }
    QTreeWidgetItem *it = itemAt(e->position().toPoint());
    if (!it || it->data(0, KindRole).toInt() != KindCollection)
        return;
    emit keysDroppedOnCollection(it->data(0, ArgRole).toInt(),
                                 QString::fromUtf8(e->mimeData()->data(mime::libraryPaths)).split('\n', Qt::SkipEmptyParts));
    e->acceptProposedAction();
}

// ------------------------------------------------------------------ page

LibraryPage::LibraryPage(Library *lib, JobQueue *jobs, QWidget *parent) : QWidget(parent), m_lib(lib), m_jobs(jobs)
{
    m_model = new LibraryModel(lib, this);
    m_filter = new LibraryFilter(lib, this);
    m_filter->setSourceModel(m_model);
    m_filter->setSortKey(LibraryFilter::ByTitle);

    auto *split = new QSplitter(Qt::Horizontal, this);
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(split);

    // --- barre latérale
    m_side = new SideBar(split);
    m_side->setHeaderHidden(true);
    m_side->setRootIsDecorated(false);
    m_side->setIndentation(10);
    m_side->setAcceptDrops(true);
    m_side->setDropIndicatorShown(true);
    m_side->setContextMenuPolicy(Qt::CustomContextMenu);
    m_side->setMinimumWidth(180);

    // --- centre
    auto *center = new QWidget(split);
    auto *cl = new QVBoxLayout(center);
    cl->setContentsMargins(4, 6, 4, 0);
    auto *bar = new QHBoxLayout;
    m_search = new QLineEdit(center);
    m_search->setPlaceholderText(tr("Rechercher un titre, un auteur, une étiquette…"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(QIcon::fromTheme("edit-find"), QLineEdit::LeadingPosition);
    m_age = new QComboBox(center);
    m_age->addItem(tr("Tous âges"), -1);
    for (int a : {0, 3, 5, 7, 9, 12})
        m_age->addItem(tr("Jusqu'à %1+").arg(a), a);
    m_sort = new QComboBox(center);
    m_sort->addItem(tr("Titre"), LibraryFilter::ByTitle);
    m_sort->addItem(tr("Âge"), LibraryFilter::ByAge);
    m_sort->addItem(tr("Ajout récent"), LibraryFilter::ByAdded);
    m_sort->addItem(tr("Taille"), LibraryFilter::BySize);
    m_sort->addItem(tr("Durée"), LibraryFilter::ByDuration);
    m_gridBtn = new QToolButton(center);
    m_gridBtn->setIcon(QIcon::fromTheme("view-list-icons"));
    m_gridBtn->setToolTip(tr("Grille"));
    m_gridBtn->setCheckable(true);
    m_gridBtn->setChecked(true);
    m_listBtn = new QToolButton(center);
    m_listBtn->setIcon(QIcon::fromTheme("view-list-details"));
    m_listBtn->setToolTip(tr("Liste"));
    m_listBtn->setCheckable(true);
    bar->addWidget(m_search, 1);
    bar->addWidget(m_age);
    bar->addWidget(new QLabel(tr("Tri :"), center));
    bar->addWidget(m_sort);
    bar->addWidget(m_gridBtn);
    bar->addWidget(m_listBtn);
    cl->addLayout(bar);

    m_view = new QListView(center);
    m_delegate = new LibraryDelegate(m_view);
    m_view->setItemDelegate(m_delegate);
    m_view->setModel(m_filter);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setUniformItemSizes(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setDragEnabled(true);
    m_view->setDragDropMode(QAbstractItemView::DragOnly);
    m_view->setAcceptDrops(true);   // dépôts depuis la Lunii (géré par le filtre d'événements)
    m_view->viewport()->setAcceptDrops(true);
    m_view->viewport()->installEventFilter(this);
    m_view->setMouseTracking(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setSpacing(4);
    cl->addWidget(m_view, 1);
    m_status = new QLabel(center);
    m_status->setStyleSheet("color: palette(placeholder-text);");
    cl->addWidget(m_status);

    // --- fiche
    m_details = new QWidget(center);
    auto *dl = new QHBoxLayout(m_details);
    dl->setContentsMargins(0, 6, 0, 6);
    m_cover = new QLabel(m_details);
    m_cover->setFixedSize(150, 150);
    m_cover->setAlignment(Qt::AlignCenter);
    dl->addWidget(m_cover, 0, Qt::AlignTop);
    auto *info = new QVBoxLayout;
    m_title = new QLabel(m_details);
    m_title->setWordWrap(true);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont tf = m_title->font();
    tf.setPointSizeF(tf.pointSizeF() * 1.3);
    tf.setBold(true);
    m_title->setFont(tf);
    m_meta = new QLabel(m_details);
    m_meta->setWordWrap(true);
    m_meta->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meta->setStyleSheet("color: palette(placeholder-text);");
    m_desc = new QTextBrowser(m_details);
    m_desc->setMaximumHeight(90);
    m_desc->setFrameShape(QFrame::NoFrame);
    m_tags = new QLineEdit(m_details);
    m_tags->setPlaceholderText(tr("Étiquettes, séparées par des virgules (ex. : Léa, dodo, Noël)"));
    auto *btns = new QHBoxLayout;
    m_sendBtn = new QPushButton(QIcon::fromTheme("go-next"), tr("Envoyer sur la Lunii"), m_details);
    m_editBtn = new QPushButton(QIcon::fromTheme("document-edit"), tr("Ouvrir dans l'éditeur"), m_details);
    m_folderBtn = new QPushButton(QIcon::fromTheme("folder-open"), tr("Afficher le fichier"), m_details);
    btns->addWidget(m_sendBtn);
    btns->addWidget(m_editBtn);
    btns->addWidget(m_folderBtn);
    btns->addStretch();
    info->addWidget(m_title);
    info->addWidget(m_meta);
    info->addWidget(m_desc);
    info->addWidget(m_tags);
    info->addLayout(btns);
    dl->addLayout(info, 1);
    cl->addWidget(m_details);

    // --- Lunii
    m_device = new DevicePanel(jobs, split);
    m_device->setMinimumWidth(260);

    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({200, 900, 320});

    buildSidebar();
    updateDetails();

    connect(m_search, &QLineEdit::textChanged, m_filter, &LibraryFilter::setText);
    connect(m_search, &QLineEdit::textChanged, this, &LibraryPage::updateCounts);
    connect(m_age, &QComboBox::currentIndexChanged, this, [this]() {
        m_filter->setMaxAge(m_age->currentData().toInt());
        updateCounts();
    });
    connect(m_sort, &QComboBox::currentIndexChanged, this,
            [this]() { m_filter->setSortKey(LibraryFilter::Sort(m_sort->currentData().toInt())); });
    auto setGrid = [this](bool grid) {
        m_gridBtn->setChecked(grid);
        m_listBtn->setChecked(!grid);
        m_delegate->setGrid(grid);
        m_view->setViewMode(grid ? QListView::IconMode : QListView::ListMode);
        m_view->setResizeMode(QListView::Adjust);
        m_view->setSpacing(grid ? 4 : 0);
        m_view->setDragDropMode(QAbstractItemView::DragOnly);
        QSettings().setValue("library/grid", grid);
    };
    connect(m_gridBtn, &QToolButton::clicked, this, [setGrid]() { setGrid(true); });
    connect(m_listBtn, &QToolButton::clicked, this, [setGrid]() { setGrid(false); });
    setGrid(QSettings().value("library/grid", true).toBool());

    connect(m_side, &QTreeWidget::itemClicked, this, &LibraryPage::onSidebarClicked);
    connect(m_side, &QTreeWidget::customContextMenuRequested, this, &LibraryPage::sidebarMenu);
    connect(m_side, &SideBar::keysDroppedOnCollection, this, [this](int id, const QStringList &paths) {
        QStringList keys;
        for (const QString &p : paths) {
            const int i = m_lib->indexOfPath(p);
            if (i >= 0)
                keys << m_lib->items()[i].key();
        }
        m_lib->addToCollection(id, keys);
    });
    connect(m_side, &SideBar::deviceStoriesDropped, this, &LibraryPage::exportDeviceStoriesToLibrary);
    connect(m_view, &QListView::customContextMenuRequested, this, &LibraryPage::gridMenu);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &LibraryPage::updateDetails);
    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex &) { m_device->sendToDevice(selectedPaths()); });
    connect(m_sendBtn, &QPushButton::clicked, this, [this]() { m_device->sendToDevice(selectedPaths()); });
    connect(m_editBtn, &QPushButton::clicked, this, [this]() {
        const QStringList p = selectedPaths();
        if (p.size() == 1)
            emit openInEditor(p.first());
    });
    connect(m_folderBtn, &QPushButton::clicked, this, [this]() {
        const QStringList p = selectedPaths();
        if (!p.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p.first()).path()));
    });
    connect(m_tags, &QLineEdit::editingFinished, this, [this]() {
        const QStringList keys = selectedKeys();
        if (keys.size() != 1)
            return;
        QStringList tags;
        for (const QString &t : m_tags->text().split(',', Qt::SkipEmptyParts))
            if (!t.trimmed().isEmpty() && !tags.contains(t.trimmed()))
                tags << t.trimmed();
        m_lib->setTags(keys.first(), tags);
        buildSidebar();
    });

    connect(lib, &Library::itemsChanged, this, [this]() {
        updateCounts();
        updateDetails();
    });
    connect(lib, &Library::collectionsChanged, this, &LibraryPage::buildSidebar);
    connect(lib, &Library::sourcesChanged, this, &LibraryPage::buildSidebar);
    connect(lib, &Library::scanProgress, this, [this](int done, int total) {
        m_status->setText(tr("Analyse de la bibliothèque : %1 / %2…").arg(done + 1).arg(total));
    });
    connect(lib, &Library::scanFinished, this, &LibraryPage::updateCounts);
    connect(m_device, &DevicePanel::storiesChanged, this, [this]() {
        m_model->setDeviceUuids(m_device->storyUuids());
        m_filter->invalidate();
        updateDetails();
    });
    connect(m_device, &DevicePanel::logMessage, this, &LibraryPage::logMessage);
    updateCounts();
}

void LibraryPage::buildSidebar()
{
    const QTreeWidgetItem *cur = m_side->currentItem();
    const int curKind = cur ? cur->data(0, KindRole).toInt() : KindScope;
    const QVariant curArg = cur ? cur->data(0, ArgRole) : QVariant(int(LibraryFilter::All));
    m_side->clear();
    QTreeWidgetItem *toSelect = nullptr;

    auto section = [this](const QString &name) {
        auto *s = new QTreeWidgetItem(m_side, {name});
        s->setFlags(Qt::ItemIsEnabled);
        QFont f = s->font(0);
        f.setBold(true);
        f.setPointSizeF(f.pointSizeF() * 0.9);
        s->setFont(0, f);
        s->setForeground(0, palette().color(QPalette::PlaceholderText));
        s->setExpanded(true);
        return s;
    };
    auto add = [&](QTreeWidgetItem *parent, const QString &icon, const QString &text, int kind, const QVariant &arg) {
        auto *it = new QTreeWidgetItem(parent, {text});
        it->setIcon(0, QIcon::fromTheme(icon));
        it->setData(0, KindRole, kind);
        it->setData(0, ArgRole, arg);
        if (kind == curKind && arg == curArg && kind != KindAddSource && kind != KindAddCollection)
            toSelect = it;
        return it;
    };

    auto *lib = section(tr("BIBLIOTHÈQUE"));
    add(lib, "folder-library", tr("Toutes les histoires"), KindScope, int(LibraryFilter::All));
    add(lib, "emblem-favorite", tr("Officielles Lunii"), KindScope, int(LibraryFilter::Official));
    add(lib, "document-edit", tr("Personnelles et STUdio"), KindScope, int(LibraryFilter::Custom));
    add(lib, "drive-removable-media", tr("Sur la Lunii"), KindScope, int(LibraryFilter::OnDevice));
    add(lib, "list-remove", tr("Pas sur la Lunii"), KindScope, int(LibraryFilter::NotOnDevice));

    auto *src = section(tr("DOSSIERS"));
    for (const QString &s : m_lib->sources()) {
        auto *it = add(src, "folder", QFileInfo(s).fileName(), KindSource, s);
        it->setToolTip(0, s);
    }
    add(src, "list-add", tr("Ajouter un dossier…"), KindAddSource, {});

    auto *col = section(tr("COLLECTIONS"));
    for (const Collection &c : m_lib->collections())
        add(col, "folder-favorites", c.name, KindCollection, c.id);
    add(col, "list-add", tr("Nouvelle collection…"), KindAddCollection, {});

    const QStringList tags = m_lib->allTags();
    if (!tags.isEmpty()) {
        auto *tg = section(tr("ÉTIQUETTES"));
        for (const QString &t : tags)
            add(tg, "tag", t, KindTag, t);
    }
    m_side->expandAll();
    if (!toSelect)
        toSelect = lib->child(0);
    m_side->setCurrentItem(toSelect);
    onSidebarClicked(toSelect);
}

void LibraryPage::onSidebarClicked(QTreeWidgetItem *it)
{
    if (!it)
        return;
    const QVariant arg = it->data(0, ArgRole);
    switch (it->data(0, KindRole).toInt()) {
    case KindScope: m_filter->setScope(LibraryFilter::Scope(arg.toInt())); break;
    case KindSource: m_filter->setScope(LibraryFilter::Source, arg); break;
    case KindCollection: m_filter->setScope(LibraryFilter::CollectionScope, arg); break;
    case KindTag: m_filter->setScope(LibraryFilter::Tag, arg); break;
    case KindAddSource: addSourceDialog(); return;
    case KindAddCollection: {
        const QString name = QInputDialog::getText(this, tr("Nouvelle collection"), tr("Nom :"));
        if (!name.trimmed().isEmpty())
            m_lib->createCollection(name.trimmed());
        return;
    }
    default: return;
    }
    updateCounts();
}

void LibraryPage::addSourceDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Dossier d'histoires à surveiller"));
    if (!dir.isEmpty())
        m_lib->addSource(dir);
}

void LibraryPage::sidebarMenu(const QPoint &pos)
{
    QTreeWidgetItem *it = m_side->itemAt(pos);
    if (!it)
        return;
    const int kind = it->data(0, KindRole).toInt();
    const QVariant arg = it->data(0, ArgRole);
    QMenu m;
    if (kind == KindSource) {
        m.addAction(QIcon::fromTheme("folder-open"), tr("Ouvrir le dossier"),
                    [arg]() { QDesktopServices::openUrl(QUrl::fromLocalFile(arg.toString())); });
        m.addAction(QIcon::fromTheme("view-refresh"), tr("Analyser à nouveau"), m_lib, &Library::rescan);
        m.addAction(QIcon::fromTheme("list-remove"), tr("Ne plus surveiller ce dossier"), [this, arg]() {
            if (QMessageBox::question(this, tr("Dossier"), tr("Retirer « %1 » de la bibliothèque ?\n"
                                                              "Les fichiers ne sont pas supprimés.").arg(arg.toString()))
                == QMessageBox::Yes)
                m_lib->removeSource(arg.toString());
        });
    } else if (kind == KindCollection) {
        m.addAction(QIcon::fromTheme("edit-rename"), tr("Renommer…"), [this, it, arg]() {
            const QString n = QInputDialog::getText(this, tr("Renommer"), tr("Nom :"), QLineEdit::Normal, it->text(0));
            if (!n.trimmed().isEmpty())
                m_lib->renameCollection(arg.toInt(), n.trimmed());
        });
        m.addAction(QIcon::fromTheme("edit-delete"), tr("Supprimer la collection"), [this, it, arg]() {
            if (QMessageBox::question(this, tr("Collection"), tr("Supprimer la collection « %1 » ?\n"
                                                                 "Les histoires restent dans la bibliothèque.").arg(it->text(0)))
                == QMessageBox::Yes)
                m_lib->deleteCollection(arg.toInt());
        });
    } else {
        return;
    }
    m.exec(m_side->viewport()->mapToGlobal(pos));
}

void LibraryPage::gridMenu(const QPoint &pos)
{
    if (!m_view->indexAt(pos).isValid())
        return;
    const QStringList paths = selectedPaths();
    const QStringList keys = selectedKeys();
    QMenu m;
    m.addAction(QIcon::fromTheme("go-next"), tr("Envoyer sur la Lunii"), [this, paths]() { m_device->sendToDevice(paths); });
    if (paths.size() == 1)
        m.addAction(QIcon::fromTheme("document-edit"), tr("Ouvrir dans l'éditeur"),
                    [this, paths]() { emit openInEditor(paths.first()); });
    QMenu *add = m.addMenu(QIcon::fromTheme("folder-favorites"), tr("Ajouter à la collection"));
    for (const Collection &c : m_lib->collections())
        add->addAction(c.name, [this, c, keys]() { m_lib->addToCollection(c.id, keys); });
    add->addSeparator();
    add->addAction(tr("Nouvelle collection…"), [this, keys]() {
        const QString name = QInputDialog::getText(this, tr("Nouvelle collection"), tr("Nom :"));
        if (name.trimmed().isEmpty())
            return;
        if (const int id = m_lib->createCollection(name.trimmed()))
            m_lib->addToCollection(id, keys);
    });
    const QTreeWidgetItem *cur = m_side->currentItem();
    if (cur && cur->data(0, KindRole).toInt() == KindCollection) {
        const int id = cur->data(0, ArgRole).toInt();
        m.addAction(QIcon::fromTheme("list-remove"), tr("Retirer de cette collection"),
                    [this, id, keys]() { m_lib->removeFromCollection(id, keys); });
    }
    m.addSeparator();
    m.addAction(QIcon::fromTheme("folder-open"), tr("Afficher le fichier"), [paths]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(paths.first()).path()));
    });
    m.exec(m_view->viewport()->mapToGlobal(pos));
}

QStringList LibraryPage::selectedPaths() const
{
    QStringList out;
    for (const QModelIndex &i : m_view->selectionModel()->selectedIndexes())
        out << i.data(LibraryModel::PathRole).toString();
    return out;
}

QStringList LibraryPage::selectedKeys() const
{
    QStringList out;
    for (const QModelIndex &i : m_view->selectionModel()->selectedIndexes())
        out << i.data(LibraryModel::KeyRole).toString();
    return out;
}

void LibraryPage::updateCounts()
{
    const int total = m_model->rowCount(), shown = m_filter->rowCount();
    if (m_lib->isScanning() && total == 0)
        return;
    if (m_lib->sources().isEmpty())
        m_status->setText(tr("Ajoutez un dossier contenant vos histoires (zip, 7z, pk) pour remplir la bibliothèque."));
    else
        m_status->setText(shown == total ? tr("%n histoire(s)", nullptr, total)
                                         : tr("%1 sur %2 histoires").arg(shown).arg(total));
}

void LibraryPage::updateDetails()
{
    const QModelIndexList sel = m_view->selectionModel()->selectedIndexes();
    m_details->setVisible(!sel.isEmpty());
    if (sel.isEmpty())
        return;
    if (sel.size() > 1) {
        m_cover->clear();
        m_title->setText(tr("%n histoires sélectionnées", nullptr, int(sel.size())));
        qint64 size = 0;
        for (const QModelIndex &i : sel)
            size += i.data(LibraryModel::SizeRole).toLongLong();
        m_meta->setText(tr("%1 au total").arg(formatSize(size)));
        m_desc->clear();
        m_tags->setEnabled(false);
        m_editBtn->setEnabled(false);
        m_sendBtn->setEnabled(m_device->hasDevice());
        return;
    }
    const QModelIndex i = sel.first();
    const QPixmap pm = LibraryDelegate::thumbnail(i.data(LibraryModel::ThumbRole).toString(), 150);
    if (pm.isNull())
        m_cover->setPixmap(QIcon::fromTheme("media-optical-audio").pixmap(96));
    else
        m_cover->setPixmap(pm);
    m_title->setText(i.data(Qt::DisplayRole).toString());
    QStringList meta;
    const auto type = lunii::PackType(i.data(LibraryModel::TypeRole).toInt());
    meta << (i.data(LibraryModel::OfficialRole).toBool() ? tr("Officielle") : lunii::packTypeLabel(type));
    if (!i.data(LibraryModel::AgeRole).toString().isEmpty())
        meta << i.data(LibraryModel::AgeRole).toString();
    if (!i.data(LibraryModel::AuthorRole).toString().isEmpty())
        meta << i.data(LibraryModel::AuthorRole).toString();
    const QString dur = formatDuration(i.data(LibraryModel::DurationRole).toLongLong());
    if (!dur.isEmpty())
        meta << dur;
    meta << formatSize(i.data(LibraryModel::SizeRole).toLongLong());
    if (i.data(LibraryModel::NightRole).toBool())
        meta << tr("mode nuit");
    if (i.data(LibraryModel::OnDeviceRole).toBool())
        meta << tr("✓ sur la Lunii");
    m_meta->setText(meta.join("  ·  ") + "\n" + i.data(LibraryModel::PathRole).toString());
    m_desc->setPlainText(i.data(LibraryModel::DescriptionRole).toString());
    m_desc->setVisible(!m_desc->toPlainText().isEmpty());
    m_tags->setEnabled(true);
    m_tags->setText(i.data(LibraryModel::TagsRole).toStringList().join(", "));
    const bool studio = type == lunii::PackType::StudioZip || type == lunii::PackType::Studio7z;
    m_editBtn->setEnabled(studio);
    m_editBtn->setToolTip(studio ? QString() : tr("Seuls les packs au format STUdio sont modifiables"));
    m_sendBtn->setEnabled(m_device->hasDevice() && !i.data(LibraryModel::OnDeviceRole).toBool());
}

// Dépôt d'histoires de la Lunii sur la grille ou la barre latérale : sauvegarde dans un dossier surveillé
bool LibraryPage::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_view->viewport()) {
        if (e->type() == QEvent::DragEnter || e->type() == QEvent::DragMove) {
            auto *de = static_cast<QDragMoveEvent *>(e);
            if (de->mimeData()->hasFormat(mime::deviceStories)) {
                de->acceptProposedAction();
                return true;
            }
        } else if (e->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(e);
            if (de->mimeData()->hasFormat(mime::deviceStories)) {
                exportDeviceStoriesToLibrary(
                    QString::fromUtf8(de->mimeData()->data(mime::deviceStories)).split('\n', Qt::SkipEmptyParts));
                de->acceptProposedAction();
                return true;
            }
        }
    }
    return QWidget::eventFilter(o, e);
}

void LibraryPage::exportDeviceStoriesToLibrary(const QStringList &uuids)
{
    if (!m_device->device())
        return;
    QList<QUuid> ids, refused;
    for (const QString &u : uuids) {
        const QUuid id(u);
        (m_device->device()->exportAllowed(id) ? ids : refused) << id;
    }
    if (!refused.isEmpty())
        QMessageBox::information(this, tr("Sauvegarde"),
                                 tr("%n histoire(s) officielle(s) ne peuvent pas être exportées depuis ce modèle de Lunii.",
                                    nullptr, int(refused.size())));
    if (ids.isEmpty())
        return;
    QString dir;
    const QStringList sources = m_lib->sources();
    if (sources.size() == 1) {
        dir = sources.first();
    } else if (sources.size() > 1) {
        bool ok = false;
        dir = QInputDialog::getItem(this, tr("Sauvegarde"), tr("Dossier de destination :"), sources, 0, false, &ok);
        if (!ok)
            return;
    } else {
        dir = QFileDialog::getExistingDirectory(this, tr("Dossier de destination"));
        if (dir.isEmpty())
            return;
        m_lib->addSource(dir);
    }
    m_device->exportStories(ids, dir, [this]() { m_lib->rescan(); });
}
