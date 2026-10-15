#include "markdown/notemode.h"

#include "markdown/frontmatter.h"
#include "markdown/lines_p.h"
#include "markdown/structure_p.h"
#include "text/text.h"

#include <QList>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace {

constexpr char16_t Bom = 0xfeff;

bool startsWithBom(QStringView content)
{
    return !content.isEmpty() && content[0].unicode() == Bom;
}

NoteMode sanitized(NoteMode mode, NoteMode fallback)
{
    return mode == NoteMode::Outline || mode == NoteMode::Document ? mode : fallback;
}

QString entryLine(NoteMode mode)
{
    return NoteModeKey.toString() + u": "_s + noteModeName(mode);
}

std::optional<NoteMode> modeOfValue(const QVariant &value)
{
    if (value.typeId() != QMetaType::QString)
        return std::nullopt;
    return parseNoteMode(value.toString());
}

bool isSeqItem(QStringView line)
{
    const QStringView t = trimEndBlanks(line);
    return t == u"-" || t.startsWith(u"- ") || t.startsWith(u"-\t");
}

/// Document lines [first, last) of one top-level `brulion` entry of the frontmatter.
struct Entry {
    int first = 0;
    int last = 0;
};

/// The top-level `brulion` entries between the opening delimiter (line 0) and the closing one
/// (`endLine`), the way parseYaml() reads them: a key line starts at column 0 and is read with the
/// YAML parser itself (quoted keys, `key :`); the entry continues with indented lines, `- ` items
/// at column 0 (a block sequence value) and a flow collection's closing bracket at column 0, blank
/// lines only when more of the entry follows.
QList<Entry> findEntries(const QList<text::Line> &lines, int endLine)
{
    QList<Entry> out;
    for (int i = 1; i < endLine; ++i) {
        const QStringView t = lines[i].text;
        if (isBlank(t) || isSpaceOrTab(t[0]) || t[0] == u'#' || isSeqItem(t))
            continue;
        QStringList keys;
        parseYaml(t, &keys);
        if (!keys.contains(NoteModeKey))
            continue;
        int last = i + 1;
        for (int j = i + 1; j < endLine; ++j) {
            const QStringView n = lines[j].text;
            if (isBlank(n))
                continue;
            if (isSpaceOrTab(n[0]) || isSeqItem(n) || n[0] == u']' || n[0] == u'}')
                last = j + 1;
            else
                break;
        }
        out.append(Entry{i, last});
        i = last - 1;
    }
    return out;
}

/// `lines` joined, with lines in `entries` dropped and `replacement` (with its terminator) put at
/// line `at` (an entry start, or the closing delimiter for an insertion).
QString rebuild(const QList<text::Line> &lines, const QList<Entry> &entries, int at, const QString &replacement)
{
    QString out;
    qsizetype next = 0; // index into entries
    for (int i = 0; i < lines.size(); ++i) {
        if (i == at)
            out += replacement;
        if (next < entries.size() && i >= entries[next].first) {
            if (i + 1 >= entries[next].last)
                ++next;
            continue;
        }
        out += lines[i].text + lines[i].eol;
    }
    return out;
}

/// The frontmatter mapping without the mode key, in a canonical form (stringifyYaml) so that
/// values compare reliably (also NaN or nested lists).
QString otherKeys(QStringView yaml)
{
    QVariantMap data = parseYaml(yaml);
    data.remove(NoteModeKey.toString());
    return stringifyYaml(data);
}

/// Checks an edit of `before` (with frontmatter `fm`): the body is byte-identical, every other key
/// parses to the same value, and the key reads back as `mode` (absent for nullopt).
bool verifiedEdit(QStringView before, const FrontmatterBlock &fm, const QString &after, std::optional<NoteMode> mode)
{
    const QStringView body = before.sliced(fm.bodyStart);
    const auto edited = findFrontmatter(after);
    if (!edited) {
        // The whole block was removed: only legal when removing the key left nothing else.
        const QString bom = startsWithBom(before) ? QString(QChar(Bom)) : QString();
        return !mode && after == bom + body && otherKeys(fm.yaml).isEmpty();
    }
    if (QStringView(after).sliced(edited->bodyStart) != body || otherKeys(edited->yaml) != otherKeys(fm.yaml))
        return false;
    const QVariantMap data = parseYaml(edited->yaml);
    const QString key = NoteModeKey.toString();
    return mode ? modeOfValue(data.value(key)) == mode : !data.contains(key);
}

} // namespace

QString noteModeName(NoteMode mode)
{
    return mode == NoteMode::Outline ? u"outline"_s : u"document"_s;
}

