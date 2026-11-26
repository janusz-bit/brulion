#include "gitdiffrows.h"

#include "diffhelper.h"

#include <QFileInfo>
#include <QHash>
#include <QMap>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::gitpanel {

namespace {

/// The lines of `text` without their terminators (a last line without one too; nothing after a
/// final newline).
QStringList linesOf(const QString &text)
{
    QStringList out;
    qsizetype start = 0;
    while (start < text.size()) {
        qsizetype end = text.indexOf(u'\n', start);
        if (end < 0)
            end = text.size();
        QString line = text.mid(start, end - start);
        if (line.endsWith(u'\r'))
            line.chop(1);
        out.append(line);
        start = end + 1;
    }
    return out;
}

QString withoutWhitespace(const QString &line)
{
    QString out;
    out.reserve(line.size());
    for (const QChar c : line) {
        if (!c.isSpace())
            out.append(c);
    }
    return out;
}

QVariantList segment(const QString &text, bool changed)
{
    return {QVariantMap{{u"text"_s, text}, {u"changed"_s, changed}}};
}

/// The changed middle of a paired line (after the common start, before the common end) — the
/// rest unchanged. [] when the lines have nothing in common (all changed: no highlight).
void wordChanges(const QString &a, const QString &b, QVariantList *segsA, QVariantList *segsB)
{
    qsizetype prefix = 0;
    const qsizetype shorter = qMin(a.size(), b.size());
    while (prefix < shorter && a.at(prefix) == b.at(prefix))
        ++prefix;
    qsizetype suffix = 0;
    while (suffix < shorter - prefix && a.at(a.size() - 1 - suffix) == b.at(b.size() - 1 - suffix))
        ++suffix;
    // Back to a word's start / end, so whole words are marked.
    const auto isWord = [](QChar c) { return c.isLetterOrNumber() || c == u'_'; };
    while (prefix > 0 && isWord(a.at(prefix - 1)) && ((prefix < a.size() && isWord(a.at(prefix)))
                                                      || (prefix < b.size() && isWord(b.at(prefix)))))
        --prefix;
    while (suffix > 0 && isWord(a.at(a.size() - suffix))
           && ((a.size() - suffix - 1 >= prefix && isWord(a.at(a.size() - suffix - 1)))
               || (b.size() - suffix - 1 >= prefix && isWord(b.at(b.size() - suffix - 1)))))
        --suffix;
    if (prefix == 0 && suffix == 0)
        return;
    const auto parts = [prefix, suffix](const QString &s) {
        QVariantList out;
        if (prefix > 0)
            out += segment(s.left(prefix), false);
        const QString middle = s.mid(prefix, s.size() - prefix - suffix);
        if (!middle.isEmpty())
            out += segment(middle, true);
        if (suffix > 0)
            out += segment(s.right(suffix), false);
        return out;
    };
    *segsA = parts(a);
    *segsB = parts(b);
}

QVariantMap row(const QString &kind, int hunk)
{
    return QVariantMap{{u"kind"_s, kind},
                       {u"hunk"_s, hunk},
                       {u"oldNo"_s, 0},
                       {u"newNo"_s, 0},
                       {u"line"_s, QString()},
                       {u"leftLine"_s, QString()},
                       {u"rightLine"_s, QString()},
                       {u"segs"_s, QVariantList()},
                       {u"leftSegs"_s, QVariantList()},
                       {u"rightSegs"_s, QVariantList()},
                       {u"hiddenLines"_s, 0},
                       {u"oldFirst"_s, 0},
                       {u"oldLast"_s, 0},
                       {u"newFirst"_s, 0},
                       {u"newLast"_s, 0}};
}

void mark(int line, int *first, int *last)
{
    if (*first == 0 || line < *first)
        *first = line;
    if (line > *last)
        *last = line;
}

} // namespace

