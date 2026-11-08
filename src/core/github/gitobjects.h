// Git's object ids and file modes as GitHub's API names them. Part of the github module — see
// github/github.h for the overview.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringView>

namespace brulion::github {

/// Git's id of a blob holding `content`: the SHA-1 of "blob <size>\0" + content, 40 lower-case
/// hexadecimal digits — what a tree lists for a file, so a file's version is known without
/// downloading it.
QString blobSha(QByteArrayView content);

/// 40 hexadecimal digits (any case).
bool isSha(QStringView text);
/// The first 7 digits ("a1b2c3d"), as GitHub shows a commit.
QString shortSha(QStringView sha);

/// A Git LFS pointer (https://git-lfs.github.com/spec/v1): a small text file standing for content
/// kept elsewhere — never a note.
bool isLfsPointer(QByteArrayView content);

/// File modes of a tree entry.
inline constexpr QStringView FileMode = u"100644";
inline constexpr QStringView ExecutableMode = u"100755";
inline constexpr QStringView SymlinkMode = u"120000";
inline constexpr QStringView SubmoduleMode = u"160000";
inline constexpr QStringView TreeMode = u"040000";

} // namespace brulion::github
