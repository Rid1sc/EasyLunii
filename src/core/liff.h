// Vignettes de la Flam (format LIFF, pixels RGB565) et translittération ASCII des fiches « info ».
// Port de image_to_liff (pkg/api/convert_image.py) et de l'usage d'unidecode (device_flam.py), Lunii.QT.
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace lunii {

// Image quelconque -> LIFF, réduite pour tenir dans 104x104 (jamais agrandie). Vide si illisible.
QByteArray imageToLiff(const QByteArray &imageData);
QByteArray imageToLiff(const QImage &image);
// LIFF -> image (aperçu dans l'interface). Image nulle si le fichier est invalide.
QImage liffToImage(const QByteArray &liff);

// Texte en ASCII pur (accents retirés, ligatures et ponctuation typographique remplacées)
QString asciiFold(const QString &text);

} // namespace lunii
