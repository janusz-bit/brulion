// Repository addresses as people paste them, and the addresses Brulion builds itself. Part of the
// github module — see github/github.h for the overview.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <QUrl>

#include <optional>
#include <utility>

namespace brulion::github {

/// github.com, its API and the host of its raw files: the only hosts the module ever contacts.
inline constexpr QStringView WebHost = u"github.com";
inline constexpr QStringView ApiHost = u"api.github.com";
inline constexpr QStringView RawHost = u"raw.githubusercontent.com";

/// A repository as typed: "owner/name", "https://github.com/owner/name(.git)", "github.com/owner/
/// name/tree/<branch>/<folder>", "…/blob/<branch>/<folder>/<file>", "git@github.com:owner/name.git",
/// "ssh://git@github.com/owner/name", "https://raw.githubusercontent.com/owner/name/<branch>/<file>".
struct RepoAddress {
    QString owner;
    QString name;
    /// The decoded segments after "tree/" or "blob/" (raw addresses: after the name) — a branch
    /// and a folder that only the repository can tell apart: a branch name may contain "/"
    /// (splitRefPath()). Empty when the address names none.
    QStringList refPath;
    /// The address names a file (blob/, raw): the last segment of refPath is the file, its folder
    /// is the notebook's.
    bool file = false;

    friend bool operator==(const RepoAddress &, const RepoAddress &) = default;
};

/// The repository named by `text` (whitespace around it and a query or fragment ignored, a final
/// ".git" dropped, segments percent-decoded); std::nullopt when it names no GitHub repository —
/// another host, a user's page, an owner or a name GitHub would not accept.
std::optional<RepoAddress> parseRepoAddress(const QString &text);

/// An account or organisation name GitHub accepts: 1–39 letters, digits, "-" or "_", not
/// starting with "-".
bool isValidOwner(QStringView owner);
/// A repository name GitHub accepts: 1–100 ASCII letters, digits, ".", "-" or "_", neither "."
/// nor "..".
bool isValidRepoName(QStringView name);
/// A branch name Git accepts (git check-ref-format, branch rules): no "..", "@{", control
/// characters, space ~ ^ : ? * [ \, no segment starting with "." or ending with ".lock", not
/// ending with "/" or ".", not "@".
bool isValidBranchName(QStringView branch);

/// A folder of the repository as Brulion keeps it: segments joined by "/", empty ones (leading,
/// trailing, doubled slashes) dropped, NFC; "" is the repository's root. std::nullopt for "." or
/// ".." segments, backslashes and control characters.
std::optional<QString> normalizeFolder(const QString &folder);

/// The branch and the folder of an address's refPath: the longest of `branches` that the segments
/// start with (whole segments), else the first segment. A file's address leaves its name out of
/// the folder. Returns {branch, folder} ("" for none).
std::pair<QString, QString> splitRefPath(const QStringList &refPath, const QStringList &branches, bool file = false);

/// "owner/name".
QString fullName(const QString &owner, const QString &name);

/// https://github.com/owner/name, with "/tree/<branch>[/<folder>]" when a branch is given.
QUrl webUrl(const QString &owner, const QString &name, const QString &branch = {}, const QString &folder = {});
/// https://github.com/owner/name/commit/<sha>.
QUrl commitUrl(const QString &owner, const QString &name, const QString &sha);
/// https://github.com/owner/name/blob/<branch>/<path> — a file of the branch.
QUrl fileUrl(const QString &owner, const QString &name, const QString &branch, const QString &path);
/// https://github.com/settings/tokens/new with Brulion's description and the scope `repo`
/// (`public_repo` with `publicOnly`): the page that makes a classic token for Brulion.
QUrl newTokenUrl(bool publicOnly = false);

/// A path of the repository with every segment percent-encoded ("/" kept): for URLs.
QString encodePath(const QString &path);

} // namespace brulion::github
