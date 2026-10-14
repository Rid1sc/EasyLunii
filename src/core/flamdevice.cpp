#include "core/flamdevice.h"

#include "core/archive.h"
#include "core/convert.h"
#include "core/crypto.h"
#include "core/liff.h"
#include "core/packtype.h"
#include "core/storydb.h"
#include "core/studiopack.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtEndian>

#include <unistd.h>

namespace lunii {

static const char *STR = "str";
static const char *STR_HIDDEN = "str.hidden";
static const char *LIB_DIR = "etc/library";
static const char *LIB_CACHE = "usr/0/library.cache";
static const QPair<quint16, quint16> FLAM_VIDPID{0x303A, 0x819E};

// Textes par défaut de Lunii.QT (écrits dans la fiche « info » lue par la Flam)
static const char *STORY_UNKNOWN = "Unknown story (maybe a User created story)...";
static const char *AUTHOR_NOT_FOUND = "No authors.";

static QString titleOf(const QUuid &u)
{
    const QString t = StoryDb::instance().title(u);
    return t.isEmpty() ? QString(STORY_UNKNOWN) : t;
}

static QString lowerUuid(const QUuid &u)
{
    return u.toString(QUuid::WithoutBraces);   // minuscules
}

bool FlamDevice::isFlam(const QString &mountPoint)
{
    return QFileInfo(mountPoint + "/.mdf").isFile();
}

FlamDevice::FlamDevice(const QString &mountPoint, QObject *parent) : StoryDevice(mountPoint, parent)
{
    if (!parseMdf())
        return;
    readStories();
    m_valid = true;
}

// ---------------------------------------------------------------- métadonnées (.mdf)

bool FlamDevice::parseMdf()
{
    QFile f(m_mount + "/.mdf");
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray mdf = f.readAll();
    if (mdf.size() < 0x4E)
        return false;

    // versions : « main: x.y.z-…\ncomm: x.y.z-… »
    QString versions = QString::fromUtf8(mdf.mid(2, 48));
    while (versions.endsWith(QChar(0)))
        versions.chop(1);
    versions.remove("main: ").remove("comm: ");
    const QStringList lines = versions.split('\n', Qt::SkipEmptyParts);
    if (!lines.isEmpty())
        m_fwMain = lines[0].section('-', 0, 0);
    if (lines.size() > 1)
        m_fwComm = lines[1].section('-', 0, 0);

    QByteArray snuHex = mdf.mid(50, 24);
    while (snuHex.endsWith('\0'))
        snuHex.chop(1);
    m_snu = QByteArray::fromHex(snuHex);
    const auto *p = reinterpret_cast<const uchar *>(mdf.constData());
    m_flamHw = QPair<quint16, quint16>{qFromLittleEndian<quint16>(p + 74), qFromLittleEndian<quint16>(p + 76)} == FLAM_VIDPID;

    const QByteArray hex = m_snu.toHex();
    if (m_fwMain.startsWith("1.")) {
        // firmware 1.x : clés dérivées du numéro de série
        m_keyfile = mdf.mid(0x4E, 32);
        m_storyKey = hex + QByteArray(2, 0);
        m_storyIv = QByteArray(8, 0) + hex.left(8);
    } else {
        m_keyfile = hex + QByteArray(10, 0) + hex.left(8);
        m_storyKey = mdf.mid(0x4E, 16);
        m_storyIv = mdf.mid(0x5E, 16);
    }

    // sauvegarde du .mdf (comme Lunii.QT)
    QDir().mkpath(configDir());
    const QString backup = QString("%1/%2.v%3.mdf").arg(configDir(), snu(), m_fwMain);
    if (!QFile::exists(backup)) {
        QFile b(backup);
        if (b.open(QIODevice::WriteOnly))
            b.write(mdf);
        info(tr("Sauvegarde du .mdf créée (%1)").arg(backup));
    }
    return true;
}

QString FlamDevice::snu() const
{
    QString s = QString::fromLatin1(m_snu.toHex()).toUpper();
    while (s.startsWith('0'))
        s.remove(0, 1);
    return s;
}

QString FlamDevice::describe() const
{
    return tr("Flam, firmware %1 (communication %2), SNU %3%4")
        .arg(m_fwMain, m_fwComm, snu(), m_flamHw ? QString() : tr(", identifiant USB inattendu"));
}

QString FlamDevice::storyDir(const DeviceStory &s) const
{
    return QString("%1/%2/%3").arg(m_mount, s.hidden ? STR_HIDDEN : STR, lowerUuid(s.uuid));
}

// ---------------------------------------------------------------- liste (etc/library/list)

static QList<QUuid> readList(const QString &path)
{
    QList<QUuid> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QUuid u(QString::fromLatin1(line.trimmed()));
        if (!u.isNull() && !out.contains(u))
            out << u;
    }
    return out;
}

