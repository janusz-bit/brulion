// Note modes (docs/SYNTAX.md §4 "Tryb notatki / Note mode"): names, the `brulion` frontmatter key,
// the content heuristic, resolution order and the pure key edit (byte preservation, round trips,
// malformed frontmatter).
#include "markdown/frontmatter.h"
#include "markdown/notemode.h"
#include "markdown/parse.h"

#include <QTest>

#include <optional>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

/// -1 = none, 0 = outline, 1 = document (readable data columns and failure messages).
int code(std::optional<NoteMode> mode)
{
    return mode ? int(*mode) : -1;
}

std::optional<NoteMode> modeOf(int code)
{
    return code < 0 ? std::nullopt : std::optional<NoteMode>(static_cast<NoteMode>(code));
}

constexpr int None = -1;
constexpr int Outline = int(NoteMode::Outline);
constexpr int Document = int(NoteMode::Document);

/// A Logseq page: page properties, bullets with children, properties, markers, block refs.
const QString logseqPage = u"title:: Remont mieszkania\n"
                           "tags:: projekt, dom\n"
                           "\n"
                           "- Plan prac na jesień\n"
                           "  collapsed:: true\n"
                           "\t- Łazienka: płytki, fugi\n"
                           "\t- Kuchnia\n"
                           "- TODO Kupić farbę\n"
                           "  SCHEDULED: <2026-10-20 Tue>\n"
                           "- Notatka ((64f1a2b3-0000-4000-8000-000000000001))\n"_s;

/// An Obsidian note: frontmatter, headings, paragraphs, a short task list, a callout, a table.
const QString obsidianNote = u"---\n"
                             "tags: [projekt]\n"
                             "aliases: [Remont]\n"
                             "---\n"
                             "# Remont mieszkania\n"
                             "\n"
                             "Plan prac na jesień. Zaczynamy od łazienki, potem kuchnia.\n"
                             "\n"
                             "## Zadania\n"
                             "\n"
                             "- [ ] Kupić płytki 📅 2026-10-20\n"
                             "- [ ] Zamówić hydraulika\n"
                             "\n"
                             "> [!tip] Pamiętaj\n"
                             "> Zmierzyć ściany przed zakupem.\n"
                             "\n"
                             "| Pokój | Koszt |\n"
                             "|---|---|\n"
                             "| Łazienka | 5000 |\n"_s;

QString repeated(const QString &line, int n)
{
    QString out;
    for (int i = 0; i < n; ++i)
        out += line.arg(i + 1);
    return out;
}

/// Resolution step 3 computed from parseNote() exactly as the spec words it (the implementation
/// uses the cheaper block structure; both must agree).
std::optional<NoteMode> specHeuristic(const QString &content)
{
    const ParsedNote note = parseNote(u"Strona.md"_s, content);
    int blocks = 0;
    int lists = 0;
    for (const Block &b : note.blocks) {
        if (b.parent >= 0 || b.kind == BlockKind::Frontmatter)
            continue;
        ++blocks;
        lists += b.kind == BlockKind::ListItem ? 1 : 0;
    }
    if (blocks < 2)
        return std::nullopt;
    const double share = double(lists) / blocks;
    if (share >= 0.7)
        return NoteMode::Outline;
    if (share <= 0.3)
        return NoteMode::Document;
    return std::nullopt;
}

/// The note without its frontmatter block (and without a BOM).
QString bodyOf(const QString &content)
{
    if (const auto fm = findFrontmatter(content))
        return content.sliced(fm->bodyStart);
    return !content.isEmpty() && content[0].unicode() == 0xfeff ? content.sliced(1) : content;
}

/// The frontmatter mapping without the mode key.
QVariantMap otherKeys(const QString &content)
{
    QVariantMap data = parseFrontmatter(content).data;
    data.remove(u"brulion"_s);
    return data;
}

} // namespace