std::optional<NoteMode> parseNoteMode(QStringView name)
{
    const QStringView t = name.trimmed();
    if (t.compare(u"outline", Qt::CaseInsensitive) == 0)
        return NoteMode::Outline;
    if (t.compare(u"document", Qt::CaseInsensitive) == 0)
        return NoteMode::Document;
    return std::nullopt;
}

std::optional<NoteMode> frontmatterNoteMode(QStringView content)
{
    const auto fm = findFrontmatter(content);
    if (!fm)
        return std::nullopt;
    return modeOfValue(parseYaml(fm->yaml).value(NoteModeKey.toString()));
}

std::optional<NoteMode> heuristicNoteMode(QStringView content)
{
    // parseStructure() gives exactly the top-level blocks of parseNote() (one block per root
    // node, same kinds) without its inline work.
    const Structure st = parseStructure(content.toString());
    int blocks = 0;
    int lists = 0;
    for (int n : st.roots) {
        const BlockKind kind = st.nodes[n].kind;
        if (kind == BlockKind::Frontmatter)
            continue; // YAML frontmatter and Logseq page properties
        ++blocks;
        if (kind == BlockKind::ListItem)
            ++lists;
    }
    if (blocks < 2)
        return std::nullopt;
    if (lists * 10 >= blocks * 7)
        return NoteMode::Outline;
    if (lists * 10 <= blocks * 3)
        return NoteMode::Document;
    return std::nullopt;
}

ResolvedNoteMode resolveNoteMode(QStringView content, bool isJournal, const VaultConfig &config)
{
    if (const auto mode = frontmatterNoteMode(content))
        return {*mode, NoteModeSource::Frontmatter};
    if (isJournal)
        return {sanitized(config.journalMode, NoteMode::Outline), NoteModeSource::JournalDefault};
    if (const auto mode = heuristicNoteMode(content))
        return {*mode, NoteModeSource::Heuristic};
    return {sanitized(config.pageMode, NoteMode::Document), NoteModeSource::PageDefault};
}

QString setNoteModeKey(QStringView content, std::optional<NoteMode> mode)
{
    if (mode)
        mode = sanitized(*mode, NoteMode::Document);
    const auto fm = findFrontmatter(content);
    if (!fm) {
        if (!mode)
            return content.toString();
        const QString eol = text::detectEol(content);
        const bool bom = startsWithBom(content);
        QString out = bom ? QString(QChar(Bom)) : QString();
        out += u"---"_s + eol + entryLine(*mode) + eol + u"---"_s + eol;
        out += content.sliced(bom ? 1 : 0);
        return out;
    }
    if (mode && modeOfValue(parseYaml(fm->yaml).value(NoteModeKey.toString())) == mode)
        return content.toString();

    const QList<text::Line> lines = text::splitLines(content);
    const QList<Entry> entries = findEntries(lines, fm->endLine);
    QString edited;
    if (mode) {
        const QString line = entryLine(*mode);
        if (!entries.isEmpty()) {
            // Replace the first entry in place (keeping its line ending), drop duplicates.
            const int at = entries.first().first;
            edited = rebuild(lines, entries, at, line + lines[at].eol);
        }
        if (entries.isEmpty() || !verifiedEdit(content, *fm, edited, mode)) {
            // A new last entry before the closing delimiter, with the line ending of the line
            // above it (always terminated) — only while the mapping has no `brulion` key: a
            // second one (a key the line scan cannot place, e.g. in an indented root mapping)
            // would leave YAML that other tools refuse (duplicate keys, mixed indentation).
            if (parseYaml(fm->yaml).contains(NoteModeKey.toString()))
                return content.toString();
            edited = rebuild(lines, {}, fm->endLine, line + lines[fm->endLine - 1].eol);
        }
    } else {
        if (entries.isEmpty())
            return content.toString();
        edited = rebuild(lines, entries, -1, QString());
        bool empty = true;
        for (int i = 1; i < fm->endLine && empty; ++i) {
            const bool removed = std::any_of(entries.begin(), entries.end(),
                                             [i](const Entry &e) { return i >= e.first && i < e.last; });
            empty = removed || isBlank(lines[i].text);
        }
        if (empty) {
            // Nothing but blank lines left: drop the whole block — unless the body would then
            // start a frontmatter of its own (an empty block is kept in that case).
            const QString bom = startsWithBom(content) ? QString(QChar(Bom)) : QString();
            const QString whole = bom + content.sliced(fm->bodyStart);
            if (verifiedEdit(content, *fm, whole, mode))
                return whole;
        }
    }
    return verifiedEdit(content, *fm, edited, mode) ? edited : content.toString();
}

QString withNoteModeChoice(QStringView content, NoteMode chosen, bool isJournal, const VaultConfig &config)
{
    if (resolveNoteMode(content, isJournal, config).mode == chosen)
        return content.toString();
    return setNoteModeKey(content, chosen);
}

} // namespace brulion::markdown