void FlamDevice::readStories()
{
    m_stories.clear();
    for (bool hidden : {false, true})
        for (const QUuid &u : readList(QString("%1/%2/%3").arg(m_mount, LIB_DIR, hidden ? "list.hidden" : "list")))
            if (indexOf(u) < 0)
                m_stories << DeviceStory{u, hidden, false};
    info(tr("%1 histoires lues").arg(m_stories.size()));
}

bool FlamDevice::writePackIndex()
{
    QByteArray list, hiddenList;
    for (const DeviceStory &s : m_stories)
        (s.hidden ? hiddenList : list) += lowerUuid(s.uuid).toLatin1() + "\n";
    QDir().mkpath(m_mount + "/" + LIB_DIR);
    // le cache de la bibliothèque est reconstruit par la Flam
    QFile::remove(m_mount + "/" + LIB_CACHE);
    bool ok = true;
    for (const auto &[name, data] : {std::pair{"list", list}, std::pair{"list.hidden", hiddenList}}) {
        QSaveFile f(QString("%1/%2/%3").arg(m_mount, LIB_DIR, name));
        ok = ok && f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit();
    }
    if (!ok)
        error(tr("Échec de l'écriture de la liste des histoires"));
    ::sync();
    emit storiesChanged();
    return ok;
}

// ---------------------------------------------------------------- chiffrement

QByteArray FlamDevice::cipheredData(const QString &file, const QByteArray &data, bool flamStory, bool force) const
{
    if (!flamStory) {
        // histoire Lunii sur Flam : AES sur les 512 premiers octets, clés inversées par mot
        if (file.endsWith("ni") || file.endsWith("nm"))
            return data;
        return aesCipherHead(data, reverseWords(m_storyKey), reverseWords(m_storyIv), 0x200);
    }
    // histoire Flam : scripts et fiches chiffrés entièrement
    if (file.endsWith(".lua") || file.endsWith(".plain") || force)
        return aesCipherHead(data, m_storyKey, m_storyIv, int(data.size()));
    return data;
}

QString FlamDevice::flamCipheredName(QString file)
{
    if (file.endsWith(".plain"))
        file.chop(6);
    if (file.endsWith(".lua"))
        file = file.left(file.size() - 4) + ".lsf";
    return file;
}

QString FlamDevice::luniiCipheredName(QString file, bool studioRi, bool studioSi)
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
    const int slash = file.lastIndexOf('/');
    const QString base = file.mid(slash + 1);
    if (base.size() >= 8)
        file = file.left(slash + 1) + base.toUpper();
    return file;
}

// ---------------------------------------------------------------- fiche et vignette

void FlamDevice::writeInfo(const QUuid &u, const QString &dir)
{
    QString name = StoryDb::instance().title(u);
    QString author = StoryDb::instance().author(u);
    name = asciiFold(name.isEmpty() ? QString(STORY_UNKNOWN) : name);
    author = asciiFold(author.isEmpty() ? QString(AUTHOR_NOT_FOUND) : author);
    writeFile(dir + "/info", QString("%1\n%1\n0\n%1\n%2").arg(name, author).toUtf8());
}

