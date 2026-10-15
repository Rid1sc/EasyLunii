#include "core/storydevice.h"

#include "core/device.h"
#include "core/flamdevice.h"
#include "core/storydb.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace lunii {

QString DeviceStory::shortId() const
{
    return shortUuid(uuid);
}

QString secureFileName(const QString &name)
{
    static const QString invalid = QStringLiteral("/\\?%*:|\"<>");
    QString n = name.normalized(QString::NormalizationForm_KD);
    n.replace(QChar(0x0141), 'L').replace(QChar(0x0142), 'l');
    QString out;
    for (QChar c : n) {
        if (invalid.contains(c))
            out += '_';
        else if (c.unicode() < 128)
            out += c;
    }
    return out;
}

QStringList storyFiles(const QString &dir)
{
    QStringList files;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files << it.next();
    files.sort();
    return files;
}

StoryDevice *StoryDevice::open(const QString &mountPoint, QObject *parent)
{
    if (FlamDevice::isFlam(mountPoint))
        return new FlamDevice(mountPoint, parent);
    if (LuniiDevice::isLunii(mountPoint))
        return new LuniiDevice(mountPoint, parent);
    return nullptr;
}

bool StoryDevice::isStoryDevice(const QString &mountPoint)
{
    return FlamDevice::isFlam(mountPoint) || LuniiDevice::isLunii(mountPoint);
}

QStringList StoryDevice::findDevices()
{
    QStringList out;
    for (const QStorageInfo &si : QStorageInfo::mountedVolumes()) {
        const QString fs = QString::fromLatin1(si.fileSystemType()).toLower();
        if (!(fs == "vfat" || fs.startsWith("msdos") || fs.startsWith("fat")))
            continue;
#ifndef Q_OS_WIN
        if (!si.device().startsWith("/dev/sd"))
            continue;
#endif
        if (isStoryDevice(si.rootPath()))
            out << si.rootPath();
    }
    return out;
}

#ifdef Q_OS_WIN
// QStorageInfo passe par le nom de volume, qui n'existe pas toujours (lecteurs mappés, Wine) :
// on interroge directement le dossier.
static bool diskSpace(const QString &path, qint64 *avail, qint64 *total)
{
    ULARGE_INTEGER a, t;
    const std::wstring p = QDir::toNativeSeparators(QDir(path).absolutePath() + '/').toStdWString();
    if (!GetDiskFreeSpaceExW(p.c_str(), &a, &t, nullptr))
        return false;
    *avail = qint64(a.QuadPart);
    *total = qint64(t.QuadPart);
    return true;
}
#endif

qint64 StoryDevice::freeSpace() const
{
#ifdef Q_OS_WIN
    qint64 avail, total;
    if (diskSpace(m_mount, &avail, &total))
        return avail;
#endif
    return QStorageInfo(m_mount).bytesAvailable();
}

qint64 StoryDevice::totalSpace() const
{
#ifdef Q_OS_WIN
    qint64 avail, total;
    if (diskSpace(m_mount, &avail, &total))
        return total;
#endif
    return QStorageInfo(m_mount).bytesTotal();
}

int StoryDevice::indexOf(const QUuid &u) const
{
    for (int i = 0; i < m_stories.size(); ++i)
        if (m_stories[i].uuid == u)
            return i;
    return -1;
}

qint64 StoryDevice::storySize(const DeviceStory &s) const
{
    qint64 total = 0;
    for (const QString &f : storyFiles(storyDir(s)))
        total += QFileInfo(f).size();
    return total;
}

bool StoryDevice::reorder(const QList<QUuid> &order)
{
    if (order.size() != m_stories.size())
        return false;
    QList<DeviceStory> next;
    for (const QUuid &u : order) {
        const int i = indexOf(u);
        if (i < 0)
            return false;
        next << m_stories[i];
    }
    m_stories = next;
    return writePackIndex();
}

bool StoryDevice::aborted()
{
    if (m_abort.loadRelaxed()) {
        warn(tr("Opération interrompue"));
        return true;
    }
    return false;
}

bool StoryDevice::alreadyLoaded(const QUuid &u)
{
    if (indexOf(u) < 0)
        return false;
    const QString t = StoryDb::instance().title(u);
    warn(tr("« %1 » est déjà sur l'appareil").arg(t.isEmpty() ? uuidStr(u) : t));
    return true;
}

bool StoryDevice::writeFile(const QString &path, const QByteArray &data, bool progress)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error(tr("Écriture impossible : %1 (%2)").arg(path, f.errorString()));
        return false;
    }
    const qint64 block = 256 * 1024;
    QElapsedTimer t;
    t.start();
    for (qint64 done = 0; done < data.size();) {
        const qint64 n = f.write(data.constData() + done, qMin(block, data.size() - done));
        if (n <= 0) {
            error(tr("Écriture impossible : %1 (%2)").arg(path, f.errorString()));
            return false;
        }
        done += n;
        if (progress) {
            const double s = qMax<qint64>(t.elapsed(), 1) / 1000.0;
            emit fileProgress(tr("%1 Ko/s").arg(qint64(done / 1024 / s)), done, data.size());
        }
    }
    f.close();
    return true;
}

} // namespace lunii