QVariantList diffRows(const QString &oldText, const QString &newText, const DiffOptions &options)
{
    const QStringList a = linesOf(oldText);
    const QStringList b = linesOf(newText);
    QHash<QString, int> ids;
    const auto intern = [&ids, &options](const QString &line) {
        const QString key = options.ignoreWhitespace ? withoutWhitespace(line) : line;
        const auto it = ids.constFind(key);
        if (it != ids.constEnd())
            return *it;
        const int id = int(ids.size());
        ids.insert(key, id);
        return id;
    };
    QList<int> ka;
    QList<int> kb;
    ka.reserve(a.size());
    kb.reserve(b.size());
    for (const QString &l : a)
        ka.append(intern(l));
    for (const QString &l : b)
        kb.append(intern(l));
    const QList<ui::DiffOp> ops = ui::diffSequences(ka, kb);

    // The changed ops' ranges, grown by the context and joined where they meet.
    struct Range {
        qsizetype from;
        qsizetype to; // (exclusive)
    };
    QList<Range> hunks;
    const qsizetype n = ops.size();
    if (options.context < 0) {
        // Everything as one hunk (no "skip" rows); nothing changed: no rows.
        bool any = false;
        for (const ui::DiffOp &op : ops)
            any = any || op.kind != ui::DiffOp::Equal;
        if (any)
            hunks.append({0, n});
    } else {
        const qsizetype c = options.context;
        for (qsizetype i = 0; i < n; ++i) {
            if (ops.at(i).kind == ui::DiffOp::Equal)
                continue;
            qsizetype j = i;
            while (j < n && ops.at(j).kind != ui::DiffOp::Equal)
                ++j;
            const Range r{qMax<qsizetype>(0, i - c), qMin(n, j + c)};
            if (!hunks.isEmpty() && r.from <= hunks.last().to)
                hunks.last().to = r.to;
            else
                hunks.append(r);
            i = j - 1;
        }
    }

    QVariantList out;
    // The old / new line before op `i` (counting from 1).
    const auto lineBefore = [&ops](qsizetype i, bool oldSide) {
        for (qsizetype k = i - 1; k >= 0; --k) {
            const ui::DiffOp &op = ops.at(k);
            const int index = oldSide ? op.oldIndex : op.newIndex;
            if (index >= 0)
                return index + 1;
        }
        return 0;
    };
    qsizetype shown = 0;
    for (int h = 0; h < hunks.size(); ++h) {
        const Range range = hunks.at(h);
        if (range.from > shown) {
            QVariantMap skip = row(u"skip"_s, -1);
            skip[u"hiddenLines"_s] = int(range.from - shown);
            out.append(skip);
        }
        // The header: starts, counts and the changed lines.
        QVariantMap header = row(u"hunk"_s, h);
        int oldCount = 0;
        int newCount = 0;
        int oldFirst = 0, oldLast = 0, newFirst = 0, newLast = 0;
        int o = lineBefore(range.from, true) + 1;
        int nn = lineBefore(range.from, false) + 1;
        const int oldStart = o;
        const int newStart = nn;
        // (gitcli::Hunk's rule: a replaced line counts at the deleted line on the old side.)
        bool afterDelete = false;
        for (qsizetype i = range.from; i < range.to; ++i) {
            const ui::DiffOp &op = ops.at(i);
            if (op.kind == ui::DiffOp::Equal) {
                ++o;
                ++nn;
                ++oldCount;
                ++newCount;
                afterDelete = false;
            } else if (op.kind == ui::DiffOp::Delete) {
                mark(o, &oldFirst, &oldLast);
                mark(nn, &newFirst, &newLast);
                ++o;
                ++oldCount;
                afterDelete = true;
            } else {
                mark(nn, &newFirst, &newLast);
                mark(afterDelete ? o - 1 : o, &oldFirst, &oldLast);
                ++nn;
                ++newCount;
            }
        }
        // (git's convention: a side with no lines starts at the line before.)
        header[u"line"_s] = u"@@ -%1,%2 +%3,%4 @@"_s.arg(oldCount == 0 ? oldStart - 1 : oldStart)
                                    .arg(oldCount)
                                    .arg(newCount == 0 ? newStart - 1 : newStart)
                                    .arg(newCount);
        header[u"oldFirst"_s] = oldFirst;
        header[u"oldLast"_s] = oldLast;
        header[u"newFirst"_s] = newFirst;
        header[u"newLast"_s] = newLast;
        out.append(header);

        qsizetype i = range.from;
        while (i < range.to) {
            const ui::DiffOp &op = ops.at(i);
            if (op.kind == ui::DiffOp::Equal) {
                QVariantMap r = row(u"equal"_s, h);
                r[u"oldNo"_s] = op.oldIndex + 1;
                r[u"newNo"_s] = op.newIndex + 1;
                if (options.split) {
                    r[u"leftLine"_s] = a.at(op.oldIndex);
                    r[u"rightLine"_s] = b.at(op.newIndex);
                } else {
                    r[u"line"_s] = b.at(op.newIndex);
                }
                out.append(r);
                ++i;
                continue;
            }
            // A block of deletions, then of insertions: paired one by one.
            QList<int> dels;
            QList<int> ins;
            while (i < range.to && ops.at(i).kind == ui::DiffOp::Delete)
                dels.append(ops.at(i++).oldIndex);
            while (i < range.to && ops.at(i).kind == ui::DiffOp::Insert)
                ins.append(ops.at(i++).newIndex);
            const qsizetype pairs = qMin(dels.size(), ins.size());
            QList<QVariantList> delSegs(dels.size());
            QList<QVariantList> insSegs(ins.size());
            for (qsizetype k = 0; k < pairs; ++k)
                wordChanges(a.at(dels.at(k)), b.at(ins.at(k)), &delSegs[k], &insSegs[k]);
            if (options.split) {
                const qsizetype count = qMax(dels.size(), ins.size());
                for (qsizetype k = 0; k < count; ++k) {
                    const bool left = k < dels.size();
                    const bool right = k < ins.size();
                    QVariantMap r = row(left && right ? u"change"_s : left ? u"delete"_s : u"insert"_s, h);
                    if (left) {
                        r[u"oldNo"_s] = dels.at(k) + 1;
                        r[u"leftLine"_s] = a.at(dels.at(k));
                        r[u"leftSegs"_s] = delSegs.at(k);
                    }
                    if (right) {
                        r[u"newNo"_s] = ins.at(k) + 1;
                        r[u"rightLine"_s] = b.at(ins.at(k));
                        r[u"rightSegs"_s] = insSegs.at(k);
                    }
                    out.append(r);
                }
            } else {
                for (qsizetype k = 0; k < dels.size(); ++k) {
                    QVariantMap r = row(u"delete"_s, h);
                    r[u"oldNo"_s] = dels.at(k) + 1;
                    r[u"line"_s] = a.at(dels.at(k));
                    r[u"segs"_s] = delSegs.at(k);
                    out.append(r);
                }
                for (qsizetype k = 0; k < ins.size(); ++k) {
                    QVariantMap r = row(u"insert"_s, h);
                    r[u"newNo"_s] = ins.at(k) + 1;
                    r[u"line"_s] = b.at(ins.at(k));
                    r[u"segs"_s] = insSegs.at(k);
                    out.append(r);
                }
            }
        }
        shown = range.to;
    }
    if (!hunks.isEmpty() && shown < n) {
        QVariantMap skip = row(u"skip"_s, -1);
        skip[u"hiddenLines"_s] = int(n - shown);
        out.append(skip);
    }
    return out;
}

