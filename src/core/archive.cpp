#include "core/archive.h"

#include <archive.h>
#include <archive_entry.h>
#include <QFile>

namespace lunii {

static QString entryName(archive_entry *e)
{
    if (const char *u = archive_entry_pathname_utf8(e))
        return QString::fromUtf8(u);
    return QString::fromLocal8Bit(archive_entry_pathname(e));
}

ArchiveReader::ArchiveReader(const QString &path) : m_path(path)
{
    archive *a = openArchive();
    if (!a)
        return;
    archive_entry *e;
    int r;
    while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
        ArchiveEntry en;
        en.name = entryName(e);
        en.size = archive_entry_size(e);
        en.isDir = archive_entry_filetype(e) == AE_IFDIR || en.name.endsWith('/');
        m_entries.append(en);
        archive_read_data_skip(a);
    }
    if (r != ARCHIVE_EOF)
        m_error = QString::fromUtf8(archive_error_string(a));
    else
        m_valid = true;
    archive_read_free(a);
}

archive *ArchiveReader::openArchive()
{
    archive *a = archive_read_new();
    archive_read_support_format_zip(a);
    archive_read_support_format_7zip(a);
    archive_read_support_filter_all(a);
    // les noms des zip sans drapeau UTF-8 sont lus en CP437 par défaut : on force l'UTF-8
    archive_read_set_options(a, "zip:hdrcharset=UTF-8");
    const QByteArray p = QFile::encodeName(m_path);
    if (archive_read_open_filename(a, p.constData(), 1 << 16) != ARCHIVE_OK) {
        m_error = QString::fromUtf8(archive_error_string(a));
        archive_read_free(a);
        return nullptr;
    }
    return a;
}

QStringList ArchiveReader::names() const
{
    QStringList l;
    for (const auto &e : m_entries)
        l << e.name;
    return l;
}

bool ArchiveReader::contains(const QString &name) const
{
    for (const auto &e : m_entries)
        if (e.name == name)
            return true;
    return false;
}

qint64 ArchiveReader::sizeOf(const QString &name) const
{
    for (const auto &e : m_entries)
        if (e.name == name)
            return e.size;
    return -1;
}

QByteArray ArchiveReader::read(const QString &name)
{
    QByteArray out;
    forEach([&](const ArchiveEntry &, const QByteArray &d) { out = d; return false; },
            [&](const QString &n) { return n == name; });
    return out;
}

bool ArchiveReader::forEach(const Visitor &visit, const std::function<bool(const QString &)> &wanted)
{
    archive *a = openArchive();
    if (!a)
        return false;
    archive_entry *e;
    int r;
    bool ok = true;
    while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
        ArchiveEntry en;
        en.name = entryName(e);
        en.size = archive_entry_size(e);
        en.isDir = archive_entry_filetype(e) == AE_IFDIR || en.name.endsWith('/');
        if (en.isDir || (wanted && !wanted(en.name))) {
            archive_read_data_skip(a);
            continue;
        }
        QByteArray data;
        if (en.size > 0)
            data.reserve(en.size);
        char buf[1 << 16];
        la_ssize_t n;
        while ((n = archive_read_data(a, buf, sizeof buf)) > 0)
            data.append(buf, n);
        if (n < 0) {
            m_error = QString::fromUtf8(archive_error_string(a));
            ok = false;
            break;
        }
        if (!visit(en, data))
            break;
    }
    if (ok && r != ARCHIVE_OK && r != ARCHIVE_WARN && r != ARCHIVE_EOF) {
        m_error = QString::fromUtf8(archive_error_string(a));
        ok = false;
    }
    archive_read_free(a);
    return ok;
}

ArchiveWriter::ArchiveWriter(const QString &path, Format format, bool compress)
{
    m_ar = archive_write_new();
    if (format == Zip) {
        archive_write_set_format_zip(m_ar);
        archive_write_set_options(m_ar, compress ? "zip:compression=deflate" : "zip:compression=store");
    } else {
        archive_write_set_format_7zip(m_ar);
    }
    const QByteArray p = QFile::encodeName(path);
    if (archive_write_open_filename(m_ar, p.constData()) != ARCHIVE_OK) {
        m_error = QString::fromUtf8(archive_error_string(m_ar));
        archive_write_free(m_ar);
        m_ar = nullptr;
    }
}

ArchiveWriter::~ArchiveWriter()
{
    close();
}

bool ArchiveWriter::add(const QString &name, const QByteArray &data)
{
    if (!m_ar)
        return false;
    archive_entry *e = archive_entry_new();
    archive_entry_set_pathname_utf8(e, name.toUtf8().constData());
    archive_entry_set_size(e, data.size());
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_entry_set_mtime(e, time(nullptr), 0);
    bool ok = archive_write_header(m_ar, e) == ARCHIVE_OK;
    if (ok && !data.isEmpty())
        ok = archive_write_data(m_ar, data.constData(), data.size()) == data.size();
    if (!ok)
        m_error = QString::fromUtf8(archive_error_string(m_ar));
    archive_entry_free(e);
    return ok;
}

bool ArchiveWriter::addDir(const QString &name)
{
    if (!m_ar)
        return false;
    archive_entry *e = archive_entry_new();
    archive_entry_set_pathname_utf8(e, name.toUtf8().constData());
    archive_entry_set_filetype(e, AE_IFDIR);
    archive_entry_set_perm(e, 0755);
    archive_entry_set_mtime(e, time(nullptr), 0);
    const bool ok = archive_write_header(m_ar, e) == ARCHIVE_OK;
    archive_entry_free(e);
    return ok;
}

bool ArchiveWriter::close()
{
    if (!m_ar)
        return true;
    const bool ok = archive_write_close(m_ar) == ARCHIVE_OK;
    if (!ok)
        m_error = QString::fromUtf8(archive_error_string(m_ar));
    archive_write_free(m_ar);
    m_ar = nullptr;
    return ok;
}

} // namespace lunii
