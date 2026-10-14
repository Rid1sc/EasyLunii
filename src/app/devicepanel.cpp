#include "app/devicepanel.h"

#include "app/jobqueue.h"
#include "app/library.h"
#include "app/librarymodel.h"
#include "core/storydevice.h"
#include "core/storydb.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent>

using lunii::StoryDevice;

static const int UuidRole = Qt::UserRole + 1;

// ------------------------------------------------------------------ liste

DeviceList::DeviceList(QWidget *parent) : QListWidget(parent)
{
    setSelectionMode(ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setIconSize({48, 48});
    setUniformItemSizes(true);
    setSpacing(1);
}

QStringList DeviceList::mimeTypes() const
{
    return {mime::deviceStories, mime::libraryPaths, "text/uri-list"};
}

QMimeData *DeviceList::mimeData(const QList<QListWidgetItem *> &items) const
{
    auto *md = new QMimeData;
    QStringList ids;
    for (auto *it : items)
        ids << it->data(UuidRole).toString();
    md->setData(mime::deviceStories, ids.join('\n').toUtf8());
    return md;
}

static QStringList archivesFrom(const QMimeData *md)
{
    QStringList out;
    if (md->hasFormat(mime::libraryPaths))
        return QString::fromUtf8(md->data(mime::libraryPaths)).split('\n', Qt::SkipEmptyParts);
    for (const QUrl &u : md->urls()) {
        const QString p = u.toLocalFile();
        const QString l = p.toLower();
        if (l.endsWith(".zip") || l.endsWith(".7z") || l.endsWith(".pk"))
            out << p;
    }
    return out;
}

void DeviceList::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->source() == this || !archivesFrom(e->mimeData()).isEmpty())
        e->acceptProposedAction();
    else
        e->ignore();
}

