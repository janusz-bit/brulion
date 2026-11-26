// The parsers of gitcli.h for history and remotes: `git log`, `git stash list`, `git remote -v`, a
// remote's URL, the fork a pull request comes from, `gh pr view --json`.
#include "gitcli/gitcli.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

namespace {

/// The NUL-terminated tokens of `data`; a last token without its NUL (a truncated output) too.
QList<QByteArrayView> nulTokens(QByteArrayView data)
{
    QList<QByteArrayView> out;
    qsizetype start = 0;
    while (start < data.size()) {
        const qsizetype end = data.indexOf('\0', start);
        if (end < 0) {
            out.append(data.sliced(start));
            break;
        }
        out.append(data.sliced(start, end - start));
        start = end + 1;
    }
    return out;
}

/// The parts of `data` between `separator`s (empty ones left out).
QList<QByteArrayView> partsOf(QByteArrayView data, char separator)
{
    QList<QByteArrayView> out;
    qsizetype start = 0;
    while (start <= data.size()) {
        qsizetype end = data.indexOf(separator, start);
        if (end < 0)
            end = data.size();
        if (end > start)
            out.append(data.sliced(start, end - start));
        start = end + 1;
    }
    return out;
}

/// A record's first field without the newline that ended the record before it.
QByteArrayView firstField(QByteArrayView field)
{
    while (!field.isEmpty() && (field.at(0) == '\n' || field.at(0) == '\r'))
        field = field.sliced(1);
    return field;
}

bool isHexId(QByteArrayView field)
{
    if (field.size() < 7 || field.size() > 64)
        return false;
    for (const char c : field) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    }
    return true;
}

constexpr int LogFields = 6;
constexpr int StashFields = 4;

} // namespace

// ---- git log -----------------------------------------------------------------------------------

QString logFormat()
{
    return u"tformat:%H%x00%P%x00%an%x00%ae%x00%at%x00%s%x00"_s;
}

QList<Commit> parseLog(QByteArrayView data)
{
    QList<Commit> out;
    const QList<QByteArrayView> tokens = nulTokens(data);
    for (qsizetype i = 0; i + LogFields <= tokens.size(); i += LogFields) {
        const QByteArrayView oid = firstField(tokens.at(i));
        if (!isHexId(oid))
            continue;
        Commit c;
        c.oid = QString::fromLatin1(oid);
        for (const QByteArrayView parent : partsOf(tokens.at(i + 1), ' ')) {
            if (isHexId(parent))
                c.parents.append(QString::fromLatin1(parent));
        }
        c.authorName = QString::fromUtf8(tokens.at(i + 2));
        c.authorEmail = QString::fromUtf8(tokens.at(i + 3));
        bool ok = false;
        const qint64 time = tokens.at(i + 4).toLongLong(&ok);
        c.authorTime = ok ? time : 0;
        c.subject = QString::fromUtf8(tokens.at(i + 5));
        out.append(c);
    }
    return out;
}

// ---- git stash list ----------------------------------------------------------------------------

QString stashFormat()
{
    return u"tformat:%gd%x00%H%x00%ct%x00%gs%x00"_s;
}

QList<Stash> parseStashes(QByteArrayView data)
{
    QList<Stash> out;
    const QList<QByteArrayView> tokens = nulTokens(data);
    for (qsizetype i = 0; i + StashFields <= tokens.size(); i += StashFields) {
        const QByteArrayView ref = firstField(tokens.at(i));
        if (!ref.startsWith("stash@{") || !ref.endsWith("}"))
            continue;
        bool ok = false;
        const int index = ref.sliced(7, ref.size() - 8).toInt(&ok);
        if (!ok || index < 0 || !isHexId(tokens.at(i + 1)))
            continue;
        Stash s;
        s.index = index;
        s.oid = QString::fromLatin1(tokens.at(i + 1));
        const qint64 time = tokens.at(i + 2).toLongLong(&ok);
        s.time = ok ? time : 0;
        s.message = QString::fromUtf8(tokens.at(i + 3));
        // "On main: message" (stash push -m) / "WIP on main: 1a2b3c4 subject".
        QStringView m(s.message);
        if (m.startsWith(u"WIP on "))
            m = m.sliced(7);
        else if (m.startsWith(u"On "))
            m = m.sliced(3);
        else
            m = {};
        const qsizetype colon = m.indexOf(u": ");
        if (colon > 0)
            s.branch = m.left(colon).toString();
        out.append(s);
    }
    return out;
}

// ---- Remotes -----------------------------------------------------------------------------------

QList<Remote> parseRemotes(QByteArrayView data)
{
    QList<Remote> out;
    for (QByteArrayView line : partsOf(data, '\n')) {
        if (line.endsWith('\r'))
            line.chop(1);
        const qsizetype tab = line.indexOf('\t');
        if (tab <= 0)
            continue;
        const QString name = QString::fromUtf8(line.first(tab));
        QByteArrayView rest = line.sliced(tab + 1);
        bool push = false;
        if (rest.endsWith(" (push)")) {
            push = true;
            rest.chop(7);
        } else if (rest.endsWith(" (fetch)")) {
            rest.chop(8);
        } else {
            continue;
        }
        const QString url = QString::fromUtf8(rest);
        Remote *remote = nullptr;
        for (Remote &r : out) {
            if (r.name == name)
                remote = &r;
        }
        if (!remote) {
            out.append(Remote{name, QString(), QString()});
            remote = &out.last();
        }
        (push ? remote->pushUrl : remote->fetchUrl) = url;
    }
    for (Remote &r : out) {
        if (r.pushUrl.isEmpty())
            r.pushUrl = r.fetchUrl;
        if (r.fetchUrl.isEmpty())
            r.fetchUrl = r.pushUrl;
    }
    return out;
}

