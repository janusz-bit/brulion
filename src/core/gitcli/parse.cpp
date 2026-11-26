#include "gitcli/gitcli.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace brulion::gitcli {

namespace {

/// The NUL-terminated tokens of `data`; a last token without its NUL (a truncated output) too.
QList<QByteArrayView> splitNul(QByteArrayView data)
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

QString text(QByteArrayView bytes)
{
    return QString::fromUtf8(bytes);
}

/// The first `count` space-separated fields of `line` into `fields` and what follows the next
/// space into `rest` (a path, which may hold spaces itself). False when the line is shorter.
bool splitFields(QByteArrayView line, int count, QList<QByteArrayView> &fields, QByteArrayView *rest)
{
    fields.clear();
    qsizetype start = 0;
    for (int i = 0; i < count; ++i) {
        const qsizetype space = line.indexOf(' ', start);
        if (space < 0) {
            if (!rest && i == count - 1) {
                fields.append(line.sliced(start));
                return true;
            }
            return false;
        }
        fields.append(line.sliced(start, space - start));
        start = space + 1;
    }
    if (rest)
        *rest = line.sliced(start);
    return true;
}

bool parseMode(QByteArrayView field, quint32 *mode)
{
    if (field.isEmpty() || field.size() > 6)
        return false;
    bool ok = false;
    const uint value = field.toUInt(&ok, 8);
    if (!ok)
        return false;
    *mode = value;
    return true;
}

bool isHex(QByteArrayView field)
{
    if (field.size() < 4)
        return false;
    for (const char c : field) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

bool isStatusLetter(char c)
{
    return c == '.' || c == 'M' || c == 'T' || c == 'A' || c == 'D' || c == 'R' || c == 'C' || c == 'U';
}

/// "XY" of a v2 record.
bool parseXY(QByteArrayView field, StatusEntry &entry)
{
    if (field.size() != 2 || !isStatusLetter(field.at(0)) || !isStatusLetter(field.at(1)))
        return false;
    entry.staged = field.at(0);
    entry.unstaged = field.at(1);
    return true;
}

/// "N..." or "S<c><m><u>".
bool parseSubmodule(QByteArrayView field, StatusEntry &entry)
{
    if (field.size() != 4 || (field.at(0) != 'N' && field.at(0) != 'S'))
        return false;
    entry.submodule = field.at(0) == 'S';
    return true;
}

void parseHeader(QByteArrayView line, Status &status)
{
    // "# branch.oid <commit> | (initial)", "# branch.head <branch> | (detached)",
    // "# branch.upstream <upstream>", "# branch.ab +<ahead> -<behind>", "# stash <n>".
    const auto value = [&](QByteArrayView key, QByteArrayView *out) {
        if (!line.startsWith(key) || line.size() <= key.size() || line.at(key.size()) != ' ')
            return false;
        *out = line.sliced(key.size() + 1);
        return true;
    };
    QByteArrayView v;
    BranchStatus &b = status.branch;
    if (value("# branch.oid", &v)) {
        if (v == "(initial)") {
            b.initial = true;
            b.oid.clear();
        } else if (isHex(v)) {
            b.oid = text(v);
        }
    } else if (value("# branch.head", &v)) {
        if (v == "(detached)") {
            b.detached = true;
            b.head.clear();
        } else if (!v.isEmpty()) {
            b.head = text(v);
        }
    } else if (value("# branch.upstream", &v)) {
        b.upstream = text(v);
    } else if (value("# branch.ab", &v)) {
        // "+<ahead> -<behind>"
        const qsizetype space = v.indexOf(' ');
        if (space > 1 && v.at(0) == '+' && space + 2 < v.size() && v.at(space + 1) == '-') {
            bool okA = false;
            bool okB = false;
            const int a = v.sliced(1, space - 1).toInt(&okA);
            const int bh = v.sliced(space + 2).toInt(&okB);
            if (okA && okB && a >= 0 && bh >= 0) {
                b.ahead = a;
                b.behind = bh;
                b.aheadBehindKnown = true;
            }
        }
    } else if (value("# stash", &v)) {
        bool ok = false;
        const int n = v.toInt(&ok);
        if (ok && n >= 0)
            status.stashCount = n;
    }
}

} // namespace

// ---- git status --------------------------------------------------------------------------------

Status parseStatus(QByteArrayView data)
{
    Status status;
    const QList<QByteArrayView> tokens = splitNul(data);
    QList<QByteArrayView> f;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const QByteArrayView line = tokens.at(i);
        if (line.isEmpty())
            continue;
        if (line.size() < 2 || line.at(1) != ' ') {
            ++status.skipped;
            continue;
        }
        const QByteArrayView body = line.sliced(2);
        StatusEntry e;
        QByteArrayView path;
        switch (line.at(0)) {
        case '#': parseHeader(line, status); continue;
        case '1':
            // 1 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <path>
            if (!splitFields(body, 7, f, &path) || !parseXY(f.at(0), e) || !parseSubmodule(f.at(1), e)
                || !parseMode(f.at(2), &e.headMode) || !parseMode(f.at(3), &e.indexMode)
                || !parseMode(f.at(4), &e.worktreeMode) || !isHex(f.at(5)) || !isHex(f.at(6)) || path.isEmpty()) {
                ++status.skipped;
                continue;
            }
            e.kind = EntryKind::Ordinary;
            e.path = text(path);
            break;
        case '2': {
            // 2 <XY> <sub> <mH> <mI> <mW> <hH> <hI> <X><score> <path>NUL<origPath>
            if (!splitFields(body, 8, f, &path) || !parseXY(f.at(0), e) || !parseSubmodule(f.at(1), e)
                || !parseMode(f.at(2), &e.headMode) || !parseMode(f.at(3), &e.indexMode)
                || !parseMode(f.at(4), &e.worktreeMode) || !isHex(f.at(5)) || !isHex(f.at(6)) || path.isEmpty()) {
                ++status.skipped;
                // A well-formed record's original path follows; this one's is unknown — when the
                // next token cannot start a record, it is that path.
                if (i + 1 < tokens.size()) {
                    const QByteArrayView next = tokens.at(i + 1);
                    if (next.size() < 2 || next.at(1) != ' ')
                        ++i;
                }
                continue;
            }
            const QByteArrayView score = f.at(7);
            if (score.isEmpty() || (score.at(0) != 'R' && score.at(0) != 'C')) {
                ++status.skipped;
                continue;
            }
            bool ok = false;
            const int value = score.sliced(1).toInt(&ok);
            e.score = ok ? qBound(0, value, 100) : 0;
            e.kind = score.at(0) == 'R' ? EntryKind::Renamed : EntryKind::Copied;
            e.path = text(path);
            if (i + 1 >= tokens.size()) {
                ++status.skipped; // truncated: the original path is missing
                continue;
            }
            e.oldPath = text(tokens.at(++i));
            break;
        }
        case 'u':
            // u <XY> <sub> <m1> <m2> <m3> <mW> <h1> <h2> <h3> <path>
            if (!splitFields(body, 9, f, &path) || !parseXY(f.at(0), e) || !parseSubmodule(f.at(1), e)
                || !parseMode(f.at(2), &e.headMode) || !parseMode(f.at(3), &e.indexMode)
                || !parseMode(f.at(5), &e.worktreeMode) || !isHex(f.at(6)) || path.isEmpty()) {
                ++status.skipped;
                continue;
            }
            e.kind = EntryKind::Unmerged;
            e.path = text(path);
            break;
        case '?':
        case '!':
            if (body.isEmpty()) {
                ++status.skipped;
                continue;
            }
            e.kind = line.at(0) == '?' ? EntryKind::Untracked : EntryKind::Ignored;
            e.staged = e.unstaged = line.at(0);
            e.path = text(body);
            break;
        default: ++status.skipped; continue;
        }
        status.entries.append(e);
    }
    return status;
}

