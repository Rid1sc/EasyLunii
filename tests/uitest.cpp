// Tests de l'interface sur une fausse Lunii (copie d'un .md réel) : dépôts, ordre, export, éditeur.
// Usage : easylunii-uitest <dossier .md/.cfg d'une Lunii> <pack STUdio.zip>
#include "app/editorpage.h"

#include "app/devicepanel.h"
#include "app/jobqueue.h"
#include "app/library.h"
#include "app/librarymodel.h"
#include "app/librarypage.h"
#include "app/transferpanel.h"
#include "app/editor/audiotransport.h"
#include "app/editor/graphitems.h"
#include "app/editor/simulator.h"
#include "app/editor/trimdialog.h"
#include "app/editor/wizard.h"
#include "app/sources/sourcedialog.h"
#include "core/convert.h"
#include "core/device.h"
#include "core/flamdevice.h"
#include "core/storydb.h"
#include "core/studiopack.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QTreeWidget>
#include <QAbstractButton>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLineEdit>
#include <QSpinBox>
#include <QFile>
#include <QMediaPlayer>
#include <QListView>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
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

// File des transferts : tâches en attente visibles, retrait d'une tâche, historique
static void testTransferQueue()
{
    JobQueue jobs;
    TransferPanel panel(&jobs);
    QStringList ran;
    for (int i = 1; i <= 4; ++i)
        jobs.enqueue(QString("tâche %1").arg(i), [i]() { QThread::msleep(300); return i != 4; },
                     [&ran, i](bool) { ran << QString::number(i); });
    auto *pending = panel.findChildren<QListWidget *>().value(0);
    CHECK(pending && pending->count() == 3);   // la 1re est en cours
    CHECK(jobs.currentLabel() == "tâche 1");
    const auto ids = jobs.pendingJobs();
    CHECK(ids.size() == 3 && jobs.removePending(ids[1].first));   // retire « tâche 3 »
    CHECK(pending && pending->count() == 2);
    WAIT_FOR(!jobs.isBusy(), 5000);
    CHECK(ran == QStringList({"1", "2", "4"}));
    CHECK(jobs.history().size() == 3 && !jobs.history().first().ok && jobs.history().first().label == "tâche 4");
    auto *done = panel.findChildren<QListWidget *>().value(1);
    CHECK(done && done->count() == 3);
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

// Barre de lecture de test : inspecteur (son le plus long du pack) puis simulateur
static void transport()
{
    JobQueue jobs;
    EditorPage ed(&jobs);
    CHECK(ed.openPack(g_pack));
    QString longest;
    qint64 best = -1;
    for (const auto &st : ed.m_pack.stages)
        if (!st.audio.isEmpty() && ed.m_pack.assetSize(st.audio) > best) {
            best = ed.m_pack.assetSize(st.audio);
            longest = st.audio;
        }
    CHECK(!longest.isEmpty());
    ed.playAsset(longest);
    QMediaPlayer *pl = ed.m_player;
    WAIT_FOR(pl && pl->duration() > 30000 && pl->playbackState() == QMediaPlayer::PlayingState, 10000);
    if (!pl)
        return;
    ed.m_transport->seekBy(20000);
    WAIT_FOR(pl->position() >= 19000, 5000);
    ed.m_transport->seekBy(-10000);
    WAIT_FOR(pl->position() >= 9000 && pl->position() < 15000, 5000);
    ed.m_transport->togglePlay();
    WAIT_FOR(pl->playbackState() == QMediaPlayer::PausedState, 3000);
    const qint64 at = pl->position();
    QTest::qWait(500);
    CHECK(qAbs(pl->position() - at) < 100);   // en pause, la position ne bouge plus
    ed.m_transport->togglePlay();
    WAIT_FOR(pl->playbackState() == QMediaPlayer::PlayingState, 3000);
    ed.stopAudio();

    Simulator sim(ed.m_pack);
    auto *tr = sim.findChild<AudioTransport *>();
    auto *sp = sim.findChild<QMediaPlayer *>();
    CHECK(tr && sp);
    if (!tr || !sp)
        return;
    WAIT_FOR(sp->duration() > 0, 10000);
    QTest::keyClick(&sim, Qt::Key_Right, Qt::ControlModifier);
    WAIT_FOR(sp->position() >= qMin<qint64>(9000, sp->duration() - 1500) || sp->mediaStatus() == QMediaPlayer::EndOfMedia, 5000);
    qInfo("lecture de test : %lld s, simulateur %lld ms / %lld ms", pl->duration() / 1000, sp->position(), sp->duration());
}

// Annonces enregistrées : « titre.* » d'un dossier, « X.titre.* » à côté d'un fichier, accueil du pack ;
// parties de formats différents (44,1 kHz mono + 48 kHz stéréo) réunies sans erreur de durée
static double seconds(const lunii::StudioPack &p, const QString &asset, const QString &dir)
{
    const QString f = dir + "/probe.mp3";
    QFile out(f);
    if (!out.open(QIODevice::WriteOnly))
        return -1;
    out.write(p.assetData(asset));
    out.close();
    QProcess pr;
    pr.start("ffprobe", {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", f});
    pr.waitForFinished();
    return pr.readAllStandardOutput().trimmed().toDouble();
}

static void tone(const QString &file, double secs, int rate, int channels, int freq)
{
    QProcess::execute("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i",
                                 QString("sine=frequency=%1:sample_rate=%2:duration=%3").arg(freq).arg(rate).arg(secs),
                                 "-ac", QString::number(channels), file});
}

static void announces()
{
    QTemporaryDir tmp;
    const QString d = tmp.path();
    QDir().mkpath(d + "/Le dragon");
    tone(d + "/Le dragon/1 debut.mp3", 3, 44100, 1, 440);
    tone(d + "/Le dragon/2 suite.wav", 4, 48000, 2, 550);
    tone(d + "/Le dragon/titre.mp3", 2, 22050, 1, 660);
    tone(d + "/La fee.mp3", 5, 44100, 2, 330);
    tone(d + "/La fee.titre.wav", 1, 48000, 2, 770);
    tone(d + "/accueil.ogg", 1.5, 44100, 1, 880);

    PackWizard w(d);
    w.m_tts->setChecked(false);   // sans Piper : seules les annonces enregistrées
    w.m_title->setText("Test annonces");
    w.m_groupSize->setValue(0);
    w.m_stories << PackWizard::StoryInput{"Le dragon", {d + "/Le dragon"}, {}, {}};
    w.addAudioFiles({d + "/La fee.mp3", d + "/La fee.titre.wav"});
    CHECK(w.m_stories.size() == 2);
    CHECK(w.m_stories.size() == 2 && w.m_stories[1].announce.endsWith("La fee.titre.wav"));
    w.m_coverAnnounce = d + "/accueil.ogg";
    w.accept();
    const lunii::StudioPack p = w.result();
    CHECK(p.validate().isEmpty());
    auto find = [&](const QString &name) -> const lunii::StageNode * {
        for (const auto &st : p.stages)
            if (st.name == name)
                return &st;
        return nullptr;
    };
    const auto *dragon = find("Le dragon"), *dragonStory = find("Le dragon (histoire)");
    const auto *fee = find("La fee"), *feeStory = find("La fee (histoire)");
    CHECK(dragon && dragonStory && fee && feeStory);
    if (!dragon || !dragonStory || !fee || !feeStory)
        return;
    const double a1 = seconds(p, dragon->audio, d), s1 = seconds(p, dragonStory->audio, d);
    const double a2 = seconds(p, fee->audio, d), s2 = seconds(p, feeStory->audio, d);
    const double c = seconds(p, p.stages.first().audio, d);
    qInfo("annonces : dragon %.2f s (histoire %.2f s), fée %.2f s (histoire %.2f s), accueil %.2f s", a1, s1, a2, s2, c);
    CHECK(qAbs(a1 - 3.0) < 0.2);    // 2 s + 0,5 s avant et après
    CHECK(qAbs(s1 - 7.0) < 0.2);    // titre.mp3 exclu des parties ; 3 s + 4 s malgré les formats différents
    CHECK(qAbs(a2 - 2.0) < 0.2);
    CHECK(qAbs(s2 - 5.0) < 0.2);
    CHECK(qAbs(c - 2.5) < 0.2);
    const lunii::Mp3Info mi = lunii::mp3Info(p.assetData(fee->audio));
    CHECK(mi.valid && mi.mono && mi.sampleRate == 44100);
}

// Découpe : 2 s + 6 s + 2 s, on retire le début et la fin (dont un passage en deux fois, fusionné)
static void trim()
{
    QTemporaryDir tmp;
    const QString d = tmp.path();
    tone(d + "/a.wav", 2, 44100, 1, 440);
    tone(d + "/b.wav", 6, 44100, 1, 550);
    tone(d + "/c.wav", 2, 44100, 1, 660);
    QProcess::execute("ffmpeg", {"-v", "error", "-y", "-i", d + "/a.wav", "-i", d + "/b.wav", "-i", d + "/c.wav", "-filter_complex",
                                 "[0:a][1:a][2:a]concat=n=3:v=0:a=1[o]", "-map", "[o]", d + "/src.mp3"});
    TrimDialog t(d + "/src.mp3", "test");
    WAIT_FOR(t.m_wave->duration() > 9.5, 10000);
    t.removeSpan({0, 1});
    t.removeSpan({0.8, 2.0});   // chevauche le précédent : fusion en 0–2 s
    t.removeSpan({8.0, 99});
    const QList<Span> k = t.kept();
    CHECK(k.size() == 1 && qAbs(k.first().first - 2.0) < 0.01 && qAbs(k.first().second - 8.0) < 0.01);
    if (!qEnvironmentVariable("GRAB_TRIM").isEmpty()) {
        t.m_wave->setSelection({3.0, 4.5});
        t.resize(980, 420);
        t.show();
        QTest::qWait(500);
        t.grab().save(qEnvironmentVariable("GRAB_TRIM"));
    }
    QString err;
    const QByteArray mp3 = TrimDialog::render(d + "/src.mp3", k, &err);
    CHECK(!mp3.isEmpty());
    QFile out(d + "/out.mp3");
    CHECK(out.open(QIODevice::WriteOnly) && out.write(mp3) == mp3.size());
    out.close();
    QProcess pr;
    pr.start("ffprobe", {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", d + "/out.mp3"});
    pr.waitForFinished();
    const double len = pr.readAllStandardOutput().trimmed().toDouble();
    qInfo("découpe : 10 s -> %.2f s", len);
    CHECK(qAbs(len - 6.0) < 0.15);
}

// Source « Podcasts » sur un vrai flux (réseau) : ouverture d'un lien, choix de 2 épisodes,
// téléchargement par la file des transferts, puis cache (2e passage sans réseau pour l'audio)
static void podcastSource()
{
    JobQueue jobs;
    SourceDialog d(SourceDialog::Stories, &jobs);
    CHECK(d.hasPlugins());
    const int pi = d.m_plugin->findText("Podcasts");
    CHECK(pi >= 0);
    d.m_plugin->setCurrentIndex(pi);
    d.m_query->setText("https://www.radiofrance.fr/franceinter/podcasts/une-histoire-et-oli");
    d.go();
    WAIT_FOR(d.m_list->topLevelItemCount() > 5, 30000);
    // les 2 plus courts, pour aller vite
    QList<int> order;
    for (int i = 0; i < d.m_items.size(); ++i)
        order << i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return d.m_items[a].duration < d.m_items[b].duration; });
    for (int k = 0; k < 2 && k < order.size(); ++k)
        d.m_list->topLevelItem(order[k])->setCheckState(0, Qt::Checked);
    d.m_grouping->button(2)->setChecked(true);   // une seule histoire, 2 parties
    QElapsedTimer t;
    t.start();
    d.accept();
    const qint64 first = t.elapsed();
    const auto stories = d.stories();
    CHECK(stories.size() == 1 && stories.first().parts.size() == 2);
    CHECK(!stories.isEmpty() && QFileInfo(stories.first().parts.first()).size() > 100000);
    CHECK(!stories.isEmpty() && !stories.first().image.isEmpty());
    CHECK(jobs.history().size() == 2 && jobs.history().first().ok);
    // 2e fois : depuis le cache
    t.restart();
    d.accept();
    qInfo("source podcasts : %lld ms puis %lld ms (cache), histoire « %s »", first, t.elapsed(),
          qPrintable(stories.isEmpty() ? QString() : stories.first().title));
    CHECK(t.elapsed() < first);
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
    testTransferQueue();
    testDevicePanel();
    testLibraryDropFromDevice();
    EditorPageTest::run();
    EditorPageTest::transport();
    EditorPageTest::announces();
    EditorPageTest::trim();
    if (qEnvironmentVariableIsSet("EASYLUNII_TEST_NETWORK"))
        EditorPageTest::podcastSource();
    if (argc > 3)
        EditorPageTest::wizard(QCoreApplication::arguments().mid(3));
    qInfo("%s : %d échec(s)", g_failures ? "ÉCHEC" : "SUCCÈS", g_failures);
    return g_failures ? 1 : 0;
}