void FlamDevice::writeThumbnail(const QUuid &u, const QString &dir)
{
    QByteArray pic = StoryDb::instance().picture(u, true);
    QByteArray liff = imageToLiff(pic);
    if (liff.isEmpty()) {
        // pas de jaquette : vignette unie (Lunii.QT échouait dans ce cas)
        QImage blank(104, 104, QImage::Format_RGB32);
        blank.fill(QColor(20, 40, 70));
        liff = imageToLiff(blank);
    }
    writeFile(dir + "/img/thumbnail.lif", liff);
}

QByteArray FlamDevice::coverImage(const QUuid &u) const
{
    const int i = indexOf(u);
    if (i < 0)
        return {};
    QFile f(storyDir(m_stories[i]) + "/img/thumbnail.lif");
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QImage img = liffToImage(f.readAll());
    if (img.isNull())
        return {};
    QByteArray png;
    QBuffer b(&png);
    b.open(QIODevice::WriteOnly);
    img.save(&b, "PNG");
    return png;
}

bool FlamDevice::finish(const DeviceStory &s)
{
    m_stories << s;
    return writePackIndex();
}

bool FlamDevice::cleanStoryDir(const QUuid &u)
{
    bool ok = true;
    for (const char *base : {STR, STR_HIDDEN}) {
        QDir d(QString("%1/%2/%3").arg(m_mount, base, lowerUuid(u)));
        if (d.exists() && !d.removeRecursively()) {
            error(tr("Suppression impossible : %1").arg(d.path()));
            ok = false;
        }
    }
    return ok;
}

// ---------------------------------------------------------------- import

bool FlamDevice::luniiStoriesSupported()
{
    if (!m_fwMain.startsWith("1."))
        return true;
    error(tr("Mettez la Flam à jour (firmware 2.x) pour y charger des histoires Lunii"));
    return false;
}

bool FlamDevice::importStory(const QString &path)
{
    m_abort.storeRelaxed(0);
    if (!canImport()) {
        error(tr("Clés absentes : import impossible sur cette Flam"));
        return false;
    }
    info(tr("Chargement de %1…").arg(path));
    if (QFileInfo(path).size() >= freeSpace()) {
        error(tr("Place insuffisante sur la Flam (%1 Mo libres)").arg(freeSpace() / 1024 / 1024));
        return false;
    }
    const PackType t = detectPackType(path);
    debug(tr("Type d'archive : %1").arg(packTypeLabel(t)));
    bool ok = false;
    switch (t) {
    case PackType::LuniiPlain: ok = luniiStoriesSupported() && importLuniiPlain(path); break;
    case PackType::FlamPlain: ok = importFlamPlain(path); break;
    case PackType::FlamZip:
    case PackType::Flam7z: ok = importFlamBackup(path); break;
    case PackType::LuniiFlamZip: ok = importLuniiFlamBackup(path); break;
    case PackType::LuniiV2Zip:
    case PackType::LuniiV2_7z: ok = luniiStoriesSupported() && importLuniiV2(path); break;
    case PackType::StudioZip:
    case PackType::Studio7z: ok = luniiStoriesSupported() && importStudio(path); break;
    default:
        error(tr("Type d'archive non pris en charge sur la Flam : %1").arg(packTypeLabel(t)));
        return false;
    }
    ::sync();
    return ok;
}

static QUuid uuidFromDir(const QString &dir)
{
    if (dir.size() < 16)
        return {};
    return dir.contains('-') ? QUuid(dir) : QUuid::fromRfc4122(QByteArray::fromHex(dir.toLatin1()));
}

