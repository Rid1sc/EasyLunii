#include "core/device.h"

#include "core/archive.h"
#include "core/convert.h"
#include "core/crypto.h"
#include "core/storydb.h"
#include "core/studiopack.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStorageInfo>
#include <QtEndian>

#include <unistd.h>

namespace lunii {

static const char *CONTENT = ".content";
static const char *CONTENT_HIDDEN = ".content.hidden";
static const char *BACKUP_MARKER = "_backup.json";

static const QPair<quint16, quint16> V1_VIDPID{0x0c45, 0x6820};
static const QPair<quint16, quint16> V1_FW2_VIDPID{0x0c45, 0x6840};
static const QPair<quint16, quint16> V2_V3_VIDPID{0x0483, 0xa341};

bool LuniiDevice::isLunii(const QString &mountPoint)
{
    return QFileInfo(mountPoint + "/.md").isFile();
}

LuniiDevice::LuniiDevice(const QString &mountPoint, QObject *parent) : StoryDevice(mountPoint, parent)
{
    if (!parseMd())
        return;
    readConfig();
    readStories();
    m_valid = true;
}

// ---------------------------------------------------------------- métadonnées (.md)

bool LuniiDevice::parseMd()
{
    QFile f(m_mount + "/.md");
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray md = f.readAll();
    if (md.size() < 2)
        return false;
    m_mdVersion = qFromLittleEndian<quint16>(md.constData());
    if (m_mdVersion >= 1 && m_mdVersion <= 5 && md.size() == 512)
        parseMdV1to5(md);
    else if (m_mdVersion >= 6 && m_mdVersion <= 7 && (md.size() == 112 || md.size() == 128))
        parseMdV6to7(md);
    else
        parseMdUnsupported(md);
    return true;
}

void LuniiDevice::parseMdV1to5(const QByteArray &md)
{
    const auto *p = reinterpret_cast<const uchar *>(md.constData());
    m_fwMajor = qFromLittleEndian<quint16>(p + 6);
    m_fwMinor = qFromLittleEndian<quint16>(p + 8);
    m_snu = md.mid(10, 8);
    const QPair<quint16, quint16> vp{qFromLittleEndian<quint16>(p + 18), qFromLittleEndian<quint16>(p + 20)};
    if (vp == V1_VIDPID || vp == V1_FW2_VIDPID)
        m_hw = HwVersion::V1;
    else if (vp == V2_V3_VIDPID)
        m_hw = HwVersion::V2;
    else
        m_hw = HwVersion::Unknown;   // v1 ou v2 indéterminé : même traitement
    const QByteArray dec = xxteaDecrypt(md.mid(0x100, 0x100), genericKey());
    m_deviceKey = dec.mid(8, 8) + dec.mid(0, 8);
}

static QByteArray hexSnu(const QByteArray &snu)
{
    return snu.toHex();
}

void LuniiDevice::parseMdV6to7(const QByteArray &md)
{
    m_hw = HwVersion::V3;
    const int mdVers = uchar(md[0]);
    m_fwMajor = uchar(md[2]) - 0x30;
    m_fwMinor = uchar(md[4]) - 0x30;
    m_fwSub = uchar(md[6]) - 0x30;
    m_snu = QByteArray::fromHex(md.mid(0x1A, 14));

    // sauvegarde du .md (comme Lunii.QT) : permet de retrouver les clés si le fichier est abîmé
    QDir().mkpath(configDir());
    const QString backup = QString("%1/%2.v%3.md").arg(configDir(), snu()).arg(mdVers);
    if (!QFile::exists(backup)) {
        QFile b(backup);
        if (b.open(QIODevice::WriteOnly))
            b.write(md);
        info(tr("Sauvegarde du .md v%1 créée (%2)").arg(mdVers).arg(backup));
    }

    if (mdVers == 6) {
        m_bt = md.mid(0x40, 0x20);
        loadMdFakeStoryKeys();
    } else {
        m_storyKey = reverseWords(md.mid(0x40, 0x10));
        m_storyIv = reverseWords(md.mid(0x50, 0x10));
        m_bt = hexSnu(m_snu) + QByteArray(10, 0) + hexSnu(m_snu).left(8);
    }

    // vraies clés de l'appareil si l'utilisateur les a fournies
    QFile k(QString("%1/%2.keys").arg(configDir(), snu()));
    if (k.open(QIODevice::ReadOnly)) {
        m_deviceKey = reverseWords(k.read(0x10));
        m_deviceIv = reverseWords(k.read(0x10));
        info(tr("Clés de l'appareil lues depuis %1").arg(k.fileName()));
    }
}

void LuniiDevice::parseMdUnsupported(const QByteArray &md)
{
    m_hw = HwVersion::V3;
    const int mdVers = md.isEmpty() ? 0 : uchar(md[0]);
    if (md.size() > 6) {
        m_fwMajor = uchar(md[2]) & ~0x30;
        m_fwMinor = uchar(md[4]) & ~0x30;
        m_fwSub = uchar(md[6]) & ~0x30;
    }
    const QByteArray hex = md.mid(0x1A, 14);
    bool okHex = hex.size() == 14;
    for (char c : hex)
        okHex = okHex && isxdigit(uchar(c));
    if (okHex) {
        m_snu = QByteArray::fromHex(hex);
    } else {
        m_snu = QByteArray(16, 0);
        error(tr("Fichier .md corrompu ? (carte SD abîmée possible)"));
    }
    warn(tr("Fichier .md v%1 non pris en charge ou corrompu, recherche de sauvegardes…").arg(mdVers));
    if (m_storyKey.isEmpty())
        loadMdBackup(6);
    if (m_storyKey.isEmpty())
        loadMdBackup(7);

    QFile k(QString("%1/%2.keys").arg(configDir(), snu()));
    if (k.open(QIODevice::ReadOnly)) {
        m_deviceKey = reverseWords(k.read(0x10));
        m_deviceIv = reverseWords(k.read(0x10));
        loadMdFakeStoryKeys();
        m_bt = aesEncrypt(reverseWords(m_storyKey) + reverseWords(m_storyIv), m_deviceKey, m_deviceIv);
    }
    if (m_storyKey.isEmpty())
        warn(tr("Aucune clé : impossible d'importer des histoires."));
}

void LuniiDevice::loadMdBackup(int version)
{
    QFile f(QString("%1/%2.v%3.md").arg(configDir(), snu()).arg(version));
    if (!f.open(QIODevice::ReadOnly)) {
        warn(tr("Pas de sauvegarde .md v%1 (%2)").arg(version).arg(f.fileName()));
        return;
    }
    const QByteArray md = f.readAll();
    if (md.size() < 0x60 || qFromLittleEndian<quint16>(md.constData()) != version)
        return;
    if (QByteArray::fromHex(md.mid(0x1A, 14)) != m_snu) {
        warn(tr("Sauvegarde .md : numéro de série différent"));
        return;
    }
    if (version == 6) {
        m_bt = md.mid(0x40, 0x20);
        loadMdFakeStoryKeys();
    } else {
        m_storyKey = reverseWords(md.mid(0x40, 0x10));
        m_storyIv = reverseWords(md.mid(0x50, 0x10));
        m_bt = hexSnu(m_snu) + QByteArray(10, 0) + hexSnu(m_snu).left(8);
    }
}

void LuniiDevice::loadMdFakeStoryKeys()
{
    m_storyKey = reverseWords(hexSnu(m_snu) + QByteArray(2, 0));
    m_storyIv = reverseWords(QByteArray(8, 0) + hexSnu(m_snu).left(8));
}

QString LuniiDevice::snu() const
{
    if (m_snu == QByteArray(16, 0))
        return QStringLiteral("empty");
    QString s = QString::fromLatin1(m_snu.toHex()).toUpper();
    while (s.startsWith('0'))
        s.remove(0, 1);
    return s;
}

QString LuniiDevice::firmware() const
{
    if (m_hw == HwVersion::V3)
        return QString("%1.%2.%3").arg(m_fwMajor).arg(m_fwMinor).arg(m_fwSub);
    return QString("%1.%2").arg(m_fwMajor).arg(m_fwMinor);
}

QString LuniiDevice::describe() const
{
    QString hw;
    switch (m_hw) {
    case HwVersion::V1: hw = "Lunii v1"; break;
    case HwVersion::V2: hw = "Lunii v2"; break;
    case HwVersion::V3: hw = "Lunii v3"; break;
    default: hw = "Lunii v1/v2"; break;
    }
    QString s = tr("%1, firmware %2, .md v%3, SNU %4").arg(hw, firmware()).arg(m_mdVersion).arg(snu());
    if (m_hw == HwVersion::V3)
        s += m_deviceKey.isEmpty() ? tr(", clés de l'appareil absentes") : tr(", clés de l'appareil présentes");
    return s;
}

bool LuniiDevice::canImport() const
{
    return m_hw == HwVersion::V3 ? !m_storyKey.isEmpty() : m_deviceKey.size() == 16;
}

bool LuniiDevice::canExportPlain() const
{
    return m_hw != HwVersion::V3 || !m_deviceKey.isEmpty();
}

// ---------------------------------------------------------------- liste des histoires (.pi)

static QList<QUuid> readPi(const QString &path)
{
    QList<QUuid> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QByteArray d = f.readAll();
    for (int i = 0; i + 16 <= d.size(); i += 16) {
        const QUuid u = QUuid::fromRfc4122(d.mid(i, 16));
        if (!out.contains(u))
            out << u;
    }
    return out;
}

void LuniiDevice::readStories()
{
    m_stories.clear();
    for (bool hidden : {false, true}) {
        for (const QUuid &u : readPi(m_mount + (hidden ? "/.pi.hidden" : "/.pi"))) {
            if (indexOf(u) >= 0) {
                warn(tr("Histoire en double dans l'index, ignorée : %1").arg(uuidStr(u)));
                continue;
            }
            DeviceStory s;
            s.uuid = u;
            s.hidden = hidden;
            s.nightMode = QFile::exists(storyDir(s) + "/nm");
            m_stories << s;
        }
    }
    info(tr("%1 histoires lues").arg(m_stories.size()));
}

QString LuniiDevice::storyDir(const DeviceStory &s) const
{
    return QString("%1/%2/%3").arg(m_mount, s.hidden ? CONTENT_HIDDEN : CONTENT, s.shortId());
}

static QByteArray storyKeysForward(const QByteArray &, const QByteArray &, const QString &, const QByteArray &);

QByteArray LuniiDevice::coverImage(const QUuid &u) const
{
    const int i = indexOf(u);
    if (i < 0)
        return {};
    const QString dir = storyDir(m_stories[i]);
    QFile riF(dir + "/ri"), niF(dir + "/ni");
    if (!riF.open(QIODevice::ReadOnly))
        return {};
    QByteArray key = genericKey(), iv;
    if (m_hw == HwVersion::V3) {
        if (!checkV3Key(dir))
            return {};
        key = storyKeysForward(m_deviceKey, m_deviceIv, dir + "/bt", hexSnu(m_snu));
        iv = key.mid(16);
        key = key.left(16);
    }
    const QByteArray ri = decipher(riF.readAll(), key, iv);
    int img = 0;
    if (niF.open(QIODevice::ReadOnly)) {
        const QByteArray ni = niF.read(0x204);
        if (ni.size() == 0x204)
            img = qFromLittleEndian<qint32>(ni.constData() + 0x200);
    }
    if (img < 0 || (img + 1) * 12 > ri.size())
        img = 0;
    const QString res = QString::fromLatin1(ri.mid(img * 12, 12)).replace('\\', '/');
    QFile rf(dir + "/rf/" + res);
    if (!rf.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bmp = decipher(rf.readAll(), key, iv);
    return bmp.startsWith("BM") ? bmp : QByteArray();
}

static bool saveAtomic(const QString &path, const QByteArray &data)
{
    QSaveFile f(path);
    // FAT : renommer par-dessus un fichier existant fonctionne sous Linux
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        return false;
    return f.commit();
}

bool LuniiDevice::writePackIndex()
{
    QByteArray pi, piHidden;
    for (const DeviceStory &s : m_stories)
        (s.hidden ? piHidden : pi) += s.uuid.toRfc4122();
    const bool ok = saveAtomic(m_mount + "/.pi", pi) && saveAtomic(m_mount + "/.pi.hidden", piHidden);
    if (!ok)
        error(tr("Échec de l'écriture de l'index des histoires (.pi)"));
    ::sync();
    emit storiesChanged();
    return ok;
}

// ---------------------------------------------------------------- réglages (.cfg)

void LuniiDevice::readConfig()
{
    QFile f(m_mount + "/.cfg");
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray d = f.readAll();
    if (d.size() < 2 || qFromLittleEndian<quint16>(d.constData()) != 0x100) {
        m_config.supported = false;
        warn(tr("Version de .cfg non prise en charge : réglages en lecture seule"));
        return;
    }
    for (int i = 2; i + 4 <= d.size(); i += 4) {
        const int idx = qFromLittleEndian<quint16>(d.constData() + i);
        const int val = qFromLittleEndian<quint16>(d.constData() + i + 2);
        if (idx >= 0 && idx < m_config.values.size())
            m_config.values[idx] = val;
    }
}

bool LuniiDevice::setConfig(const DeviceConfig &cfg)
{
    if (!m_config.supported) {
        error(tr("Réglages non modifiables : version de .cfg inconnue"));
        return false;
    }
    QByteArray d("\x00\x01", 2);
    for (int i = 0; i < cfg.values.size(); ++i) {
        char b[4];
        qToLittleEndian<quint16>(quint16(i), b);
        qToLittleEndian<quint16>(quint16(cfg.values[i]), b + 2);
        d.append(b, 4);
    }
    if (!saveAtomic(m_mount + "/.cfg", d))
        return false;
    m_config = cfg;
    m_config.supported = true;
    return true;
}

// ---------------------------------------------------------------- chiffrement

QByteArray LuniiDevice::decipher(const QByteArray &buf, const QByteArray &key, const QByteArray &iv) const
{
    if (m_hw == HwVersion::V3)
        return aesDecipherHead(buf, key, iv);
    return xxteaDecipherHead(buf, key);
}

QByteArray LuniiDevice::cipher(const QByteArray &buf, const QByteArray &key, const QByteArray &iv) const
{
    if (m_hw == HwVersion::V3)
        return aesCipherHead(buf, key, iv);
    return xxteaCipherHead(buf, key);
}

QByteArray LuniiDevice::plainData(const QString &file) const
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    QByteArray key, iv;
    if (m_hw == HwVersion::V3) {
        key = m_storyKey;
        iv = m_storyIv;
    } else {
        key = genericKey();
    }
    if (file.endsWith("bt")) {
        key = m_deviceKey;
        iv = m_hw == HwVersion::V3 ? m_deviceIv : QByteArray();
    }
    if (file.endsWith("ni") || file.endsWith("nm"))
        key.clear();
    return key.isEmpty() ? data : decipher(data, key, iv);
}

QByteArray LuniiDevice::cipheredData(const QString &file, const QByteArray &data) const
{
    QByteArray key, iv;
    if (m_hw == HwVersion::V3) {
        key = m_storyKey;
        iv = m_storyIv;
    } else {
        key = genericKey();
    }
    if (file.endsWith("bt"))
        key = m_deviceKey;
    if (file.endsWith("ni") || file.endsWith("nm"))
        key.clear();
    return key.isEmpty() ? data : cipher(data, key, iv);
}

QString LuniiDevice::cipheredName(QString file, bool studioRi, bool studioSi)
{
    if (file.endsWith(".plain"))
        file.chop(6);
    if (studioRi)
        file = "rf/000/" + file;
    if (studioSi)
        file = "sf/000/" + file;
    file = file.toLower();
    if (file.endsWith(".mp3"))
        file.chop(4);
    if (file.endsWith(".bmp"))
        file.chop(4);
    file.replace('\\', '/');
    const int slash = file.lastIndexOf('/');
    QString dir = slash >= 0 ? file.left(slash) : QString();
    QString base = slash >= 0 ? file.mid(slash + 1) : file;
    if (base.size() >= 8)
        base = base.toUpper();
    file = dir.isEmpty() ? base : dir + "/" + base;
    // dossier UUID en tête (8 caractères sans séparateur)
    if (dir.size() >= 8 && !file.left(8).contains('/'))
        file = file.left(8).toUpper() + file.mid(8);
    return file;
}

QString LuniiDevice::plainName(const QString &rel)
{
    if (rel.startsWith("rf/"))
        return rel + ".bmp";
    if (rel.startsWith("sf/"))
        return rel + ".mp3";
    if (rel == "li" || rel == "ri" || rel == "si")
        return rel + ".plain";
    return rel;
}

// Clés d'une histoire v3 : depuis son bt si les clés de l'appareil sont connues, sinon clés « astuce »
static void storyKeysFor(const QByteArray &devKey, const QByteArray &devIv, const QString &btPath,
                         const QByteArray &snuHex, QByteArray &key, QByteArray &iv)
{
    QFile bt(btPath);
    if (!devKey.isEmpty() && !devIv.isEmpty() && bt.open(QIODevice::ReadOnly)) {
        const QByteArray plain = aesDecipherHead(bt.read(0x20), devKey, devIv);
        key = reverseWords(plain.left(16));
        iv = reverseWords(plain.mid(16, 16));
    } else {
        key = reverseWords(snuHex + QByteArray(2, 0));
        iv = reverseWords(QByteArray(8, 0) + snuHex.left(8));
    }
}

static QByteArray storyKeysForward(const QByteArray &devKey, const QByteArray &devIv, const QString &btPath,
                                   const QByteArray &snuHex)
{
    QByteArray k, iv;
    storyKeysFor(devKey, devIv, btPath, snuHex, k, iv);
    return k + iv;
}

bool LuniiDevice::checkV3Key(const QString &storyPath) const
{
    QFile ri(storyPath + "/ri");
    if (!ri.open(QIODevice::ReadOnly))
        return false;
    QByteArray key, iv;
    storyKeysFor(m_deviceKey, m_deviceIv, storyPath + "/bt", hexSnu(m_snu), key, iv);
    return aesDecipherHead(ri.readAll(), key, iv).startsWith("000");
}

bool LuniiDevice::checkV2Bt(const QString &storyPath) const
{
    QFile bt(storyPath + "/bt"), ri(storyPath + "/ri");
    if (!bt.open(QIODevice::ReadOnly) || !ri.open(QIODevice::ReadOnly))
        return false;
    return ri.read(0x40) == xxteaDecipherHead(bt.readAll(), m_deviceKey);
}

// ---------------------------------------------------------------- utilitaires

bool LuniiDevice::cleanStoryDir(const QUuid &u)
{
    const QString id = shortUuid(u);
    bool ok = true;
    for (const char *base : {CONTENT, CONTENT_HIDDEN}) {
        QDir d(QString("%1/%2/%3").arg(m_mount, base, id));
        if (d.exists() && !d.removeRecursively()) {
            error(tr("Suppression impossible : %1").arg(d.path()));
            ok = false;
        }
    }
    return ok;
}

// ---------------------------------------------------------------- import

bool LuniiDevice::importStory(const QString &path)
{
    m_abort.storeRelaxed(0);
    if (!canImport()) {
        error(tr("Clés absentes : import impossible sur cette Lunii"));
        return false;
    }
    info(tr("Chargement de %1…").arg(path));
    if (QFileInfo(path).size() >= freeSpace()) {
        error(tr("Place insuffisante sur la Lunii (%1 Mo libres)").arg(freeSpace() / 1024 / 1024));
        return false;
    }
    {
        ArchiveReader probe(path);
        if (probe.contains(BACKUP_MARKER)) {
            const bool ok = restoreBackup(path);
            ::sync();
            return ok;
        }
    }
    const PackType t = detectPackType(path);
    debug(tr("Type d'archive : %1").arg(packTypeLabel(t)));
    bool ok = false;
    switch (t) {
    case PackType::LuniiPlain: ok = importPlain(path); break;
    case PackType::LuniiV2Zip:
    case PackType::LuniiV2_7z: ok = importV2(path); break;
    case PackType::LuniiV3Zip: ok = importV3(path); break;
    case PackType::StudioZip:
    case PackType::Studio7z: ok = importStudio(path); break;
    default:
        error(tr("Type d'archive non pris en charge : %1").arg(packTypeLabel(t)));
        return false;
    }
    ::sync();
    return ok;
}

bool LuniiDevice::importPlain(const QString &path)
{
    ArchiveReader ar(path);
    const QByteArray uuidBin = ar.read("uuid.bin");
    if (uuidBin.size() != 16) {
        error(tr("uuid.bin absent ou invalide"));
        return false;
    }
    const QUuid u = QUuid::fromRfc4122(uuidBin);
    if (alreadyLoaded(u))
        return false;
    if (ar.contains("_metadata.json")) {
        const QJsonObject meta = QJsonDocument::fromJson(ar.read("_metadata.json")).object();
        if (meta.value("uuid").toString().toUpper() != uuidStr(u))
            return false;
        StoryDb::instance().addThirdParty(u, meta.value("title").toString(), meta.value("description").toString());
    }
    if (ar.contains("_thumbnail.png"))
        StoryDb::instance().addThumb(u, ar.read("_thumbnail.png"));

    const QString id = shortUuid(u);
    const QString out = QString("%1/%2/%3").arg(m_mount, CONTENT, id);
    bool nm = false, ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &data) {
        emit storyProgress(id, index++, total);
        if (aborted())
            return ok = false;
        if (e.name == "uuid.bin" || e.name == "_metadata.json" || e.name == "_thumbnail.png")
            return true;
        if (e.name.endsWith("nm"))
            nm = true;
        const QByteArray ciph = cipheredData(e.name, data);
        const QString name = cipheredName(e.name);
        debug(tr("Fichier %1/%2 > %3").arg(index).arg(total).arg(name));
        if (!writeFile(out + "/" + name, ciph, true))
            return ok = false;
        if (m_hw != HwVersion::V3 && e.name.endsWith("ri.plain"))
            m_bt = cipher(ciph.left(0x40), m_deviceKey, {});
        return true;
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    info(tr("Création du fichier d'autorisation…"));
    if (!writeFile(out + "/bt", m_bt))
        return false;
    m_stories << DeviceStory{u, false, nm};
    return writePackIndex();
}

// Archive v1/v2 (zip/7z) : dossier UUID chiffré en XXTEA avec la clé générique
bool LuniiDevice::importV2(const QString &path)
{
    ArchiveReader ar(path);
    if (ar.entries().isEmpty()) {
        error(tr("Archive vide"));
        return false;
    }
    const QString first = ar.entries().first().name;
    const QString uuidPart = first.section('/', 0, 0);
    QUuid u;
    if (uuidPart.size() >= 16)
        u = uuidPart.contains('-') ? QUuid(uuidPart) : QUuid::fromRfc4122(QByteArray::fromHex(uuidPart.toLatin1()));
    if (u.isNull()) {
        error(tr("Dossier UUID absent de l'archive"));
        return false;
    }
    if (alreadyLoaded(u))
        return false;

    const QString id = shortUuid(u);
    const QString contentDir = QString("%1/%2").arg(m_mount, CONTENT);
    bool nm = false, ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &dataV2) {
        emit storyProgress(id, index++, total);
        if (aborted())
            return ok = false;
        if (e.name.endsWith("bt"))
            return true;
        if (e.name.endsWith("nm"))
            nm = true;
        // on retire le début de l'UUID pour ne garder que les 8 derniers caractères
        const QString file = e.name.mid(uuidPart.contains('-') ? 28 : 24);
        QByteArray data;
        if (m_hw != HwVersion::V3) {
            data = dataV2;
        } else {
            // v2 -> v3 : déchiffrement XXTEA puis chiffrement AES
            const QByteArray plain = (file.endsWith("ni") || file.endsWith("nm")) ? dataV2
                                                                                 : xxteaDecipherHead(dataV2, genericKey());
            data = cipheredData(file, plain);
        }
        const QString name = cipheredName(file);
        debug(tr("Fichier %1/%2 > %3").arg(index).arg(total).arg(name));
        if (!writeFile(contentDir + "/" + name, data, true))
            return ok = false;
        if (m_hw != HwVersion::V3 && file.endsWith("ri"))
            m_bt = cipher(data.left(0x40), m_deviceKey, {});
        return true;
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    info(tr("Création du fichier d'autorisation…"));
    if (!writeFile(QString("%1/%2/bt").arg(contentDir, id), m_bt))
        return false;
    m_stories << DeviceStory{u, false, nm};
    return writePackIndex();
}

// Sauvegarde chiffrée faite par EasyLunii : copiée telle quelle, sur la Lunii d'origine seulement
bool LuniiDevice::restoreBackup(const QString &path)
{
    ArchiveReader ar(path);
    const QJsonObject marker = QJsonDocument::fromJson(ar.read(BACKUP_MARKER)).object();
    if (marker.value("device").toString() != "lunii" || marker.value("snu").toString() != snu()) {
        error(tr("Cette sauvegarde provient d'une autre conteuse (n° %1) : elle ne peut être restaurée que sur celle-ci")
                  .arg(marker.value("snu").toString()));
        return false;
    }
    const QUuid u(marker.value("uuid").toString());
    if (u.isNull()) {
        error(tr("Sauvegarde invalide"));
        return false;
    }
    if (alreadyLoaded(u))
        return false;
    info(tr("Restauration d'une sauvegarde chiffrée…"));
    const QString out = QString("%1/%2/%3").arg(m_mount, CONTENT, shortUuid(u));
    bool ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &data) {
        emit storyProgress(shortUuid(u), index++, total);
        if (aborted())
            return ok = false;
        if (e.name == BACKUP_MARKER)
            return true;
        return ok = writeFile(out + "/" + e.name.section('/', 1), data, true);
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    m_stories << DeviceStory{u, false, QFile::exists(out + "/nm")};
    return writePackIndex();
}

// Sauvegarde personnelle v3 : déjà chiffrée pour cet appareil, copiée telle quelle
bool LuniiDevice::importV3(const QString &path)
{
    if (!QFileInfo(path).fileName().toLower().startsWith(snu().toLower())) {
        error(tr("Cette sauvegarde v3 provient d'une autre Lunii (numéro de série différent)"));
        return false;
    }
    info(tr("Restauration d'une sauvegarde personnelle v3…"));
    ArchiveReader ar(path);
    if (ar.entries().isEmpty())
        return false;
    const QString uuidPart = ar.entries().first().name.section('/', 0, 0);
    QUuid u;
    if (uuidPart.size() >= 16)
        u = uuidPart.contains('-') ? QUuid(uuidPart) : QUuid::fromRfc4122(QByteArray::fromHex(uuidPart.toLatin1()));
    if (u.isNull()) {
        error(tr("Dossier UUID absent de l'archive"));
        return false;
    }
    if (alreadyLoaded(u))
        return false;
    const QString id = shortUuid(u);
    bool nm = false, ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &data) {
        emit storyProgress(id, index++, total);
        if (aborted())
            return ok = false;
        if (e.name.endsWith("nm"))
            nm = true;
        const QString rel = e.name.mid(uuidPart.size() + 1);
        if (!writeFile(QString("%1/%2/%3/%4").arg(m_mount, CONTENT, id, cipheredName(rel)), data, true))
            return ok = false;
        return true;
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    m_stories << DeviceStory{u, false, nm};
    return writePackIndex();
}

bool LuniiDevice::importStudio(const QString &path)
{
    StudioPack pack;
    QString err;
    // story.json seul d'abord : les assets sont lus un par un ensuite (mémoire)
    if (!StudioPack::loadArchive(path, pack, &err, false)) {
        error(tr("Pack STUdio illisible : %1").arg(err));
        return false;
    }
    const QUuid u = pack.packUuid();
    if (u.isNull()) {
        error(tr("Pack STUdio sans nœud de départ"));
        return false;
    }
    StoryDb::instance().addThirdParty(u, pack.title, pack.description);
    if (alreadyLoaded(u))
        return false;
    if (!pack.thumbnail.isEmpty())
        StoryDb::instance().addThumb(u, pack.thumbnail);

    const StudioPack::LuniiIndex ix = pack.luniiIndex();
    const QString id = shortUuid(u);
    const QString out = QString("%1/%2/%3").arg(m_mount, CONTENT, id);
    ArchiveReader ar(path);
    bool ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach(
        [&](const ArchiveEntry &e, const QByteArray &raw) {
            emit storyProgress(id, index++, total);
            if (aborted())
                return ok = false;
            const QString asset = e.name.mid(7);
            QByteArray data;
            QString name;
            if (ix.riPos.contains(asset)) {
                name = cipheredName(ix.riName.value(asset), true, false);
                data = imageToBmpRle4(raw);
                if (data.isEmpty()) {
                    error(tr("Image illisible : %1").arg(asset));
                    return ok = false;
                }
            } else if (ix.siPos.contains(asset)) {
                name = cipheredName(ix.siName.value(asset), false, true);
                data = raw;
                if (transcodingRequired(asset, data)) {
                    warn(tr("Conversion audio de %1 (%2 Ko)…").arg(asset).arg(data.size() / 1024));
                    emit fileProgress(QStringLiteral("ffmpeg"), 0, 0);
                    QString terr;
                    data = transcodeToMp3(raw, &terr);
                    if (data.isEmpty()) {
                        error(tr("Conversion audio impossible (%1) : %2").arg(asset, terr));
                        return ok = false;
                    }
                }
                const Mp3Info mi = mp3Info(data);
                if (mi.hasId3v1 || mi.hasId3v2)
                    data = stripMp3Tags(data);
            } else {
                return true;   // asset non référencé
            }
            debug(tr("Fichier %1/%2 > %3").arg(index).arg(total).arg(name));
            return ok = writeFile(out + "/" + name, cipheredData(asset, data), true);
        },
        [](const QString &n) { return n.startsWith("assets/"); });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }

