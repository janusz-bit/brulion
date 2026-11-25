#pragma once
// Internal to gitcli: the messages for the user, translated by the app's translator (context
// "GitMessages"), and helpers the client's files share.

#include <QCoreApplication>
#include <QString>

namespace brulion::gitcli::detail {

/// `GitMessages::tr("…")` — English source text.
class GitMessages {
    Q_DECLARE_TR_FUNCTIONS(GitMessages)
};

/// The first 7 digits of a commit's id.
inline QString shortOid(const QString &oid)
{
    return oid.left(7);
}

/// `folder` + "/" + `path` (a '/'-separated path inside the work tree).
inline QString inTree(const QString &folder, const QString &path)
{
    if (folder.endsWith(u'/'))
        return folder + path;
    return folder + u'/' + path;
}

} // namespace brulion::gitcli::detail
