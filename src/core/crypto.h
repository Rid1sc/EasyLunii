// Chiffrement des fichiers d'histoires Lunii.
// Port de pkg/api/stories.py (xxtea_*, aes_*) et pkg/api/constants.py de Lunii.QT.
#pragma once

#include <QByteArray>

namespace lunii {

// Clé générique (v1/v2), stockée en flash externe : 91BD7A0A A75440A9 BBD49D6C E0DCC0E3
QByteArray genericKey();

// Nombre de tours XXTEA utilisé par la Lunii : 1 + 52 / nombre de mots
int teaRounds(int byteLen);

// XXTEA (Corrected Block TEA) sans padding, sur le buffer entier (longueur multiple de 4, >= 8)
QByteArray xxteaEncrypt(const QByteArray &data, const QByteArray &key);
QByteArray xxteaDecrypt(const QByteArray &data, const QByteArray &key);

// AES-128-CBC sans padding (longueur multiple de 16)
QByteArray aesEncrypt(const QByteArray &data, const QByteArray &key, const QByteArray &iv);
QByteArray aesDecrypt(const QByteArray &data, const QByteArray &key, const QByteArray &iv);

// Traitement des `len` premiers octets seulement (512 par défaut sur la Lunii), le reste est inchangé.
QByteArray xxteaDecipherHead(QByteArray buf, const QByteArray &key, int len = 512);
QByteArray xxteaCipherHead(QByteArray buf, const QByteArray &key, int len = 512);
QByteArray aesDecipherHead(QByteArray buf, const QByteArray &key, const QByteArray &iv, int len = 512);
// Comme Lunii.QT : si la zone n'est pas multiple de 16, le buffer est complété par des zéros.
QByteArray aesCipherHead(QByteArray buf, const QByteArray &key, const QByteArray &iv, int len = 512);

// Inverse l'ordre des octets dans chaque groupe de 4 (aes_keys.reverse_bytes)
QByteArray reverseWords(const QByteArray &in);

} // namespace lunii
