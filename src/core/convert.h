// Conversions des ressources pour la Lunii.
// Port de pkg/api/convert_image.py et convert_audio.py (Lunii.QT).
#pragma once

#include <QByteArray>
#include <QString>

namespace lunii {

// Image quelconque -> BMP 320x240, 4 bits (16 niveaux de gris), compression RLE4.
// Renvoie l'entrée telle quelle si c'est déjà un BMP RLE4 320x240. Vide en cas d'échec.
QByteArray imageToBmpRle4(const QByteArray &data);

// Infos MP3 (premier en-tête de trame valide après un éventuel ID3v2)
struct Mp3Info {
    bool valid = false;
    int sampleRate = 0;
    bool mono = false;
    bool hasId3v2 = false;
    bool hasId3v1 = false;
};
Mp3Info mp3Info(const QByteArray &data);

// La Lunii veut du MP3 mono 44,1 kHz : faut-il transcoder ?
bool transcodingRequired(const QString &fileName, const QByteArray &data);
// Retire les tags ID3v2 (début) et ID3v1 (fin)
QByteArray stripMp3Tags(const QByteArray &data);

QString ffmpegPath();  // ~/.lunii-qt/ffmpeg ou ffmpeg du système ; vide si absent
// Transcodage via ffmpeg : MP3 mono 44,1 kHz VBR (-aq 5), sans métadonnées ni en-tête Xing
QByteArray transcodeToMp3(const QByteArray &data, QString *error = nullptr);

} // namespace lunii