QList<int> findRows(const QVariantList &rows, const QString &query)
{
    QList<int> out;
    if (query.isEmpty())
        return out;
    for (int i = 0; i < rows.size(); ++i) {
        const QVariantMap r = rows.at(i).toMap();
        const QString kind = r.value(u"kind"_s).toString();
        if (kind == u"hunk"_s || kind == u"skip"_s)
            continue;
        if (r.value(u"line"_s).toString().contains(query, Qt::CaseInsensitive)
            || r.value(u"leftLine"_s).toString().contains(query, Qt::CaseInsensitive)
            || r.value(u"rightLine"_s).toString().contains(query, Qt::CaseInsensitive))
            out.append(i);
    }
    return out;
}

int longestLine(const QVariantList &rows)
{
    int longest = 0;
    const auto width = [](const QString &s) {
        int w = 0;
        for (const QChar c : s)
            w += c == u'\t' ? 4 : 1;
        return w;
    };
    for (const QVariant &v : rows) {
        const QVariantMap r = v.toMap();
        if (r.value(u"kind"_s).toString() == u"hunk"_s)
            continue;
        longest = qMax(longest, width(r.value(u"line"_s).toString()));
        longest = qMax(longest, width(r.value(u"leftLine"_s).toString()));
        longest = qMax(longest, width(r.value(u"rightLine"_s).toString()));
    }
    return longest;
}

