#include "app/devicewatcher.h"

#include "core/flamdevice.h"
#include "core/platform.h"
#include "core/storydevice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>

// Identifiants USB (constants.py de Lunii.QT)
static QString modelFor(const QString &vid, const QString &pid)
{
    if (vid == "0c45" && (pid == "6820" || pid == "6840"))
        return QStringLiteral("Lunii v1");
    if (vid == "0483" && pid == "a341")
        return QStringLiteral("Lunii v2/v3");
    if (vid == "303a" && pid == "819e")
        return QStringLiteral("Flam");
    return {};
}

static QString readSys(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed() : QString();
}

DeviceWatcher::DeviceWatcher(QObject *parent) : QObject(parent)
{
    // sondage léger : lecture de /sys et de la table des montages, toutes les 2 s
    m_timer.setInterval(2000);
    connect(&m_timer, &QTimer::timeout, this, &DeviceWatcher::refresh);
    m_timer.start();
    refresh();
}

#ifdef Q_OS_WIN
// Windows : les conteuses apparaissent comme des lecteurs FAT montés automatiquement
void DeviceWatcher::refresh()
{
    QList<DetectedDevice> found;
    for (const QStorageInfo &si : QStorageInfo::mountedVolumes()) {
        const QString root = si.rootPath();
        if (!lunii::StoryDevice::isStoryDevice(root))
            continue;
        found << DetectedDevice{root, root, lunii::FlamDevice::isFlam(root) ? QStringLiteral("Flam") : QStringLiteral("Lunii")};
    }
    if (found != m_devices) {
        m_devices = found;
        emit devicesChanged();
    }
}

bool DeviceWatcher::mount(const QString &block, QString *mountPoint, QString *)
{
    *mountPoint = block;   // déjà monté par Windows
    return true;
}

bool DeviceWatcher::eject(const QString &block, QString *error)
{
    lunii::flushDisks();
    // verbe « Éjecter » de l'Explorateur (comme le menu contextuel du lecteur)
    const QString drive = QString(block).left(2);   // « E: »
    QProcess p;
    p.start("powershell", {"-NoProfile", "-NonInteractive", "-Command",
                           QString("(New-Object -ComObject Shell.Application).Namespace(17).ParseName('%1').InvokeVerb('Eject')").arg(drive)});
    if (!p.waitForFinished(30000) || p.exitCode() != 0) {
        if (error)
            *error = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        return false;
    }
    return true;
}
#else
void DeviceWatcher::refresh()
{
    QHash<QString, QString> mounts;   // périphérique -> point de montage
    for (const QStorageInfo &si : QStorageInfo::mountedVolumes())
        mounts.insert(QString::fromLatin1(si.device()), si.rootPath());

    QList<DetectedDevice> found;
    const QStringList disks = QDir("/sys/block").entryList({"sd*"}, QDir::Dirs | QDir::System);
    for (const QString &disk : disks) {
        // remontée dans l'arborescence USB jusqu'au fichier idVendor
        // (le premier ancêtre qui a un idVendor est le périphérique USB lui-même)
        QString p = QFileInfo("/sys/block/" + disk).canonicalFilePath();
        QString model;
        while (p.startsWith("/sys/devices/")) {
            if (QFile::exists(p + "/idVendor")) {
                model = modelFor(readSys(p + "/idVendor"), readSys(p + "/idProduct"));
                break;
            }
            p = QFileInfo(p).path();
        }
        if (model.isEmpty() || readSys("/sys/block/" + disk + "/size") == "0")
            continue;
        const QStringList parts = QDir("/sys/block/" + disk).entryList({disk + "*"}, QDir::Dirs);
        const QString block = "/dev/" + (parts.isEmpty() ? disk : parts.first());
        found << DetectedDevice{block, mounts.value(block), model};
    }
    // Lunii montées hors USB détecté (ex. dossier de test) : ignorées ici, ajout manuel possible
    if (found != m_devices) {
        m_devices = found;
        emit devicesChanged();
    }
}

bool DeviceWatcher::mount(const QString &block, QString *mountPoint, QString *error)
{
    QProcess p;
    p.start("udisksctl", {"mount", "-b", block, "--no-user-interaction"});
    p.waitForFinished(30000);
    const QString outText = QString::fromUtf8(p.readAllStandardOutput()) + QString::fromUtf8(p.readAllStandardError());
    // « Mounted /dev/sdc1 at /run/media/… » ou « already mounted at `…' »
    const auto m = QRegularExpression(R"(at [`']?(/[^\s'`]+?)[`'.]?\s*$)").match(outText.trimmed());
    if (m.hasMatch()) {
        *mountPoint = m.captured(1);
        return true;
    }
    if (error)
        *error = outText.trimmed();
    return false;
}

bool DeviceWatcher::eject(const QString &block, QString *error)
{
    lunii::flushDisks();
    QProcess p;
    p.start("udisksctl", {"unmount", "-b", block, "--no-user-interaction"});
    p.waitForFinished(30000);
    if (p.exitCode() != 0) {
        const QString e = QString::fromUtf8(p.readAllStandardError()).trimmed();
        if (!e.contains("not mounted")) {
            if (error)
                *error = e;
            return false;
        }
    }
    // mise hors tension du disque entier (sdc pour sdc1)
    QString disk = block;
    disk.remove(QRegularExpression(R"(\d+$)"));
    QProcess off;
    off.start("udisksctl", {"power-off", "-b", disk, "--no-user-interaction"});
    off.waitForFinished(30000);
    return true;
}
#endif
