#include "core/platform.h"

#include "core/storydb.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace lunii {

void flushDisks()
{
#ifdef Q_OS_UNIX
    ::sync();
#endif
    // Windows : les fichiers sont fermés après écriture et les conteuses sont en « retrait rapide »
}

QString findTool(const QString &name, const QStringList &extraDirs)
{
#ifdef Q_OS_WIN
    const QString exe = name + ".exe";
#else
    const QString exe = name;
#endif
    QStringList dirs = extraDirs;
    if (QCoreApplication::instance())
        dirs.prepend(QCoreApplication::applicationDirPath());
    dirs << configDir();
    for (const QString &d : dirs) {
        const QFileInfo fi(d + "/" + exe);
        if (fi.isFile() && fi.isExecutable())
            return fi.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(name);
}

} // namespace lunii