QString lineHtml(const QString &text, const QVariantList &segs, const QString &changeColor, const QString &query,
                 const QString &findColor, bool wrap)
{
    // Per character: 0 plain, 1 changed, 2 found.
    QList<quint8> marks(text.size(), 0);
    qsizetype at = 0;
    for (const QVariant &v : segs) {
        const QVariantMap s = v.toMap();
        const qsizetype length = s.value(u"text"_s).toString().size();
        if (s.value(u"changed"_s).toBool()) {
            for (qsizetype k = at; k < at + length && k < marks.size(); ++k)
                marks[k] = 1;
        }
        at += length;
    }
    if (!query.isEmpty()) {
        qsizetype from = 0;
        while (true) {
            const qsizetype found = text.indexOf(query, from, Qt::CaseInsensitive);
            if (found < 0)
                break;
            for (qsizetype k = found; k < found + query.size(); ++k)
                marks[k] = 2;
            from = found + qMax<qsizetype>(1, query.size());
        }
    }
    QString out = wrap ? u"<span style=\"white-space:pre-wrap\">"_s : u"<span style=\"white-space:pre\">"_s;
    qsizetype i = 0;
    while (i < text.size()) {
        qsizetype j = i;
        while (j < text.size() && marks.at(j) == marks.at(i))
            ++j;
        const QString piece = text.mid(i, j - i).toHtmlEscaped();
        if (marks.at(i) == 0)
            out += piece;
        else
            out += u"<span style=\"background-color:%1\">"_s.arg((marks.at(i) == 1 ? changeColor : findColor).toHtmlEscaped())
                   + piece + u"</span>"_s;
        i = j;
    }
    out += u"</span>"_s;
    return out;
}

// ---- The tree ----------------------------------------------------------------------------------

namespace {

struct Folder {
    QMap<QString, Folder> folders; // by name
    QList<QVariantMap> files;
    int fileCount = 0;
};

void addFile(Folder &root, const QVariantMap &change)
{
    QString path = change.value(u"path"_s).toString();
    while (path.endsWith(u'/'))
        path.chop(1);
    const QStringList parts = path.split(u'/', Qt::SkipEmptyParts);
    Folder *f = &root;
    ++f->fileCount;
    for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
        f = &f->folders[parts.at(i)];
        ++f->fileCount;
    }
    f->files.append(change);
}

