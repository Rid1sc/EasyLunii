// lunii-natif-cli : accès en ligne de commande au cœur (tests, scripts).
#include "core/convert.h"
#include "core/crypto.h"
#include "core/device.h"
#include "core/packtype.h"
#include "core/storydb.h"
#include "core/studiopack.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

using namespace lunii;

static QTextStream out(stdout);
static QTextStream err(stderr);

static int usage()
{
    err << "Usage : lunii-natif-cli <commande> …\n"
           "  devices                         Lunii montées\n"
           "  info <montage>                  modèle, firmware, clés\n"
           "  list <montage>                  histoires (ordre de la Lunii)\n"
           "  import <montage> <archive>…     ajoute des histoires\n"
           "  export <montage> <uuid> <dossier>\n"
           "  remove <montage> <uuid>\n"
           "  hide|show|nm-on|nm-off <montage> <uuid>\n"
           "  move <montage> <uuid> <position>   (1 = première)\n"
           "  recover <montage> [--dry-run]   histoires perdues (dans .content, absentes de .pi)\n"
           "  cleanup <montage>               supprime les dossiers d'histoires non référencés\n"
           "  type <archive>                  type d'archive détecté\n"
           "  studio-index <pack.zip> <dossier>  écrit ri/si/li/ni en clair\n"
           "  image <entrée> <sortie.bmp>     conversion BMP RLE4\n"
           "  studio-resave <pack> <sortie.zip> [dossier temporaire]  relit puis réenregistre un pack\n"
           "  xxtea <enc|dec> <entrée> <sortie> [clé hex]\n"
           "  aes <enc|dec> <entrée> <sortie> <clé hex> <iv hex>\n";
    return 2;
}