bool FlamDevice::importLuniiPlain(const QString &path)
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

    const QString out = QString("%1/%2/%3").arg(m_mount, STR, lowerUuid(u));
    bool ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &data) {
        emit storyProgress(shortUuid(u), index++, total);
        if (aborted())
            return ok = false;
        if (e.name == "uuid.bin" || e.name == "_metadata.json" || e.name == "_thumbnail.png" || e.name.endsWith("bt"))
            return true;
        return ok = writeFile(out + "/" + luniiCipheredName(e.name), cipheredData(e.name, data, false), true);
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    info(tr("Création du fichier d'autorisation…"));
    ok = writeFile(out + "/key", m_keyfile);
    writeInfo(u, out);
    writeThumbnail(u, out);
    return ok && finish({u, false, false});
}

bool FlamDevice::importFlamPlain(const QString &path)
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
    const QString out = QString("%1/%2/%3").arg(m_mount, STR, lowerUuid(u));
    bool ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &data) {
        emit storyProgress(shortUuid(u), index++, total);
        if (aborted())
            return ok = false;
        if (e.name == "uuid.bin" || e.name == "_metadata.json" || e.name == "_thumbnail.png")
            return true;
        return ok = writeFile(out + "/" + flamCipheredName(e.name), cipheredData(e.name, data, true), true);
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    info(tr("Création du fichier d'autorisation…"));
    return writeFile(out + "/key", m_keyfile) && finish({u, false, false});
}

// Sauvegarde d'une histoire Flam (dossier UUID + fichier « key ») : copie telle quelle
bool FlamDevice::importFlamBackup(const QString &path)
{
    ArchiveReader ar(path);
    const QStringList names = ar.names();
    bool hasKey = false, hasBt = false;
    QString keyName;
    for (const QString &n : names) {
        if (n.endsWith("bt"))
            hasBt = true;
        if (n.endsWith("key"))
            hasKey = true, keyName = n;
    }
    if (hasBt) {
        // transfert d'histoires officielles entre appareils : non repris de Lunii.QT
        error(tr("Cette archive contient une histoire officielle Flam : elle ne peut être restaurée que par Lunii sur son appareil d'origine"));
        return false;
    }
    if (!hasKey) {
        error(tr("Sauvegarde Flam incomplète : fichier « key » absent"));
        return false;
    }
    if (ar.read(keyName) != m_keyfile) {
        error(tr("Cette sauvegarde provient d'une autre Flam : elle n'y serait pas lisible"));
        return false;
    }
    const QUuid u = uuidFromDir(names.first().section('/', 0, 0));
    if (u.isNull()) {
        error(tr("Dossier UUID absent de l'archive"));
        return false;
    }
    if (alreadyLoaded(u))
        return false;
    info(tr("Restauration d'une sauvegarde Flam…"));
    bool ok = true;
    int index = 0;
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &data) {
        emit storyProgress(shortUuid(u), index++, int(names.size()));
        if (aborted())
            return ok = false;
        const QString rel = e.name.section('/', 1);
        return ok = writeFile(QString("%1/%2/%3/%4").arg(m_mount, STR, lowerUuid(u), rel), data, true);
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    return finish({u, false, false});
}

// Histoire Lunii sauvegardée depuis une Flam : liée à l'appareil (numéro de série dans le nom)
bool FlamDevice::importLuniiFlamBackup(const QString &path)
{
    if (!QFileInfo(path).fileName().toLower().startsWith(snu().toLower())) {
        error(tr("Cette sauvegarde provient d'une autre Flam (numéro de série différent)"));
        return false;
    }
    ArchiveReader ar(path);
    const QStringList names = ar.names();
    const QUuid u = uuidFromDir(names.first().section('/', 0, 0));
    if (u.isNull()) {
        error(tr("Dossier UUID absent de l'archive"));
        return false;
    }
    if (alreadyLoaded(u))
        return false;
    bool ok = true, nm = false;
    int index = 0;
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &data) {
        emit storyProgress(shortUuid(u), index++, int(names.size()));
        if (aborted())
            return ok = false;
        nm = nm || e.name.endsWith("nm");
        const QString rel = e.name.section('/', 1);
        return ok = writeFile(QString("%1/%2/%3/%4").arg(m_mount, STR, lowerUuid(u), rel), data, true);
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    return finish({u, false, nm});
}

