// gitcli: cloning a repository for Code mode's "Clone a repository" (docs/CODE.md "Cloning") —
// what the user typed read as a clone address (T3 Code's rules: "owner/name" is GitHub, a web page
// of a repository is cut to the repository), ssh ↔ https for a second try, `git clone --progress`'s
// stages, `gh repo list`'s JSON; Client::clone() and Client::repositoryList().

#include "gitcli/client.h"
#include "gitcli/gitcli.h"
#include "gitcli/messages_p.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

using detail::GitMessages;

namespace {

/// A folder name that is safe on every platform: not "." or "..", none of \ / : * ? " < > |, no
/// control characters, no trailing dot or space, no Windows device name.
bool isSafeFolderName(const QString &name)
{
    if (name.isEmpty() || name == u"." || name == u".." || name.size() > 255)
        return false;
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || c == u'\\' || c == u'/' || c == u':' || c == u'*' || c == u'?' || c == u'"'
            || c == u'<' || c == u'>' || c == u'|' || c.unicode() == 0x7f)
            return false;
    }
    if (name.endsWith(u'.') || name.endsWith(u' '))
        return false;
    static const QRegularExpression device(u"^(con|prn|aux|nul|com[0-9]|lpt[0-9])( *\\.| *$)"_s,
                                           QRegularExpression::CaseInsensitiveOption);
    return !device.match(name).hasMatch();
}

/// The repository's name from the last segment of its path, ".git" taken off.
QString repositoryName(QStringView path)
{
    while (path.endsWith(u'/') || path.endsWith(u'\\'))
        path.chop(1);
    const qsizetype slash = std::max(path.lastIndexOf(u'/'), path.lastIndexOf(u'\\'));
    QStringView last = slash >= 0 ? path.sliced(slash + 1) : path;
    if (last.endsWith(u".git", Qt::CaseInsensitive))
        last.chop(4);
    const QString name = last.toString();
    return isSafeFolderName(name) ? name : QString();
}

/// Where a repository's web page path stops being the repository's: GitHub's and Gitea's /tree/,
/// /blob/, /pull/ …, GitLab's /-/, Bitbucket's /src/.
qsizetype webPageCut(const QStringList &segments, bool github)
{
    if (github)
        return std::min<qsizetype>(segments.size(), 2);
    static const QStringList markers{u"-"_s,        u"tree"_s,   u"blob"_s,           u"raw"_s,
                                     u"src"_s,      u"commit"_s, u"commits"_s,        u"pull"_s,
                                     u"pulls"_s,    u"issues"_s, u"merge_requests"_s, u"pull-requests"_s,
                                     u"branches"_s, u"tags"_s,   u"releases"_s,       u"actions"_s,
                                     u"wiki"_s};
    for (qsizetype i = 2; i < segments.size(); ++i) {
        if (markers.contains(segments.at(i)))
            return i;
    }
    return segments.size();
}

/// A host name as written in an address: letters, digits, dots and dashes (or an [IPv6] one).
bool isHostName(QStringView host)
{
    static const QRegularExpression re(u"^(?:\\[[0-9A-Fa-f:.]+\\]|[A-Za-z0-9](?:[A-Za-z0-9.-]*[A-Za-z0-9])?)$"_s);
    return re.matchView(host).hasMatch();
}

/// The path of an address checked: segments without "." or "..", spaces or control characters.
bool isSafeRepositoryPath(const QStringList &segments)
{
    if (segments.size() < 2)
        return false;
    for (const QString &s : segments) {
        if (s.isEmpty() || s == u"." || s == u"..")
            return false;
        for (const QChar c : s) {
            if (c.unicode() <= 0x20 || c.unicode() == 0x7f || c == u'\\')
                return false;
        }
    }
    return true;
}

CloneSource failed(const QString &error)
{
    CloneSource s;
    s.error = error;
    return s;
}

} // namespace

QString CloneSource::displayName() const
{
    if (local)
        return url;
    if (host == u"github.com")
        return path;
    return host + u'/' + path;
}

