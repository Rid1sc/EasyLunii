#include "app/mainwindow.h"

#include "app/devicepanel.h"
#include "app/editorpage.h"
#include "app/jobqueue.h"
#include "app/library.h"
#include "app/librarypage.h"
#include "core/device.h"
#include "core/storydb.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QtConcurrent>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("Lunii Natif"));
    setWindowIcon(QIcon(":/icons/lunii-natif.png"));

    m_lib = new Library(this);
    m_jobs = new JobQueue(this);

    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_libPage = new LibraryPage(m_lib, m_jobs, m_tabs);
    m_editor = new EditorPage(m_jobs, m_tabs);
    m_tabs->addTab(m_libPage, QIcon::fromTheme("folder-library"), tr("Bibliothèque"));
    m_tabs->addTab(m_editor, QIcon::fromTheme("document-edit"), tr("Éditeur"));
    setCentralWidget(m_tabs);

    // journal
    auto *dock = new QDockWidget(tr("Journal"), this);
    dock->setObjectName("log");
    m_log = new QPlainTextEdit(dock);
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    dock->setWidget(m_log);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
    dock->hide();

    // barre d'état : tâche en cours
    m_jobLabel = new QLabel(this);
    m_jobProgress = new QProgressBar(this);
    m_jobProgress->setMaximumWidth(220);
    m_jobProgress->setMaximumHeight(14);
    m_jobProgress->hide();
    m_cancel = new QToolButton(this);
    m_cancel->setIcon(QIcon::fromTheme("process-stop"));
    m_cancel->setToolTip(tr("Annuler les transferts"));
    m_cancel->setAutoRaise(true);
    m_cancel->hide();
    statusBar()->addPermanentWidget(m_jobLabel);
    statusBar()->addPermanentWidget(m_jobProgress);
    statusBar()->addPermanentWidget(m_cancel);

    connect(m_jobs, &JobQueue::started, this, [this](const QString &label, int remaining) {
        m_jobLabel->setText(remaining ? tr("%1 (+%2 en attente)").arg(label).arg(remaining) : label);
        m_jobProgress->setRange(0, 0);
        m_jobProgress->show();
        m_cancel->show();
    });
    connect(m_jobs, &JobQueue::finished, this, [this](const QString &label, bool ok) {
        log(ok ? QtInfoMsg : QtWarningMsg, ok ? tr("Terminé : %1").arg(label) : tr("Échec : %1").arg(label));
    });
    connect(m_jobs, &JobQueue::idle, this, [this]() {
        m_jobLabel->clear();
        m_jobProgress->hide();
        m_cancel->hide();
    });
    connect(m_cancel, &QToolButton::clicked, this, [this]() {
        m_jobs->clearPending();
        if (auto *d = m_libPage->devicePanel()->device())
            d->requestAbort();
        log(QtWarningMsg, tr("Annulation demandée"));
    });
    connect(m_libPage->devicePanel(), &DevicePanel::progress, this, [this](const QString &, int c, int t) {
        if (t > 0) {
            m_jobProgress->setRange(0, t);
            m_jobProgress->setValue(c + 1);
        }
    });
    connect(m_libPage, &LibraryPage::logMessage, this, &MainWindow::log);
    connect(m_libPage, &LibraryPage::openInEditor, this, &MainWindow::openInEditor);
    connect(m_editor, &EditorPage::logMessage, this, &MainWindow::log);
    connect(m_editor, &EditorPage::sendToDevice, this, [this](const QString &path) {
        m_libPage->devicePanel()->sendToDevice({path});
    });

    buildMenus();
    const QSettings s;
    restoreGeometry(s.value("window/geometry").toByteArray());
    restoreState(s.value("window/state").toByteArray());
    if (!s.contains("window/geometry"))
        resize(1500, 900);

    // base des titres (réseau au premier lancement) puis analyse de la bibliothèque
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w]() {
        if (!w->result())
            log(QtWarningMsg, tr("Base officielle Lunii indisponible (hors ligne ?) : titres en cache utilisés."));
        w->deleteLater();
        m_libPage->devicePanel()->reloadStories();
        m_lib->rescan();
    });
    w->setFuture(QtConcurrent::run([]() { return lunii::StoryDb::instance().load(false); }));
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu(tr("&Fichier"));
    file->addAction(QIcon::fromTheme("folder-new"), tr("Ajouter un dossier à la bibliothèque…"), m_libPage,
                    &LibraryPage::addSourceDialog);
    file->addAction(QIcon::fromTheme("view-refresh"), tr("Analyser à nouveau la bibliothèque"), QKeySequence::Refresh,
                    m_lib, &Library::rescan);
    file->addSeparator();
    file->addAction(QIcon::fromTheme("document-new"), tr("Nouveau pack…"), QKeySequence::New, this, [this]() {
        m_tabs->setCurrentWidget(m_editor);
        m_editor->newPack();
    });
    file->addAction(QIcon::fromTheme("document-open"), tr("Ouvrir un pack STUdio…"), QKeySequence::Open, this, [this]() {
        m_tabs->setCurrentWidget(m_editor);
        m_editor->openDialog();
    });
    file->addSeparator();
    file->addAction(QIcon::fromTheme("application-exit"), tr("Quitter"), QKeySequence::Quit, this, &QWidget::close);

    auto *dev = menuBar()->addMenu(tr("&Lunii"));
    DevicePanel *panel = m_libPage->devicePanel();
    dev->addAction(QIcon::fromTheme("weather-clear-night"), tr("Réglages du mode nuit…"), panel, &DevicePanel::nightModeSettings);
    dev->addSeparator();
    dev->addAction(QIcon::fromTheme("edit-find"), tr("Récupérer les histoires perdues…"), panel, &DevicePanel::recover);
    dev->addAction(QIcon::fromTheme("edit-clear"), tr("Nettoyer les dossiers orphelins…"), panel, &DevicePanel::cleanup);
    dev->addSeparator();
    dev->addAction(QIcon::fromTheme("view-refresh"), tr("Mettre à jour la base officielle Lunii"), this, [this]() {
        log(QtInfoMsg, tr("Téléchargement de la base officielle…"));
        auto *w = new QFutureWatcher<bool>(this);
        connect(w, &QFutureWatcher<bool>::finished, this, [this, w]() {
            log(w->result() ? QtInfoMsg : QtWarningMsg,
                w->result() ? tr("Base officielle à jour") : tr("Échec du téléchargement de la base officielle"));
            w->deleteLater();
            m_libPage->devicePanel()->reloadStories();
        });
        w->setFuture(QtConcurrent::run([]() { return lunii::StoryDb::instance().load(true); }));
    });

    auto *view = menuBar()->addMenu(tr("&Affichage"));
    auto *logAct = findChild<QDockWidget *>("log")->toggleViewAction();
    logAct->setText(tr("Journal"));
    logAct->setShortcut(QKeySequence("Ctrl+L"));
    view->addAction(logAct);

    auto *help = menuBar()->addMenu(tr("&Aide"));
    help->addAction(QIcon::fromTheme("help-about"), tr("À propos"), this, &MainWindow::about);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("À propos de Lunii Natif"),
                       tr("<h3>Lunii Natif %1</h3>"
                          "<p>Gestion des histoires de la Lunii : bibliothèque, transferts et éditeur de packs.</p>"
                          "<p>Application C++/Qt dérivée de <a href='https://github.com/o-daneel/Lunii.QT'>Lunii.QT</a> "
                          "(o-daneel, GPLv3). Format des packs : "
                          "<a href='https://github.com/marian-m12l/studio'>STUdio</a> (marian-m12l, MPL-2.0).</p>"
                          "<p>Licence GPLv3.</p>")
                           .arg(QApplication::applicationVersion()));
}

void MainWindow::log(int level, const QString &message)
{
    const char *icon = level == QtCriticalMsg ? "🛑 " : level == QtWarningMsg ? "⚠️ " : level == QtDebugMsg ? "· " : "";
    m_log->appendPlainText(QString("%1  %2%3").arg(QTime::currentTime().toString("HH:mm:ss"), icon, message));
    if (level != QtDebugMsg)
        statusBar()->showMessage(message, 6000);
    if (level == QtCriticalMsg)
        findChild<QDockWidget *>("log")->show();
}

void MainWindow::openInEditor(const QString &path)
{
    m_tabs->setCurrentWidget(m_editor);
    m_editor->openPack(path);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (m_jobs->isBusy()
        && QMessageBox::question(this, tr("Quitter"), tr("Des transferts sont en cours. Quitter quand même ?"))
               != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    if (!m_editor->maybeSave()) {
        e->ignore();
        return;
    }
    QSettings s;
    s.setValue("window/geometry", saveGeometry());
    s.setValue("window/state", saveState());
    e->accept();
}