// Archive Lunii v1/v2 : XXTEA (clé générique) -> AES de la Flam
bool FlamDevice::importLuniiV2(const QString &path)
{
    ArchiveReader ar(path);
    if (ar.contains("story.json")) {
        error(tr("Format d'archive corrompu"));
        return false;
    }
    const QString uuidPart = ar.entries().first().name.section('/', 0, 0);
    const QUuid u = uuidFromDir(uuidPart);
    if (u.isNull()) {
        error(tr("Dossier UUID absent de l'archive"));
        return false;
    }
    if (alreadyLoaded(u))
        return false;
    const QString out = QString("%1/%2/%3").arg(m_mount, STR, lowerUuid(u));
    bool ok = true, nm = false;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach([&](const ArchiveEntry &e, const QByteArray &dataV2) {
        emit storyProgress(shortUuid(u), index++, total);
        if (aborted())
            return ok = false;
        if (e.name == "uuid.bin" || e.name.endsWith("bt"))
            return true;
        nm = nm || e.name.endsWith("nm");
        const QByteArray plain = (e.name.endsWith("ni") || e.name.endsWith("nm")) ? dataV2
                                                                                 : xxteaDecipherHead(dataV2, genericKey());
        const QString rel = e.name.mid(uuidPart.size() + 1);
        return ok = writeFile(out + "/" + luniiCipheredName(rel), cipheredData(e.name, plain, false), true);
    });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    info(tr("Création du fichier d'autorisation…"));
    ok = writeFile(out + "/key", m_keyfile);
    writeInfo(u, out);
    writeThumbnail(u, out);
    return ok && finish({u, false, nm});
}

