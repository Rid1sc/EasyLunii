// Accès à une Lunii montée (v1, v2, v3). Port de pkg/api/device_lunii.py (Lunii.QT, GPLv3).
// Différences voulues : le constructeur n'écrit rien sur l'appareil ; les index sont écrits de façon atomique.
#pragma once

#include "core/packtype.h"
#include "core/storydevice.h"

#include <QAtomicInt>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

namespace lunii {

enum class HwVersion { Unknown = 0, V1 = 1, V2 = 2, V3 = 3 };

class LuniiDevice : public StoryDevice
{
    Q_OBJECT
public:
    static bool isLunii(const QString &mountPoint);

    explicit LuniiDevice(const QString &mountPoint, QObject *parent = nullptr);

    Kind kind() const override { return Kind::Lunii; }
    HwVersion hwVersion() const { return m_hw; }
    int mdVersion() const { return m_mdVersion; }
    QString firmware() const;
    QString snu() const override;         // numéro de série affiché
    QString describe() const override;    // résumé lisible (modèle, firmware, SNU, clés)
    bool canImport() const override;      // clés disponibles pour écrire des histoires
    bool supportsNightMode() const override { return true; }
    bool canExportPlain() const;          // export déchiffré possible (v1/v2, ou v3 avec clés réelles)
    QString storyDir(const DeviceStory &s) const override;
    QByteArray coverImage(const QUuid &u) const override;   // premier écran (BMP), vide si illisible

    // --- Opérations (bloquantes : à lancer dans un thread de travail)
    bool importStory(const QString &archivePath) override;
    QString exportStory(const QUuid &u, const QString &outDir) override;
    bool removeStory(const QUuid &u) override;
    bool setHidden(const QUuid &u, bool hidden) override;
    bool setNightMode(const QUuid &u, bool enabled) override;
    int recoverStories(bool dryRun) override;      // histoires présentes dans .content mais absentes de .pi
    int cleanupStories(qint64 *freedBytes = nullptr) override;

    DeviceConfig config() const override { return m_config; }
    bool setConfig(const DeviceConfig &cfg) override;

    // Toujours vrai ; les histoires officielles des v1/v2 sont sauvegardées chiffrées (pas d'export déchiffré)
    bool exportAllowed(const QUuid &u) const override;

private:
    bool parseMd();
    void parseMdV1to5(const QByteArray &md);
    void parseMdV6to7(const QByteArray &md);
    void parseMdUnsupported(const QByteArray &md);
    void loadMdBackup(int version);
    void loadMdFakeStoryKeys();
    void loadStoryKeys(const QString &btPath);
    void readStories();
    void readConfig();
    QList<QPair<QString, QString>> keyFiles() const override;
    bool writePackIndex() override;

    QByteArray decipher(const QByteArray &buf, const QByteArray &key, const QByteArray &iv) const;
    QByteArray cipher(const QByteArray &buf, const QByteArray &key, const QByteArray &iv) const;
    QByteArray plainData(const QString &file) const;
    QByteArray cipheredData(const QString &file, const QByteArray &data) const;
    static QString cipheredName(QString file, bool studioRi = false, bool studioSi = false);
    static QString plainName(const QString &relPath);

    bool importPlain(const QString &path);
    bool importV2(const QString &path);
    bool importV3(const QString &path);
    bool restoreBackup(const QString &path);
    bool importStudio(const QString &path);
    QString exportPlain(const DeviceStory &s, const QString &outDir);
    QString exportBackup(const DeviceStory &s, const QString &outDir);

    bool checkV3Key(const QString &storyPath) const;
    // clés qui déchiffrent vraiment cette histoire v3 : bt + clés de l'appareil, clé dérivée du numéro de série, ou clé du .md
    bool findV3Keys(const QString &storyPath, QByteArray &key, QByteArray &iv) const;
    bool checkV2Bt(const QString &storyPath) const;
    bool validStory(const QString &storyPath);
    bool cleanStoryDir(const QUuid &u);

    HwVersion m_hw = HwVersion::Unknown;
    int m_mdVersion = 0;
    int m_fwMajor = 0, m_fwMinor = 0, m_fwSub = 0;
    QByteArray m_snu;                    // octets bruts (v1/v2 : 8 octets ; v3 : 7 octets)
    QByteArray m_deviceKey, m_deviceIv;  // clés de l'appareil
    QByteArray m_storyKey, m_storyIv;    // clés des histoires (v3)
    QByteArray m_bt;                     // contenu du fichier d'autorisation à écrire
    DeviceConfig m_config;
    quint16 m_cfgVersion = 0x100;        // en-tête du .cfg, conservé à l'écriture
    QList<int> m_cfgKeys;                // entrées présentes dans le .cfg, dans l'ordre
};


} // namespace lunii
