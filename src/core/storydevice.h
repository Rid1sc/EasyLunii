// Base commune aux conteuses (Lunii, Flam) : liste d'histoires, opérations, signaux de progression.
#pragma once

#include <QAtomicInt>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

namespace lunii {

struct DeviceStory {
    QUuid uuid;
    bool hidden = false;
    bool nightMode = false;
    QString shortId() const;   // 8 derniers caractères hexadécimaux, majuscules
};

// Réglages .cfg de la Lunii (mode nuit…), indices comme LUNII_CFGPOS_*
struct DeviceConfig {
    enum Pos { IdleTime = 0, LowBatTime = 2, NmEnabled = 3, NmVolLimit = 4, NmStoryCount = 5, NmAutoplay = 7, NmTurnoff = 8 };
    QList<int> values = {300, 60, 5, 0, 0, 3, 0, 1, 1, 1};
    bool supported = true;   // false si version de .cfg inconnue (lecture seule)
};

class StoryDevice : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Lunii, Flam };

    // Lunii (.md) ou Flam (.mdf) montée dans ce dossier ; nullptr sinon
    static StoryDevice *open(const QString &mountPoint, QObject *parent = nullptr);
    static bool isStoryDevice(const QString &mountPoint);
    // Points de montage FAT sous /dev/sd* contenant une conteuse
    static QStringList findDevices();

    virtual Kind kind() const = 0;
    bool isValid() const { return m_valid; }
    QString mountPoint() const { return m_mount; }
    virtual QString describe() const = 0;
    virtual QString snu() const = 0;   // numéro de série affiché
    virtual bool canImport() const = 0;
    virtual bool exportAllowed(const QUuid &u) const = 0;
    virtual bool supportsNightMode() const { return false; }   // mode nuit par histoire et réglages
    qint64 freeSpace() const;
    qint64 totalSpace() const;

    const QList<DeviceStory> &stories() const { return m_stories; }
    int indexOf(const QUuid &u) const;
    virtual QString storyDir(const DeviceStory &s) const = 0;
    qint64 storySize(const DeviceStory &s) const;
    virtual QByteArray coverImage(const QUuid &u) const = 0;   // image (BMP, PNG…), vide si illisible

    // --- Opérations (bloquantes : à lancer dans un thread de travail)
    virtual bool importStory(const QString &archivePath) = 0;
    virtual QString exportStory(const QUuid &u, const QString &outDir) = 0;   // archive créée, vide si échec
    virtual bool removeStory(const QUuid &u) = 0;
    virtual bool setHidden(const QUuid &u, bool hidden) = 0;
    virtual bool setNightMode(const QUuid &, bool) { return false; }
    bool reorder(const QList<QUuid> &order);       // nouvel ordre complet (visibles et masquées)
    virtual int recoverStories(bool dryRun) = 0;
    virtual int cleanupStories(qint64 *freedBytes = nullptr) = 0;

    // Sauvegarde des fichiers d'identité et de clés (.md/.mdf de l'appareil, clés et copies .md de ~/.lunii-qt)
    QString exportKeys(const QString &outDir);   // zip créé, vide si échec
    // Remet les clés d'une sauvegarde dans ~/.lunii-qt (rien n'est écrit sur la conteuse) ; nombre de fichiers copiés, -1 si erreur
    static int restoreKeys(const QString &zipPath, QString *error = nullptr);

    virtual DeviceConfig config() const { return {}; }
    virtual bool setConfig(const DeviceConfig &) { return false; }

    void requestAbort() { m_abort.storeRelaxed(1); }

signals:
    void log(int level, const QString &message);   // QtMsgType
    void storyProgress(const QString &shortId, int current, int total);
    void fileProgress(const QString &label, qint64 current, qint64 total);
    void storiesChanged();

protected:
    explicit StoryDevice(const QString &mountPoint, QObject *parent) : QObject(parent), m_mount(mountPoint) {}
    virtual bool writePackIndex() = 0;
    // fichiers à sauvegarder : nom dans le zip -> chemin
    virtual QList<QPair<QString, QString>> keyFiles() const = 0;
    bool writeFile(const QString &path, const QByteArray &data, bool progress = false);
    bool aborted();
    bool alreadyLoaded(const QUuid &u);
    void info(const QString &m) { emit log(QtInfoMsg, m); }
    void warn(const QString &m) { emit log(QtWarningMsg, m); }
    void error(const QString &m) { emit log(QtCriticalMsg, m); }
    void debug(const QString &m) { emit log(QtDebugMsg, m); }

    QString m_mount;
    bool m_valid = false;
    QList<DeviceStory> m_stories;
    QAtomicInt m_abort;
};

QString secureFileName(const QString &name);
QStringList storyFiles(const QString &dir);   // fichiers d'un dossier, récursif, triés

} // namespace lunii