    const QByteArray ri = StudioPack::riData(ix);
    ok = writeFile(out + "/ri", cipheredData("ri", ri));
    if (m_hw != HwVersion::V3)
        m_bt = cipher(cipheredData("ri", ri).left(0x40), m_deviceKey, {});
    ok = ok && writeFile(out + "/si", cipheredData("si", StudioPack::siData(ix)));
    ok = ok && writeFile(out + "/li", cipheredData("li", StudioPack::liData(ix)));
    ok = ok && writeFile(out + "/ni", pack.niData(ix));
    info(tr("Création du fichier d'autorisation…"));
    ok = ok && writeFile(out + "/bt", m_bt);
    if (ok && pack.nightMode)
        ok = writeFile(out + "/nm", {});
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    m_stories << DeviceStory{u, false, pack.nightMode};
    return writePackIndex();
}

// ---------------------------------------------------------------- export

bool LuniiDevice::exportAllowed(const QUuid &) const
{
    // toujours possible : les histoires officielles partent en sauvegarde chiffrée liée à cette Lunii
    return true;
}

QString LuniiDevice::exportStory(const QUuid &u, const QString &outDir)
{
    m_abort.storeRelaxed(0);
    const int i = indexOf(u);
    if (i < 0) {
        error(tr("Cette histoire n'est pas sur la Lunii"));
        return {};
    }
    const DeviceStory &s = m_stories[i];
    if (!QFileInfo(storyDir(s)).isDir()) {
        error(tr("Dossier de l'histoire introuvable (%1)").arg(storyDir(s)));
        return {};
    }
    // Lunii.QT interdit l'export déchiffré des histoires officielles sur v1/v2 : sauvegarde chiffrée à la place
    const bool official = m_hw != HwVersion::V3 && StoryDb::instance().isOfficial(u);
    return canExportPlain() && !official ? exportPlain(s, outDir) : exportBackup(s, outDir);
}

