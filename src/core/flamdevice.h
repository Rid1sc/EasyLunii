// Accès à une Flam montée. Port de pkg/api/device_flam.py (Lunii.QT, GPLv3).
// Non repris : déchiffrement et transfert entre appareils des histoires officielles Flam (fichier « bt »).
#pragma once

#include "core/storydevice.h"

namespace lunii {

class FlamDevice : public StoryDevice
{
    Q_OBJECT
public:
    static bool isFlam(const QString &mountPoint);
    explicit FlamDevice(const QString &mountPoint, QObject *parent = nullptr);

    Kind kind() const override { return Kind::Flam; }
    QString describe() const override;
    bool canImport() const override { return !m_storyKey.isEmpty(); }
    bool exportAllowed(const QUuid &) const override { return true; }   // déchiffré ou sauvegarde selon l'histoire
    QString storyDir(const DeviceStory &s) const override;
    QByteArray coverImage(const QUuid &u) const override;
    QString snu() const override;
    QString firmware() const { return m_fwMain; }

    bool importStory(const QString &archivePath) override;
    QString exportStory(const QUuid &u, const QString &outDir) override;
    bool removeStory(const QUuid &u) override;
    bool setHidden(const QUuid &u, bool hidden) override;
    int recoverStories(bool dryRun) override;
    int cleanupStories(qint64 *freedBytes = nullptr) override;

protected:
    QList<QPair<QString, QString>> keyFiles() const override;
    bool writePackIndex() override;

private:
    bool parseMdf();
    void readStories();
    bool luniiStoriesSupported();
    QByteArray cipheredData(const QString &file, const QByteArray &data, bool flamStory, bool force = false) const;
    static QString flamCipheredName(QString file);
    static QString luniiCipheredName(QString file, bool studioRi = false, bool studioSi = false);
    void writeInfo(const QUuid &u, const QString &dir);
    void writeThumbnail(const QUuid &u, const QString &dir);
    bool finish(const DeviceStory &s);
    bool cleanStoryDir(const QUuid &u);

    bool importLuniiPlain(const QString &path);
    bool importFlamPlain(const QString &path);
    bool importFlamBackup(const QString &path);
    bool importLuniiFlamBackup(const QString &path);
    bool importLuniiV2(const QString &path);
    bool importStudio(const QString &path);
    QString exportBackup(const DeviceStory &s, const QString &outDir);
    QString exportFlamPlain(const DeviceStory &s, const QString &outDir);
    QString exportLuniiPlain(const DeviceStory &s, const QString &outDir);

    QString m_fwMain = "?.?.?", m_fwComm = "?.?.?";
    QByteArray m_snu;
    QByteArray m_storyKey, m_storyIv;   // clés des histoires Flam (non inversées)
    QByteArray m_keyfile;               // contenu du fichier « key » écrit avec chaque histoire
    bool m_flamHw = false;
};

} // namespace lunii