class TestNoteMode : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- names ---------------------------------------------------------------------------
    void names()
    {
        QCOMPARE(NoteModeKey.toString(), u"brulion"_s);
        QCOMPARE(noteModeName(NoteMode::Outline), u"outline"_s);
        QCOMPARE(noteModeName(NoteMode::Document), u"document"_s);
        for (NoteMode m : {NoteMode::Outline, NoteMode::Document})
            QCOMPARE(code(parseNoteMode(noteModeName(m))), int(m));
        // The VaultConfig defaults of the spec.
        const VaultConfig config;
        QCOMPARE(config.journalMode, NoteMode::Outline);
        QCOMPARE(config.pageMode, NoteMode::Document);
    }

    void parseName_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("expected");
        QTest::newRow("outline") << u"outline"_s << Outline;
        QTest::newRow("document") << u"document"_s << Document;
        QTest::newRow("upper case") << u"OUTLINE"_s << Outline;
        QTest::newRow("capitalised") << u"Document"_s << Document;
        QTest::newRow("spaces") << u"  document\t"_s << Document;
        QTest::newRow("empty") << QString() << None;
        QTest::newRow("blank") << u"   "_s << None;
        QTest::newRow("plural") << u"outlines"_s << None;
        QTest::newRow("prefix") << u"doc"_s << None;
        QTest::newRow("polish dokument") << u"dokument"_s << None;
        QTest::newRow("polish konspekt") << u"konspekt"_s << None;
        QTest::newRow("inner space") << u"out line"_s << None;
        QTest::newRow("kanban") << u"board"_s << None;
    }
    void parseName()
    {
        QFETCH(QString, name);
        QFETCH(int, expected);
        QCOMPARE(code(parseNoteMode(name)), expected);
    }

    // ---- frontmatter key -----------------------------------------------------------------
    void frontmatterKey_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("expected");
        QTest::newRow("no frontmatter") << u"# Tytuł\n\nTreść\n"_s << None;
        QTest::newRow("empty") << QString() << None;
        QTest::newRow("other keys") << u"---\ntitle: Dom\n---\n"_s << None;
        QTest::newRow("document") << u"---\nbrulion: document\n---\nTreść\n"_s << Document;
        QTest::newRow("outline") << u"---\ntitle: Łódź\nbrulion: outline\n---\n"_s << Outline;
        QTest::newRow("double quoted") << u"---\nbrulion: \"outline\"\n---\n"_s << Outline;
        QTest::newRow("single quoted") << u"---\nbrulion: 'document'\n---\n"_s << Document;
        QTest::newRow("upper case value") << u"---\nbrulion: DOCUMENT\n---\n"_s << Document;
        QTest::newRow("comment after value") << u"---\nbrulion: outline # wybrane ręcznie\n---\n"_s << Outline;
        QTest::newRow("quoted key") << u"---\n\"brulion\": document\n---\n"_s << Document;
        QTest::newRow("crlf") << u"---\r\nbrulion: outline\r\n---\r\n- a\r\n"_s << Outline;
        QTest::newRow("bom") << u"\uFEFF---\nbrulion: document\n---\n"_s << Document;
        QTest::newRow("dots close") << u"---\nbrulion: outline\n...\n"_s << Outline;
        QTest::newRow("unknown value") << u"---\nbrulion: kanban\n---\n"_s << None;
        QTest::newRow("polish value") << u"---\nbrulion: dokument\n---\n"_s << None;
        QTest::newRow("bool value") << u"---\nbrulion: true\n---\n"_s << None;
        QTest::newRow("number value") << u"---\nbrulion: 1\n---\n"_s << None;
        QTest::newRow("null value") << u"---\nbrulion:\n---\n"_s << None;
        QTest::newRow("list value") << u"---\nbrulion: [document]\n---\n"_s << None;
        QTest::newRow("map value") << u"---\nbrulion:\n  mode: document\n---\n"_s << None;
        QTest::newRow("key case matters") << u"---\nBrulion: document\n---\n"_s << None;
        QTest::newRow("nested key") << u"---\nmeta:\n  brulion: document\n---\n"_s << None;
        QTest::newRow("duplicate: last wins") << u"---\nbrulion: document\nbrulion: outline\n---\n"_s << Outline;
        QTest::newRow("in the body") << u"# Tytuł\n\nbrulion: document\n"_s << None;
        QTest::newRow("unclosed") << u"---\nbrulion: document\n"_s << None;
        QTest::newRow("not at the start") << u"\n---\nbrulion: document\n---\n"_s << None;
        QTest::newRow("logseq property") << u"brulion:: document\n\n- a\n"_s << None;
    }
    void frontmatterKey()
    {
        QFETCH(QString, content);
        QFETCH(int, expected);
        QCOMPARE(code(frontmatterNoteMode(content)), expected);
    }

    // ---- heuristic -----------------------------------------------------------------------
    void heuristic_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("expected");
        QTest::newRow("empty") << QString() << None;
        QTest::newRow("blank lines") << u"\n\n  \n\t\n"_s << None;
        QTest::newRow("frontmatter only") << u"---\ntitle: x\n---\n"_s << None;
        QTest::newRow("one paragraph") << u"Zażółć gęślą jaźń.\n"_s << None;
        QTest::newRow("one item with children") << u"- a\n  - b\n  - c\n  - d\n"_s << None;
        QTest::newRow("two items") << u"- a\n- b\n"_s << Outline;
        QTest::newRow("two paragraphs") << u"Pierwszy akapit.\n\nDrugi akapit.\n"_s << Document;
        QTest::newRow("logseq page") << logseqPage << Outline;
        QTest::newRow("logseq crlf") << QString(logseqPage).replace(u"\n"_s, u"\r\n"_s) << Outline;
        QTest::newRow("logseq bom") << u"\uFEFF"_s + logseqPage << Outline;
        QTest::newRow("obsidian note") << obsidianNote << Document;
        QTest::newRow("obsidian crlf") << QString(obsidianNote).replace(u"\n"_s, u"\r\n"_s) << Document;
        QTest::newRow("exactly 70 %") << repeated(u"- punkt %1\n"_s, 7) + repeated(u"\nAkapit %1\n"_s, 3) << Outline;
        QTest::newRow("two of three") << u"- a\n- b\n\nAkapit\n"_s << None;
        QTest::newRow("exactly 30 %") << repeated(u"- punkt %1\n"_s, 3) + repeated(u"\nAkapit %1\n"_s, 7) << Document;
        QTest::newRow("one of three") << u"- a\n\nAkapit\n\nDrugi\n"_s << None;
        QTest::newRow("half: heading is not a list") << u"# Nagłówek\n- punkt\n"_s << None;
        QTest::newRow("headings are not lists") << u"# A\n## B\n### C\n- x\n"_s << Document;
        QTest::newRow("children are not counted") << u"- a\n  - b\n  - c\n  - d\n  - e\n\nAkapit\n"_s << None;
        QTest::newRow("numbered, tasks, markers") << u"1. a\n2. b\n- [ ] c\n* d\n+ e\n"_s << Outline;
        QTest::newRow("code table quote rule html")
                << u"```\nkod\n```\n\n| a |\n|---|\n| 1 |\n\n> cytat\n\n---\n\n<div>x</div>\n"_s << Document;
        QTest::newRow("calc and query blocks") << u"```calc\n2 + 2\n```\n\n```query\ntype:tasks\n```\n"_s << Document;
        QTest::newRow("lists inside a callout") << u"> [!note] Notatka\n> - jeden\n> - dwa\n\nAkapit\n"_s << Document;
        QTest::newRow("page properties excluded") << u"alias:: Dom\ntags:: x\n\n- a\n- b\n"_s << Outline;
        QTest::newRow("page properties and one block") << u"alias:: Dom\n\nAkapit\n"_s << None;
        QTest::newRow("frontmatter excluded") << u"---\na: 1\nb: 2\n---\n- a\n"_s << None;
        QTest::newRow("frontmatter and two items") << u"---\na: 1\n---\n- a\n- b\n"_s << Outline;
        QTest::newRow("obsidian shopping list")
                << u"# Lista zakupów\n\n- mleko\n- chleb\n- masło\n- jajka\n- ser\n"_s << Outline;
        QTest::newRow("journal-like obsidian day")
                << u"## Rano\n\nKawa i plan dnia.\n\n## Wieczór\n\n- spacer\n"_s << Document;
    }
    void heuristic()
    {
        QFETCH(QString, content);
        QFETCH(int, expected);
        QCOMPARE(code(heuristicNoteMode(content)), expected);
        QCOMPARE(code(specHeuristic(content)), expected); // the spec's wording, via parseNote()
    }

    void heuristicLargeNote()
    {
        const QString big = repeated(u"- punkt %1 z ąćęłńóśźż\n  - dziecko\n"_s, 20000);
        QCOMPARE(code(heuristicNoteMode(big)), Outline);
        QCOMPARE(code(resolveNoteMode(big, false, VaultConfig()).mode), Outline);
    }

    // ---- resolution ----------------------------------------------------------------------
    void resolve_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("journal");
        QTest::addColumn<int>("journalMode");
        QTest::addColumn<int>("pageMode");
        QTest::addColumn<int>("mode");
        QTest::addColumn<int>("source");
        const int fm = int(NoteModeSource::Frontmatter);
        const int jd = int(NoteModeSource::JournalDefault);
        const int he = int(NoteModeSource::Heuristic);
        const int pd = int(NoteModeSource::PageDefault);
        const QString docKey = u"---\nbrulion: document\n---\n"_s;
        const QString outKey = u"---\nbrulion: outline\n---\n"_s;
        QTest::newRow("key beats journal default")
                << docKey + u"- a\n- b\n"_s << true << Outline << Document << Document << fm;
        QTest::newRow("key beats heuristic")
                << outKey + u"# H\n\nAkapit\n\nDrugi\n"_s << false << Outline << Document << Outline << fm;
        QTest::newRow("key beats page default") << outKey << false << Outline << Document << Outline << fm;
        QTest::newRow("bom crlf key") << u"\uFEFF---\r\nbrulion: OUTLINE\r\n---\r\nTekst\r\n"_s << false << Outline
                                      << Document << Outline << fm;
        QTest::newRow("invalid key, journal")
                << u"---\nbrulion: kanban\n---\n# H\n\nAkapit\n"_s << true << Outline << Document << Outline << jd;
        QTest::newRow("invalid key, heuristic")
                << u"---\nbrulion: 42\n---\n- a\n- b\n"_s << false << Outline << Document << Outline << he;
        QTest::newRow("journal default") << u"- rano\n- wieczór\n"_s << true << Outline << Document << Outline << jd;
        QTest::newRow("journal configured as document")
                << u"- rano\n"_s << true << Document << Document << Document << jd;
        QTest::newRow("journal ignores heuristic") << obsidianNote << true << Outline << Document << Outline << jd;
        QTest::newRow("empty journal") << QString() << true << Outline << Outline << Outline << jd;
        QTest::newRow("logseq page") << logseqPage << false << Outline << Document << Outline << he;
        QTest::newRow("obsidian page") << obsidianNote << false << Outline << Document << Document << he;
        QTest::newRow("heuristic beats page default") << obsidianNote << false << Outline << Outline << Document << he;
        QTest::newRow("empty page") << QString() << false << Outline << Document << Document << pd;
        QTest::newRow("empty page, outline default") << QString() << false << Document << Outline << Outline << pd;
        QTest::newRow("single block") << u"- jedyny punkt\n"_s << false << Outline << Document << Document << pd;
        QTest::newRow("mixed") << u"# Nagłówek\n- punkt\n"_s << false << Outline << Outline << Outline << pd;
        QTest::newRow("frontmatter only")
                << u"---\ntitle: Żółw\n---\n"_s << false << Outline << Document << Document << pd;
    }
    void resolve()
    {
        QFETCH(QString, content);
        QFETCH(bool, journal);
        QFETCH(int, journalMode);
        QFETCH(int, pageMode);
        QFETCH(int, mode);
        QFETCH(int, source);
        VaultConfig config;
        config.journalMode = static_cast<NoteMode>(journalMode);
        config.pageMode = static_cast<NoteMode>(pageMode);
        const ResolvedNoteMode r = resolveNoteMode(content, journal, config);
        QCOMPARE(int(r.mode), mode);
        QCOMPARE(int(r.source), source);
        QCOMPARE(r.isExplicit(), source == int(NoteModeSource::Frontmatter));
        QVERIFY(r == (ResolvedNoteMode{static_cast<NoteMode>(mode), static_cast<NoteModeSource>(source)}));
    }

    void resolveSanitisesConfig()
    {
        VaultConfig config;
        config.journalMode = static_cast<NoteMode>(9);
        config.pageMode = static_cast<NoteMode>(-3);
        QCOMPARE(resolveNoteMode(QString(), true, config).mode, NoteMode::Outline);
        QCOMPARE(resolveNoteMode(QString(), false, config).mode, NoteMode::Document);
    }

    // ---- setting / removing the key ------------------------------------------------------
    void setKey_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("mode");
        QTest::addColumn<QString>("expected");
        // A note without frontmatter gets one.
        QTest::newRow("no frontmatter") << u"# Tytuł\n\nZażółć gęślą jaźń.\n"_s << Document
                                        << u"---\nbrulion: document\n---\n# Tytuł\n\nZażółć gęślą jaźń.\n"_s;
        QTest::newRow("empty note") << QString() << Outline << u"---\nbrulion: outline\n---\n"_s;
        QTest::newRow("crlf note") << u"- a\r\n- b\r\n"_s << Document
                                   << u"---\r\nbrulion: document\r\n---\r\n- a\r\n- b\r\n"_s;
        QTest::newRow("lone cr note") << u"a\rb"_s << Outline << u"---\rbrulion: outline\r---\ra\rb"_s;
        QTest::newRow("bom note") << u"\uFEFFTreść"_s << Document << u"\uFEFF---\nbrulion: document\n---\nTreść"_s;
        QTest::newRow("no final newline") << u"x"_s << Document << u"---\nbrulion: document\n---\nx"_s;
        QTest::newRow("unclosed dashes") << u"---\nżółw\n"_s << Document
                                         << u"---\nbrulion: document\n---\n---\nżółw\n"_s;
        QTest::newRow("logseq page properties")
                << u"title:: Dom\n\n- a\n"_s << Outline << u"---\nbrulion: outline\n---\ntitle:: Dom\n\n- a\n"_s;
        // Frontmatter without the key: a new last entry, everything else untouched.
        QTest::newRow("append") << u"---\ntitle: Dom\ntags: [a, b]\n---\nBody\n"_s << Outline
                                << u"---\ntitle: Dom\ntags: [a, b]\nbrulion: outline\n---\nBody\n"_s;
        QTest::newRow("comments and formatting kept")
                << u"---\n# komentarz\ntitle:   \"Łódź\"   # tytuł\n\nlista:\n  - x\n  -   y\n---\n# H\n"_s << Document
                << u"---\n# komentarz\ntitle:   \"Łódź\"   # tytuł\n\nlista:\n  - x\n  -   y\nbrulion: document\n---\n# H\n"_s;
        QTest::newRow("crlf append") << u"---\r\ntitle: Ćma\r\n---\r\n"_s << Outline
                                     << u"---\r\ntitle: Ćma\r\nbrulion: outline\r\n---\r\n"_s;
        QTest::newRow("mixed line endings")
                << u"---\ntitle: a\r\n---\nx\n"_s << Document << u"---\ntitle: a\r\nbrulion: document\r\n---\nx\n"_s;
        QTest::newRow("dots and trailing spaces")
                << u"--- \na: 1\n... \nBody"_s << Document << u"--- \na: 1\nbrulion: document\n... \nBody"_s;
        QTest::newRow("frontmatter at the end")
                << u"---\na: 1\n---"_s << Outline << u"---\na: 1\nbrulion: outline\n---"_s;
        QTest::newRow("empty frontmatter") << u"---\n---\nBody"_s << Document << u"---\nbrulion: document\n---\nBody"_s;
        QTest::newRow("bom frontmatter") << u"\uFEFF---\ntags: x\n---\n"_s << Outline
                                         << u"\uFEFF---\ntags: x\nbrulion: outline\n---\n"_s;
        QTest::newRow("other spelling is another key") << u"---\nBrulion: outline\n---\n"_s << Document
                                                       << u"---\nBrulion: outline\nbrulion: document\n---\n"_s;
        QTest::newRow("nested key untouched") << u"---\nmeta:\n  brulion: outline\n---\n"_s << Document
                                              << u"---\nmeta:\n  brulion: outline\nbrulion: document\n---\n"_s;
        QTest::newRow("block scalar untouched") << u"---\nopis: |\n  brulion: outline\n---\n"_s << Outline
                                                << u"---\nopis: |\n  brulion: outline\nbrulion: outline\n---\n"_s;
        // An existing entry is replaced in place.
        QTest::newRow("replace in place") << u"---\nbrulion: outline\ntitle: x\n---\n- a\n"_s << Document
                                          << u"---\nbrulion: document\ntitle: x\n---\n- a\n"_s;
        QTest::newRow("replace with comment") << u"---\na: 1\nbrulion: outline # stare\nb: 2\n---\n"_s << Document
                                              << u"---\na: 1\nbrulion: document\nb: 2\n---\n"_s;
        QTest::newRow("replace invalid value")
                << u"---\nbrulion: kanban\n---\n"_s << Outline << u"---\nbrulion: outline\n---\n"_s;
        QTest::newRow("replace null") << u"---\nbrulion:\n---\n"_s << Document << u"---\nbrulion: document\n---\n"_s;
        QTest::newRow("replace quoted key")
                << u"---\n\"brulion\": outline\n---\n"_s << Document << u"---\nbrulion: document\n---\n"_s;
        QTest::newRow("replace spaced key")
                << u"---\nbrulion : outline\n---\n"_s << Document << u"---\nbrulion: document\n---\n"_s;
        QTest::newRow("duplicates dropped") << u"---\nbrulion: outline\na: 1\nbrulion: kanban\n---\n"_s << Document
                                            << u"---\nbrulion: document\na: 1\n---\n"_s;
        QTest::newRow("nested entry") << u"---\nbrulion:\n  mode: outline\n  extra: [1, 2]\ntitle: T\n---\n"_s
                                      << Document << u"---\nbrulion: document\ntitle: T\n---\n"_s;
        QTest::newRow("sequence entry") << u"---\nbrulion:\n- a\n- b\ntitle: T\n---\n"_s << Outline
                                        << u"---\nbrulion: outline\ntitle: T\n---\n"_s;
        QTest::newRow("blank lines in entry") << u"---\nbrulion:\n  a: 1\n\n  b: 2\n\nc: 3\n---\n"_s << Document
                                              << u"---\nbrulion: document\n\nc: 3\n---\n"_s;
        QTest::newRow("crlf replace") << u"---\r\nbrulion: outline\r\n---\r\nTreść\r\n"_s << Document
                                      << u"---\r\nbrulion: document\r\n---\r\nTreść\r\n"_s;
        // Already that mode: unchanged (comment, quotes and case kept).
        QTest::newRow("same value") << u"---\nbrulion: Document   # wybrane\n---\nx\n"_s << Document
                                    << u"---\nbrulion: Document   # wybrane\n---\nx\n"_s;
        QTest::newRow("same quoted value")
                << u"---\nbrulion: \"outline\"\n---\n"_s << Outline << u"---\nbrulion: \"outline\"\n---\n"_s;
        // Removing.
        QTest::newRow("remove: no frontmatter") << u"# a\n"_s << None << u"# a\n"_s;
        QTest::newRow("remove: empty") << QString() << None << QString();
        QTest::newRow("remove: no key") << u"---\ntitle: x\n---\nBody\n"_s << None << u"---\ntitle: x\n---\nBody\n"_s;
        QTest::newRow("remove: only the key") << u"---\nbrulion: outline\n---\nBody\n"_s << None << u"Body\n"_s;
        QTest::newRow("remove: key and blank lines")
                << u"---\n\nbrulion: document\n \n---\n# H\n"_s << None << u"# H\n"_s;
        QTest::newRow("remove: bom kept") << u"\uFEFF---\nbrulion: outline\n---\nŻ"_s << None << u"\uFEFFŻ"_s;
        QTest::newRow("remove: no body") << u"---\nbrulion: document\n---"_s << None << QString();
        QTest::newRow("remove: comment kept")
                << u"---\n# komentarz\nbrulion: outline\n---\nx"_s << None << u"---\n# komentarz\n---\nx"_s;
        QTest::newRow("remove: among others") << u"---\ntitle: Łąka\nbrulion: document\ntags: [a]\n---\nx\n"_s << None
                                              << u"---\ntitle: Łąka\ntags: [a]\n---\nx\n"_s;
        QTest::newRow("remove: duplicates")
                << u"---\nbrulion: a\nx: 1\nbrulion: outline\n---\n"_s << None << u"---\nx: 1\n---\n"_s;
        QTest::newRow("remove: crlf") << u"---\r\nbrulion: outline\r\ntitle: T\r\n---\r\nB\r\n"_s << None
                                      << u"---\r\ntitle: T\r\n---\r\nB\r\n"_s;
        QTest::newRow("remove: nested entry")
                << u"---\nbrulion:\n  mode: x\na: 1\n---\n"_s << None << u"---\na: 1\n---\n"_s;
        QTest::newRow("remove: invalid value")
                << u"---\nbrulion: kanban\nb: 1\n---\n"_s << None << u"---\nb: 1\n---\n"_s;
        QTest::newRow("remove: other spelling kept")
                << u"---\nBrulion: outline\n---\n"_s << None << u"---\nBrulion: outline\n---\n"_s;
        // The body would become frontmatter if the emptied block went: keep an empty block.
        QTest::newRow("remove: body starts with dashes block")
                << u"---\nbrulion: outline\n---\n---\ntitle: x\n---\nbody\n"_s << None
                << u"---\n---\n---\ntitle: x\n---\nbody\n"_s;
        QTest::newRow("remove: body starts with unclosed dashes")
                << u"---\nbrulion: outline\n---\n---\nżółw\n"_s << None << u"---\nżółw\n"_s;
    }
    void setKey()
    {
        QFETCH(QString, content);
        QFETCH(int, mode);
        QFETCH(QString, expected);
        const QString out = setNoteModeKey(content, modeOf(mode));
        QCOMPARE(out, expected);
        QCOMPARE(code(frontmatterNoteMode(out)), mode);
        QCOMPARE(bodyOf(out), bodyOf(content));
        QCOMPARE(otherKeys(out), otherKeys(content));
        QCOMPARE(setNoteModeKey(out, modeOf(mode)), out); // idempotent
    }

    void roundTrip_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("restored"); ///< after setting a mode and removing it again
        int row = 0;
        const auto same = [&row](const QString &s) { QTest::addRow("unchanged %d", ++row) << s << s; };
        same(QString());
        same(u"Zażółć gęślą jaźń"_s);
        same(u"# Tytuł\r\n\r\n- a\r\n"_s);
        same(u"\uFEFF- a\n"_s);
        same(u"a\rb\r"_s);
        same(u"---\ntitle: Dom\n---\nBody"_s);
        same(u"---\r\ntags: [a, b]\r\n# c\r\n---\r\n"_s);
        same(u"\uFEFF--- \na: 1\n...\n"_s);
        same(u"---\nopis: >\n  zażółć\n  gęślą\n---\n"_s);
        same(u"---\ntitle: a\r\n---\nx\n"_s);
        same(u"title:: Logseq\n\n- a\n"_s);
        same(u"---\nunclosed\n"_s);
        same(logseqPage);
        same(obsidianNote);
        same(QString(obsidianNote).replace(u"\n"_s, u"\r\n"_s));
        // Exceptions: an existing key is gone afterwards, an empty block goes with it.
        QTest::newRow("key") << u"---\nbrulion: outline\ntitle: x\n---\n- a\n"_s << u"---\ntitle: x\n---\n- a\n"_s;
        QTest::newRow("empty frontmatter") << u"---\n---\nBody\n"_s << u"Body\n"_s;
    }
    void roundTrip()
    {
        QFETCH(QString, content);
        QFETCH(QString, restored);
        for (NoteMode m : {NoteMode::Outline, NoteMode::Document}) {
            const NoteMode other = m == NoteMode::Outline ? NoteMode::Document : NoteMode::Outline;
            const QString set = setNoteModeKey(content, m);
            QCOMPARE(code(frontmatterNoteMode(set)), int(m));
            QCOMPARE(code(resolveNoteMode(set, true, VaultConfig()).mode), int(m));
            QCOMPARE(bodyOf(set), bodyOf(content));
            QCOMPARE(otherKeys(set), otherKeys(content));
            QCOMPARE(setNoteModeKey(set, m), set);
            // Toggling back and forth ends where a single choice would.
            QCOMPARE(setNoteModeKey(setNoteModeKey(content, other), m), set);
            QCOMPARE(setNoteModeKey(set, std::nullopt), restored);
            QCOMPARE(setNoteModeKey(setNoteModeKey(set, other), std::nullopt), restored);
        }
    }

    void malformedFrontmatterIsSafe_data()
    {
        QTest::addColumn<QString>("content");
        // A flow mapping spanning lines: its second line looks like a top-level key.
        QTest::newRow("flow mapping") << u"---\nmeta: {a: 1,\nbrulion: x}\ntitle: T\n---\nTreść\n"_s;
        QTest::newRow("flow sequence") << u"---\ntags: [a,\nbrulion: b]\n---\nTreść\n"_s;
        QTest::newRow("root sequence") << u"---\n- a\n- b\n---\nTreść\n"_s;
        QTest::newRow("plain text") << u"---\nto nie jest yaml\n---\nTreść\n"_s;
        QTest::newRow("unterminated quote") << u"---\ntitle: \"Łódź\nbrulion: outline\n---\nTreść\n"_s;
        QTest::newRow("tabs") << u"---\n\tbrulion: outline\nx:\t1\n---\n"_s;
        QTest::newRow("only dashes") << u"---\n---\n---\n"_s;
    }
    void malformedFrontmatterIsSafe()
    {
        // Whatever the YAML, an edit either succeeds without touching anything else or leaves
        // the note unchanged.
        QFETCH(QString, content);
        for (int mode : {Outline, Document, None}) {
            const QString out = setNoteModeKey(content, modeOf(mode));
            if (out == content)
                continue;
            QCOMPARE(code(frontmatterNoteMode(out)), mode);
            QCOMPARE(bodyOf(out), bodyOf(content));
            QCOMPARE(otherKeys(out), otherKeys(content));
        }
    }

    void flowValueClosingAtColumnZero()
    {
        // A flow collection may close at the key's indentation: the bracket belongs to the entry.
        const QString src = u"---\nbrulion: {tryb: outline,\n}\ntitle: Żółw\n---\nTreść\n"_s;
        QCOMPARE(parseFrontmatter(src).data.value(u"brulion"_s).toMap().value(u"tryb"_s).toString(), u"outline"_s);
        QCOMPARE(setNoteModeKey(src, NoteMode::Document), u"---\nbrulion: document\ntitle: Żółw\n---\nTreść\n"_s);
        QCOMPARE(setNoteModeKey(src, std::nullopt), u"---\ntitle: Żółw\n---\nTreść\n"_s);
    }

    void indentedRootMapping()
    {
        // The key the parser reads from an indented mapping is not on a column-0 line, so it
        // cannot be replaced in place. A second entry at column 0 would leave the YAML invalid
        // for other tools (mixed indentation, a duplicate key — errors in Obsidian's YAML
        // parser), and removing cannot be done without touching the other lines: the note stays
        // unchanged (callers see it through frontmatterNoteMode()).
        const QString src = u"---\n  brulion: outline\n  title: x\n---\nTreść\n"_s;
        QCOMPARE(code(frontmatterNoteMode(src)), Outline);
        QCOMPARE(setNoteModeKey(src, NoteMode::Document), src);
        QCOMPARE(setNoteModeKey(src, NoteMode::Outline), src);
        QCOMPARE(setNoteModeKey(src, std::nullopt), src);
    }

    void neverASecondKey_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("indented root") << u"---\n  brulion: outline\n  title: x\n---\nTreść\n"_s;
        QTest::newRow("tab indented key") << u"---\n\tbrulion: outline\nx:\t1\n---\n"_s;
        QTest::newRow("crlf, indented") << u"---\r\n   brulion: document\r\n   tags: [a]\r\n---\r\n"_s;
        QTest::newRow("existing key") << u"---\nbrulion: outline\ntitle: Łódź\n---\n- a\n"_s;
        QTest::newRow("duplicates") << u"---\nbrulion: outline\na: 1\nbrulion: kanban\n---\n"_s;
    }
    void neverASecondKey()
    {
        // Whatever the frontmatter, an edit leaves at most one line naming the key, so the YAML
        // stays valid for parsers that refuse duplicate keys.
        QFETCH(QString, content);
        for (NoteMode m : {NoteMode::Outline, NoteMode::Document}) {
            const QString out = setNoteModeKey(content, m);
            const auto fm = findFrontmatter(out);
            QVERIFY(fm);
            int keyLines = 0;
            for (const QString &line : fm->yaml.split(u'\n')) {
                if (line.trimmed().startsWith(u"brulion"))
                    ++keyLines;
            }
            QVERIFY2(keyLines <= 1, qPrintable(out));
            if (out != content) {
                QCOMPARE(code(frontmatterNoteMode(out)), int(m));
                QCOMPARE(bodyOf(out), bodyOf(content));
                QCOMPARE(otherKeys(out), otherKeys(content));
            }
        }
    }

    // ---- new notes -----------------------------------------------------------------------
    void choiceForNewNote_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("journal");
        QTest::addColumn<int>("pageMode");
        QTest::addColumn<int>("chosen");
        QTest::addColumn<QString>("expected");
        QTest::newRow("default document chosen") << QString() << false << Document << Document << QString();
        QTest::newRow("outline chosen") << QString() << false << Document << Outline
                                        << u"---\nbrulion: outline\n---\n"_s;
        QTest::newRow("outline is the default") << QString() << false << Outline << Outline << QString();
        QTest::newRow("document against outline default")
                << u"# Żółw\n"_s << false << Outline << Document << u"---\nbrulion: document\n---\n# Żółw\n"_s;
        QTest::newRow("template decides") << u"- a\n- b\n"_s << false << Document << Outline << u"- a\n- b\n"_s;
        QTest::newRow("template overruled")
                << u"- a\n- b\n"_s << false << Document << Document << u"---\nbrulion: document\n---\n- a\n- b\n"_s;
        QTest::newRow("journal outline") << u"## Plan\n"_s << true << Document << Outline << u"## Plan\n"_s;
        QTest::newRow("journal document")
                << u"## Plan\n"_s << true << Document << Document << u"---\nbrulion: document\n---\n## Plan\n"_s;
        QTest::newRow("template key replaced") << u"---\nbrulion: outline\ntags: dziennik\n---\n"_s << false << Document
                                               << Document << u"---\nbrulion: document\ntags: dziennik\n---\n"_s;
        QTest::newRow("template key kept") << u"---\nbrulion: outline\n---\n"_s << false << Document << Outline
                                           << u"---\nbrulion: outline\n---\n"_s;
    }
    void choiceForNewNote()
    {
        QFETCH(QString, content);
        QFETCH(bool, journal);
        QFETCH(int, pageMode);
        QFETCH(int, chosen);
        QFETCH(QString, expected);
        VaultConfig config;
        config.pageMode = static_cast<NoteMode>(pageMode);
        const QString out = withNoteModeChoice(content, static_cast<NoteMode>(chosen), journal, config);
        QCOMPARE(out, expected);
        QCOMPARE(int(resolveNoteMode(out, journal, config).mode), chosen);
    }
};

QTEST_GUILESS_MAIN(TestNoteMode)
#include "tst_markdown_notemode.moc"