Change changeOf(const StatusEntry &entry)
{
    switch (entry.kind) {
    case EntryKind::Unmerged: return Change::Conflicted;
    case EntryKind::Untracked: return Change::Untracked;
    case EntryKind::Ignored: return Change::Ignored;
    case EntryKind::Renamed: return Change::Renamed;
    case EntryKind::Copied: return Change::Added;
    case EntryKind::Ordinary: break;
    }
    if (entry.staged == 'U' || entry.unstaged == 'U')
        return Change::Conflicted;
    if (entry.staged == 'D' || entry.unstaged == 'D')
        return Change::Deleted;
    if (entry.staged == 'A')
        return Change::Added;
    return Change::Modified;
}

QString changeName(Change change)
{
    switch (change) {
    case Change::Modified: return u"modified"_s;
    case Change::Added: return u"added"_s;
    case Change::Deleted: return u"deleted"_s;
    case Change::Renamed: return u"renamed"_s;
    case Change::Untracked: return u"untracked"_s;
    case Change::Conflicted: return u"conflicted"_s;
    case Change::Ignored: return u"ignored"_s;
    }
    return u"modified"_s;
}

QString changeLetter(Change change)
{
    switch (change) {
    case Change::Modified: return u"M"_s;
    case Change::Added: return u"A"_s;
    case Change::Deleted: return u"D"_s;
    case Change::Renamed: return u"R"_s;
    case Change::Untracked: return u"U"_s;
    case Change::Conflicted: return u"C"_s;
    case Change::Ignored: return u"I"_s;
    }
    return u"M"_s;
}

