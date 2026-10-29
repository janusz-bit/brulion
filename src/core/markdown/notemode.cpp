#include "markdown/notemode.h"

#include "markdown/frontmatter.h"
#include "markdown/frontmatter_p.h"
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
        const QString bom = startsWithBom(before) ? QString(QChar(ByteOrderMark)) : QString();
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
        QString out = bom ? QString(QChar(ByteOrderMark)) : QString();
        out += u"---"_s + eol + entryLine(*mode) + eol + u"---"_s + eol;
        out += content.sliced(bom ? 1 : 0);
        return out;
    }
    if (mode && modeOfValue(parseYaml(fm->yaml).value(NoteModeKey.toString())) == mode)
        return content.toString();

    const QList<text::Line> lines = text::splitLines(content);
    const QList<FrontmatterEntry> entries = frontmatterEntries(lines, fm->endLine, NoteModeKey.toString());
    QString edited;
    if (mode) {
        const QString line = entryLine(*mode);
        if (!entries.isEmpty()) {
            // Replace the first entry in place (keeping its line ending), drop duplicates.
            const int at = entries.first().first;
            edited = rebuildLines(lines, entries, at, line + lines[at].eol);
        }
        if (entries.isEmpty() || !verifiedEdit(content, *fm, edited, mode)) {
            // A new last entry before the closing delimiter, with the line ending of the line
            // above it (always terminated) — only while the mapping has no `brulion` key: a
            // second one (a key the line scan cannot place, e.g. in an indented root mapping)
            // would leave YAML that other tools refuse (duplicate keys, mixed indentation).
            // Nor into frontmatter that is no mapping (isMapping()).
            if (parseYaml(fm->yaml).contains(NoteModeKey.toString()) || !isYamlMapping(fm->yaml))
                return content.toString();
            edited = rebuildLines(lines, {}, fm->endLine, line + lines[fm->endLine - 1].eol);
        }
    } else {
        if (entries.isEmpty())
            return content.toString();
        edited = rebuildLines(lines, entries, -1, QString());
        bool empty = true;
        for (int i = 1; i < fm->endLine && empty; ++i) {
            const bool removed = std::any_of(entries.begin(), entries.end(),
                                             [i](const FrontmatterEntry &e) { return i >= e.first && i < e.last; });
            empty = removed || isBlank(lines[i].text);
        }
        if (empty) {
            // Nothing but blank lines left: drop the whole block — unless the body would then
            // start a frontmatter of its own (an empty block is kept in that case).
            const QString bom = startsWithBom(content) ? QString(QChar(ByteOrderMark)) : QString();
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