void DeviceList::dragMoveEvent(QDragMoveEvent *e)
{
    QListWidget::dragMoveEvent(e);
    if (e->source() == this) {
        e->setDropAction(Qt::MoveAction);
        e->accept();
    } else if (!archivesFrom(e->mimeData()).isEmpty()) {
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
}

void DeviceList::dropEvent(QDropEvent *e)
{
    if (e->source() == this) {
        // déplacement interne : on calcule le nouvel ordre, l'écriture est faite par le panneau
        QListWidgetItem *target = itemAt(e->position().toPoint());
        int row = target ? this->row(target) : count();
        if (target && e->position().y() > visualItemRect(target).center().y())
            ++row;
        QList<QUuid> moved, order;
        for (auto *it : selectedItems())
            moved << QUuid(it->data(UuidRole).toString());
        int insertAt = row;
        for (int i = 0; i < count(); ++i) {
            const QUuid u(item(i)->data(UuidRole).toString());
            if (moved.contains(u)) {
                if (i < row)
                    --insertAt;
                continue;
            }
            order << u;
        }
        for (int i = 0; i < moved.size(); ++i)
            order.insert(qBound(0, insertAt + i, int(order.size())), moved[i]);
        e->setDropAction(Qt::IgnoreAction);   // la liste est reconstruite après écriture
        e->accept();
        emit orderChanged(order);
        return;
    }
    const QStringList paths = archivesFrom(e->mimeData());
    if (paths.isEmpty())
        return;
    QListWidgetItem *target = itemAt(e->position().toPoint());
    emit archivesDropped(paths, target ? row(target) : -1);
    e->acceptProposedAction();
}

// ------------------------------------------------------------------ panneau

static QToolButton *toolButton(const QString &icon, const QString &tip, QWidget *parent)
{
    auto *b = new QToolButton(parent);
    b->setIcon(QIcon::fromTheme(icon));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

DevicePanel::DevicePanel(JobQueue *jobs, QWidget *parent) : QWidget(parent), m_jobs(jobs)
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 6, 6, 6);

    auto *title = new QLabel(tr("<b>Conteuse</b> (Lunii ou Flam)"), this);
    lay->addWidget(title);

    auto *row = new QHBoxLayout;
    m_combo = new QComboBox(this);
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_mountBtn = new QPushButton(QIcon::fromTheme("media-mount"), tr("Monter"), this);
    m_ejectBtn = new QPushButton(QIcon::fromTheme("media-eject"), tr("Éjecter"), this);
    row->addWidget(m_combo, 1);
    row->addWidget(m_mountBtn);
    row->addWidget(m_ejectBtn);
    lay->addLayout(row);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    m_info->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lay->addWidget(m_info);
    m_space = new QProgressBar(this);
    m_space->setTextVisible(true);
    m_space->setMaximumHeight(16);
    lay->addWidget(m_space);

    m_list = new DeviceList(this);
    lay->addWidget(m_list, 1);

    auto *tools = new QHBoxLayout;
    m_up = toolButton("go-up", tr("Monter dans la liste"), this);
    m_down = toolButton("go-down", tr("Descendre dans la liste"), this);
    m_hide = toolButton("view-hidden", tr("Masquer / afficher sur la Lunii"), this);
    m_night = toolButton("weather-clear-night", tr("Mode nuit pour cette histoire"), this);
    m_export = toolButton("document-export", tr("Exporter vers un dossier…"), this);
    m_remove = toolButton("edit-delete", tr("Retirer de la Lunii"), this);
    for (auto *b : {m_up, m_down, m_hide, m_night, m_export, m_remove})
        tools->addWidget(b);
    tools->addStretch();
    lay->addLayout(tools);

    auto *hint = new QLabel(tr("Glissez des histoires depuis la bibliothèque pour les envoyer, "
                               "ou vers la bibliothèque pour les sauvegarder."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: palette(placeholder-text);");
    lay->addWidget(hint);

    connect(&m_watcher, &DeviceWatcher::devicesChanged, this, &DevicePanel::onDevicesChanged);
    connect(m_combo, &QComboBox::currentIndexChanged, this, &DevicePanel::selectDevice);
    connect(m_mountBtn, &QPushButton::clicked, this, [this]() {
        const int i = m_combo->currentIndex();
        if (i < 0 || i >= m_watcher.devices().size())
            return;
        QString mp, err;
        if (!DeviceWatcher::mount(m_watcher.devices()[i].blockDevice, &mp, &err))
            QMessageBox::warning(this, tr("Montage"), tr("Impossible de monter la Lunii :\n%1").arg(err));
        m_watcher.refresh();
    });
    connect(m_ejectBtn, &QPushButton::clicked, this, [this]() {
        if (m_jobs->isBusy()) {
            QMessageBox::information(this, tr("Éjecter"), tr("Des transferts sont en cours."));
            return;
        }
        QString err;
        const QString block = m_block;
        delete m_device;
        if (!DeviceWatcher::eject(block, &err))
            QMessageBox::warning(this, tr("Éjecter"), tr("Impossible d'éjecter la Lunii :\n%1").arg(err));
        else
            emit logMessage(QtInfoMsg, tr("Lunii éjectée, vous pouvez la débrancher."));
        m_watcher.refresh();
        selectDevice(m_combo->currentIndex());
    });
    connect(m_list, &DeviceList::archivesDropped, this, [this](const QStringList &p, int) { sendToDevice(p); });
    connect(m_list, &DeviceList::orderChanged, this, [this](const QList<QUuid> &order) {
        runOnDevice(tr("Réorganisation"), [order](StoryDevice *d) { return d->reorder(order); });
    });
    connect(m_list, &QListWidget::itemSelectionChanged, this, &DevicePanel::updateButtons);
    connect(m_up, &QToolButton::clicked, this, [this]() { moveSelection(-1); });
    connect(m_down, &QToolButton::clicked, this, [this]() { moveSelection(1); });
    connect(m_hide, &QToolButton::clicked, this, [this]() {
        const auto sel = selectedStories();
        runOnDevice(tr("Masquage"), [sel](StoryDevice *d) {
            bool ok = true;
            for (const QUuid &u : sel) {
                const int i = d->indexOf(u);
                ok = i >= 0 && d->setHidden(u, !d->stories()[i].hidden) && ok;
            }
            return ok;
        });
    });
    connect(m_night, &QToolButton::clicked, this, [this]() {
        const auto sel = selectedStories();
        runOnDevice(tr("Mode nuit"), [sel](StoryDevice *d) {
            bool ok = true;
            for (const QUuid &u : sel) {
                const int i = d->indexOf(u);
                ok = i >= 0 && d->setNightMode(u, !d->stories()[i].nightMode) && ok;
            }
            return ok;
        });
    });
    connect(m_export, &QToolButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Exporter vers…"));
        if (!dir.isEmpty())
            exportStories(selectedStories(), dir);
    });
    connect(m_remove, &QToolButton::clicked, this, [this]() {
        const auto sel = selectedStories();
        if (sel.isEmpty())
            return;
        QStringList names;
        for (auto *it : m_list->selectedItems())
            names << it->text();
        if (QMessageBox::question(this, tr("Retirer de la Lunii"),
                                  tr("Retirer %n histoire(s) de la Lunii ?\n\n%1", nullptr, int(sel.size()))
                                      .arg(names.join('\n')))
            != QMessageBox::Yes)
            return;
        runOnDevice(tr("Suppression"), [sel](StoryDevice *d) {
            bool ok = true;
            for (const QUuid &u : sel)
                ok = d->removeStory(u) && ok;
            return ok;
        });
    });

    onDevicesChanged();
}

void DevicePanel::onDevicesChanged()
{
    const QString current = m_block;
    m_combo->blockSignals(true);
    m_combo->clear();
    int select = -1;
    const auto &devs = m_watcher.devices();
    for (int i = 0; i < devs.size(); ++i) {
        const auto &d = devs[i];
        m_combo->addItem(QIcon::fromTheme("drive-removable-media"),
                         d.mountPoint.isEmpty() ? tr("%1 (non montée)").arg(d.model)
                                                : QString("%1 — %2").arg(d.model, d.mountPoint));
        if (d.blockDevice == current)
            select = i;
    }
    if (devs.isEmpty())
        m_combo->addItem(tr("Aucune Lunii branchée"));
    m_combo->blockSignals(false);
    m_combo->setCurrentIndex(select >= 0 ? select : 0);
    selectDevice(m_combo->currentIndex());
}

void DevicePanel::selectDevice(int index)
{
    const auto &devs = m_watcher.devices();
    const bool valid = index >= 0 && index < devs.size();
    const QString mp = valid ? devs[index].mountPoint : QString();
    const QString block = valid ? devs[index].blockDevice : QString();
    m_mountBtn->setVisible(valid && mp.isEmpty());
    m_ejectBtn->setEnabled(valid);
    if (m_device && m_device->mountPoint() == mp && m_block == block)
        return;
    if (m_jobs->isBusy() && m_device)
        return;   // pas de changement d'appareil pendant un transfert
    delete m_device;
    m_block = block;
    if (!mp.isEmpty() && StoryDevice::isStoryDevice(mp)) {
        m_device = StoryDevice::open(mp, this);
        connect(m_device, &StoryDevice::log, this, &DevicePanel::logMessage);
        connect(m_device, &StoryDevice::storyProgress, this,
                [this](const QString &id, int c, int t) { emit progress(id, c, t); });
    }
    reloadStories();
    emit deviceChanged();
}

void DevicePanel::openPath(const QString &mountPoint)
{
    delete m_device;
    m_block.clear();
    m_device = StoryDevice::open(mountPoint, this);
    if (!m_device) {
        emit logMessage(QtWarningMsg, tr("Ni Lunii ni Flam dans %1").arg(mountPoint));
        reloadStories();
        return;
    }
    connect(m_device, &StoryDevice::log, this, &DevicePanel::logMessage);
    connect(m_device, &StoryDevice::storyProgress, this,
            [this](const QString &id, int c, int t) { emit progress(id, c, t); });
    m_combo->blockSignals(true);
    m_combo->insertItem(0, QIcon::fromTheme("folder"), mountPoint);
    m_combo->setCurrentIndex(0);
    m_combo->blockSignals(false);
    reloadStories();
    emit deviceChanged();
}

void DevicePanel::updateInfo()
{
    if (!m_device || !m_device->isValid()) {
        m_info->setText(m_combo->count() && m_watcher.devices().size() ? tr("Lunii non montée") : QString());
        m_space->setVisible(false);
        return;
    }
    m_info->setText(m_device->describe());
    const qint64 total = m_device->totalSpace(), free = m_device->freeSpace();
    m_space->setVisible(total > 0);
    m_space->setRange(0, 1000);
    m_space->setValue(total > 0 ? int(1000 * (total - free) / total) : 0);
    m_space->setFormat(tr("%1 libres sur %2").arg(formatSize(free), formatSize(total)));
}

void DevicePanel::reloadStories()
{
    m_list->clear();
    if (m_device && m_device->isValid()) {
        const auto &db = lunii::StoryDb::instance();
        const QIcon placeholder = QIcon::fromTheme("media-optical-audio");
        for (const auto &s : m_device->stories()) {
            QString t = db.title(s.uuid);
            if (t.isEmpty())
                t = tr("Histoire inconnue (%1)").arg(s.shortId());
            auto *it = new QListWidgetItem(placeholder, t, m_list);
            it->setData(UuidRole, lunii::uuidStr(s.uuid));
            QStringList tip{t, lunii::uuidStr(s.uuid)};
            if (s.hidden) {
                it->setForeground(palette().color(QPalette::PlaceholderText));
                it->setText(t + tr(" (masquée)"));
                tip << tr("Masquée : invisible sur la Lunii");
            }
            if (s.nightMode)
                tip << tr("Disponible en mode nuit");
            it->setToolTip(tip.join('\n'));
        }
        loadCovers();
    }
    updateInfo();
    updateButtons();
    emit storiesChanged();
}

// Jaquettes : cache disque (base officielle, vignettes STUdio) ou premier écran lu sur la Lunii
void DevicePanel::loadCovers()
{
    if (!m_device)
        return;
    const int gen = ++m_coverGeneration;
    QList<QUuid> ids;
    for (const auto &s : m_device->stories())
        ids << s.uuid;
    QPointer<StoryDevice> dev = m_device;
    auto *w = new QFutureWatcher<QHash<QString, QString>>(this);
    connect(w, &QFutureWatcher<QHash<QString, QString>>::finished, this, [this, w, gen]() {
        const auto files = w->result();
        w->deleteLater();
        if (gen != m_coverGeneration)
            return;
        for (int i = 0; i < m_list->count(); ++i) {
            auto *it = m_list->item(i);
            const QString f = files.value(it->data(UuidRole).toString());
            if (!f.isEmpty())
                it->setIcon(QIcon(LibraryDelegate::thumbnail(f, 48)));
        }
    });
    w->setFuture(QtConcurrent::run([dev, ids]() {
        QHash<QString, QString> out;
        for (const QUuid &u : ids) {
            // jaquette de la base (officielle ou vignette STUdio) en priorité, sinon premier écran lu sur la Lunii
            const QByteArray pic = lunii::StoryDb::instance().picture(u, true);
            const QString file = Library::thumbDir() + (pic.isEmpty() ? "/dev-" : "/pic-") + lunii::uuidStr(u) + ".png";
            if (!QFile::exists(file)) {
                const QByteArray data = pic.isEmpty() && dev ? dev->coverImage(u) : pic;
                QImage img;
                if (data.isEmpty() || !img.loadFromData(data))
                    continue;
                img.scaled(160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(file, "PNG");
            }
            out.insert(lunii::uuidStr(u), file);
        }
        return out;
    }));
}

QSet<QString> DevicePanel::storyUuids() const
{
    QSet<QString> s;
    if (m_device)
        for (const auto &st : m_device->stories())
            s.insert(lunii::uuidStr(st.uuid));
    return s;
}

QList<QUuid> DevicePanel::selectedStories() const
{
    QList<QUuid> out;
    for (auto *it : m_list->selectedItems())
        out << QUuid(it->data(UuidRole).toString());
    return out;
}

void DevicePanel::updateButtons()
{
    const bool has = m_device && m_device->isValid() && !m_busy;
    const bool sel = has && !m_list->selectedItems().isEmpty();
    for (auto *b : {m_up, m_down, m_hide, m_night, m_remove})
        b->setEnabled(sel);
    m_night->setEnabled(sel && m_device->supportsNightMode());   // pas de mode nuit par histoire sur la Flam
    bool exportable = sel;
    if (sel)
        for (const QUuid &u : selectedStories())
            exportable = exportable && m_device->exportAllowed(u);
    m_export->setEnabled(exportable);
    m_export->setToolTip(tr("Exporter vers un dossier… (histoires officielles : sauvegarde chiffrée, "
                            "restaurable uniquement sur cette conteuse)"));
    m_list->setDragDropMode(has ? QAbstractItemView::DragDrop : QAbstractItemView::NoDragDrop);
}

void DevicePanel::moveSelection(int delta)
{
    if (!m_device)
        return;
    QList<QUuid> order;
    for (const auto &s : m_device->stories())
        order << s.uuid;
    QList<int> rows;
    for (auto *it : m_list->selectedItems())
        rows << m_list->row(it);
    std::sort(rows.begin(), rows.end());
    if (delta > 0)
        std::reverse(rows.begin(), rows.end());
    for (int r : rows) {
        const int to = r + delta;
        if (to < 0 || to >= order.size() || rows.contains(to))
            return;
        order.move(r, to);
    }
    runOnDevice(tr("Réorganisation"), [order](StoryDevice *d) { return d->reorder(order); });
}

void DevicePanel::runOnDevice(const QString &label, std::function<bool(StoryDevice *)> op)
{
    if (!m_device)
        return;
    QPointer<StoryDevice> dev = m_device;
    m_busy = true;
    updateButtons();
    m_jobs->enqueue(
        label, [dev, op]() { return dev && op(dev); },
        [this, label](bool ok) {
            m_busy = false;
            if (!ok)
                emit logMessage(QtWarningMsg, tr("%1 : échec (voir le journal)").arg(label));
            reloadStories();
        });
}

void DevicePanel::sendToDevice(const QStringList &archives)
{
    if (!m_device || !m_device->isValid()) {
        QMessageBox::information(this, tr("Envoyer sur la Lunii"), tr("Branchez et montez d'abord une Lunii."));
        return;
    }
    if (!m_device->canImport()) {
        QMessageBox::warning(this, tr("Envoyer sur la Lunii"),
                             tr("Les clés de cette Lunii sont inconnues : impossible d'y écrire des histoires."));
        return;
    }
    for (const QString &p : archives) {
        const QString name = QFileInfo(p).completeBaseName();
        runOnDevice(tr("Envoi de « %1 »").arg(name), [p](StoryDevice *d) { return d->importStory(p); });
    }
}

void DevicePanel::exportStories(const QList<QUuid> &uuids, const QString &dir, const std::function<void()> &done)
{
    if (!m_device)
        return;
    QPointer<StoryDevice> dev = m_device;
    for (const QUuid &u : uuids) {
        const QString t = lunii::StoryDb::instance().title(u);
        m_jobs->enqueue(
            tr("Export de « %1 »").arg(t.isEmpty() ? lunii::shortUuid(u) : t),
            [dev, u, dir]() { return dev && !dev->exportStory(u, dir).isEmpty(); },
            [this, done](bool ok) {
                if (!ok)
                    emit logMessage(QtWarningMsg, tr("Export : échec (voir le journal)"));
                if (done && m_jobs->pending() == 0)   // dernière tâche de la file
                    done();
            });
    }
}

void DevicePanel::recover()
{
    if (!m_device)
        return;
    QPointer<StoryDevice> dev = m_device;
    auto found = std::make_shared<int>(0);
    m_jobs->enqueue(
        tr("Recherche des histoires perdues"), [dev, found]() { return dev && (*found = dev->recoverStories(true)) >= 0; },
        [this, found](bool) {
            if (*found == 0) {
                QMessageBox::information(this, tr("Histoires perdues"), tr("Aucune histoire perdue trouvée."));
                return;
            }
            if (QMessageBox::question(this, tr("Histoires perdues"),
                                      tr("%n histoire(s) présente(s) sur la Lunii mais absente(s) de sa liste. "
                                         "Les réintégrer ?", nullptr, *found))
                == QMessageBox::Yes)
                runOnDevice(tr("Récupération"), [](StoryDevice *d) { return d->recoverStories(false) >= 0; });
        });
}

void DevicePanel::cleanup()
{
    if (!m_device)
        return;
    if (QMessageBox::question(this, tr("Nettoyage"),
                              tr("Supprimer de la Lunii les dossiers d'histoires qui ne figurent pas dans sa liste ?\n"
                                 "Pensez à lancer d'abord « Récupérer les histoires perdues »."))
        != QMessageBox::Yes)
        return;
    runOnDevice(tr("Nettoyage"), [](StoryDevice *d) {
        qint64 freed = 0;
        d->cleanupStories(&freed);
        return true;
    });
}

void DevicePanel::nightModeSettings()
{
    if (!m_device)
        return;
    if (!m_device->supportsNightMode()) {
        QMessageBox::information(this, tr("Mode nuit"), tr("Les réglages du mode nuit ne concernent que la Lunii."));
        return;
    }
    lunii::DeviceConfig cfg = m_device->config();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Mode nuit"));
    auto *form = new QFormLayout(&dlg);
    auto *enabled = new QCheckBox(tr("Activer le mode nuit"), &dlg);
    enabled->setChecked(cfg.values[lunii::DeviceConfig::NmEnabled]);
    // mêmes réglages que la fenêtre « Night Mode » de Lunii.QT (nm_window.py)
    auto *limit = new QCheckBox(tr("Limiter le volume"), &dlg);
    limit->setChecked(cfg.values[lunii::DeviceConfig::NmVolLimit] != 0);
    auto *autoplay = new QCheckBox(tr("Enchaîner les histoires"), &dlg);
    autoplay->setChecked(cfg.values[lunii::DeviceConfig::NmAutoplay] != 0);
    auto *count = new QSpinBox(&dlg);
    count->setRange(1, 8);
    count->setValue(cfg.values[lunii::DeviceConfig::NmStoryCount]);
    auto *turnoff = new QCheckBox(tr("Repasser en mode jour après l'extinction"), &dlg);
    turnoff->setChecked(cfg.values[lunii::DeviceConfig::NmTurnoff] != 0);
    form->addRow(enabled);
    form->addRow(limit);
    form->addRow(autoplay);
    form->addRow(tr("Nombre maximum d'histoires :"), count);
    form->addRow(turnoff);
    for (QWidget *w : std::initializer_list<QWidget *>{limit, autoplay, count, turnoff}) {
        w->setEnabled(enabled->isChecked());
        connect(enabled, &QCheckBox::toggled, w, &QWidget::setEnabled);
    }
    if (!cfg.supported) {
        auto *warn = new QLabel(tr("Le fichier de réglages de cette Lunii est d'une version inconnue : "
                                   "modification impossible."), &dlg);
        warn->setWordWrap(true);
        form->addRow(warn);
    }
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setEnabled(cfg.supported);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    cfg.values[lunii::DeviceConfig::NmEnabled] = enabled->isChecked();
    cfg.values[lunii::DeviceConfig::NmVolLimit] = limit->isChecked() ? 30 : 0;
    cfg.values[lunii::DeviceConfig::NmAutoplay] = autoplay->isChecked();
    cfg.values[lunii::DeviceConfig::NmStoryCount] = count->value();
    cfg.values[lunii::DeviceConfig::NmTurnoff] = turnoff->isChecked();
    runOnDevice(tr("Réglages du mode nuit"), [cfg](StoryDevice *d) { return d->setConfig(cfg); });
}