Change changeOfRawStatus(char status)
{
    switch (status) {
    case 'A':
    case 'C': return Change::Added;
    case 'D': return Change::Deleted;
    case 'R': return Change::Renamed;
    case 'U': return Change::Conflicted;
    default: return Change::Modified;
    }
}

// ---- git diff --numstat / --raw ----------------------------------------------------------------

DiffRecords parseDiff(QByteArrayView data)
{
    DiffRecords out;
    const QList<QByteArrayView> tokens = splitNul(data);
    QList<QByteArrayView> f;
    for (qsizetype i = 0; i < tokens.size(); ++i) {
        const QByteArrayView t = tokens.at(i);
        if (t.isEmpty())
            continue;
        if (t.at(0) == ':') {
            // :<old mode> <new mode> <old oid> <new oid> <status>[<score>] NUL <path> NUL [<path> NUL]
            RawEntry e;
            if (!splitFields(t.sliced(1), 5, f, nullptr) || !parseMode(f.at(0), &e.oldMode)
                || !parseMode(f.at(1), &e.newMode) || !isHex(f.at(2)) || !isHex(f.at(3)) || f.at(4).isEmpty()) {
                continue;
            }
            e.oldOid = text(f.at(2));
            e.newOid = text(f.at(3));
            const QByteArrayView status = f.at(4);
            e.status = status.at(0);
            if (!QByteArrayView("ACDMRTUX").contains(e.status))
                continue;
            if (status.size() > 1) {
                bool ok = false;
                const int score = status.sliced(1).toInt(&ok);
                e.score = ok ? qBound(0, score, 100) : 0;
            }
            const bool twoPaths = e.status == 'R' || e.status == 'C';
            if (i + (twoPaths ? 2 : 1) >= tokens.size())
                break; // truncated
            if (twoPaths) {
                e.oldPath = text(tokens.at(++i));
                e.path = text(tokens.at(++i));
            } else {
                e.path = text(tokens.at(++i));
            }
            if (e.path.isEmpty())
                continue;
            out.raw.append(e);
            continue;
        }
        // <added>\t<deleted>\t<path> NUL, or <added>\t<deleted>\t NUL <old> NUL <new> NUL
        const qsizetype tab1 = t.indexOf('\t');
        const qsizetype tab2 = tab1 < 0 ? -1 : t.indexOf('\t', tab1 + 1);
        if (tab1 <= 0 || tab2 <= tab1 + 1)
            continue;
        const QByteArrayView added = t.sliced(0, tab1);
        const QByteArrayView deleted = t.sliced(tab1 + 1, tab2 - tab1 - 1);
        NumstatEntry e;
        if (added == "-" && deleted == "-") {
            e.binary = true;
        } else {
            bool okA = false;
            bool okD = false;
            e.additions = added.toInt(&okA);
            e.deletions = deleted.toInt(&okD);
            if (!okA || !okD || e.additions < 0 || e.deletions < 0)
                continue;
        }
        const QByteArrayView path = t.sliced(tab2 + 1);
        if (path.isEmpty()) {
            if (i + 2 >= tokens.size())
                break; // truncated
            e.oldPath = text(tokens.at(++i));
            e.path = text(tokens.at(++i));
        } else {
            e.path = text(path);
        }
        if (e.path.isEmpty())
            continue;
        out.numstat.append(e);
    }
    return out;
}

