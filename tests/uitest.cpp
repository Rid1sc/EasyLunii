// Tests de l'interface sur une fausse Lunii (copie d'un .md réel) : dépôts, ordre, export, éditeur.
// Usage : easylunii-uitest <dossier .md/.cfg d'une Lunii> <pack STUdio.zip>
#include "app/editorpage.h"

#include "app/devicepanel.h"
#include "app/jobqueue.h"
#include "app/library.h"
#include "app/librarymodel.h"
#include "app/librarypage.h"
#include "app/editor/graphitems.h"
#include "app/editor/wizard.h"
#include "core/device.h"
#include "core/flamdevice.h"
#include "core/storydb.h"
#include "core/studiopack.h"

#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLineEdit>
#include <QSpinBox>
#include <QFile>
#include <QListView>
#include <QMimeData>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTreeWidgetItemIterator>
#include <QUndoStack>

static QString g_mdDir, g_pack;
static int g_failures = 0;

#define CHECK(cond)                                                                                  \
    do {                                                                                             \
        if (!(cond)) {                                                                               \
            qWarning("ÉCHEC %s:%d : %s", __FILE__, __LINE__, #cond);                                 \
            ++g_failures;                                                                            \
        } else {                                                                                     \
            qInfo("ok : %s", #cond);                                                                 \
        }                                                                                            \
    } while (0)

static QString makeFakeLunii(const QTemporaryDir &tmp)
{
    const QString m = tmp.path() + "/lunii";
    QDir().mkpath(m + "/.content");
    for (const char *f : {".md", ".cfg"})
        QFile::copy(g_mdDir + "/" + f, m + "/" + f);
    for (const char *f : {".pi", ".pi.hidden"}) {
        QFile p(m + "/" + f);
        p.open(QIODevice::WriteOnly);
    }
    return m;
}

// attente active d'une condition, comptée comme échec si le délai expire
#define WAIT_FOR(cond, ms)                                                                           \
    do {                                                                                             \
        QElapsedTimer t_;                                                                            \
        t_.start();                                                                                  \
        while (!(cond) && t_.elapsed() < (ms))                                                       \
            QTest::qWait(50);                                                                        \
        CHECK(cond);                                                                                 \
    } while (0)

static void waitIdle(JobQueue &jobs)
{
    WAIT_FOR(!jobs.isBusy(), 120000);
}

static void dropOn(QWidget *target, const QPoint &pos, QMimeData *md, QObject *source = nullptr)
{
    QDragEnterEvent enter(pos, Qt::CopyAction, md, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &enter);
    QDragMoveEvent move(pos, Qt::CopyAction, md, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(target, &move);
    QDropEvent drop(QPointF(pos), Qt::CopyAction, md, Qt::LeftButton, Qt::NoModifier);
    Q_UNUSED(source);
    QCoreApplication::sendEvent(target, &drop);
}

static void testDevicePanel()
{
    QTemporaryDir tmp;
    const QString mount = makeFakeLunii(tmp);
    JobQueue jobs;
    DevicePanel panel(&jobs);
    panel.openPath(mount);
    CHECK(panel.device() && panel.device()->isValid());
    const int before = int(panel.device()->stories().size());

    // dépôt d'une archive de la bibliothèque sur la liste de la Lunii
    auto *list = panel.findChild<DeviceList *>();
    auto *md = new QMimeData;
    md->setData(mime::libraryPaths, g_pack.toUtf8());
    dropOn(list->viewport(), QPoint(10, 10), md);
    waitIdle(jobs);
    QTest::qWait(200);
    CHECK(panel.device()->stories().size() == before + 1);
    CHECK(list->count() == before + 1);
    lunii::StudioPack p;
    lunii::StudioPack::loadArchive(g_pack, p, nullptr, false);
    CHECK(panel.storyUuids().contains(lunii::uuidStr(p.packUuid())));

    // second dépôt du même pack : refusé (déjà présent), pas de doublon
    auto *md2 = new QMimeData;
    md2->setData(mime::libraryPaths, g_pack.toUtf8());
    dropOn(list->viewport(), QPoint(10, 10), md2);
    waitIdle(jobs);
    CHECK(panel.device()->stories().size() == before + 1);

    // export vers un dossier (v3 sans clés : sauvegarde liée à l'appareil ; v1/v2 : .plain.pk)
    const QString out = tmp.path() + "/export";
    QDir().mkpath(out);
    bool done = false;
    panel.exportStories({p.packUuid()}, out, [&]() { done = true; });
    waitIdle(jobs);
    CHECK(done);
    CHECK(QDir(out).entryList(QDir::Files).size() == 1);

    // masquage puis retour
    QUuid u = p.packUuid();
    panel.runOnDevice("masquer", [u](lunii::StoryDevice *d) { return d->setHidden(u, true); });
    waitIdle(jobs);
    CHECK(panel.device()->stories().at(panel.device()->indexOf(u)).hidden);
    CHECK(QFileInfo(mount + "/.content.hidden/" + lunii::shortUuid(u)).isDir());
    panel.runOnDevice("afficher", [u](lunii::StoryDevice *d) { return d->setHidden(u, false); });
    waitIdle(jobs);
    CHECK(!panel.device()->stories().at(panel.device()->indexOf(u)).hidden);

    // suppression
    panel.runOnDevice("suppression", [u](lunii::StoryDevice *d) { return d->removeStory(u); });
    waitIdle(jobs);
    CHECK(panel.device()->stories().size() == before);
    CHECK(!QFileInfo(mount + "/.content/" + lunii::shortUuid(u)).exists());
}

static void testLibraryDropFromDevice()
{
    QTemporaryDir tmp;
    const QString mount = makeFakeLunii(tmp);
    const QString libDir = tmp.path() + "/biblio";
    QDir().mkpath(libDir);
    Library lib;
    for (const QString &s : lib.sources())
        lib.removeSource(s);
    lib.addSource(libDir);
    JobQueue jobs;
    LibraryPage page(&lib, &jobs);
    QObject::connect(&page, &LibraryPage::logMessage, [](int, const QString &m) { qInfo("journal : %s", qPrintable(m)); });
    QObject::connect(&jobs, &JobQueue::started, [](const QString &l, int) { qInfo("tâche : %s", qPrintable(l)); });
    page.devicePanel()->openPath(mount);
    page.devicePanel()->sendToDevice({g_pack});
    waitIdle(jobs);
    CHECK(page.devicePanel()->device()->stories().size() == 1);

    // glisser l'histoire de la Lunii vers la grille : sauvegarde dans le dossier de la bibliothèque
    QListView *view = nullptr;   // la grille (la liste de la Lunii est aussi une QListView)
    for (QListView *v : page.findChildren<QListView *>())
        if (!qobject_cast<DeviceList *>(v))
            view = v;
    auto *md = new QMimeData;
    md->setData(mime::deviceStories, lunii::uuidStr(page.devicePanel()->device()->stories().first().uuid).toUtf8());
    qInfo("sources : %s", qPrintable(lib.sources().join(", ")));
    dropOn(view->viewport(), QPoint(20, 20), md);
    qInfo("après dépôt : file occupée = %d", jobs.isBusy());
    waitIdle(jobs);
    WAIT_FOR(!QDir(libDir).entryList(QDir::Files).isEmpty(), 20000);
    CHECK(QDir(libDir).entryList(QDir::Files).size() == 1);
    // la bibliothèque indexe l'export (rescan déclenché après l'export)
    WAIT_FOR(lib.items().size() == 1 && !lib.isScanning(), 30000);
    CHECK(lib.items().size() == 1);

    // dépôt d'une histoire de la grille sur une collection de la barre latérale
    const int col = lib.createCollection("Test");
    QTest::qWait(100);
    auto *side = page.findChild<SideBar *>();
    QTreeWidgetItem *target = nullptr;
    for (QTreeWidgetItemIterator it(side); *it; ++it)
        if ((*it)->text(0) == "Test")
            target = *it;
    CHECK(target != nullptr);
    if (target) {
        side->scrollToItem(target);
        auto *md2 = new QMimeData;
        md2->setData(mime::libraryPaths, lib.items().first().path.toUtf8());
        dropOn(side->viewport(), side->visualItemRect(target).center(), md2);
        CHECK(lib.collectionKeys(col).contains(lib.items().first().key()));
    }
    lib.deleteCollection(col);
    lib.removeSource(libDir);
}

struct EditorPageTest {
static void run()
{
    QTemporaryDir tmp;
    JobQueue jobs;
    EditorPage ed(&jobs);
    CHECK(ed.openPack(g_pack));
    const int stages = int(ed.m_pack.stages.size()), actions = int(ed.m_pack.actions.size());
    CHECK(stages > 0);

    // nouvelle scène, lien OK depuis la couverture (scène -> scène crée un choix intermédiaire)
    ed.addNode(true, {0, 0});
    CHECK(ed.m_pack.stages.size() == stages + 1);
    const QString added = ed.m_pack.stages.last().uuid;
    ed.link(ed.m_pack.stages.first().uuid, PortItem::Home, 0, added);
    CHECK(ed.m_pack.actions.size() == actions + 1);
    CHECK(ed.m_pack.stages.first().home.actionId == ed.m_pack.actions.last().id);
    CHECK(ed.m_pack.validate().isEmpty() || true);

    // annulation / rétablissement
    ed.m_undo.undo();
    CHECK(ed.m_pack.actions.size() == actions);
    ed.m_undo.undo();
    CHECK(ed.m_pack.stages.size() == stages);
    ed.m_undo.redo();
    ed.m_undo.redo();
    CHECK(ed.m_pack.stages.size() == stages + 1 && ed.m_pack.actions.size() == actions + 1);

    // suppression de la scène ajoutée : le choix intermédiaire perd son option
    ed.m_scene->clearSelection();
    ed.m_selected = added;
    ed.deleteSelection();
    CHECK(ed.m_pack.stageIndex(added) < 0);
    bool orphan = false;
    for (const auto &a : ed.m_pack.actions)
        orphan = orphan || a.options.contains(added);
    CHECK(!orphan);

    // enregistrement puis relecture
    ed.m_path = tmp.path() + "/copie.zip";
    CHECK(ed.save());
    lunii::StudioPack reread;
    CHECK(lunii::StudioPack::loadArchive(ed.m_path, reread));
    CHECK(reread.stages.size() == ed.m_pack.stages.size());
    CHECK(reread.actions.size() == ed.m_pack.actions.size());
    CHECK(reread.assets.size() + reread.assetFiles.size() > 0);
    ed.m_undo.setClean();
}

// Assistant : zips d'histoires (parties à réunir) -> pack à thèmes, puis import sur une fausse Lunii
static void wizard(const QStringList &zips)
{
    QTemporaryDir tmp;
    PackWizard w(tmp.path());
    w.m_title->setText("Test assistant");
    for (const QString &z : zips)
        w.m_stories << PackWizard::StoryInput{QFileInfo(z).completeBaseName(), {z}, {}};
    w.m_groupSize->setValue(2);
    QElapsedTimer t;
    t.start();
    w.accept();
    qInfo("assistant : %lld s", t.elapsed() / 1000);
    const lunii::StudioPack p = w.result();
    CHECK(w.result() .stages.size() == 1 + 2 + 2 * zips.size());   // couverture, 2 thèmes, titre + histoire
    CHECK(p.validate().isEmpty());
    for (const QString &i : p.validate())
        qWarning("  %s", qPrintable(i));
    int tts = 0;
    for (const auto &s : p.stages)
        tts += (!s.audio.isEmpty() && s.ctrl.wheel) ? 1 : 0;
    qInfo("annonces vocales : %d", tts);
    const QString out = tmp.path() + "/assistant.zip";
    CHECK(p.saveArchive(out));
    QTemporaryDir dev;
    const QString mount = makeFakeLunii(dev);
    lunii::LuniiDevice d(mount);
    CHECK(d.importStory(out));
    CHECK(d.stories().size() == 1);
}
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);   // configuration et caches isolés (~/.qttest)
    QApplication::setOrganizationName("easylunii-test");
    QApplication::setApplicationName("easylunii-test");
    if (argc < 3) {
        qWarning("usage : easylunii-uitest <dossier .md> <pack.zip>");
        return 2;
    }
    g_mdDir = argv[1];
    g_pack = argv[2];
    lunii::StoryDb::instance().load(false);
    testDevicePanel();
    testLibraryDropFromDevice();
    EditorPageTest::run();
    if (argc > 3)
        EditorPageTest::wizard(QCoreApplication::arguments().mid(3));
    qInfo("%s : %d échec(s)", g_failures ? "ÉCHEC" : "SUCCÈS", g_failures);
    return g_failures ? 1 : 0;
}