QString LuniiDevice::exportBackup(const DeviceStory &s, const QString &outDir)
{
    const QString dir = storyDir(s);
    const QString title = StoryDb::instance().title(s.uuid);
    info(tr("Export de %1 - %2").arg(s.shortId(), title));
    const QString zipPath = QString("%1/%2.%3.%4.zip").arg(outDir, snu(), secureFileName(title), s.shortId());
    ArchiveWriter w(zipPath);
    const QStringList files = storyFiles(dir);
    for (int i = 0; i < files.size(); ++i) {
        if (aborted())
            return {};
        emit storyProgress(s.shortId(), i, int(files.size()));
        QFile f(files[i]);
        if (!f.open(QIODevice::ReadOnly) || !w.add(uuidStr(s.uuid).toLower() + files[i].mid(dir.size()), f.readAll())) {
            error(tr("Échec de création du zip : %1").arg(w.error()));
            return {};
        }
    }
    // marqueur : sauvegarde chiffrée, restaurable uniquement sur cette Lunii
    const QJsonObject marker{{"device", "lunii"}, {"snu", snu()}, {"uuid", uuidStr(s.uuid)}, {"nightMode", s.nightMode}};
    if (!w.add(BACKUP_MARKER, QJsonDocument(marker).toJson(QJsonDocument::Compact)))
        return {};
    return w.close() ? zipPath : QString();
}