QList<NumstatEntry> parseNumstat(QByteArrayView data)
{
    return parseDiff(data).numstat;
}

// ---- git worktree list -------------------------------------------------------------------------

QList<Worktree> parseWorktrees(QByteArrayView data)
{
    const char separator = data.contains('\0') ? '\0' : '\n';
    QList<Worktree> out;
    bool open = false;
    Worktree w;
    const auto close = [&] {
        if (open && !w.path.isEmpty())
            out.append(w);
        open = false;
        w = Worktree();
    };
    qsizetype start = 0;
    while (start < data.size()) {
        qsizetype end = data.indexOf(separator, start);
        if (end < 0)
            end = data.size();
        QByteArrayView line = data.sliced(start, end - start);
        start = end + 1;
        if (separator == '\n' && line.endsWith('\r'))
            line.chop(1);
        if (line.isEmpty()) {
            close(); // the end of a record
            continue;
        }
        const qsizetype space = line.indexOf(' ');
        const QByteArrayView key = space < 0 ? line : line.sliced(0, space);
        const QByteArrayView value = space < 0 ? QByteArrayView() : line.sliced(space + 1);
        if (key == "worktree") {
            close();
            open = true;
            w.path = text(value);
        } else if (!open) {
            continue;
        } else if (key == "HEAD") {
            w.head = text(value);
        } else if (key == "branch") {
            QString ref = text(value);
            if (ref.startsWith(u"refs/heads/"_s))
                ref = ref.sliced(11);
            w.branch = ref;
        } else if (key == "bare") {
            w.bare = true;
        } else if (key == "detached") {
            w.detached = true;
        } else if (key == "locked") {
            w.locked = true;
            w.lockReason = text(value);
        } else if (key == "prunable") {
            w.prunable = true;
            w.prunableReason = text(value);
        }
    }
    close();
    return out;
}

// ---- git branch --------------------------------------------------------------------------------

namespace {
constexpr int BranchFields = 7;
}

QString branchFormat()
{
    return u"%(refname)%00%(objectname)%00%(upstream:short)%00%(upstream:track,nobracket)%00%(HEAD)%00"
           "%(worktreepath)%00%(symref)%00"_s;
}

QList<Branch> parseBranches(QByteArrayView data)
{
    QList<Branch> out;
    const QList<QByteArrayView> tokens = splitNul(data);
    for (qsizetype i = 0; i + BranchFields <= tokens.size(); i += BranchFields) {
        QByteArrayView ref = tokens.at(i);
        while (!ref.isEmpty() && (ref.at(0) == '\n' || ref.at(0) == '\r'))
            ref = ref.sliced(1);
        const QString refName = text(ref);
        Branch b;
        if (refName.startsWith(u"refs/heads/"_s)) {
            b.name = refName.sliced(11);
        } else if (refName.startsWith(u"refs/remotes/"_s)) {
            b.name = refName.sliced(13);
            b.remote = true;
        } else {
            continue;
        }
        if (b.name.isEmpty() || !tokens.at(i + 6).isEmpty())
            continue; // a symbolic ref (origin/HEAD)
        b.refName = refName;
        b.oid = text(tokens.at(i + 1));
        b.upstream = text(tokens.at(i + 2));
        const QString track = text(tokens.at(i + 3)).trimmed();
        if (track == u"gone"_s) {
            b.upstreamGone = true;
        } else {
            for (const QString &part : track.split(u',', Qt::SkipEmptyParts)) {
                const QString p = part.trimmed();
                bool ok = false;
                if (p.startsWith(u"ahead "_s)) {
                    const int n = p.sliced(6).toInt(&ok);
                    if (ok && n >= 0)
                        b.ahead = n;
                } else if (p.startsWith(u"behind "_s)) {
                    const int n = p.sliced(7).toInt(&ok);
                    if (ok && n >= 0)
                        b.behind = n;
                }
            }
        }
        b.current = tokens.at(i + 4) == "*";
        b.worktreePath = text(tokens.at(i + 5));
        out.append(b);
    }
    return out;
}

