// Détection du type d'archive d'histoire. Port de archive_check_* (pkg/api/stories.py, Lunii.QT).
#pragma once

#include <QString>

namespace lunii {

enum class PackType {
    Unknown,
    LuniiPlain,     // .plain.pk : fichiers déchiffrés + uuid.bin
    LuniiV2Zip,     // dossier UUID chiffré XXTEA (zip)
    LuniiV2_7z,     // idem en 7z
    LuniiV3Zip,     // sauvegarde personnelle v3 (AES, liée à l'appareil)
    LuniiFlamZip,   // histoire Lunii issue d'une Flam (non géré pour l'instant)
    StudioZip,
    Studio7z,
    FlamZip,
    Flam7z,
    FlamPlain,
};

PackType detectPackType(const QString &path);
QString packTypeLabel(PackType t);

} // namespace lunii