CloneSource parseCloneSource(QStringView text)
{
    const QString t = text.trimmed().toString();
    if (t.isEmpty())
        return {};
    if (t.startsWith(u'-'))
        return failed(GitMessages::tr("“%1” is no repository’s address.").arg(t));
    for (const QChar c : t) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return failed(GitMessages::tr("The address has a control character."));
    }

    // A repository on this computer: an absolute path or a file: URL.
    if (t.startsWith(u"file:", Qt::CaseInsensitive) || QDir::isAbsolutePath(t) || t.startsWith(u'/')) {
        const QString path = t.startsWith(u"file:", Qt::CaseInsensitive) ? QUrl(t).toLocalFile() : t;
        if (path.isEmpty() || !QDir::isAbsolutePath(path))
            return failed(GitMessages::tr("“%1” is no folder of this computer.").arg(t));
        CloneSource s;
        s.local = true;
        s.url = QDir::cleanPath(path);
        s.name = repositoryName(s.url);
        if (s.name.isEmpty())
            s.name = u"repository"_s;
        return s;
    }

    // "owner/name": GitHub (T3 Code's shorthand; GitHub's own rules for both names).
    static const QRegularExpression shorthand(u"^([A-Za-z0-9](?:[A-Za-z0-9-]{0,38}))/([A-Za-z0-9._-]+?)(?:\\.git)?$"_s);
    if (const QRegularExpressionMatch m = shorthand.match(t); m.hasMatch()) {
        const QString name = m.captured(2);
        if (name == u"." || name == u"..")
            return failed(GitMessages::tr("“%1” is no repository’s address.").arg(t));
        CloneSource s;
        s.host = u"github.com"_s;
        s.path = m.captured(1) + u'/' + name;
        s.url = u"https://github.com/"_s + s.path + u".git"_s;
        s.name = isSafeFolderName(name) ? name : u"repository"_s;
        s.shorthand = true;
        return s;
    }
    static const QRegularExpression ownerOnly(u"^[A-Za-z0-9][A-Za-z0-9-]*/*$"_s);
    if (ownerOnly.match(t).hasMatch())
        return failed(GitMessages::tr("Type the whole owner/repository name."));

    QString scheme;
    QString rest = t;
    if (const qsizetype at = t.indexOf(u"://"); at > 0) {
        scheme = t.first(at).toLower();
        rest = t.sliced(at + 3);
    } else if (static const QRegularExpression hostFirst(u"^[A-Za-z0-9.-]+\\.[A-Za-z]{2,}/"_s);
               hostFirst.match(t).hasMatch()) {
        // "github.com/owner/name": a web address without its scheme.
        scheme = u"https"_s;
    }

    if (scheme.isEmpty()) {
        // scp-like: [user@]host:path — never "host::…" (a transport helper: `ext::sh -c …`).
        static const QRegularExpression scp(u"^(?:([^@/\\s]+)@)?([^:/\\s\\[\\]]+|\\[[^\\]/\\s]+\\]):(?!/|:)(.+)$"_s);
        const QRegularExpressionMatch m = scp.match(t);
        if (!m.hasMatch() || !isHostName(m.captured(2)) || m.captured(2).size() < 2)
            return failed(GitMessages::tr("“%1” is no repository’s address: type owner/name, an https or ssh address, "
                                          "or a repository’s page.")
                                  .arg(t));
        QString path = m.captured(3);
        while (path.endsWith(u'/'))
            path.chop(1);
        QStringList segments = path.split(u'/', Qt::SkipEmptyParts);
        if (!segments.isEmpty() && segments.last().endsWith(u".git", Qt::CaseInsensitive))
            segments.last().chop(4);
        if (!isSafeRepositoryPath(segments))
            return failed(GitMessages::tr("“%1” is no repository’s address.").arg(t));
        CloneSource s;
        s.host = m.captured(2).toLower();
        s.path = segments.join(u'/');
        s.url = t;
        s.name = repositoryName(s.path);
        if (s.name.isEmpty())
            s.name = u"repository"_s;
        return s;
    }

    if (scheme != u"https" && scheme != u"http" && scheme != u"ssh" && scheme != u"git")
        return failed(GitMessages::tr("Brulion clones over https and ssh, not “%1”.").arg(scheme));

    // [user[:password]@]host[:port]/path[?query][#fragment]
    const qsizetype slash = rest.indexOf(u'/');
    if (slash <= 0)
        return failed(GitMessages::tr("“%1” names no repository.").arg(t));
    QString authority = rest.first(slash);
    QString path = rest.sliced(slash + 1);
    const qsizetype at = authority.lastIndexOf(u'@');
    const QString userInfo = at >= 0 ? authority.first(at) : QString();
    if (at >= 0)
        authority = authority.sliced(at + 1);
    QString host = authority;
    QString port;
    if (const qsizetype colon = authority.lastIndexOf(u':'); colon > 0 && !authority.endsWith(u']')) {
        host = authority.first(colon);
        port = authority.sliced(colon + 1);
        static const QRegularExpression digits(u"^[0-9]{1,5}$"_s);
        if (!digits.match(port).hasMatch())
            return failed(GitMessages::tr("“%1” is no repository’s address.").arg(t));
    }
    if (!isHostName(host))
        return failed(GitMessages::tr("“%1” is no repository’s address.").arg(t));
    host = host.toLower();
    const bool web = scheme == u"https" || scheme == u"http";
    // A page's query and fragment are not the repository's (an ssh path keeps a literal '#').
    if (web) {
        const qsizetype end = path.indexOf(QRegularExpression(u"[?#]"_s));
        if (end >= 0)
            path.truncate(end);
    }
    QStringList segments = path.split(u'/', Qt::SkipEmptyParts);
    const bool github = host == u"github.com" || host == u"www.github.com";
    if (web)
        segments = segments.first(webPageCut(segments, github));
    const bool dotGit = !segments.isEmpty() && segments.last().endsWith(u".git", Qt::CaseInsensitive);
    if (dotGit)
        segments.last().chop(4);
    if (web) {
        for (QString &segment : segments)
            segment = QUrl::fromPercentEncoding(segment.toUtf8());
    }
    if (!isSafeRepositoryPath(segments))
        return failed(GitMessages::tr("“%1” names no repository: an address of one is host/owner/name.").arg(t));

    CloneSource s;
    s.host = github ? u"github.com"_s : host;
    s.path = segments.join(u'/');
    s.name = repositoryName(s.path);
    if (s.name.isEmpty())
        s.name = u"repository"_s;
    if (web) {
        // The repository's own address: the page cut off, ".git" on GitHub, a login kept (the
        // user pasted it) — never the query.
        QStringList encoded;
        for (const QString &segment : std::as_const(segments))
            encoded << QString::fromUtf8(QUrl::toPercentEncoding(segment, "-._~"));
        s.url = scheme + u"://"_s + (userInfo.isEmpty() ? QString() : userInfo + u'@') + s.host
                + (port.isEmpty() ? QString() : u':' + port) + u'/' + encoded.join(u'/')
                + (github || dotGit ? u".git"_s : QString());
    } else {
        s.url = t;
    }
    return s;
}