QString LuniiDevice::exportPlain(const DeviceStory &s, const QString &outDir)
{
    const QString dir = storyDir(s);
    const QString title = StoryDb::instance().title(s.uuid);
    info(tr("Export de %1 - %2").arg(s.shortId(), title));
    QByteArray savedKey = m_storyKey, savedIv = m_storyIv;
    if (m_hw == HwVersion::V3) {
        storyKeysFor(m_deviceKey, m_deviceIv, dir + "/bt", hexSnu(m_snu), m_storyKey, m_storyIv);
        if (!checkV3Key(dir)) {
            m_storyKey = savedKey;
            m_storyIv = savedIv;
            error(tr("Les clés de l'appareil sont nécessaires pour exporter cette histoire"));
            return {};
        }
    }
    const QString zipPath = QString("%1/%2.%3.plain.pk").arg(outDir, secureFileName(title), s.shortId());
    ArchiveWriter w(zipPath);
    const QStringList files = storyFiles(dir);
    bool ok = true;
    for (int i = 0; ok && i < files.size(); ++i) {
        if (aborted()) {
            ok = false;
            break;
        }
        emit storyProgress(s.shortId(), i, int(files.size()));
        const QString rel = files[i].mid(dir.size() + 1);
        if (rel == "bt" || rel == "md")
            continue;
        ok = w.add(plainName(rel), plainData(files[i]));
    }
    ok = ok && w.add("uuid.bin", s.uuid.toRfc4122());
    if (ok && !StoryDb::instance().isOfficial(s.uuid)) {
        const QByteArray pic = StoryDb::instance().picture(s.uuid);
        if (!pic.isEmpty())
            ok = w.add("_thumbnail.png", pic);
        const QByteArray meta = StoryDb::instance().thirdPartyMeta(s.uuid);
        if (ok && !meta.isEmpty())
            ok = w.add("_metadata.json", meta);
    }
    ok = w.close() && ok;
    m_storyKey = savedKey;
    m_storyIv = savedIv;
    if (!ok) {
        error(tr("Échec de création de l'archive : %1").arg(w.error()));
        QFile::remove(zipPath);
        return {};
    }
    return zipPath;
}

