// What the file system of a test run can do, for the tests whose subject needs it: symbolic links,
// Unix file modes and names that differ only in letter case. Where it cannot, such a test (or the
// part of it) is skipped with the reason instead of failing on what the platform never does.
//
// Windows: QFile::link() makes a shortcut (a .lnk file), not a symbolic link; file modes are only
// the read-only attribute (folders ignore it, other users are kept out by access lists, not mode
// bits); NTFS folders are case-insensitive by default.
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTest>
#include <QUuid>

namespace brulion::test {

/// Whether QFile::link() makes symbolic links here.
inline bool hasSymlinks()
{
#ifdef Q_OS_WIN
    return false;
#else
    return true;
#endif
}

/// Whether Unix file modes (owner/group/other bits, a folder without write permission) apply here.
inline bool hasUnixPermissions()
{
#ifdef Q_OS_WIN
    return false;
#else
    return true;
#endif
}

/// Whether `dir` (an existing folder) tells "a" from "A": a probe file is created and looked up
/// in the other case.
inline bool isCaseSensitive(const QString &dir)
{
    const QString name = QStringLiteral("brulion-case-probe-") + QUuid::createUuid().toString(QUuid::Id128).left(8);
    QFile probe(QDir(dir).filePath(name));
    if (!probe.open(QIODevice::WriteOnly))
        return true;
    probe.close();
    const bool sensitive = !QFileInfo::exists(QDir(dir).filePath(name.toUpper()));
    probe.remove();
    return sensitive;
}

} // namespace brulion::test

#define BRULION_SKIP_WITHOUT_SYMLINKS()                                                                                \
    do {                                                                                                               \
        if (!brulion::test::hasSymlinks())                                                                             \
            QSKIP("QFile::link() makes a shortcut (.lnk) on Windows, not a symbolic link");                            \
    } while (false)

#define BRULION_SKIP_WITHOUT_UNIX_PERMISSIONS()                                                                        \
    do {                                                                                                               \
        if (!brulion::test::hasUnixPermissions())                                                                      \
            QSKIP("no Unix file modes on Windows (only the read-only attribute; access lists instead)");               \
    } while (false)

#define BRULION_SKIP_IF_CASE_INSENSITIVE(dir)                                                                          \
    do {                                                                                                               \
        if (!brulion::test::isCaseSensitive(dir))                                                                      \
            QSKIP("needs names that differ only in letter case: this file system is case-insensitive");                \
    } while (false)