QString cloneUrlForProtocol(QStringView url, bool ssh)
{
    const CloneSource s = parseCloneSource(url);
    if (!s.isValid() || s.local)
        return {};
    const QString &t = s.url; // ("owner/name": its https address)
    const bool isSsh = !t.contains(u"://") || t.startsWith(u"ssh://", Qt::CaseInsensitive);
    const bool isWeb = t.startsWith(u"https://", Qt::CaseInsensitive) || t.startsWith(u"http://", Qt::CaseInsensitive);
    if (!isSsh && !isWeb)
        return {};
    if (ssh == isSsh)
        return t;
    // A custom port belongs to one protocol only.
    static const QRegularExpression port(u"^[a-z]+://[^/]*:[0-9]+/"_s, QRegularExpression::CaseInsensitiveOption);
    if (port.match(t).hasMatch())
        return {};
    // (The path is the same on both protocols on GitHub, GitLab, Codeberg, Bitbucket's bitbucket.org.)
    if (ssh)
        return u"git@"_s + s.host + u':' + s.path + u".git"_s;
    QStringList encoded;
    for (const QString &segment : s.path.split(u'/'))
        encoded << QString::fromUtf8(QUrl::toPercentEncoding(segment, "-._~"));
    return u"https://"_s + s.host + u'/' + encoded.join(u'/') + u".git"_s;
}

