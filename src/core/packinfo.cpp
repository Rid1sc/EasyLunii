#include "core/packinfo.h"

#include "core/archive.h"
#include "core/crypto.h"
#include "core/storydb.h"
#include "core/studiopack.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QtEndian>

namespace lunii {

// Premier écran d'une histoire au format Lunii : image du nœud 0 (ni) dans la liste ri
static QByteArray luniiCover(ArchiveReader &ar, const QString &prefix, bool ciphered)
{
    const QString riName = prefix + (ciphered ? "ri" : "ri.plain");
    QByteArray ri = ar.read(riName);
    if (ri.isEmpty())
        return {};
    if (ciphered)
        ri = xxteaDecipherHead(ri, genericKey());
    int imgIndex = 0;
    const QByteArray ni = ar.read(prefix + "ni");
    if (ni.size() >= 0x204)
        imgIndex = qFromLittleEndian<qint32>(ni.constData() + 0x200);
    if (imgIndex < 0 || (imgIndex + 1) * 12 > ri.size())
        imgIndex = 0;
    const QString res = QString::fromLatin1(ri.mid(imgIndex * 12, 12)).replace('\\', '/');
    if (res.size() != 12)
        return {};
    QByteArray img;
    // la casse des noms varie selon les outils qui ont créé l'archive
    for (const QString &n : ar.names()) {
        const QString want = prefix + "rf/" + res + (ciphered ? "" : ".bmp");
        if (n.compare(want, Qt::CaseInsensitive) == 0) {
            img = ar.read(n);
            break;
        }
    }
    if (ciphered && !img.isEmpty())
        img = xxteaDecipherHead(img, genericKey());
    return img.startsWith("BM") ? img : QByteArray();
}

static QUuid uuidFromDir(const QString &dir)
{
    if (dir.size() < 16)
        return {};
    return dir.contains('-') ? QUuid(dir) : QUuid::fromRfc4122(QByteArray::fromHex(dir.toLatin1()));
}

PackInfo inspectPack(const QString &path)
{
    PackInfo pi;
    pi.type = detectPackType(path);
    ArchiveReader ar(path);
    if (!ar.isValid() || pi.type == PackType::Unknown)
        return pi;

    switch (pi.type) {
    case PackType::StudioZip:
    case PackType::Studio7z: {
        StudioPack p;
        if (!StudioPack::loadArchive(path, p, nullptr, false))
            return pi;
        pi.uuid = p.packUuid();
        pi.title = p.title;
        pi.description = p.description;
        pi.nightMode = p.nightMode;
        pi.nodeCount = int(p.stages.size());
        pi.cover = p.thumbnail;
        if (pi.cover.isEmpty() && !p.stages.isEmpty() && !p.stages.first().image.isEmpty())
            pi.cover = ar.read("assets/" + p.stages.first().image);
        break;
    }
    case PackType::LuniiPlain: {
        const QByteArray u = ar.read("uuid.bin");
        if (u.size() == 16)
            pi.uuid = QUuid::fromRfc4122(u);
        const QJsonObject meta = QJsonDocument::fromJson(ar.read("_metadata.json")).object();
        pi.title = meta.value("title").toString();
        pi.description = meta.value("description").toString();
        pi.cover = ar.read("_thumbnail.png");
        if (pi.cover.isEmpty())
            pi.cover = luniiCover(ar, QString(), false);
        pi.nightMode = ar.contains("nm");
        break;
    }
    case PackType::LuniiV2Zip:
    case PackType::LuniiV2_7z: {
        const QString dir = ar.entries().first().name.section('/', 0, 0);
        pi.uuid = uuidFromDir(dir);
        pi.cover = luniiCover(ar, dir + "/", true);
        pi.nightMode = ar.contains(dir + "/nm");
        break;
    }
    case PackType::LuniiV3Zip: {
        // chiffrée avec les clés d'un appareil : seule l'identité est lisible
        pi.uuid = uuidFromDir(ar.entries().first().name.section('/', 0, 0));
        break;
    }
    default:
        return pi;
    }

    const StoryDb &db = StoryDb::instance();
    if (!pi.uuid.isNull()) {
        pi.official = db.isOfficial(pi.uuid);
        if (db.isKnown(pi.uuid)) {
            const QString t = db.title(pi.uuid);
            if (!t.isEmpty() && (pi.official || pi.title.isEmpty()))
                pi.title = t;
            if (pi.description.isEmpty())
                pi.description = db.description(pi.uuid);
            pi.author = db.author(pi.uuid);
            pi.age = db.ageLabel(pi.uuid);
            pi.durationSec = db.durationSec(pi.uuid);
        }
    }
    const QString base = QFileInfo(path).completeBaseName();
    if (pi.age.isEmpty()) {
        const auto m = QRegularExpression(R"(^(\d{1,2})\+)").match(base);
        if (m.hasMatch())
            pi.age = m.captured(1) + "+";
    }
    if (pi.title.isEmpty()) {
        // nom de fichier sans le préfixe d'âge
        pi.title = QString(base).remove(QRegularExpression(R"(^\d{1,2}\+\s*)")).replace('_', ' ').trimmed();
    }
    pi.valid = true;
    return pi;
}

} // namespace lunii