// ---------------------------------------------------------------- organisation

bool LuniiDevice::removeStory(const QUuid &u)
{
    const int i = indexOf(u);
    if (i < 0) {
        error(tr("Cette histoire n'est pas sur la Lunii"));
        return false;
    }
    info(tr("Suppression de %1 - %2…").arg(shortUuid(u), StoryDb::instance().title(u)));
    emit storyProgress(shortUuid(u), 0, 2);
    if (!cleanStoryDir(u))
        return false;
    m_stories.removeAt(i);
    emit storyProgress(shortUuid(u), 1, 2);
    return writePackIndex();
}

bool LuniiDevice::setHidden(const QUuid &u, bool hidden)
{
    const int i = indexOf(u);
    if (i < 0 || m_stories[i].hidden == hidden)
        return i >= 0;
    const QString from = storyDir(m_stories[i]);
    DeviceStory moved = m_stories[i];
    moved.hidden = hidden;
    const QString to = storyDir(moved);
    QDir().mkpath(QFileInfo(to).path());
    if (!QFileInfo(from).isDir() || !QDir().rename(from, to)) {
        error(tr("Déplacement impossible de %1 vers %2").arg(from, to));
        return false;
    }
    m_stories[i] = moved;
    return writePackIndex();
}

bool LuniiDevice::setNightMode(const QUuid &u, bool enabled)
{
    const int i = indexOf(u);
    if (i < 0)
        return false;
    const QString nm = storyDir(m_stories[i]) + "/nm";
    const bool ok = enabled ? writeFile(nm, {}) : (!QFile::exists(nm) || QFile::remove(nm));
    if (ok)
        m_stories[i].nightMode = enabled;
    ::sync();
    return ok;
}