void emitFolder(const Folder &folder, const QString &prefix, int depth, const QStringList &collapsed, QVariantList &out)
{
    for (auto it = folder.folders.constBegin(); it != folder.folders.constEnd(); ++it) {
        // A chain of folders with only one folder in each: one row.
        QString name = it.key();
        const Folder *f = &it.value();
        while (f->files.isEmpty() && f->folders.size() == 1) {
            name += u'/' + f->folders.firstKey();
            f = &f->folders.first();
        }
        const QString path = prefix.isEmpty() ? name : prefix + u'/' + name;
        const bool folded = collapsed.contains(path);
        QVariantMap r{{u"kind"_s, u"folder"_s},  {u"path"_s, path},      {u"oldPath"_s, QString()},
                      {u"status"_s, QString()},  {u"letter"_s, QString()}, {u"staged"_s, false},
                      {u"unstaged"_s, false},    {u"additions"_s, -1},    {u"deletions"_s, -1},
                      {u"binary"_s, false},      {u"name"_s, name},       {u"dir"_s, prefix},
                      {u"depth"_s, depth},       {u"collapsed"_s, folded}, {u"fileCount"_s, f->fileCount}};
        out.append(r);
        if (!folded)
            emitFolder(*f, path, depth + 1, collapsed, out);
    }
    QList<QVariantMap> files = folder.files;
    std::sort(files.begin(), files.end(), [](const QVariantMap &x, const QVariantMap &y) {
        return x.value(u"name"_s).toString().compare(y.value(u"name"_s).toString(), Qt::CaseInsensitive) < 0;
    });
    for (QVariantMap f : std::as_const(files)) {
        f[u"kind"_s] = u"file"_s;
        f[u"depth"_s] = depth;
        f[u"collapsed"_s] = false;
        f[u"fileCount"_s] = 0;
        out.append(f);
    }
}

} // namespace

QVariantList treeRows(const QVariantList &changes, const QStringList &collapsed)
{
    Folder root;
    for (const QVariant &v : changes)
        addFile(root, v.toMap());
    QVariantList out;
    emitFolder(root, QString(), 0, collapsed, out);
    return out;
}

// ---- Review comments ---------------------------------------------------------------------------

QString languageOf(const QString &path)
{
    static const QHash<QString, QString> bySuffix{
            {u"c"_s, u"c"_s},          {u"h"_s, u"cpp"_s},        {u"hpp"_s, u"cpp"_s},       {u"cpp"_s, u"cpp"_s},
            {u"cc"_s, u"cpp"_s},       {u"cxx"_s, u"cpp"_s},      {u"qml"_s, u"qml"_s},       {u"js"_s, u"javascript"_s},
            {u"mjs"_s, u"javascript"_s}, {u"jsx"_s, u"jsx"_s},     {u"ts"_s, u"typescript"_s}, {u"tsx"_s, u"tsx"_s},
            {u"py"_s, u"python"_s},    {u"rs"_s, u"rust"_s},      {u"go"_s, u"go"_s},         {u"java"_s, u"java"_s},
            {u"kt"_s, u"kotlin"_s},    {u"kts"_s, u"kotlin"_s},   {u"swift"_s, u"swift"_s},   {u"cs"_s, u"csharp"_s},
            {u"rb"_s, u"ruby"_s},      {u"php"_s, u"php"_s},      {u"lua"_s, u"lua"_s},       {u"luau"_s, u"luau"_s},
            {u"sh"_s, u"bash"_s},      {u"bash"_s, u"bash"_s},    {u"zsh"_s, u"bash"_s},      {u"nix"_s, u"nix"_s},
            {u"cmake"_s, u"cmake"_s},  {u"json"_s, u"json"_s},    {u"yaml"_s, u"yaml"_s},     {u"yml"_s, u"yaml"_s},
            {u"toml"_s, u"toml"_s},    {u"xml"_s, u"xml"_s},      {u"html"_s, u"html"_s},     {u"css"_s, u"css"_s},
            {u"scss"_s, u"scss"_s},    {u"sql"_s, u"sql"_s},      {u"md"_s, u"markdown"_s},   {u"markdown"_s, u"markdown"_s},
            {u"dart"_s, u"dart"_s},    {u"zig"_s, u"zig"_s},      {u"hs"_s, u"haskell"_s},    {u"ex"_s, u"elixir"_s},
            {u"vue"_s, u"vue"_s},      {u"svelte"_s, u"svelte"_s}, {u"ini"_s, u"ini"_s},      {u"diff"_s, u"diff"_s},
    };
    const QString name = QFileInfo(path).fileName();
    if (name == u"CMakeLists.txt"_s)
        return u"cmake"_s;
    if (name == u"Makefile"_s)
        return u"makefile"_s;
    if (name == u"Dockerfile"_s)
        return u"dockerfile"_s;
    return bySuffix.value(QFileInfo(path).suffix().toLower());
}

