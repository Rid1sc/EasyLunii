#include "app/editorpage.h"

#include "app/editor/graphitems.h"
#include "app/editor/simulator.h"
#include "app/editor/wizard.h"
#include "app/jobqueue.h"
#include "core/convert.h"

#include <QAudioOutput>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolBar>
#include <QUndoCommand>
#include <QUuid>
#include <QVBoxLayout>
#include <QTimer>
#include <QWheelEvent>

using lunii::ActionNode;
using lunii::StageNode;
using lunii::StudioPack;
using lunii::Transition;

// ------------------------------------------------------------------ annulation par instantanés

class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(EditorPage *page, const QString &label, const QJsonObject &before, const QJsonObject &after)
        : QUndoCommand(label), m_page(page), m_before(before), m_after(after)
    {
    }
    void undo() override { m_page->restore(m_before); }
    void redo() override
    {
        if (m_first)
            m_first = false;   // déjà appliqué
        else
            m_page->restore(m_after);
    }

private:
    EditorPage *m_page;
    QJsonObject m_before, m_after;
    bool m_first = true;
};

// ------------------------------------------------------------------ vue du graphe

GraphView::GraphView(QWidget *parent) : QGraphicsView(parent)
{
    setRenderHint(QPainter::Antialiasing);
    setDragMode(RubberBandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setViewportUpdateMode(SmartViewportUpdate);
    setAcceptDrops(true);
}

void GraphView::drawBackground(QPainter *p, const QRectF &rect)
{
    p->fillRect(rect, palette().color(QPalette::Window));
    QColor dot = palette().color(QPalette::Mid);
    dot.setAlpha(90);
    p->setPen(QPen(dot, 0));
    const int step = 40;
    for (qreal x = std::floor(rect.left() / step) * step; x < rect.right(); x += step)
        for (qreal y = std::floor(rect.top() / step) * step; y < rect.bottom(); y += step)
            p->drawPoint(QPointF(x, y));
}

void GraphView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton) {
        m_panning = true;
        m_panStart = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() == Qt::LeftButton) {
        auto *port = dynamic_cast<PortItem *>(itemAt(e->pos()));
        if (port && port->role() != PortItem::In) {
            m_dragPort = port;
            m_temp = scene()->addPath(QPainterPath(), QPen(palette().color(QPalette::Highlight), 2, Qt::DashLine));
            m_temp->setZValue(5);
            return;
        }
    }
    QGraphicsView::mousePressEvent(e);
}