static QByteArray readFile(const QString &p)
{
    QFile f(p);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writeFile(const QString &p, const QByteArray &d)
{
    QFile f(p);
    return f.open(QIODevice::WriteOnly) && f.write(d) == d.size();
}

static QUuid findStory(const LuniiDevice &dev, const QString &part)
{
    const QString p = QString(part).remove('-').toUpper();
    for (const DeviceStory &s : dev.stories())
        if (QString::fromLatin1(s.uuid.toRfc4122().toHex()).toUpper().contains(p))
            return s.uuid;
    return {};
}

static void attachLog(LuniiDevice &dev, bool verbose)
{
    QObject::connect(&dev, &LuniiDevice::log, [verbose](int level, const QString &m) {
        if (level == QtDebugMsg && !verbose)
            return;
        const char *tag = level == QtCriticalMsg ? "ERREUR " : level == QtWarningMsg ? "ATTENTION " : "";
        err << tag << m << "\n";
        err.flush();
    });
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QStringList a = app.arguments().mid(1);
    const bool verbose = a.removeAll("-v") > 0;
    if (a.isEmpty())
        return usage();
    const QString cmd = a.takeFirst();

    if (cmd == "devices") {
        for (const QString &m : LuniiDevice::findDevices())
            out << m << "\n";
        return 0;
    }
    if (cmd == "type" && a.size() == 1) {
        out << packTypeLabel(detectPackType(a[0])) << "\n";
        return 0;
    }
    if (cmd == "studio-resave" && a.size() >= 2) {
        StudioPack p;
        QString e;
        if (!StudioPack::loadArchive(a[0], p, &e, true, a.value(2)) || !p.saveArchive(a[1], &e)) {
            err << e << "\n";
            return 1;
        }
        out << p.assets.size() << " ressources en mémoire, " << p.assetFiles.size() << " sur disque\n";
        return 0;
    }
    if (cmd == "image" && a.size() == 2) {
        const QByteArray bmp = imageToBmpRle4(readFile(a[0]));
        return bmp.isEmpty() || !writeFile(a[1], bmp) ? 1 : 0;
    }
    if (cmd == "xxtea" && a.size() >= 3) {
        const QByteArray key = a.size() > 3 ? QByteArray::fromHex(a[3].toLatin1()) : genericKey();
        const QByteArray in = readFile(a[1]);
        return writeFile(a[2], a[0] == "enc" ? xxteaCipherHead(in, key) : xxteaDecipherHead(in, key)) ? 0 : 1;
    }
    if (cmd == "aes" && a.size() == 5) {
        const QByteArray key = QByteArray::fromHex(a[3].toLatin1()), iv = QByteArray::fromHex(a[4].toLatin1());
        const QByteArray in = readFile(a[1]);
        return writeFile(a[2], a[0] == "enc" ? aesCipherHead(in, key, iv) : aesDecipherHead(in, key, iv)) ? 0 : 1;
    }
    if (cmd == "studio-index" && a.size() == 2) {
        StudioPack p;
        QString e;
        if (!StudioPack::loadArchive(a[0], p, &e, false)) {
            err << e << "\n";
            return 1;
        }
        const auto ix = p.luniiIndex();
        const bool ok = writeFile(a[1] + "/ri", StudioPack::riData(ix)) && writeFile(a[1] + "/si", StudioPack::siData(ix))
                     && writeFile(a[1] + "/li", StudioPack::liData(ix)) && writeFile(a[1] + "/ni", p.niData(ix));
        for (const QString &issue : p.validate())
            err << "ATTENTION " << issue << "\n";
        return ok ? 0 : 1;
    }

    if (a.isEmpty())
        return usage();
    StoryDb::instance().load(false);
    LuniiDevice dev(a.takeFirst());
    attachLog(dev, verbose);
    if (!dev.isValid()) {
        err << "Pas une Lunii (.md absent)\n";
        return 1;
    }

    if (cmd == "info") {
        out << dev.describe() << "\n"
            << "import : " << (dev.canImport() ? "oui" : "non") << ", export déchiffré : "
            << (dev.canExportPlain() ? "oui" : "non") << "\n"
            << "histoires : " << dev.stories().size() << ", libre : " << dev.freeSpace() / 1024 / 1024 << " Mo\n";
        return 0;
    }
    if (cmd == "list") {
        int i = 0;
        for (const DeviceStory &s : dev.stories()) {
            QString t = StoryDb::instance().title(s.uuid);
            out << QString("%1 %2 %3%4%5 %6\n")
                       .arg(++i, 3)
                       .arg(s.shortId())
                       .arg(StoryDb::instance().isOfficial(s.uuid) ? "O" : "T")
                       .arg(s.hidden ? "H" : "-")
                       .arg(s.nightMode ? "N" : "-")
                       .arg(t.isEmpty() ? "(inconnue)" : t);
        }
        return 0;
    }
    if (cmd == "import" && !a.isEmpty()) {
        int failed = 0;
        for (const QString &p : a)
            failed += dev.importStory(p) ? 0 : 1;
        return failed ? 1 : 0;
    }
    if (cmd == "export" && a.size() == 2) {
        const QString path = dev.exportStory(findStory(dev, a[0]), a[1]);
        out << path << "\n";
        return path.isEmpty() ? 1 : 0;
    }
    if (cmd == "remove" && a.size() == 1)
        return dev.removeStory(findStory(dev, a[0])) ? 0 : 1;
    if ((cmd == "hide" || cmd == "show") && a.size() == 1)
        return dev.setHidden(findStory(dev, a[0]), cmd == "hide") ? 0 : 1;
    if ((cmd == "nm-on" || cmd == "nm-off") && a.size() == 1)
        return dev.setNightMode(findStory(dev, a[0]), cmd == "nm-on") ? 0 : 1;
    if (cmd == "move" && a.size() == 2) {
        QList<QUuid> order;
        for (const DeviceStory &s : dev.stories())
            order << s.uuid;
        const QUuid u = findStory(dev, a[0]);
        if (!order.removeOne(u))
            return 1;
        order.insert(qBound(0, a[1].toInt() - 1, int(order.size())), u);
        return dev.reorder(order) ? 0 : 1;
    }
    if (cmd == "recover") {
        out << dev.recoverStories(a.contains("--dry-run")) << " histoire(s)\n";
        return 0;
    }
    if (cmd == "cleanup") {
        qint64 freed = 0;
        out << dev.cleanupStories(&freed) << " dossier(s), " << freed / 1024 << " Ko libérés\n";
        return 0;
    }
    return usage();
}
