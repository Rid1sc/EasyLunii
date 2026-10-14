// Fiche d'une archive d'histoire pour la bibliothèque : identité, titre, jaquette.
#pragma once

#include "core/packtype.h"

#include <QByteArray>
#include <QString>
#include <QUuid>

namespace lunii {

struct PackInfo {
    bool valid = false;
    PackType type = PackType::Unknown;
    QUuid uuid;
    QString title;          // titre du pack (STUdio / base) ; vide si inconnu
    QString description;
    QString author;
    QString age;            // "3+"… depuis la base officielle ou le nom de fichier
    bool official = false;
    bool nightMode = false;
    int nodeCount = 0;      // nœuds de scène (STUdio)
    qint64 durationSec = 0; // base officielle si connue
    QByteArray cover;       // image (PNG/JPEG/BMP) : jaquette ou premier écran
};

// Lecture rapide (story.json, uuid.bin, index ri/ni et une image) ; pas d'accès réseau.
PackInfo inspectPack(const QString &path);

} // namespace lunii