void GraphView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_panning) {
        const QPoint d = e->pos() - m_panStart;
        m_panStart = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        return;
    }
    if (m_dragPort) {
        const QPointF a = m_dragPort->anchor(), b = mapToScene(e->pos());
        QPainterPath path(a);
        const qreal dx = qMax<qreal>(60, qAbs(b.x() - a.x()) / 2);
        path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
        m_temp->setPath(path);
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void GraphView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning && e->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
        return;
    }
    if (m_dragPort) {
        delete m_temp;
        m_temp = nullptr;
        NodeItem *target = nullptr;
        for (QGraphicsItem *it : items(e->pos())) {
            QGraphicsItem *top = it->parentItem() ? it->parentItem() : it;
            if (auto *n = dynamic_cast<NodeItem *>(top)) {
                target = n;
                break;
            }
        }
        PortItem *port = m_dragPort;
        m_dragPort = nullptr;
        if (target && target != port->node())
            emit linkRequested(port->node()->id(), port->role(), port->index(), target->id());
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void GraphView::wheelEvent(QWheelEvent *e)
{
    // molette = zoom (déplacement : clic du milieu ou barres de défilement)
    const qreal f = e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    const qreal s = transform().m11() * f;
    if (s > 0.15 && s < 3)
        scale(f, f);
}

void GraphView::contextMenuEvent(QContextMenuEvent *e)
{
    if (itemAt(e->pos()) && !itemAt(e->pos())->isSelected()) {
        scene()->clearSelection();
        QGraphicsItem *it = itemAt(e->pos());
        (it->parentItem() ? it->parentItem() : it)->setSelected(true);
    }
    const QPointF pos = mapToScene(e->pos());
    QMenu m;
    m.addAction(QIcon::fromTheme("list-add"), tr("Ajouter une scène"), [this, pos]() { emit addRequested(true, pos); });
    m.addAction(QIcon::fromTheme("list-add"), tr("Ajouter un choix (menu)"), [this, pos]() { emit addRequested(false, pos); });
    if (!scene()->selectedItems().isEmpty()) {
        m.addSeparator();
        m.addAction(QIcon::fromTheme("edit-delete"), tr("Supprimer la sélection"), [this]() { emit deleteRequested(); });
    }
    m.exec(e->globalPos());
}

void GraphView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        emit deleteRequested();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void GraphView::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void GraphView::dragMoveEvent(QDragMoveEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void GraphView::dropEvent(QDropEvent *e)
{
    QStringList files;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile())
            files << u.toLocalFile();
    if (!files.isEmpty())
        emit filesDropped(files, mapToScene(e->position().toPoint()));
    e->acceptProposedAction();
}

void GraphView::zoomToFit()
{
    QRectF r;
    for (QGraphicsItem *it : scene()->items())
        if (!it->parentItem())
            r |= it->sceneBoundingRect();
    if (r.isEmpty())
        return;
    fitInView(r.adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    if (transform().m11() > 1.0)
        setTransform(QTransform());
    if (transform().m11() < 0.35)
        showStart();   // graphe trop grand : lisibilité d'abord, centré sur le départ
}

void GraphView::showStart()
{
    setTransform(QTransform::fromScale(0.6, 0.6));
    QRectF first;
    for (QGraphicsItem *it : scene()->items())
        if (auto *n = dynamic_cast<NodeItem *>(it))
            if (first.isNull() || n->pos().x() < first.x() || (n->pos().x() == first.x() && n->pos().y() < first.y()))
                first = n->sceneBoundingRect();
    if (!first.isNull())
        centerOn(first.center() + QPointF(width() / 2.0 / 0.6 - first.width(), height() / 2.0 / 0.6 - first.height()));
}

// ------------------------------------------------------------------ page

static QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

EditorPage::EditorPage(JobQueue *jobs, QWidget *parent) : QWidget(parent), m_jobs(jobs)
{
    m_player = nullptr;   // créé à la première écoute : évite de charger la pile multimédia au démarrage
    m_audioOut = nullptr;
    buildUi();
    newPack();
    connect(&m_undo, &QUndoStack::cleanChanged, this, &EditorPage::updateTitle);
}

void EditorPage::buildUi()
{
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    m_bar = new QToolBar(this);
    m_bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    lay->addWidget(m_bar);

    m_bar->addAction(QIcon::fromTheme("document-new"), tr("Nouveau"), this, [this]() {
        if (maybeSave())
            newPack();
    });
    m_bar->addAction(QIcon::fromTheme("document-open"), tr("Ouvrir"), this, &EditorPage::openDialog);
    auto *saveAct = m_bar->addAction(QIcon::fromTheme("document-save"), tr("Enregistrer"), this, &EditorPage::save);
    saveAct->setShortcut(QKeySequence::Save);
    saveAct->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_bar->addAction(QIcon::fromTheme("document-save-as"), tr("Enregistrer sous…"), this, &EditorPage::saveAs);
    m_bar->addSeparator();
    auto *undo = m_undo.createUndoAction(this, tr("Annuler"));
    undo->setIcon(QIcon::fromTheme("edit-undo"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    auto *redo = m_undo.createRedoAction(this, tr("Rétablir"));
    redo->setIcon(QIcon::fromTheme("edit-redo"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(undo);
    addAction(redo);
    addAction(saveAct);
    m_bar->addAction(undo);
    m_bar->addAction(redo);
    m_bar->addSeparator();
    m_bar->addAction(QIcon::fromTheme("list-add"), tr("Scène"), this, [this]() {
        addNode(true, m_view->mapToScene(m_view->viewport()->rect().center()));
    });
    m_bar->addAction(QIcon::fromTheme("format-list-unordered"), tr("Choix"), this, [this]() {
        addNode(false, m_view->mapToScene(m_view->viewport()->rect().center()));
    });
    m_bar->addAction(QIcon::fromTheme("distribute-graph-directed"), tr("Ranger"), this, &EditorPage::autoLayout);
    m_bar->addAction(QIcon::fromTheme("zoom-fit-best"), tr("Tout voir"), this, [this]() { m_view->zoomToFit(); });
    auto *homeAct = m_bar->addAction(QIcon::fromTheme("go-home"), tr("Liens maison"));
    homeAct->setCheckable(true);
    homeAct->setChecked(QSettings().value("editor/showHome", true).toBool());
    m_showHome = homeAct->isChecked();
    connect(homeAct, &QAction::toggled, this, [this](bool on) {
        m_showHome = on;
        QSettings().setValue("editor/showHome", on);
        rebuildScene();
    });
    m_bar->addSeparator();
    m_bar->addAction(QIcon::fromTheme("tools-wizard"), tr("Assistant…"), this, &EditorPage::wizard);
    m_bar->addAction(QIcon::fromTheme("dialog-ok-apply"), tr("Vérifier"), this, &EditorPage::validate);
    m_bar->addAction(QIcon::fromTheme("media-playback-start"), tr("Simuler"), this, &EditorPage::simulate);
    m_bar->addAction(QIcon::fromTheme("go-next"), tr("Envoyer sur la Lunii"), this, &EditorPage::send);
    auto *spacer = new QWidget(m_bar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_bar->addWidget(spacer);
    m_fileLabel = new QLabel(m_bar);
    m_fileLabel->setStyleSheet("color: palette(placeholder-text); padding-right: 8px;");
    m_bar->addWidget(m_fileLabel);

    auto *split = new QSplitter(Qt::Horizontal, this);
    lay->addWidget(split, 1);
    m_scene = new QGraphicsScene(this);
    m_view = new GraphView(split);
    m_view->setScene(m_scene);

    auto *scroll = new QScrollArea(split);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(330);
    m_stack = new QStackedWidget(scroll);
    scroll->setWidget(m_stack);
    split->setStretchFactor(0, 1);
    split->setSizes({1000, 360});

    // --- panneau pack
    auto *packW = new QWidget;
    auto *pf = new QFormLayout(packW);
    pf->addRow(new QLabel(tr("<b>Pack</b>")));
    m_packTitle = new QLineEdit;
    m_packDesc = new QPlainTextEdit;
    m_packDesc->setMaximumHeight(90);
    m_packVersion = new QSpinBox;
    m_packVersion->setRange(1, 999);
    m_packNight = new QCheckBox(tr("Disponible en mode nuit"));
    m_packThumb = new QLabel;
    m_packThumb->setFixedSize(160, 120);
    m_packThumb->setFrameShape(QFrame::StyledPanel);
    m_packThumb->setAlignment(Qt::AlignCenter);
    auto *thumbBtns = new QHBoxLayout;
    auto *thumbPick = new QPushButton(tr("Choisir…"));
    auto *thumbClear = new QPushButton(tr("Retirer"));
    thumbBtns->addWidget(thumbPick);
    thumbBtns->addWidget(thumbClear);
    m_packStats = new QLabel;
    m_packStats->setWordWrap(true);
    m_issues = new QListWidget;
    m_issues->setMinimumHeight(120);
    pf->addRow(tr("Titre :"), m_packTitle);
    pf->addRow(tr("Description :"), m_packDesc);
    pf->addRow(tr("Version :"), m_packVersion);
    pf->addRow(m_packNight);
    pf->addRow(tr("Vignette :"), m_packThumb);
    pf->addRow(QString(), thumbBtns);
    pf->addRow(m_packStats);
    pf->addRow(new QLabel(tr("<b>Vérification</b>")));
    pf->addRow(m_issues);
    m_stack->addWidget(packW);

    connect(m_packTitle, &QLineEdit::editingFinished, this, [this]() {
        if (m_packTitle->text() != m_pack.title)
            change(tr("Titre du pack"), [this]() { m_pack.title = m_packTitle->text(); });
    });
    connect(m_packDesc, &QPlainTextEdit::textChanged, this, [this]() {
        if (!m_rebuilding && m_packDesc->toPlainText() != m_pack.description) {
            m_pack.description = m_packDesc->toPlainText();   // pas d'entrée d'annulation par frappe
            m_undo.resetClean();
            updateTitle();
        }
    });
    connect(m_packVersion, &QSpinBox::valueChanged, this, [this](int v) {
        if (!m_rebuilding && v != m_pack.version)
            change(tr("Version"), [this, v]() { m_pack.version = v; });
    });
    connect(m_packNight, &QCheckBox::toggled, this, [this](bool b) {
        if (!m_rebuilding && b != m_pack.nightMode)
            change(tr("Mode nuit"), [this, b]() { m_pack.nightMode = b; });
    });
    connect(thumbPick, &QPushButton::clicked, this, [this]() {
        const QString f = QFileDialog::getOpenFileName(this, tr("Vignette du pack"), {}, tr("Images (*.png *.jpg *.jpeg *.bmp)"));
        QImage img(f);
        if (img.isNull())
            return;
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&b, "PNG");
        change(tr("Vignette"), [this, png]() { m_pack.thumbnail = png; });
    });
    connect(thumbClear, &QPushButton::clicked, this, [this]() {
        change(tr("Vignette"), [this]() { m_pack.thumbnail.clear(); });
    });

    // --- panneau scène
    auto *stageW = new QWidget;
    auto *sf = new QFormLayout(stageW);
    sf->addRow(new QLabel(tr("<b>Scène</b>")));
    m_stageName = new QLineEdit;
    m_stageStart = new QLabel;
    m_setStart = new QPushButton(tr("Définir comme nœud de départ"));
    m_stageImage = new QLabel;
    m_stageImage->setFixedSize(240, 180);
    m_stageImage->setFrameShape(QFrame::StyledPanel);
    m_stageImage->setAlignment(Qt::AlignCenter);
    auto *imgBtns = new QHBoxLayout;
    auto *imgPick = new QPushButton(QIcon::fromTheme("document-open"), tr("Image…"));
    auto *imgClear = new QPushButton(QIcon::fromTheme("edit-clear"), tr("Retirer"));
    imgBtns->addWidget(imgPick);
    imgBtns->addWidget(imgClear);
    m_stageAudio = new QLabel;
    m_stageAudio->setWordWrap(true);
    auto *audBtns = new QHBoxLayout;
    auto *audPlay = new QPushButton(QIcon::fromTheme("media-playback-start"), QString());
    audPlay->setToolTip(tr("Écouter"));
    auto *audStop = new QPushButton(QIcon::fromTheme("media-playback-stop"), QString());
    audStop->setToolTip(tr("Arrêter"));
    auto *audPick = new QPushButton(QIcon::fromTheme("document-open"), tr("Son…"));
    auto *audClear = new QPushButton(QIcon::fromTheme("edit-clear"), tr("Retirer"));
    for (auto *b : {audPlay, audStop, audPick, audClear})
        audBtns->addWidget(b);
    auto *ctrlBox = new QGroupBox(tr("Commandes actives sur ce nœud"));
    auto *cl = new QVBoxLayout(ctrlBox);
    const QString ctrlNames[5] = {tr("Molette (passer d'un choix à l'autre)"), tr("Bouton OK"), tr("Bouton maison"),
                                  tr("Pause"), tr("Lecture automatique (OK à la fin du son)")};
    for (int i = 0; i < 5; ++i) {
        m_ctrl[i] = new QCheckBox(ctrlNames[i]);
        cl->addWidget(m_ctrl[i]);
    }
    m_okAction = new QComboBox;
    m_okIndex = new QSpinBox;
    m_homeAction = new QComboBox;
    m_homeIndex = new QSpinBox;
    for (auto *s : {m_okIndex, m_homeIndex}) {
        s->setRange(-1, 99);
        s->setSpecialValueText(tr("au hasard"));
        s->setPrefix(tr("option "));
    }
    auto *delStage = new QPushButton(QIcon::fromTheme("edit-delete"), tr("Supprimer cette scène"));
    sf->addRow(tr("Nom :"), m_stageName);
    sf->addRow(m_stageStart);
    sf->addRow(m_setStart);
    sf->addRow(tr("Image :"), m_stageImage);
    sf->addRow(QString(), imgBtns);
    sf->addRow(tr("Son :"), m_stageAudio);
    sf->addRow(QString(), audBtns);
    sf->addRow(ctrlBox);
    sf->addRow(new QLabel(tr("<b>Après OK</b> (ou fin du son en lecture auto)")));
    sf->addRow(tr("Vers :"), m_okAction);
    sf->addRow(QString(), m_okIndex);
    sf->addRow(new QLabel(tr("<b>Bouton maison</b> (vide : retour au départ)")));
    sf->addRow(tr("Vers :"), m_homeAction);
    sf->addRow(QString(), m_homeIndex);
    sf->addRow(delStage);
    m_stack->addWidget(stageW);

    auto stageIdx = [this]() { return m_pack.stageIndex(m_selected); };
    connect(m_stageName, &QLineEdit::editingFinished, this, [this, stageIdx]() {
        const int i = stageIdx();
        if (i >= 0 && m_pack.stages[i].name != m_stageName->text())
            change(tr("Nom de la scène"), [this, i]() { m_pack.stages[i].name = m_stageName->text(); });
    });
    connect(m_setStart, &QPushButton::clicked, this, [this, stageIdx]() {
        const int i = stageIdx();
        if (i > 0)
            change(tr("Nœud de départ"), [this, i]() { m_pack.stages.move(i, 0); });
    });
    connect(imgPick, &QPushButton::clicked, this, [this, stageIdx]() {
        const int i = stageIdx();
        const QString f = QFileDialog::getOpenFileName(this, tr("Image de la scène"), {}, tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (i < 0 || f.isEmpty())
            return;
        const QString a = importImage(f);
        if (!a.isEmpty())
            change(tr("Image"), [this, i, a]() { m_pack.stages[i].image = a; });
    });
    connect(imgClear, &QPushButton::clicked, this, [this, stageIdx]() {
        const int i = stageIdx();
        if (i >= 0)
            change(tr("Image"), [this, i]() { m_pack.stages[i].image.clear(); });
    });
    connect(audPick, &QPushButton::clicked, this, [this, stageIdx]() {
        const int i = stageIdx();
        const QString f = QFileDialog::getOpenFileName(this, tr("Son de la scène"), {},
                                                       tr("Audio (*.mp3 *.ogg *.oga *.wav *.m4a *.flac *.opus)"));
        if (i < 0 || f.isEmpty())
            return;
        const QString a = importAudio(f);
        if (!a.isEmpty())
            change(tr("Son"), [this, i, a]() { m_pack.stages[i].audio = a; });
    });
    connect(audClear, &QPushButton::clicked, this, [this, stageIdx]() {
        const int i = stageIdx();
        if (i >= 0)
            change(tr("Son"), [this, i]() { m_pack.stages[i].audio.clear(); });
    });
    connect(audPlay, &QPushButton::clicked, this, [this, stageIdx]() {
        const int i = stageIdx();
        if (i >= 0)
            playAsset(m_pack.stages[i].audio);
    });
    connect(audStop, &QPushButton::clicked, this, &EditorPage::stopAudio);
    for (int c = 0; c < 5; ++c)
        connect(m_ctrl[c], &QCheckBox::toggled, this, [this, c, stageIdx](bool on) {
            const int i = stageIdx();
            if (m_rebuilding || i < 0)
                return;
            change(tr("Commandes"), [this, i, c, on]() {
                bool *f[5] = {&m_pack.stages[i].ctrl.wheel, &m_pack.stages[i].ctrl.ok, &m_pack.stages[i].ctrl.home,
                              &m_pack.stages[i].ctrl.pause, &m_pack.stages[i].ctrl.autoplay};
                *f[c] = on;
            });
        });
    auto transEdit = [this, stageIdx](QComboBox *combo, QSpinBox *spin, bool ok) {
        auto apply = [this, combo, spin, ok, stageIdx]() {
            const int i = stageIdx();
            if (m_rebuilding || i < 0)
                return;
            Transition t{combo->currentData().toString(), spin->value()};
            const Transition &cur = ok ? m_pack.stages[i].ok : m_pack.stages[i].home;
            if (t.actionId == cur.actionId && (t.optionIndex == cur.optionIndex || !t.isSet()))
                return;
            change(ok ? tr("Transition OK") : tr("Transition maison"), [this, i, t, ok]() {
                (ok ? m_pack.stages[i].ok : m_pack.stages[i].home) = t;
            });
        };
        connect(combo, &QComboBox::currentIndexChanged, this, apply);
        connect(spin, &QSpinBox::valueChanged, this, apply);
    };
    transEdit(m_okAction, m_okIndex, true);
    transEdit(m_homeAction, m_homeIndex, false);
    connect(delStage, &QPushButton::clicked, this, &EditorPage::deleteSelection);

    // --- panneau choix
    auto *actW = new QWidget;
    auto *af = new QFormLayout(actW);
    af->addRow(new QLabel(tr("<b>Choix</b> — la molette passe d'une option à l'autre")));
    m_actionName = new QLineEdit;
    m_options = new QListWidget;
    m_options->setMinimumHeight(160);
    auto *optBtns = new QHBoxLayout;
    auto *optUp = new QPushButton(QIcon::fromTheme("go-up"), QString());
    auto *optDown = new QPushButton(QIcon::fromTheme("go-down"), QString());
    auto *optDel = new QPushButton(QIcon::fromTheme("list-remove"), tr("Retirer"));
    optBtns->addWidget(optUp);
    optBtns->addWidget(optDown);
    optBtns->addWidget(optDel);
    m_addOption = new QComboBox;
    auto *optAdd = new QPushButton(QIcon::fromTheme("list-add"), tr("Ajouter"));
    auto *addRow = new QHBoxLayout;
    addRow->addWidget(m_addOption, 1);
    addRow->addWidget(optAdd);
    auto *delAct = new QPushButton(QIcon::fromTheme("edit-delete"), tr("Supprimer ce choix"));
    af->addRow(tr("Nom :"), m_actionName);
    af->addRow(tr("Options :"), m_options);
    af->addRow(QString(), optBtns);
    af->addRow(tr("Nouvelle :"), addRow);
    af->addRow(delAct);
    m_stack->addWidget(actW);

    auto actIdx = [this]() { return m_pack.actionIndex(m_selected); };
    connect(m_actionName, &QLineEdit::editingFinished, this, [this, actIdx]() {
        const int i = actIdx();
        if (i >= 0 && m_pack.actions[i].name != m_actionName->text())
            change(tr("Nom du choix"), [this, i]() { m_pack.actions[i].name = m_actionName->text(); });
    });
    auto moveOpt = [this, actIdx](int d) {
        const int i = actIdx(), r = m_options->currentRow();
        if (i < 0 || r < 0 || r + d < 0 || r + d >= m_pack.actions[i].options.size())
            return;
        change(tr("Ordre des options"), [this, i, r, d]() { m_pack.actions[i].options.move(r, r + d); });
        m_options->setCurrentRow(r + d);
    };
    connect(optUp, &QPushButton::clicked, this, [moveOpt]() { moveOpt(-1); });
    connect(optDown, &QPushButton::clicked, this, [moveOpt]() { moveOpt(1); });
    connect(optDel, &QPushButton::clicked, this, [this, actIdx]() {
        const int i = actIdx(), r = m_options->currentRow();
        if (i >= 0 && r >= 0)
            change(tr("Retrait d'option"), [this, i, r]() { m_pack.actions[i].options.removeAt(r); });
    });
    connect(optAdd, &QPushButton::clicked, this, [this, actIdx]() {
        const int i = actIdx();
        const QString target = m_addOption->currentData().toString();
        if (i >= 0 && !target.isEmpty())
            change(tr("Ajout d'option"), [this, i, target]() { m_pack.actions[i].options << target; });
    });
    connect(delAct, &QPushButton::clicked, this, &EditorPage::deleteSelection);

    connect(m_scene, &QGraphicsScene::selectionChanged, this, &EditorPage::selectionChanged);
    connect(m_view, &GraphView::linkRequested, this, &EditorPage::link);
    connect(m_view, &GraphView::addRequested, this, &EditorPage::addNode);
    connect(m_view, &GraphView::deleteRequested, this, &EditorPage::deleteSelection);
    connect(m_view, &GraphView::filesDropped, this, &EditorPage::importFiles);
}

void EditorPage::updateTitle()
{
    const QString name = m_path.isEmpty() ? tr("Nouveau pack (non enregistré)") : QFileInfo(m_path).fileName();
    m_fileLabel->setText((m_undo.isClean() ? QString() : QStringLiteral("● ")) + name);
}

void EditorPage::change(const QString &label, const std::function<void()> &mutate)
{
    const QJsonObject before = m_pack.toJson();
    mutate();
    m_undo.push(new SnapshotCommand(this, label, before, m_pack.toJson()));
    rebuildScene();
}

void EditorPage::restore(const QJsonObject &json)
{
    // les assets ne sont jamais retirés pendant l'édition : seule la structure est restaurée
    StudioPack p = StudioPack::fromJson(json);
    p.assets = m_pack.assets;
    p.assetFiles = m_pack.assetFiles;
    p.thumbnail = m_pack.thumbnail;
    m_pack = p;
    rebuildScene();
}

// ------------------------------------------------------------------ fichiers

void EditorPage::newPack()
{
    m_pack = StudioPack();
    m_pack.title = tr("Nouveau pack");
    StageNode cover;
    cover.uuid = newId();
    cover.name = tr("Couverture");
    cover.type = "cover";
    cover.pos = {0, 0};
    cover.ctrl = {true, true, false, false, false};
    m_pack.stages << cover;
    m_path.clear();
    m_undo.clear();
    m_selected.clear();
    rebuildScene();
    updateTitle();
}

bool EditorPage::maybeSave()
{
    if (m_undo.isClean())
        return true;
    const auto r = QMessageBox::question(this, tr("Éditeur"), tr("Le pack « %1 » a été modifié. Enregistrer ?").arg(m_pack.title),
                                         QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Save)
        return save();
    return r == QMessageBox::Discard;
}

void EditorPage::openDialog()
{
    if (!maybeSave())
        return;
    const QString f = QFileDialog::getOpenFileName(this, tr("Ouvrir un pack STUdio"), {}, tr("Packs STUdio (*.zip *.7z)"));
    if (!f.isEmpty())
        openPack(f);
}

bool EditorPage::openPack(const QString &path)
{
    if (!maybeSave())
        return false;
    StudioPack p;
    QString err;
    if (!StudioPack::loadArchive(path, p, &err, true, m_spill.path())) {
        QMessageBox::warning(this, tr("Ouvrir"), tr("Ce fichier n'est pas un pack STUdio lisible :\n%1").arg(err));
        return false;
    }
    m_pack = p;
    m_path = path.endsWith(".zip", Qt::CaseInsensitive) ? path : QString();   // les 7z sont réenregistrés en zip
    m_undo.clear();
    m_selected.clear();
    rebuildScene();
    updateTitle();
    QTimer::singleShot(0, m_view, &GraphView::zoomToFit);   // une fois la vue dimensionnée
    emit logMessage(QtInfoMsg, tr("Pack ouvert : %1 (%2 scènes, %3 choix)")
                                   .arg(m_pack.title).arg(m_pack.stages.size()).arg(m_pack.actions.size()));
    return true;
}

bool EditorPage::save()
{
    if (m_path.isEmpty())
        return saveAs();
    QString err;
    if (!m_pack.saveArchive(m_path, &err)) {
        QMessageBox::warning(this, tr("Enregistrer"), tr("Échec de l'enregistrement :\n%1").arg(err));
        return false;
    }
    m_undo.setClean();
    updateTitle();
    emit logMessage(QtInfoMsg, tr("Pack enregistré : %1").arg(m_path));
    return true;
}

bool EditorPage::saveAs()
{
    const QString dir = QSettings().value("editor/lastDir", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    QString f = QFileDialog::getSaveFileName(this, tr("Enregistrer le pack"), dir + "/" + m_pack.title + ".zip",
                                             tr("Pack STUdio (*.zip)"));
    if (f.isEmpty())
        return false;
    if (!f.endsWith(".zip", Qt::CaseInsensitive))
        f += ".zip";
    QSettings().setValue("editor/lastDir", QFileInfo(f).path());
    m_path = f;
    return save();
}

// ------------------------------------------------------------------ assets

QString EditorPage::addAsset(const QByteArray &data, const QString &ext)
{
    const QString name = QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex() + "." + ext;
    m_pack.putAsset(name, data, m_spill.path());
    return name;
}

QString EditorPage::importImage(const QString &file)
{
    QImage img(file);
    if (img.isNull()) {
        QMessageBox::warning(this, tr("Image"), tr("Image illisible : %1").arg(file));
        return {};
    }
    if (img.size() == QSize(320, 240)) {
        QFile f(file);
        if (f.open(QIODevice::ReadOnly))
            return addAsset(f.readAll(), QFileInfo(file).suffix().toLower());
    }
    // recadrage centré en 4:3 puis 320x240 (taille de l'écran de la Lunii)
    const qreal ratio = 4.0 / 3.0;
    QRect crop = img.rect();
    if (img.width() > img.height() * ratio)
        crop.setWidth(int(img.height() * ratio)), crop.moveLeft((img.width() - crop.width()) / 2);
    else
        crop.setHeight(int(img.width() / ratio)), crop.moveTop((img.height() - crop.height()) / 2);
    img = img.copy(crop).scaled(320, 240, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QByteArray png;
    QBuffer b(&png);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    return addAsset(png, "png");
}

QString EditorPage::importAudio(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray data = f.readAll();
    const QString ext = QFileInfo(file).suffix().toLower();
    if (ext == "mp3" && !lunii::transcodingRequired(file, data))
        return addAsset(lunii::stripMp3Tags(data), "mp3");
    // conversion immédiate en MP3 mono 44,1 kHz (format de la Lunii)
    QString err;
    const QByteArray mp3 = lunii::transcodeToMp3(data, &err);
    if (mp3.isEmpty()) {
        QMessageBox::warning(this, tr("Son"), tr("Conversion impossible (ffmpeg) :\n%1").arg(err));
        return {};
    }
    return addAsset(mp3, "mp3");
}

QPixmap EditorPage::assetPixmap(const QString &asset, const QSize &size) const
{
    if (asset.isEmpty() || !m_pack.hasAsset(asset))
        return {};
    QImage img;
    img.loadFromData(m_pack.assetData(asset));
    return QPixmap::fromImage(img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

QMediaPlayer *EditorPage::player()
{
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        m_audioOut = new QAudioOutput(this);
        m_player->setAudioOutput(m_audioOut);
    }
    return m_player;
}

void EditorPage::playAsset(const QString &asset)
{
    stopAudio();
    if (asset.isEmpty() || !m_pack.hasAsset(asset))
        return;
    if (m_pack.assetFiles.contains(asset)) {
        player()->setSource(QUrl::fromLocalFile(m_pack.assetFiles.value(asset)));
    } else {
        m_audioBuf = new QBuffer(this);
        m_audioBuf->setData(m_pack.assets.value(asset));
        m_audioBuf->open(QIODevice::ReadOnly);
        player()->setSourceDevice(m_audioBuf, QUrl(asset));
    }
    m_player->play();
}

void EditorPage::stopAudio()
{
    if (m_player) {
        m_player->stop();
        m_player->setSourceDevice(nullptr);
    }
    delete m_audioBuf;
    m_audioBuf = nullptr;
}

void EditorPage::importFiles(const QStringList &files, const QPointF &pos)
{
    // une scène par fichier déposé (image et/ou son de même nom regroupés)
    QMap<QString, QPair<QString, QString>> byName;   // nom -> (image, son)
    for (const QString &f : files) {
        const QString ext = QFileInfo(f).suffix().toLower();
        const QString base = QFileInfo(f).completeBaseName();
        if (QStringList{"png", "jpg", "jpeg", "bmp", "webp"}.contains(ext))
            byName[base].first = f;
        else if (QStringList{"mp3", "ogg", "oga", "wav", "m4a", "flac", "opus"}.contains(ext))
            byName[base].second = f;
    }
    if (byName.isEmpty())
        return;
    QList<StageNode> nodes;
    QPointF p = pos;
    for (auto it = byName.begin(); it != byName.end(); ++it) {
        StageNode n;
        n.uuid = newId();
        n.name = it.key();
        n.pos = p;
        n.ctrl = {false, false, true, true, true};
        if (!it->first.isEmpty())
            n.image = importImage(it->first);
        if (!it->second.isEmpty())
            n.audio = importAudio(it->second);
        nodes << n;
        p += QPointF(0, 160);
    }
    change(tr("Import de fichiers"), [this, nodes]() { m_pack.stages << nodes; });
}

// ------------------------------------------------------------------ graphe

void EditorPage::rebuildScene()
{
    m_rebuilding = true;
    const QString keep = m_selected;
    m_scene->clear();
    QHash<QString, NodeItem *> items;
    for (int i = 0; i < m_pack.stages.size(); ++i) {
        const StageNode &n = m_pack.stages[i];
        QString audio;
        if (!n.audio.isEmpty())
            audio = m_pack.hasAsset(n.audio) ? tr("%1 Ko").arg(m_pack.assetSize(n.audio) / 1024) : tr("absent !");
        auto *it = new StageItem(n, i == 0, assetPixmap(n.image, {88, 66}), audio);
        m_scene->addItem(it);
        connect(it, &NodeItem::moved, this, [this](const QString &id, const QPointF &pos) {
            const int i = m_pack.stageIndex(id);
            if (i >= 0)
                change(tr("Déplacement"), [this, i, pos]() { m_pack.stages[i].pos = pos; });
        }, Qt::QueuedConnection);
        items.insert(n.uuid, it);
    }
    for (const ActionNode &a : m_pack.actions) {
        QStringList names;
        for (const QString &o : a.options) {
            const int si = m_pack.stageIndex(o);
            names << (si >= 0 ? m_pack.stages[si].name : tr("(absent)"));
        }
        auto *it = new ActionItem(a, names);
        m_scene->addItem(it);
        connect(it, &NodeItem::moved, this, [this](const QString &id, const QPointF &pos) {
            const int i = m_pack.actionIndex(id);
            if (i >= 0)
                change(tr("Déplacement"), [this, i, pos]() { m_pack.actions[i].pos = pos; });
        }, Qt::QueuedConnection);
        items.insert(a.id, it);
    }
    auto label = [this](const Transition &t) {
        const int ai = m_pack.actionIndex(t.actionId);
        if (ai < 0 || m_pack.actions[ai].options.size() <= 1)
            return QString();
        return t.optionIndex < 0 ? tr("au hasard") : QString("#%1").arg(t.optionIndex + 1);
    };
    for (const StageNode &n : m_pack.stages) {
        auto *s = static_cast<StageItem *>(items.value(n.uuid));
        if (auto *a = items.value(n.ok.actionId))
            m_scene->addItem(new EdgeItem(EdgeItem::OkEdge, s->okPort(), a->inPort(), label(n.ok)));
        if (auto *a = items.value(n.home.actionId); a && m_showHome)
            m_scene->addItem(new EdgeItem(EdgeItem::HomeEdge, s->homePort(), a->inPort(), label(n.home)));
    }
    for (const ActionNode &a : m_pack.actions) {
        auto *ai = static_cast<ActionItem *>(items.value(a.id));
        for (int i = 0; i < a.options.size(); ++i)
            if (auto *s = items.value(a.options[i]))
                m_scene->addItem(new EdgeItem(EdgeItem::OptionEdge, ai->optionPort(i), s->inPort(), QString()));
    }
    if (auto *sel = items.value(keep))
        sel->setSelected(true);
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-2000, -2000, 2000, 2000));
    m_rebuilding = false;
    m_selected = keep;
    if (keep.isEmpty() || !items.contains(keep))
        showPackPanel();
    else if (m_pack.stageIndex(keep) >= 0)
        showStagePanel(m_pack.stageIndex(keep));
    else
        showActionPanel(m_pack.actionIndex(keep));
}

QString EditorPage::selectedId() const
{
    for (QGraphicsItem *it : m_scene->selectedItems())
        if (auto *n = dynamic_cast<NodeItem *>(it))
            return n->id();
    return {};
}

void EditorPage::selectionChanged()
{
    if (m_rebuilding)
        return;
    m_selected = selectedId();
    if (m_selected.isEmpty())
        showPackPanel();
    else if (m_pack.stageIndex(m_selected) >= 0)
        showStagePanel(m_pack.stageIndex(m_selected));
    else
        showActionPanel(m_pack.actionIndex(m_selected));
}

void EditorPage::showPackPanel()
{
    m_rebuilding = true;
    m_stack->setCurrentIndex(0);
    m_packTitle->setText(m_pack.title);
    if (m_packDesc->toPlainText() != m_pack.description)
        m_packDesc->setPlainText(m_pack.description);
    m_packVersion->setValue(m_pack.version);
    m_packNight->setChecked(m_pack.nightMode);
    QImage thumb;
    thumb.loadFromData(m_pack.thumbnail);
    if (thumb.isNull())
        m_packThumb->setText(tr("aucune"));
    else
        m_packThumb->setPixmap(QPixmap::fromImage(thumb.scaled(160, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    qint64 size = 0;
    QSet<QString> used;
    for (const StageNode &n : m_pack.stages)
        used << n.image << n.audio;
    for (const QString &a : used)
        size += m_pack.assetSize(a);
    m_packStats->setText(tr("%1 scènes, %2 choix, %3 Mo de ressources.\nUUID : %4")
                             .arg(m_pack.stages.size()).arg(m_pack.actions.size())
                             .arg(size / 1024.0 / 1024, 0, 'f', 1).arg(m_pack.packUuid().toString(QUuid::WithoutBraces)));
    m_issues->clear();
    const QStringList issues = m_pack.validate();
    if (issues.isEmpty())
        m_issues->addItem(new QListWidgetItem(QIcon::fromTheme("dialog-ok-apply"), tr("Aucun problème détecté")));
    for (const QString &i : issues)
        m_issues->addItem(new QListWidgetItem(QIcon::fromTheme("dialog-warning"), i));
    m_rebuilding = false;
}

static void fillActions(QComboBox *combo, const StudioPack &p, const QString &current)
{
    combo->clear();
    combo->addItem(QObject::tr("(aucune)"), QString());
    for (const ActionNode &a : p.actions) {
        QString label = a.name.isEmpty() ? QObject::tr("Choix") : a.name;
        if (a.options.size() == 1) {
            const int si = p.stageIndex(a.options.first());
            if (si >= 0)
                label += QString::fromUtf8(" → ") + p.stages[si].name;
        } else {
            label += QObject::tr(" (%1 options)").arg(a.options.size());
        }
        combo->addItem(label, a.id);
    }
    combo->setCurrentIndex(qMax(0, combo->findData(current)));
}

void EditorPage::showStagePanel(int i)
{
    if (i < 0)
        return showPackPanel();
    m_rebuilding = true;
    const StageNode &n = m_pack.stages[i];
    m_stack->setCurrentIndex(1);
    m_stageName->setText(n.name);
    m_stageStart->setText(i == 0 ? tr("★ Nœud de départ du pack (couverture)") : QString());
    m_setStart->setVisible(i != 0);
    const QPixmap pm = assetPixmap(n.image, {240, 180});
    if (pm.isNull())
        m_stageImage->setText(n.image.isEmpty() ? tr("pas d'image") : tr("image absente"));
    else
        m_stageImage->setPixmap(pm);
    if (n.audio.isEmpty()) {
        m_stageAudio->setText(tr("pas de son"));
    } else {
        const QByteArray d = m_pack.assetData(n.audio).left(256 * 1024);   // en-tête suffisant
        const lunii::Mp3Info mi = lunii::mp3Info(d);
        QString info = tr("%1 (%2 Ko)").arg(n.audio.left(12) + "…").arg(m_pack.assetSize(n.audio) / 1024);
        if (n.audio.endsWith(".mp3") && mi.valid && (!mi.mono || mi.sampleRate < 44100))
            info += tr("\nsera converti à l'envoi");
        m_stageAudio->setText(info);
    }
    const bool c[5] = {n.ctrl.wheel, n.ctrl.ok, n.ctrl.home, n.ctrl.pause, n.ctrl.autoplay};
    for (int k = 0; k < 5; ++k)
        m_ctrl[k]->setChecked(c[k]);
    fillActions(m_okAction, m_pack, n.ok.actionId);
    fillActions(m_homeAction, m_pack, n.home.actionId);
    m_okIndex->setValue(n.ok.optionIndex);
    m_homeIndex->setValue(n.home.optionIndex);
    m_okIndex->setEnabled(n.ok.isSet());
    m_homeIndex->setEnabled(n.home.isSet());
    m_rebuilding = false;
}

void EditorPage::showActionPanel(int i)
{
    if (i < 0)
        return showPackPanel();
    m_rebuilding = true;
    const ActionNode &a = m_pack.actions[i];
    m_stack->setCurrentIndex(2);
    m_actionName->setText(a.name);
    m_options->clear();
    for (const QString &o : a.options) {
        const int si = m_pack.stageIndex(o);
        m_options->addItem(si >= 0 ? m_pack.stages[si].name : tr("(scène absente)"));
    }
    m_addOption->clear();
    for (const StageNode &s : m_pack.stages)
        m_addOption->addItem(s.name.isEmpty() ? s.uuid.left(8) : s.name, s.uuid);
    m_rebuilding = false;
}

void EditorPage::link(const QString &fromId, int portRole, int portIndex, const QString &toId)
{
    const int fromStage = m_pack.stageIndex(fromId);
    const int fromAction = m_pack.actionIndex(fromId);
    const int toStage = m_pack.stageIndex(toId);
    const int toAction = m_pack.actionIndex(toId);
    if (fromStage >= 0 && (portRole == PortItem::Ok || portRole == PortItem::Home)) {
        const bool ok = portRole == PortItem::Ok;
        if (toAction >= 0) {
            change(tr("Lien"), [=, this]() { (ok ? m_pack.stages[fromStage].ok : m_pack.stages[fromStage].home) = {toId, 0}; });
        } else if (toStage >= 0) {
            // scène -> scène : un nœud de choix à une option est créé entre les deux (comme STUdio)
            change(tr("Lien"), [=, this]() {
                ActionNode a;
                a.id = newId();
                a.name = m_pack.stages[toStage].name;
                a.options << toId;
                a.pos = (m_pack.stages[fromStage].pos + m_pack.stages[toStage].pos) / 2 + QPointF(0, 40);
                m_pack.actions << a;
                (ok ? m_pack.stages[fromStage].ok : m_pack.stages[fromStage].home) = {a.id, 0};
            });
        }
    } else if (fromAction >= 0 && toStage >= 0) {
        if (portRole == PortItem::AddOption)
            change(tr("Ajout d'option"), [=, this]() { m_pack.actions[fromAction].options << toId; });
        else if (portRole == PortItem::Option)
            change(tr("Option"), [=, this]() { m_pack.actions[fromAction].options[portIndex] = toId; });
    }
}

void EditorPage::addNode(bool stage, const QPointF &pos)
{
    const QString id = newId();
    change(stage ? tr("Nouvelle scène") : tr("Nouveau choix"), [=, this]() {
        if (stage) {
            StageNode n;
            n.uuid = id;
            n.name = tr("Scène %1").arg(m_pack.stages.size() + 1);
            n.pos = pos;
            n.ctrl = {true, true, true, false, false};
            m_pack.stages << n;
        } else {
            ActionNode a;
            a.id = id;
            a.name = tr("Choix %1").arg(m_pack.actions.size() + 1);
            a.pos = pos;
            m_pack.actions << a;
        }
    });
    m_selected = id;
    rebuildScene();
}

void EditorPage::deleteSelection()
{
    QStringList nodes;
    QList<EdgeItem *> edges;
    for (QGraphicsItem *it : m_scene->selectedItems()) {
        if (auto *n = dynamic_cast<NodeItem *>(it))
            nodes << n->id();
        else if (auto *e = dynamic_cast<EdgeItem *>(it))
            edges << e;
    }
    if (nodes.isEmpty() && edges.isEmpty() && !m_selected.isEmpty())
        nodes << m_selected;
    if (nodes.isEmpty() && edges.isEmpty())
        return;
    if (!nodes.isEmpty() && m_pack.stageIndex(nodes.first()) == 0 && nodes.size() == 1 && m_pack.stages.size() > 1
        && QMessageBox::question(this, tr("Supprimer"), tr("Supprimer le nœud de départ ? Le nœud suivant deviendra la couverture."))
               != QMessageBox::Yes)
        return;
    // liens : on identifie leur origine avant de reconstruire la scène
    struct EdgeRef { QString from; int role; int index; QString to; };
    QList<EdgeRef> refs;
    for (EdgeItem *e : edges)
        refs << EdgeRef{e->from()->node()->id(), e->from()->role(), e->from()->index(), e->to()->node()->id()};
    change(tr("Suppression"), [this, nodes, refs]() {
        for (const EdgeRef &r : refs) {
            const int s = m_pack.stageIndex(r.from), a = m_pack.actionIndex(r.from);
            if (s >= 0 && r.role == PortItem::Ok)
                m_pack.stages[s].ok = {};
            else if (s >= 0 && r.role == PortItem::Home)
                m_pack.stages[s].home = {};
            else if (a >= 0 && r.index < m_pack.actions[a].options.size())
                m_pack.actions[a].options.removeAt(r.index);
        }
        for (const QString &id : nodes) {
            const int s = m_pack.stageIndex(id);
            if (s >= 0) {
                m_pack.stages.removeAt(s);
                for (ActionNode &a : m_pack.actions)
                    a.options.removeAll(id);
            }
            const int a = m_pack.actionIndex(id);
            if (a >= 0) {
                m_pack.actions.removeAt(a);
                for (StageNode &n : m_pack.stages) {
                    if (n.ok.actionId == id)
                        n.ok = {};
                    if (n.home.actionId == id)
                        n.home = {};
                }
            }
        }
        // les options qui dépassent après retrait sont ramenées à la dernière
        for (StageNode &n : m_pack.stages)
            for (Transition *t : {&n.ok, &n.home}) {
                const int ai = m_pack.actionIndex(t->actionId);
                if (ai >= 0 && t->optionIndex >= m_pack.actions[ai].options.size())
                    t->optionIndex = qMax(0, int(m_pack.actions[ai].options.size()) - 1);
            }
    });
    m_selected.clear();
    rebuildScene();
}

// Rangement automatique : colonnes par profondeur depuis le nœud de départ (scènes et choix alternés)
void EditorPage::autoLayout()
{
    if (m_pack.stages.isEmpty())
        return;
    QHash<QString, int> depth;
    QList<QString> queue{m_pack.stages.first().uuid};
    depth.insert(queue.first(), 0);
    while (!queue.isEmpty()) {
        const QString id = queue.takeFirst();
        const int d = depth.value(id);
        QStringList next;
        const int s = m_pack.stageIndex(id);
        if (s >= 0) {
            // la transition maison mène souvent vers l'arrière : seule OK fait avancer
            if (m_pack.stages[s].ok.isSet())
                next << m_pack.stages[s].ok.actionId;
        } else {
            const int a = m_pack.actionIndex(id);
            if (a >= 0)
                next << m_pack.actions[a].options;
        }
        for (const QString &n : next)
            if (!depth.contains(n)) {
                depth.insert(n, d + 1);
                queue << n;
            }
    }
    int maxDepth = 0;
    for (int d : depth)
        maxDepth = qMax(maxDepth, d);
    QHash<int, int> rowsUsed;
    auto place = [&](const QString &id) {
        const int d = depth.value(id, maxDepth + 1);
        const int r = rowsUsed[d]++;
        return QPointF(d * 300, r * 170);
    };
    change(tr("Rangement"), [&]() {
        for (StageNode &n : m_pack.stages)
            n.pos = place(n.uuid);
        for (ActionNode &a : m_pack.actions)
            a.pos = place(a.id) + QPointF(20, 20);
    });
    m_view->zoomToFit();
}

void EditorPage::validate()
{
    m_scene->clearSelection();
    m_selected.clear();
    showPackPanel();
    const QStringList issues = m_pack.validate();
    emit logMessage(issues.isEmpty() ? QtInfoMsg : QtWarningMsg,
                    issues.isEmpty() ? tr("Pack vérifié : aucun problème") : tr("%n problème(s) détecté(s)", nullptr, int(issues.size())));
}

void EditorPage::simulate()
{
    stopAudio();
    Simulator sim(m_pack, this);
    sim.exec();
}

void EditorPage::send()
{
    const QStringList issues = m_pack.validate();
    if (!issues.isEmpty()
        && QMessageBox::question(this, tr("Envoyer sur la Lunii"),
                                 tr("Le pack présente %n problème(s) :\n\n%1\n\nEnvoyer quand même ?", nullptr, int(issues.size()))
                                     .arg(issues.mid(0, 8).join('\n')))
               != QMessageBox::Yes)
        return;
    if (!m_undo.isClean() || m_path.isEmpty()) {
        if (QMessageBox::question(this, tr("Envoyer sur la Lunii"), tr("Le pack doit d'abord être enregistré. Enregistrer ?"))
            != QMessageBox::Yes)
            return;
        if (!save())
            return;
    }
    emit sendToDevice(m_path);
}

void EditorPage::wizard()
{
    if (!maybeSave())
        return;
    PackWizard w(m_spill.path(), this);
    if (w.exec() != QDialog::Accepted)
        return;
    m_pack = w.result();
    m_path.clear();
    m_undo.clear();
    m_undo.resetClean();
    m_selected.clear();
    rebuildScene();
    updateTitle();
    QTimer::singleShot(0, m_view, &GraphView::zoomToFit);
}