RepoSlug parseRemoteUrl(QStringView url)
{
    url = url.trimmed();
    QStringView host;
    QStringView path;
    const qsizetype scheme = url.indexOf(u"://");
    if (scheme > 0) {
        // https://[user[:token]@]host[:port]/owner/name[.git]
        QStringView rest = url.sliced(scheme + 3);
        const qsizetype slash = rest.indexOf(u'/');
        if (slash <= 0)
            return {};
        QStringView authority = rest.first(slash);
        path = rest.sliced(slash + 1);
        const qsizetype at = authority.lastIndexOf(u'@');
        if (at >= 0)
            authority = authority.sliced(at + 1);
        const qsizetype colon = authority.indexOf(u':');
        host = colon >= 0 ? authority.first(colon) : authority;
    } else {
        // [user@]host:owner/name[.git] — not a Windows drive ("C:\…") or a local path.
        const qsizetype colon = url.indexOf(u':');
        if (colon <= 1 || url.first(colon).contains(u'/'))
            return {};
        QStringView authority = url.first(colon);
        const qsizetype at = authority.lastIndexOf(u'@');
        if (at >= 0)
            authority = authority.sliced(at + 1);
        host = authority;
        path = url.sliced(colon + 1);
    }
    while (path.startsWith(u'/'))
        path = path.sliced(1);
    while (path.endsWith(u'/'))
        path.chop(1);
    if (path.endsWith(u".git"))
        path.chop(4);
    const qsizetype last = path.lastIndexOf(u'/');
    if (host.isEmpty() || last <= 0 || last == path.size() - 1)
        return {};
    RepoSlug slug;
    slug.host = host.toString().toLower();
    slug.owner = path.first(last).toString();
    slug.name = path.sliced(last + 1).toString();
    if (slug.owner.contains(u"//") || slug.owner.contains(u' ') || slug.name.contains(u' '))
        return {};
    return slug;
}

ForkTarget forkTarget(const QList<Remote> &remotes, const QString &pushRemote, const QString &branch)
{
    const QString pushName = pushRemote.isEmpty() ? u"origin"_s : pushRemote;
    const Remote *push = nullptr;
    const Remote *upstream = nullptr;
    for (const Remote &r : remotes) {
        if (r.name == pushName)
            push = &r;
        if (r.name == u"upstream"_s)
            upstream = &r;
    }
    if (!push || !upstream || push == upstream || branch.isEmpty())
        return {};
    RepoSlug from = parseRemoteUrl(push->pushUrl);
    if (!from.isValid()) // (a push URL of its own that names no host: the fetch URL's owner)
        from = parseRemoteUrl(push->fetchUrl);
    const RepoSlug to = parseRemoteUrl(upstream->fetchUrl);
    if (!from.isValid() || !to.isValid() || from.host != to.host
        || from.path().compare(to.path(), Qt::CaseInsensitive) == 0)
        return {};
    ForkTarget t;
    t.repo = to.host == u"github.com"_s ? to.path() : to.host + u'/' + to.path();
    t.head = from.owner + u':' + branch;
    return t;
}

// ---- gh ----------------------------------------------------------------------------------------

PullRequestInfo parsePullRequestView(QByteArrayView json)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    const QJsonObject o = doc.object();
    PullRequestInfo pr;
    pr.number = o.value(u"number"_s).toInt();
    pr.url = o.value(u"url"_s).toString();
    pr.title = o.value(u"title"_s).toString();
    pr.baseRefName = o.value(u"baseRefName"_s).toString();
    const QString state = o.value(u"state"_s).toString().toLower();
    if (state == u"merged"_s || state == u"closed"_s)
        pr.state = state;
    else
        pr.state = o.value(u"isDraft"_s).toBool() ? u"draft"_s : u"open"_s;
    if (!pr.url.startsWith(u"https://"_s) && !pr.url.startsWith(u"http://"_s))
        pr.url.clear();
    if (!pr.isValid())
        return {};
    return pr;
}

bool isValidRepositoryName(QStringView name)
{
    const QList<QStringView> parts = name.split(u'/');
    if (parts.isEmpty() || parts.size() > 2)
        return false;
    for (const QStringView part : parts) {
        if (part.isEmpty() || part.size() > 100 || part == u"." || part == u"..")
            return false;
        for (const QChar c : part) {
            const char16_t u = c.unicode();
            const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9')
                            || u == u'.' || u == u'_' || u == u'-';
            if (!ok)
                return false;
        }
    }
    return !name.startsWith(u'-');
}

} // namespace brulion::gitcli
