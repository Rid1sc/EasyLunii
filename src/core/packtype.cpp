#include "core/packtype.h"

#include "core/archive.h"
#include "core/crypto.h"

#include <QObject>

namespace lunii {

static bool anyEndsWith(const QStringList &names, const QString &pattern)
{
    for (const QString &n : names)
        if (n.toLower().endsWith(pattern))
            return true;
    return false;
}

static bool allPatterns(const QStringList &names, const QStringList &patterns)
{
    for (const QString &p : patterns)
        if (!anyEndsWith(names, p))
            return false;
    return true;
}

static bool isStudio(const QStringList &names)
{
    for (const QString &n : names)
        if (n.contains("story.json") || n.contains("thumbnail.png") || n.contains("assets/"))
            return true;
    return false;
}

PackType detectPackType(const QString &path)
{
    const QString lower = path.toLower();
    ArchiveReader ar(path);
    if (!ar.isValid())
        return PackType::Unknown;
    const QStringList names = ar.names();

    if (lower.endsWith(".plain.pk")) {
        if (!anyEndsWith(names, "uuid.bin"))
            return PackType::Unknown;
        if (anyEndsWith(names, ".lua"))
            return PackType::FlamPlain;
        if (allPatterns(names, {"ri.plain", "si.plain", "li.plain"}))
            return PackType::LuniiPlain;
        return PackType::Unknown;
    }
    if (lower.endsWith(".v1.pk") || lower.endsWith(".v2.pk"))
        return PackType::LuniiV2Zip;

    const bool is7z = lower.endsWith(".7z");
    if (anyEndsWith(names, ".lsf"))
        return is7z ? PackType::Flam7z : PackType::FlamZip;
    if (allPatterns(names, {"ri", "si", "li", "ni"})) {
        if (is7z)
            return PackType::LuniiV2_7z;
        for (const ArchiveEntry &e : ar.entries())
            if (e.name.endsWith("bt"))
                return e.size == 0x20 ? PackType::LuniiV3Zip : PackType::LuniiV2Zip;
        if (anyEndsWith(names, "key"))
            return PackType::LuniiFlamZip;
        // ni bt ni key : on devine avec ri (doit commencer par "000\" une fois déchiffré en XXTEA)
        for (const QString &n : names) {
            if (n.endsWith("ri")) {
                const QByteArray plain = xxteaDecipherHead(ar.read(n), genericKey());
                return plain.startsWith("000\\") ? PackType::LuniiV2Zip : PackType::Unknown;
            }
        }
        return PackType::Unknown;
    }
    if (isStudio(names))
        return is7z ? PackType::Studio7z : PackType::StudioZip;
    return PackType::Unknown;
}

QString packTypeLabel(PackType t)
{
    switch (t) {
    case PackType::LuniiPlain: return QObject::tr("Lunii déchiffrée (.plain.pk)");
    case PackType::LuniiV2Zip: return QObject::tr("Lunii v1/v2 (zip)");
    case PackType::LuniiV2_7z: return QObject::tr("Lunii v1/v2 (7z)");
    case PackType::LuniiV3Zip: return QObject::tr("Sauvegarde Lunii v3");
    case PackType::LuniiFlamZip: return QObject::tr("Lunii issue d'une Flam");
    case PackType::StudioZip: return QObject::tr("STUdio (zip)");
    case PackType::Studio7z: return QObject::tr("STUdio (7z)");
    case PackType::FlamZip:
    case PackType::Flam7z:
    case PackType::FlamPlain: return QObject::tr("Flam");
    case PackType::Unknown: break;
    }
    return QObject::tr("Inconnu");
}

} // namespace lunii