bool LuniiDevice::validStory(const QString &storyPath)
{
    const QStringList files = storyFiles(storyPath);
    auto has = [&](const QString &suffix) {
        for (const QString &f : files)
            if (f.toLower().endsWith(suffix))
                return true;
        return false;
    };
    for (const char *p : {"li", "ni", "ri", "si"})
        if (!has(p)) {
            warn(tr("%1 absent dans %2").arg(p, storyPath));
            return false;
        }
    if (m_hw == HwVersion::V3) {
        if (!checkV3Key(storyPath))
            return true;   // clés inconnues : impossible d'aller plus loin
    } else if (!checkV2Bt(storyPath)) {
        warn(tr("Fichier d'autorisation bt incorrect dans %1, réparation").arg(storyPath));
        QFile ri(storyPath + "/ri");
        if (ri.open(QIODevice::ReadOnly))
            writeFile(storyPath + "/bt", cipher(ri.read(0x40), m_deviceKey, {}));
    }
    if (!QFileInfo(storyPath + "/rf").isDir() || !QFileInfo(storyPath + "/sf").isDir()) {
        warn(tr("rf ou sf absent dans %1").arg(storyPath));
        return false;
    }
    QByteArray key = m_storyKey, iv = m_storyIv;
    if (m_hw == HwVersion::V3)
        storyKeysFor(m_deviceKey, m_deviceIv, storyPath + "/bt", hexSnu(m_snu), m_storyKey, m_storyIv);
    bool ok = true;
    for (const char *idx : {"ri", "si"}) {
        QByteArray plain = plainData(storyPath + "/" + idx);
        while (plain.endsWith('\0'))
            plain.chop(1);
        const QString sub = idx[0] == 'r' ? "rf" : "sf";
        for (int i = 0; ok && i + 12 <= plain.size(); i += 12) {
            const QString res = QString::fromLatin1(plain.mid(i, 12)).replace('\\', '/');
            if (!QFile::exists(QString("%1/%2/%3").arg(storyPath, sub, res))) {
                warn(tr("%1/%2 absent dans %3").arg(sub, res, storyPath));
                ok = false;
            }
        }
    }
    m_storyKey = key;
    m_storyIv = iv;
    return ok;
}