// ---- gh ----------------------------------------------------------------------------------------

QString parsePullRequestUrl(QStringView output)
{
    static const QRegularExpression url(u"https?:\\/\\/[^\\s\"'<>]+\\/pull\\/[0-9]+"_s);
    QString last;
    QRegularExpressionMatchIterator it = url.globalMatchView(output);
    while (it.hasNext())
        last = it.next().captured(0);
    return last;
}

// ---- Rules -------------------------------------------------------------------------------------

bool isValidBranchName(QStringView name)
{
    if (name.isEmpty() || name.size() > 250 || name.startsWith(u'-') || name == u"@" || name == u"HEAD")
        return false;
    if (name.startsWith(u'/') || name.endsWith(u'/') || name.endsWith(u'.'))
        return false;
    if (name.contains(u"..") || name.contains(u"@{") || name.contains(u"//"))
        return false;
    for (const QChar c : name) {
        const char16_t u = c.unicode();
        if (u < 0x20 || u == 0x7f || u == u' ' || u == u'~' || u == u'^' || u == u':' || u == u'?' || u == u'*'
            || u == u'[' || u == u'\\')
            return false;
    }
    for (const QStringView part : name.tokenize(u'/')) {
        if (part.isEmpty() || part.startsWith(u'.') || part.endsWith(u".lock"))
            return false;
    }
    return true;
}

bool isValidCheckpointId(QStringView id)
{
    if (id.isEmpty() || id.size() > 200 || id.startsWith(u'/') || id.endsWith(u'/') || id.contains(u"//"))
        return false;
    for (const QChar c : id) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9') || u == u'_'
                        || u == u'-' || u == u'/';
        if (!ok)
            return false;
    }
    return true;
}

bool isSafeRelativePath(QStringView path)
{
    if (path.isEmpty() || path.startsWith(u'/') || path.contains(u'\\') || path.contains(QChar(0)))
        return false;
    // "C:" — a drive on Windows.
    if (path.size() >= 2 && path.at(1) == u':')
        return false;
    for (const QStringView part : path.tokenize(u'/')) {
        if (part.isEmpty() || part == u"." || part == u"..")
            return false;
        // NTFS names .git also as ".git." / ".git " (Windows drops trailing dots and spaces) and by
        // its short name GIT~1 — git's own core.protectNTFS rule.
        QStringView name = part;
        while (!name.isEmpty() && (name.back() == u'.' || name.back() == u' '))
            name.chop(1);
        if (name.compare(u".git", Qt::CaseInsensitive) == 0 || name.compare(u"git~1", Qt::CaseInsensitive) == 0)
            return false;
    }
    return true;
}

bool isSafeRevision(QStringView revision)
{
    if (revision.isEmpty() || revision.size() > 250 || revision.startsWith(u'-'))
        return false;
    for (const QChar c : revision) {
        const char16_t u = c.unicode();
        if (u <= 0x20 || u == 0x7f || u == u':' || u == u'\\')
            return false;
    }
    return true;
}

bool looksBinary(QByteArrayView data)
{
    return data.first(qMin<qsizetype>(data.size(), 8000)).contains('\0');
}

QString subjectOf(QStringView message)
{
    for (const QStringView line : message.tokenize(u'\n')) {
        const QStringView t = line.trimmed();
        if (!t.isEmpty())
            return t.toString();
    }
    return {};
}

} // namespace brulion::gitcli