QString withoutUrlCredentials(QStringView url)
{
    QString t = url.trimmed().toString();
    static const QRegularExpression web(u"^(https?://)[^/?#]*@"_s, QRegularExpression::CaseInsensitiveOption);
    if (!t.startsWith(u"http://", Qt::CaseInsensitive) && !t.startsWith(u"https://", Qt::CaseInsensitive))
        return t;
    t.replace(web, u"\\1"_s);
    const qsizetype query = t.indexOf(u'?');
    if (query >= 0) {
        const qsizetype fragment = t.indexOf(u'#', query);
        t.remove(query, (fragment >= 0 ? fragment : t.size()) - query);
    }
    return t;
}

CloneProgress parseCloneProgress(QStringView err)
{
    CloneProgress p;
    // The last line that says a stage: git rewrites a line with '\r' while it counts.
    static const QRegularExpression line(u"(?:remote: )?(Enumerating objects|Counting objects|Compressing "
                                         u"objects|Receiving objects|Resolving deltas|"
                                         "Updating files|Checking out files):\\s*(?:([0-9]{1,3})%)?[^\\r\\n]*"_s);
    QRegularExpressionMatchIterator it = line.globalMatchView(err);
    QRegularExpressionMatch last;
    while (it.hasNext())
        last = it.next();
    if (!last.hasMatch())
        return p;
    const QString stage = last.captured(1);
    if (stage == u"Receiving objects")
        p.stage = CloneStage::Receiving;
    else if (stage == u"Resolving deltas")
        p.stage = CloneStage::Resolving;
    else if (stage == u"Updating files" || stage == u"Checking out files")
        p.stage = CloneStage::Checkout;
    else
        p.stage = CloneStage::Counting;
    if (!last.captured(2).isEmpty())
        p.percent = std::min(100, last.captured(2).toInt());
    // "45% (450/1000), 12.40 MiB | 3.10 MiB/s" → "12.40 MiB | 3.10 MiB/s"; ", done." → "".
    const QString whole = last.captured(0);
    if (const qsizetype comma = whole.indexOf(u"), "_s); comma >= 0) {
        QString detail = whole.sliced(comma + 3).trimmed();
        if (detail.endsWith(u", done."_s))
            detail.chop(7);
        if (detail != u"done." && detail != u"done")
            p.detail = detail.left(80);
    }
    return p;
}

QString cloneStageName(CloneStage stage)
{
    switch (stage) {
    case CloneStage::Connecting: return u"connecting"_s;
    case CloneStage::Counting: return u"counting"_s;
    case CloneStage::Receiving: return u"receiving"_s;
    case CloneStage::Resolving: return u"resolving"_s;
    case CloneStage::Checkout: return u"checkout"_s;
    }
    return u"connecting"_s;
}

bool isAuthenticationFailure(QStringView error)
{
    static const QStringList signs{
            u"permission denied (publickey"_s, u"host key verification failed"_s, u"authentication failed"_s,
            u"could not read username"_s,      u"could not read password"_s,      u"terminal prompts disabled"_s,
            u"invalid username or password"_s, u"returned error: 401"_s,          u"returned error: 403"_s};
    const QString e = error.toString().toLower();
    for (const QString &sign : signs) {
        if (e.contains(sign))
            return true;
    }
    return false;
}

QString repositoryListFields()
{
    return u"nameWithOwner,description,url,sshUrl,isPrivate,isFork,isArchived,updatedAt"_s;
}

QList<HostedRepository> parseRepositoryList(QByteArrayView json)
{
    QList<HostedRepository> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toByteArray());
    if (!doc.isArray())
        return out;
    static const QRegularExpression name(u"^[A-Za-z0-9-]+/[A-Za-z0-9._-]+$"_s);
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        HostedRepository r;
        r.nameWithOwner = o.value(u"nameWithOwner"_s).toString();
        if (!name.match(r.nameWithOwner).hasMatch())
            continue;
        r.description = o.value(u"description"_s).toString().simplified().left(300);
        r.url = o.value(u"url"_s).toString();
        r.sshUrl = o.value(u"sshUrl"_s).toString();
        r.isPrivate = o.value(u"isPrivate"_s).toBool();
        r.isFork = o.value(u"isFork"_s).toBool();
        r.isArchived = o.value(u"isArchived"_s).toBool();
        r.updatedAt = o.value(u"updatedAt"_s).toString();
        out << r;
    }
    return out;
}

// ---- Client --------------------------------------------------------------------------------------