static QStringList subdirs(const QString &path)
{
    return QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

int LuniiDevice::recoverStories(bool dryRun)
{
    int recovered = 0;
    const QStringList active = subdirs(m_mount + "/" + CONTENT);
    QStringList all = active + subdirs(m_mount + "/" + CONTENT_HIDDEN);
    all.sort();
    for (int i = 0; i < all.size(); ++i) {
        const QString dirName = all[i];
        emit storyProgress(dirName, i, int(all.size()));
        QString full = StoryDb::instance().findFullUuid(dirName);
        if (full.isEmpty() && dirName.size() == 8) {
            bool hex = false;
            dirName.toUInt(&hex, 16);
            if (hex)
                full = QString("00000000-0000-0000-0000-0000") + dirName;
        }
        const QUuid u(full);
        if (u.isNull())
            continue;
        const bool hidden = !active.contains(dirName);
        const QString dir = QString("%1/%2/%3").arg(m_mount, hidden ? CONTENT_HIDDEN : CONTENT, dirName);
        if (indexOf(u) >= 0)
            continue;
        if (!validStory(dir)) {
            info(tr("Histoire perdue ignorée (incomplète) : %1").arg(uuidStr(u)));
            continue;
        }
        info(tr("%1 : %2").arg(dryRun ? tr("Trouvée") : tr("Récupérée"), uuidStr(u)));
        if (!dryRun)
            m_stories << DeviceStory{u, hidden, QFile::exists(dir + "/nm")};
        ++recovered;
    }
    if (!dryRun && recovered)
        writePackIndex();
    return recovered;
}

int LuniiDevice::cleanupStories(qint64 *freedBytes)
{
    int removed = 0;
    qint64 freed = 0;
    for (const char *base : {CONTENT, CONTENT_HIDDEN}) {
        for (const QString &dirName : subdirs(m_mount + "/" + base)) {
            bool known = false;
            for (const DeviceStory &s : m_stories)
                known = known || s.shortId().compare(dirName, Qt::CaseInsensitive) == 0;
            if (known)
                continue;
            const QString dir = QString("%1/%2/%3").arg(m_mount, base, dirName);
            for (const QString &f : storyFiles(dir))
                freed += QFileInfo(f).size();
            info(tr("Suppression de %1").arg(dir));
            if (QDir(dir).removeRecursively())
                ++removed;
            else
                warn(tr("Échec de suppression : %1").arg(dir));
        }
    }
    ::sync();
    if (freedBytes)
        *freedBytes = freed;
    return removed;
}

} // namespace lunii
