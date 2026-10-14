// Lecture/écriture d'archives zip et 7z via libarchive (remplace zipfile / py7zr de Lunii.QT).
// Lecture en flux : une entrée à la fois en mémoire, pas toute l'archive.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

struct archive;

namespace lunii {

struct ArchiveEntry {
    QString name;
    qint64 size = 0;
    bool isDir = false;
};

class ArchiveReader
{
public:
    explicit ArchiveReader(const QString &path);

    bool isValid() const { return m_valid; }
    QString error() const { return m_error; }

    const QList<ArchiveEntry> &entries() const { return m_entries; }
    QStringList names() const;
    bool contains(const QString &name) const;
    qint64 sizeOf(const QString &name) const;

    // Lit une entrée (relit l'archive depuis le début : à réserver aux petits fichiers)
    QByteArray read(const QString &name);

    // Parcourt toutes les entrées-fichiers dans l'ordre de l'archive ; retourner false interrompt.
    using Visitor = std::function<bool(const ArchiveEntry &, const QByteArray &)>;
    bool forEach(const Visitor &visit, const std::function<bool(const QString &)> &wanted = {});

private:
    archive *openArchive();
    QString m_path;
    QString m_error;
    bool m_valid = false;
    QList<ArchiveEntry> m_entries;
};

class ArchiveWriter
{
public:
    enum Format { Zip, SevenZip };
    ArchiveWriter(const QString &path, Format format = Zip, bool compress = true);
    ~ArchiveWriter();

    bool isOpen() const { return m_ar != nullptr; }
    QString error() const { return m_error; }
    bool add(const QString &name, const QByteArray &data);
    bool addDir(const QString &name);
    bool close();

private:
    archive *m_ar = nullptr;
    QString m_error;
};

} // namespace lunii