void Client::clone(const QString &url, const QString &destination, const QString &tag,
                   std::function<void(const CloneProgress &)> progress, std::function<void(const Outcome &)> done)
{
    const QString dest = QDir::cleanPath(QFileInfo(destination).absoluteFilePath());
    const auto fail = [this, done](const QString &message) {
        later([done, message] { done(Outcome{false, message, {}, {}}); });
    };
    if (url.trimmed().isEmpty() || url.trimmed().startsWith(u'-') || destination.trimmed().isEmpty()) {
        fail(GitMessages::tr("Nothing to clone"));
        return;
    }
    const QFileInfo info(dest);
    const bool existed = info.exists();
    if (existed
        && (!info.isDir()
            || !QDir(dest).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))) {
        fail(GitMessages::tr("“%1” is there already and is not an empty folder.").arg(QDir::toNativeSeparators(dest)));
        return;
    }
    const QString parent = info.absolutePath();
    if (!QDir().mkpath(parent)) {
        fail(GitMessages::tr("The folder “%1” could not be made.").arg(QDir::toNativeSeparators(parent)));
        return;
    }
    // The last of standard error (the message of a failure comes at the end, after any amount of
    // progress), and the progress said so far.
    auto tail = std::make_shared<QByteArray>();
    RunOptions ro;
    ro.timeoutMs = m_cloneTimeoutMs;
    ro.tag = tag;
    // ssh asks nothing on the terminal Brulion may have been started from (a host key, a key's
    // passphrase): only through an askpass program — the user's, else none, and it fails at once
    // instead of waiting for an answer nobody sees (https never asks: GIT_TERMINAL_PROMPT=0).
    if (qEnvironmentVariableIsEmpty("SSH_ASKPASS_REQUIRE"))
        ro.env.append({u"SSH_ASKPASS_REQUIRE"_s, u"force"_s});
    ro.onError = [tail, progress](const QByteArray &chunk) {
        tail->append(chunk);
        if (tail->size() > 16 * 1024)
            tail->remove(0, tail->size() - 8 * 1024);
        if (progress)
            progress(parseCloneProgress(QString::fromUtf8(*tail)));
    };
    git(parent, {u"clone"_s, u"--progress"_s, u"--"_s, url.trimmed(), dest}, ro,
        [dest, existed, tail, done](const RunResult &r) {
            if (r.ok()) {
                done(Outcome{true, GitMessages::tr("Cloned into %1").arg(QDir::toNativeSeparators(dest)), {}, dest});
                return;
            }
            // (git takes away a folder it made when it fails, not when it is killed.)
            if (!existed)
                QDir(dest).removeRecursively();
            else if (QFileInfo(dest).isDir()) {
                QDir d(dest);
                for (const QString &entry :
                     d.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System)) {
                    const QFileInfo e(d.filePath(entry));
                    if (e.isDir() && !e.isSymLink())
                        QDir(e.filePath()).removeRecursively();
                    else
                        QFile::remove(e.filePath());
                }
            }
            RunResult shown = r;
            QString text = QString::fromUtf8(*tail);
            text.replace(u'\r', u'\n');
            shown.err = text;
            QString message = shown.message().right(1000);
            // git's "fatal:" line alone hides why ssh or https gave up ("Permission denied
            // (publickey)." comes before it): that line goes first, so the user — and the second try
            // over the other protocol (isAuthenticationFailure()) — see it.
            if (!isAuthenticationFailure(message)) {
                for (const QString &line : text.split(u'\n', Qt::SkipEmptyParts)) {
                    if (isAuthenticationFailure(line)) {
                        message = line.trimmed().left(300) + u'\n' + message;
                        break;
                    }
                }
            }
            done(Outcome{false, message, {}, dest});
        });
}

void Client::repositoryList(int limit, std::function<void(const QList<HostedRepository> &, const QString &)> done)
{
    RunOptions ro;
    ro.readOnly = true;
    gh(QDir::homePath(),
       {u"repo"_s, u"list"_s, u"--limit"_s, QString::number(std::clamp(limit, 1, 1000)), u"--json"_s,
        repositoryListFields()},
       ro, [done](const RunResult &r) {
           if (!r.ok()) {
               done({}, r.message());
               return;
           }
           done(parseRepositoryList(r.out), QString());
       });
}

} // namespace brulion::gitcli
