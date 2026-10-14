#include "core/liff.h"

#include <QHash>
#include <QtEndian>

namespace lunii {

QByteArray imageToLiff(const QByteArray &imageData)
{
    QImage img;
    if (imageData.isEmpty() || !img.loadFromData(imageData))
        return {};
    return imageToLiff(img);
}

QByteArray imageToLiff(const QImage &source)
{
    if (source.isNull())
        return {};
    QImage img = source;
    const int maxSide = 104;
    if (img.width() > maxSide || img.height() > maxSide)
        img = img.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_RGBA8888);

    const int w = img.width(), h = img.height();
    QByteArray out;
    out.reserve(13 + w * h * 3 + 8);
    out += "liff";
    char b[4];
    qToBigEndian<quint32>(quint32(w), b);
    out.append(b, 4);
    qToBigEndian<quint32>(quint32(h), b);
    out.append(b, 4);
    out.append(char(0xA2));   // informations de canaux
    for (int y = 0; y < h; ++y) {
        const uchar *line = img.constScanLine(y);
        for (int x = 0; x < w; ++x) {
            const uchar *px = line + 4 * x;
            // troncature sur 5/6/5 bits, octet de poids faible en premier (comme la Flam)
            const quint16 v = quint16(((px[0] * 32) / 256) << 11 | ((px[1] * 64) / 256) << 5 | ((px[2] * 32) / 256));
            out.append(char(0xFE));
            out.append(char(v & 0xFF));
            out.append(char(v >> 8));
        }
    }
    out.append(QByteArray(7, 0));
    out.append(char(1));
    return out;
}

QImage liffToImage(const QByteArray &liff)
{
    if (liff.size() < 13 || !liff.startsWith("liff"))
        return {};
    const auto *p = reinterpret_cast<const uchar *>(liff.constData());
    const int w = int(qFromBigEndian<quint32>(p + 4)), h = int(qFromBigEndian<quint32>(p + 8));
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || liff.size() < 13 + qint64(w) * h * 3)
        return {};
    QImage img(w, h, QImage::Format_RGB32);
    const uchar *px = p + 13;
    for (int y = 0; y < h; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x, px += 3) {
            const quint16 v = quint16(px[1] | px[2] << 8);
            line[x] = qRgb(((v >> 11) & 0x1F) * 255 / 31, ((v >> 5) & 0x3F) * 255 / 63, (v & 0x1F) * 255 / 31);
        }
    }
    return img;
}

QString asciiFold(const QString &text)
{
    static const QHash<QChar, QString> special = {
        {QChar(0x0152), "OE"}, {QChar(0x0153), "oe"}, {QChar(0x00C6), "AE"}, {QChar(0x00E6), "ae"},
        {QChar(0x00DF), "ss"}, {QChar(0x0141), "L"},  {QChar(0x0142), "l"},  {QChar(0x00D8), "O"},
        {QChar(0x00F8), "o"},  {QChar(0x2019), "'"},  {QChar(0x2018), "'"},  {QChar(0x201C), "\""},
        {QChar(0x201D), "\""}, {QChar(0x00AB), "<<"}, {QChar(0x00BB), ">>"}, {QChar(0x2013), "-"},
        {QChar(0x2014), "--"}, {QChar(0x2026), "..."}, {QChar(0x00A0), " "}, {QChar(0x202F), " "},
    };
    QString out;
    for (QChar c : text) {
        if (c.unicode() < 128) {
            out += c;
            continue;
        }
        if (auto it = special.constFind(c); it != special.constEnd()) {
            out += *it;
            continue;
        }
        // lettres accentuées : forme décomposée sans les diacritiques
        for (QChar d : QString(c).normalized(QString::NormalizationForm_KD))
            if (d.unicode() < 128)
                out += d;
    }
    return out;
}

} // namespace lunii
