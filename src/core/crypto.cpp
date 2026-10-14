#include "core/crypto.h"

#include <openssl/evp.h>
#include <QtEndian>
#include <vector>

namespace lunii {

QByteArray genericKey()
{
    static const quint32 raw[4] = {0x91BD7A0A, 0xA75440A9, 0xBBD49D6C, 0xE0DCC0E3};
    QByteArray key(16, 0);
    for (int i = 0; i < 4; ++i)
        qToLittleEndian(raw[i], key.data() + 4 * i);
    return key;
}

int teaRounds(int byteLen)
{
    // int(1 + 52 / (len/4)) en flottant, comme Lunii.QT
    return int(1.0 + 52.0 / (byteLen / 4.0));
}

namespace {

constexpr quint32 DELTA = 0x9e3779b9;

std::vector<quint32> toWords(const QByteArray &b)
{
    std::vector<quint32> v(b.size() / 4);
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = qFromLittleEndian<quint32>(b.constData() + 4 * i);
    return v;
}

QByteArray fromWords(const std::vector<quint32> &v)
{
    QByteArray b(int(v.size() * 4), 0);
    for (size_t i = 0; i < v.size(); ++i)
        qToLittleEndian(v[i], b.data() + 4 * i);
    return b;
}

#define MX (((z >> 5 ^ y << 2) + (y >> 3 ^ z << 4)) ^ ((sum ^ y) + (k[(p & 3) ^ e] ^ z)))

bool validTea(const QByteArray &data, const QByteArray &key)
{
    return key.size() == 16 && data.size() >= 8 && data.size() % 4 == 0;
}

} // namespace

QByteArray xxteaEncrypt(const QByteArray &data, const QByteArray &key)
{
    if (!validTea(data, key))
        return data;
    auto v = toWords(data);
    const auto k = toWords(key);
    const quint32 n = quint32(v.size());
    quint32 rounds = teaRounds(data.size());
    quint32 z = v[n - 1], y, sum = 0, e, p;
    while (rounds-- > 0) {
        sum += DELTA;
        e = sum >> 2 & 3;
        for (p = 0; p < n - 1; p++) {
            y = v[p + 1];
            z = v[p] += MX;
        }
        y = v[0];
        z = v[n - 1] += MX;
    }
    return fromWords(v);
}

QByteArray xxteaDecrypt(const QByteArray &data, const QByteArray &key)
{
    if (!validTea(data, key))
        return data;
    auto v = toWords(data);
    const auto k = toWords(key);
    const quint32 n = quint32(v.size());
    const quint32 rounds = teaRounds(data.size());
    quint32 z, y = v[0], sum = rounds * DELTA, e, p;
    while (sum != 0) {
        e = sum >> 2 & 3;
        for (p = n - 1; p > 0; p--) {
            z = v[p - 1];
            y = v[p] -= MX;
        }
        z = v[n - 1];
        y = v[0] -= MX;
        sum -= DELTA;
    }
    return fromWords(v);
}

#undef MX

static QByteArray aesRun(const QByteArray &data, const QByteArray &key, const QByteArray &iv, bool enc)
{
    if (key.size() != 16 || iv.size() != 16 || data.isEmpty() || data.size() % 16)
        return data;
    QByteArray out(data.size() + 16, 0);
    int len1 = 0, len2 = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_CipherInit_ex(ctx, EVP_aes_128_cbc(), nullptr,
                      reinterpret_cast<const unsigned char *>(key.constData()),
                      reinterpret_cast<const unsigned char *>(iv.constData()), enc ? 1 : 0);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    EVP_CipherUpdate(ctx, reinterpret_cast<unsigned char *>(out.data()), &len1,
                     reinterpret_cast<const unsigned char *>(data.constData()), int(data.size()));
    EVP_CipherFinal_ex(ctx, reinterpret_cast<unsigned char *>(out.data()) + len1, &len2);
    EVP_CIPHER_CTX_free(ctx);
    out.resize(len1 + len2);
    return out;
}

QByteArray aesEncrypt(const QByteArray &data, const QByteArray &key, const QByteArray &iv)
{
    return aesRun(data, key, iv, true);
}

QByteArray aesDecrypt(const QByteArray &data, const QByteArray &key, const QByteArray &iv)
{
    return aesRun(data, key, iv, false);
}

QByteArray xxteaDecipherHead(QByteArray buf, const QByteArray &key, int len)
{
    len = qMin(len, int(buf.size()));
    if (len > 0)
        buf.replace(0, len, xxteaDecrypt(buf.left(len), key));
    return buf;
}

QByteArray xxteaCipherHead(QByteArray buf, const QByteArray &key, int len)
{
    len = qMin(len, int(buf.size()));
    if (len > 0)
        buf.replace(0, len, xxteaEncrypt(buf.left(len), key));
    return buf;
}

QByteArray aesDecipherHead(QByteArray buf, const QByteArray &key, const QByteArray &iv, int len)
{
    len = qMin(len, int(buf.size()));
    if (len > 0)
        buf.replace(0, len, aesDecrypt(buf.left(len), key, iv));
    return buf;
}

QByteArray aesCipherHead(QByteArray buf, const QByteArray &key, const QByteArray &iv, int len)
{
    len = qMin(len, int(buf.size()));
    if (len % 16) {
        // reproduit aes_cipher : padding calculé sur la taille totale du buffer
        const int pad = 16 - int(buf.size()) % 16;
        buf.append(QByteArray(pad, 0));
        len += pad;
    }
    if (len > 0)
        buf.replace(0, len, aesEncrypt(buf.left(len), key, iv));
    return buf;
}

QByteArray reverseWords(const QByteArray &in)
{
    QByteArray out = in;
    for (int i = 0; i + 3 < out.size(); i += 4)
        std::reverse(out.begin() + i, out.begin() + i + 4);
    return out;
}

} // namespace lunii