bool FlamDevice::importStudio(const QString &path)
{
    StudioPack pack;
    QString err;
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
    const QString out = QString("%1/%2/%3").arg(m_mount, STR, lowerUuid(u));
    ArchiveReader ar(path);
    bool ok = true;
    int index = 0;
    const int total = int(ar.entries().size());
    ar.forEach(
        [&](const ArchiveEntry &e, const QByteArray &raw) {
            emit storyProgress(shortUuid(u), index++, total);
            if (aborted())
                return ok = false;
            const QString asset = e.name.mid(7);
            QByteArray data;
            QString name;
            if (ix.riPos.contains(asset)) {
                name = luniiCipheredName(ix.riName.value(asset), true, false);
                data = imageToBmpRle4(raw);
                if (data.isEmpty()) {
                    error(tr("Image illisible : %1").arg(asset));
                    return ok = false;
                }
            } else if (ix.siPos.contains(asset)) {
                name = luniiCipheredName(ix.siName.value(asset), false, true);
                data = raw;
                if (transcodingRequired(asset, data)) {
                    warn(tr("Conversion audio de %1 (%2 Ko)…").arg(asset).arg(data.size() / 1024));
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
                return true;
            }
            return ok = writeFile(out + "/" + name, cipheredData(asset, data, false), true);
        },
        [](const QString &n) { return n.startsWith("assets/"); });
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    ok = writeFile(out + "/ri", cipheredData("ri", StudioPack::riData(ix), false))
      && writeFile(out + "/si", cipheredData("si", StudioPack::siData(ix), false))
      && writeFile(out + "/li", cipheredData("li", StudioPack::liData(ix), false))
      && writeFile(out + "/ni", pack.niData(ix));
    info(tr("Création du fichier d'autorisation…"));
    ok = ok && writeFile(out + "/key", m_keyfile);
    if (ok && pack.nightMode)
        ok = writeFile(out + "/nm", {});
    if (!ok) {
        cleanStoryDir(u);
        return false;
    }
    writeInfo(u, out);
    writeThumbnail(u, out);
    return finish({u, false, pack.nightMode});
}

// ---------------------------------------------------------------- export

QString FlamDevice::exportStory(const QUuid &u, const QString &outDir)
{
    m_abort.storeRelaxed(0);
    const int i = indexOf(u);
    if (i < 0) {
        error(tr("Cette histoire n'est pas sur la Flam"));
        return {};
    }
    const DeviceStory &s = m_stories[i];
    const QString dir = storyDir(s);
    if (!QFileInfo(dir).isDir()) {
        error(tr("Dossier de l'histoire introuvable (%1)").arg(dir));
        return {};
    }
    // export déchiffré seulement pour les histoires chiffrées avec les clés de cette Flam
    QFile key(dir + "/key");
    const bool ownKeys = key.open(QIODevice::ReadOnly) && key.readAll() == m_keyfile;
    // histoires officielles : toujours en sauvegarde chiffrée (pas de version déchiffrée)
    if (!ownKeys || StoryDb::instance().isOfficial(u))
        return exportBackup(s, outDir);
    bool flam = false, lunii[4] = {false, false, false, false};
    for (const QString &f : storyFiles(dir)) {
        flam = flam || f.toLower().endsWith(".lsf");
        const char *idx[4] = {"ri", "si", "li", "ni"};
        for (int k = 0; k < 4; ++k)
            lunii[k] = lunii[k] || f.toLower().endsWith(idx[k]);
    }
    if (flam)
        return exportFlamPlain(s, outDir);
    if (lunii[0] && lunii[1] && lunii[2] && lunii[3])
        return exportLuniiPlain(s, outDir);
    error(tr("Format d'histoire non pris en charge pour l'export"));
    return {};
}

QString FlamDevice::exportBackup(const DeviceStory &s, const QString &outDir)
{
    const QString dir = storyDir(s);
    const QString title = titleOf(s.uuid);
    info(tr("Sauvegarde de %1 - %2").arg(s.shortId(), title));
    const QString zipPath = QString("%1/%2.%3.%4.zip").arg(outDir, snu(), secureFileName(title), s.shortId());
    ArchiveWriter w(zipPath);
    const QStringList files = storyFiles(dir);
    const QString base = lowerUuid(s.uuid);
    for (int i = 0; i < files.size(); ++i) {
        if (aborted())
            return {};
        emit storyProgress(s.shortId(), i, int(files.size()));
        QFile f(files[i]);
        if (!f.open(QIODevice::ReadOnly) || !w.add(base + files[i].mid(dir.size()), f.readAll())) {
            error(tr("Échec de création du zip : %1").arg(w.error()));
            return {};
        }
    }
    return w.close() ? zipPath : QString();
}

QString FlamDevice::exportFlamPlain(const DeviceStory &s, const QString &outDir)
{
    const QString dir = storyDir(s);
    const QString title = titleOf(s.uuid);
    info(tr("Export de %1 - %2").arg(lowerUuid(s.uuid), title));
    const QString zipPath = QString("%1/%2.%3.plain.pk").arg(outDir, secureFileName(title), s.shortId());
    ArchiveWriter w(zipPath);
    bool ok = true;
    const QStringList files = storyFiles(dir);
    for (int i = 0; ok && i < files.size(); ++i) {
        if (aborted())
            return {};
        emit storyProgress(s.shortId(), i, int(files.size()));
        const QString fileName = QFileInfo(files[i]).fileName();
        if (fileName == "key" || fileName == "bt")
            continue;
        QString rel = files[i].mid(dir.size() + 1);
        QFile f(files[i]);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QByteArray data = f.readAll();
        if (files[i].endsWith(".lsf") || files[i].endsWith("info"))
            data = aesDecipherHead(data, m_storyKey, m_storyIv, int(data.size()));
        if (rel.endsWith(".lsf"))
            rel = rel.left(rel.size() - 4) + ".lua";
        if (rel.endsWith("info"))
            rel += ".plain";
        ok = w.add(rel, data);
    }
    ok = ok && w.add("uuid.bin", s.uuid.toRfc4122());
    ok = w.close() && ok;
    if (!ok) {
        QFile::remove(zipPath);
        return {};
    }
    return zipPath;
}

QString FlamDevice::exportLuniiPlain(const DeviceStory &s, const QString &outDir)
{
    const QString dir = storyDir(s);
    const QString title = titleOf(s.uuid);
    info(tr("Export de %1 - %2").arg(lowerUuid(s.uuid), title));
    const QString zipPath = QString("%1/%2.%3.plain.pk").arg(outDir, secureFileName(title), s.shortId());
    const QByteArray key = reverseWords(m_storyKey), iv = reverseWords(m_storyIv);
    ArchiveWriter w(zipPath);
    bool ok = true;
    const QStringList files = storyFiles(dir);
    for (int i = 0; ok && i < files.size(); ++i) {
        if (aborted())
            return {};
        emit storyProgress(s.shortId(), i, int(files.size()));
        QString rel = files[i].mid(dir.size() + 1);
        const QString fileName = QFileInfo(files[i]).fileName();
        if (rel.startsWith("img/") || fileName == "key" || fileName == "bt" || fileName == "info")
            continue;
        if (rel.startsWith("rf/"))
            rel += ".bmp";
        if (rel.startsWith("sf/"))
            rel += ".mp3";
        if (rel == "li" || rel == "ri" || rel == "si")
            rel += ".plain";
        QFile f(files[i]);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        QByteArray data = f.readAll();
        if (rel.endsWith(".mp3") || rel.endsWith(".bmp") || rel.endsWith(".plain"))
            data = aesDecipherHead(data, key, iv, 0x200);
        ok = w.add(rel, data);
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
    if (!ok) {
        QFile::remove(zipPath);
        return {};
    }
    return zipPath;
}

// ---------------------------------------------------------------- organisation

bool FlamDevice::removeStory(const QUuid &u)
{
    const int i = indexOf(u);
    if (i < 0) {
        error(tr("Cette histoire n'est pas sur la Flam"));
        return false;
    }
    info(tr("Suppression de %1 - %2…").arg(shortUuid(u), StoryDb::instance().title(u)));
    if (!cleanStoryDir(u))
        return false;
    m_stories.removeAt(i);
    return writePackIndex();
}

bool FlamDevice::setHidden(const QUuid &u, bool hidden)
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

static QStringList subdirs(const QString &path)
{
    return QDir(path).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
}

int FlamDevice::recoverStories(bool dryRun)
{
    int recovered = 0;
    const QStringList active = subdirs(m_mount + "/" + STR);
    QStringList all = active + subdirs(m_mount + "/" + STR_HIDDEN);
    all.sort();
    for (int i = 0; i < all.size(); ++i) {
        emit storyProgress(all[i], i, int(all.size()));
        QString full = StoryDb::instance().findFullUuid(all[i]);
        if (full.isEmpty())
            full = all[i];
        const QUuid u(full);
        if (u.isNull() || indexOf(u) >= 0)
            continue;
        info(tr("%1 : %2").arg(dryRun ? tr("Trouvée") : tr("Récupérée"), uuidStr(u)));
        if (!dryRun)
            m_stories << DeviceStory{u, !active.contains(all[i]), false};
        ++recovered;
    }
    if (!dryRun && recovered)
        writePackIndex();
    return recovered;
}

int FlamDevice::cleanupStories(qint64 *freedBytes)
{
    int removed = 0;
    qint64 freed = 0;
    for (const char *base : {STR, STR_HIDDEN}) {
        for (const QString &dirName : subdirs(m_mount + "/" + base)) {
            bool known = false;
            for (const DeviceStory &s : m_stories)
                known = known || lowerUuid(s.uuid).compare(dirName, Qt::CaseInsensitive) == 0;
            if (known)
                continue;
            const QString dir = QString("%1/%2/%3").arg(m_mount, base, dirName);
            for (const QString &f : storyFiles(dir))
                freed += QFileInfo(f).size();
            info(tr("Suppression de %1").arg(dir));
            if (QDir(dir).removeRecursively())
                ++removed;
        }
    }
    ::sync();
    if (freedBytes)
        *freedBytes = freed;
    return removed;
}

} // namespace lunii
