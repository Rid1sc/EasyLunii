// Accès à une Lunii montée (v1, v2, v3). Port de pkg/api/device_lunii.py (Lunii.QT, GPLv3).
// Différences voulues : le constructeur n'écrit rien sur l'appareil ; les index sont écrits de façon atomique.
#pragma once

#include "core/packtype.h"

#include <QAtomicInt>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

namespace lunii {

enum class HwVersion { Unknown = 0, V1 = 1, V2 = 2, V3 = 3 };

struct DeviceStory {
    QUuid uuid;
    bool hidden = false;
    bool nightMode = false;
    QString shortId() const;   // 8 derniers caractères, nom du dossier dans .content
};

// Réglages .cfg (mode nuit…), indices comme LUNII_CFGPOS_*
struct DeviceConfig {
    enum Pos { IdleTime = 0, LowBatTime = 2, NmEnabled = 3, NmVolLimit = 4, NmStoryCount = 5, NmAutoplay = 7, NmTurnoff = 8 };
    QList<int> values = {300, 60, 5, 0, 0, 3, 0, 1, 1, 1};
    bool supported = true;   // false si version de .cfg inconnue (lecture seule)
};

class LuniiDevice : public QObject
{
    Q_OBJECT
public:
    static bool isLunii(const QString &mountPoint);
    // Points de montage FAT sous /dev/sd* contenant un .md
    static QStringList findDevices();

    explicit LuniiDevice(const QString &mountPoint, QObject *parent = nullptr);

    bool isValid() const { return m_valid; }
    QString mountPoint() const { return m_mount; }
    HwVersion hwVersion() const { return m_hw; }
    int mdVersion() const { return m_mdVersion; }
    QString firmware() const;
    QString snu() const;                  // numéro de série affiché
    QString describe() const;             // résumé lisible (modèle, firmware, SNU, clés)
    bool canImport() const;               // clés disponibles pour écrire des histoires
    bool canExportPlain() const;          // export déchiffré possible (v1/v2, ou v3 avec clés réelles)
    qint64 freeSpace() const;
    qint64 totalSpace() const;

    const QList<DeviceStory> &stories() const { return m_stories; }
    int indexOf(const QUuid &u) const;
    QString storyDir(const DeviceStory &s) const;
    qint64 storySize(const DeviceStory &s) const;

    // --- Opérations (bloquantes : à lancer dans un thread de travail)
    bool importStory(const QString &archivePath);
    QString exportStory(const QUuid &u, const QString &outDir);   // chemin de l'archive créée, vide si échec
    bool removeStory(const QUuid &u);
    bool setHidden(const QUuid &u, bool hidden);
    bool setNightMode(const QUuid &u, bool enabled);
    bool reorder(const QList<QUuid> &order);       // nouvel ordre complet (visibles et masquées)
    int recoverStories(bool dryRun);               // histoires présentes dans .content mais absentes de .pi
    int cleanupStories(qint64 *freedBytes = nullptr);

    DeviceConfig config() const { return m_config; }
    bool setConfig(const DeviceConfig &cfg);

    void requestAbort() { m_abort.storeRelaxed(1); }

    // Export interdit pour les histoires officielles sur v1/v2 (règle de Lunii.QT, conservée)
    bool exportAllowed(const QUuid &u) const;

signals:
    void log(int level, const QString &message);   // QtMsgType
    void storyProgress(const QString &shortId, int current, int total);
    void fileProgress(const QString &label, qint64 current, qint64 total);
    void storiesChanged();

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
    bool writePackIndex();

    QByteArray decipher(const QByteArray &buf, const QByteArray &key, const QByteArray &iv) const;
    QByteArray cipher(const QByteArray &buf, const QByteArray &key, const QByteArray &iv) const;
    QByteArray plainData(const QString &file) const;
    QByteArray cipheredData(const QString &file, const QByteArray &data) const;
    static QString cipheredName(QString file, bool studioRi = false, bool studioSi = false);
    static QString plainName(const QString &relPath);

    bool importPlain(const QString &path);
    bool importV2(const QString &path);
    bool importV3(const QString &path);
    bool importStudio(const QString &path);
    QString exportPlain(const DeviceStory &s, const QString &outDir);
    QString exportBackup(const DeviceStory &s, const QString &outDir);

    bool checkV3Key(const QString &storyPath) const;
    bool checkV2Bt(const QString &storyPath) const;
    bool validStory(const QString &storyPath);
    bool cleanStoryDir(const QUuid &u);
    bool writeFile(const QString &path, const QByteArray &data, bool progress = false);
    bool aborted();
    bool alreadyLoaded(const QUuid &u);
    void info(const QString &m) { emit log(QtInfoMsg, m); }
    void warn(const QString &m) { emit log(QtWarningMsg, m); }
    void error(const QString &m) { emit log(QtCriticalMsg, m); }
    void debug(const QString &m) { emit log(QtDebugMsg, m); }

    QString m_mount;
    bool m_valid = false;
    HwVersion m_hw = HwVersion::Unknown;
    int m_mdVersion = 0;
    int m_fwMajor = 0, m_fwMinor = 0, m_fwSub = 0;
    QByteArray m_snu;                    // octets bruts (v1/v2 : 8 octets ; v3 : 7 octets)
    QByteArray m_deviceKey, m_deviceIv;  // clés de l'appareil
    QByteArray m_storyKey, m_storyIv;    // clés des histoires (v3)
    QByteArray m_bt;                     // contenu du fichier d'autorisation à écrire
    QList<DeviceStory> m_stories;
    DeviceConfig m_config;
    QAtomicInt m_abort;
};

QString secureFileName(const QString &name);

} // namespace lunii