QString reviewComment(const QString &path, const QVariantList &rows, int from, int to, const QString &comment)
{
    if (from > to)
        std::swap(from, to);
    from = qMax(0, from);
    to = qMin(int(rows.size()) - 1, to);
    QStringList newLines;
    QStringList diffLines;
    int newFirst = 0, newLast = 0, oldFirst = 0, oldLast = 0;
    bool removed = false;
    bool added = false;
    for (int i = from; i <= to; ++i) {
        const QVariantMap r = rows.at(i).toMap();
        const QString kind = r.value(u"kind"_s).toString();
        const int oldNo = r.value(u"oldNo"_s).toInt();
        const int newNo = r.value(u"newNo"_s).toInt();
        if (kind == u"hunk"_s || kind == u"skip"_s)
            continue;
        if (newNo > 0)
            mark(newNo, &newFirst, &newLast);
        if (oldNo > 0)
            mark(oldNo, &oldFirst, &oldLast);
        const bool split = r.value(u"line"_s).toString().isEmpty()
                           && (!r.value(u"leftLine"_s).toString().isEmpty() || !r.value(u"rightLine"_s).toString().isEmpty());
        const QString oldText = split ? r.value(u"leftLine"_s).toString() : r.value(u"line"_s).toString();
        const QString newText = split ? r.value(u"rightLine"_s).toString() : r.value(u"line"_s).toString();
        if (kind == u"equal"_s) {
            newLines << newText;
            diffLines << u' ' + newText;
        } else if (kind == u"delete"_s) {
            removed = true;
            diffLines << u'-' + oldText;
        } else if (kind == u"insert"_s) {
            added = true;
            newLines << newText;
            diffLines << u'+' + newText;
        } else if (kind == u"change"_s) {
            removed = added = true;
            diffLines << u'-' + oldText;
            newLines << newText;
            diffLines << u'+' + newText;
        }
    }
    const QString where = newFirst > 0 ? (newFirst == newLast ? u"line %1"_s.arg(newFirst)
                                                              : u"lines %1–%2"_s.arg(newFirst).arg(newLast))
                          : oldFirst > 0 ? (oldFirst == oldLast ? u"old line %1"_s.arg(oldFirst)
                                                                : u"old lines %1–%2"_s.arg(oldFirst).arg(oldLast))
                                         : QString();
    QString out = where.isEmpty() ? u"`%1`:\n"_s.arg(path) : u"`%1` %2:\n"_s.arg(path, where);
    const bool asDiff = removed;
    const QStringList &body = asDiff ? diffLines : newLines;
    Q_UNUSED(added)
    // A fence longer than any run of backticks in the lines.
    int ticks = 3;
    for (const QString &l : body) {
        int run = 0;
        for (const QChar c : l) {
            run = c == u'`' ? run + 1 : 0;
            ticks = qMax(ticks, run + 1);
        }
    }
    const QString fence(ticks, u'`');
    out += fence + (asDiff ? u"diff"_s : languageOf(path)) + u'\n' + body.join(u'\n') + u'\n' + fence + u'\n';
    const QString note = comment.trimmed();
    if (!note.isEmpty())
        out += note + u'\n';
    return out;
}

} // namespace brulion::gitpanel
