// The highlighter's rules as a pure function (src/app/editor/markdownstyles.h): the style of every
// construct in each mode, the operations' order (set / merge, modifiers, tints), source mode's
// states across lines (fences, frontmatter, %% comments, $$ formulas, a Luau fence's lexer), the
// prose for spell checking, the reminder chips, the flattened runs — and, over a corpus, the same
// look character by character as MarkdownHighlighter's QTextLayout formats (it applies the very
// same operations), with the default colours of its palette.
#include "markdownhighlighter.h"
#include "markdownstyles.h"

#include <QColor>
#include <QFont>
#include <QSet>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

using namespace brulion::editor;
using namespace Qt::StringLiterals;

Q_DECLARE_METATYPE(brulion::editor::StyleOp)
Q_DECLARE_METATYPE(brulion::editor::StyleRun)
Q_DECLARE_METATYPE(brulion::editor::LineChip)

namespace {

/// MarkdownHighlighter's property holding a format's name.
constexpr int NameProperty = QTextFormat::UserProperty + 1;

/// "inline", "source", "code", "luau" (code mode in Luau).
StyleOptions optionsFor(const QString &mode)
{
    StyleOptions o;
    if (mode == u"source")
        o.mode = MarkdownMode::Source;
    else if (mode == u"code" || mode == u"luau")
        o.mode = MarkdownMode::Code;
    o.luau = mode == u"luau";
    return o;
}

/// The styles of every line of `text` (split at \n), each line starting in the state the one
/// before it left.
QList<LineStyles> styleText(const QString &text, StyleOptions options)
{
    QList<LineStyles> out;
    int state = 0;
    const QStringList lines = text.split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        options.firstLine = i == 0;
        out.append(styleMarkdownLine(lines[i], state, options));
        state = out.last().state;
    }
    return out;
}

/// The name of the style at `pos` of the runs ("" when none covers it).
QString nameAt(const QList<StyleRun> &runs, int pos)
{
    for (const StyleRun &r : runs) {
        if (pos >= r.from && pos < r.from + r.length)
            return QString(markdownStyleName(r.style));
    }
    return {};
}

const StyleRun *runAt(const QList<StyleRun> &runs, int pos)
{
    for (const StyleRun &r : runs) {
        if (pos >= r.from && pos < r.from + r.length)
            return &r;
    }
    return nullptr;
}

QString roleName(ColorRole role)
{
    return role == ColorRole::None ? QString() : QString(colorRolePalette(role).key);
}

/// "heading demibold", "marker", "code background=code mono", "background=tint:#…": what a
/// character looks like, for the expectations and failure messages.
QString describe(const StyleAttributes &a)
{
    QStringList parts;
    if (a.color != ColorRole::None)
        parts.append(roleName(a.color));
    switch (a.background) {
    case BackgroundRole::None: break;
    case BackgroundRole::Code: parts.append(u"background=code"_s); break;
    case BackgroundRole::Highlight: parts.append(u"background=highlight"_s); break;
    case BackgroundRole::Tint: parts.append(QString(u"background=tint:"_s + a.tintColor())); break;
    }
    if (a.weight == FontWeight::DemiBold)
        parts.append(u"demibold"_s);
    if (a.weight == FontWeight::Bold)
        parts.append(u"bold"_s);
    if (a.italic)
        parts.append(u"italic"_s);
    if (a.strike)
        parts.append(u"strike"_s);
    if (a.underline)
        parts.append(u"underline"_s);
    if (a.monospace)
        parts.append(u"mono"_s);
    return parts.join(u' ');
}

/// The position of the `occurrence`-th `needle` in `line` (−1: none).
int find(const QString &line, const QString &needle, int occurrence = 0)
{
    qsizetype pos = -1;
    for (int i = 0; i <= occurrence; ++i) {
        pos = line.indexOf(needle, pos + 1);
        if (pos < 0)
            return -1;
    }
    return int(pos);
}

/// A palette with a colour of its own for every key the formats read, so a format's colour says
/// which key it came from.
QVariantMap uniquePalette()
{
    QVariantMap palette;
    int i = 0;
    const auto add = [&](QLatin1StringView key) {
        if (!key.isEmpty() && !palette.contains(QString(key)))
            palette.insert(QString(key), QColor(10, 20, 30 + 4 * i++));
    };
    for (int r = int(ColorRole::Heading); r <= int(ColorRole::String); ++r)
        add(colorRolePalette(ColorRole(r)).key);
    add(backgroundRolePalette(BackgroundRole::Code).key);
    add(backgroundRolePalette(BackgroundRole::Highlight).key);
    return palette;
}

/// What a QTextCharFormat of MarkdownHighlighter shows, read back as StyleAttributes (colours by
/// the unique palette's keys); `ok` false for a colour no key or tint gives.
StyleAttributes attributesOf(const QTextCharFormat &f, const QVariantMap &palette, bool *ok)
{
    *ok = true;
    StyleAttributes a;
    if (f.hasProperty(QTextFormat::ForegroundBrush)) {
        const QColor c = f.foreground().color();
        *ok = false;
        for (int r = int(ColorRole::Heading); r <= int(ColorRole::String); ++r) {
            if (palette.value(QString(colorRolePalette(ColorRole(r)).key)).value<QColor>() == c) {
                a.color = ColorRole(r);
                *ok = true;
            }
        }
    }
    if (f.hasProperty(QTextFormat::BackgroundBrush)) {
        const QColor c = f.background().color();
        if (c == palette.value(QString(backgroundRolePalette(BackgroundRole::Code).key)).value<QColor>()) {
            a.background = BackgroundRole::Code;
        } else if (c == palette.value(QString(backgroundRolePalette(BackgroundRole::Highlight).key)).value<QColor>()) {
            a.background = BackgroundRole::Highlight;
        } else {
            bool found = false;
            for (int t = 1; t < brulion::markdown::HighlightColorCount; ++t) {
                const auto tint = brulion::markdown::HighlightColor(t);
                if (QColor(brulion::markdown::highlightTint(tint)) == c) {
                    a.background = BackgroundRole::Tint;
                    a.tint = tint;
                    found = true;
                }
            }
            *ok = *ok && found;
        }
    }
    if (f.hasProperty(QTextFormat::FontWeight)) {
        const int w = f.fontWeight();
        a.weight = w == QFont::Bold       ? FontWeight::Bold
                   : w == QFont::DemiBold ? FontWeight::DemiBold
                                          : FontWeight::Normal;
        *ok = *ok && (w == QFont::Bold || w == QFont::DemiBold);
    }
    a.italic = f.fontItalic();
    a.strike = f.fontStrikeOut();
    a.underline = f.fontUnderline();
    a.monospace = f.hasProperty(QTextFormat::FontFamilies) && !f.fontFamilies().toStringList().isEmpty();
    return a;
}

} // namespace

class TestMarkdownStyles : public QObject {
    Q_OBJECT

private Q_SLOTS:
    /// The style (the format's name) at a needle of a line, every construct in every mode;
    /// multi-line texts run line by line with their states.
    void styleNames_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("needle");
        QTest::addColumn<int>("occurrence");
        QTest::addColumn<QString>("style");
        const auto row = [](const char *name, const QString &mode, const QString &text, int line, const QString &needle,
                            const QString &style, int occurrence = 0) {
            QTest::newRow(name) << mode << text << line << needle << occurrence << style;
        };
        const QString in = u"inline"_s;
        const QString src = u"source"_s;
        // Headings, quotes, callouts, checkboxes, task keywords, properties.
        row("heading", in, u"## Koszty remontu ąę"_s, 0, u"Koszty"_s, u"heading"_s);
        row("heading marks", in, u"## Koszty"_s, 0, u"##"_s, u"marker"_s);
        row("heading without text", in, u"#"_s, 0, u"#"_s, u"marker"_s);
        row("seven # is no heading", in, u"####### nie"_s, 0, u"nie"_s, QString());
        row("heading in a list item", src, u"- # Żółw"_s, 0, u"Żółw"_s, u"heading"_s);
        row("quote", in, u"> cytat"_s, 0, u">"_s, u"marker"_s);
        row("nested quote", in, u"> > głębiej"_s, 0, u">"_s, u"marker"_s, 1);
        row("callout", in, u"> [!warning]- Uwaga"_s, 0, u"[!warning]-"_s, u"muted"_s);
        row("callout's title text", in, u"> [!warning]- Uwaga"_s, 0, u"Uwaga"_s, QString());
        row("checkbox", in, u"[ ] zadanie"_s, 0, u"[ ]"_s, u"marker"_s);
        row("checkbox done", in, u"[x] zrobione"_s, 0, u"[x]"_s, u"marker"_s);
        row("checkbox needs a space", in, u"[x]zrobione"_s, 0, u"[x]"_s, QString());
        row("task keyword", in, u"TODO zadzwonić"_s, 0, u"TODO"_s, u"marker"_s);
        row("keyword in a word", in, u"TODOS zadzwonić"_s, 0, u"TODO"_s, QString());
        row("property", in, u"status:: w toku"_s, 0, u"status::"_s, u"property"_s);
        row("property's value", in, u"status:: w toku"_s, 0, u"toku"_s, QString());
        row("single colon", in, u"status: w toku"_s, 0, u"status"_s, QString());
        row("list marker inline", in, u"- [ ] zadanie"_s, 0, u"-"_s, QString());
        row("list marker source", src, u"- [ ] zadanie"_s, 0, u"-"_s, u"marker"_s);
        row("checkbox after the marker", src, u"- [ ] zadanie"_s, 0, u"[ ]"_s, u"marker"_s);
        row("ordered marker", src, u"12. dwunasty"_s, 0, u"12."_s, u"marker"_s);
        row("indented marker", src, u"\t* dziecko"_s, 0, u"*"_s, u"marker"_s);
        // Emphasis.
        row("bold", in, u"to **ważne** słowo"_s, 0, u"ważne"_s, u"bold"_s);
        row("bold marks", in, u"to **ważne**"_s, 0, u"**"_s, u"marker"_s, 1);
        row("bold underscores", in, u"__mocne__"_s, 0, u"mocne"_s, u"bold"_s);
        row("italic", in, u"bardzo *pochyłe* słowo"_s, 0, u"pochyłe"_s, u"italic"_s);
        row("italic underscore", in, u"bardzo _pochyłe_ słowo"_s, 0, u"pochyłe"_s, u"italic"_s);
        row("snake_case", in, u"zmienna_nazwa_tu"_s, 0, u"nazwa"_s, QString());
        row("nested italic", in, u"**pogrubione _pochyłe_**"_s, 0, u"pochyłe"_s, u"italic"_s);
        row("unclosed bold", in, u"**niezamknięte"_s, 0, u"niezamknięte"_s, QString());
        row("bold of spaces", in, u"**  **"_s, 0, u"*"_s, QString());
        row("strike", in, u"~~stare~~ nowe"_s, 0, u"stare"_s, u"strike"_s);
        row("highlight", in, u"==zaznacz=="_s, 0, u"zaznacz"_s, u"highlight"_s);
        row("mark", in, u"a <mark class=\"hltr-green\">zielone</mark>"_s, 0, u"zielone"_s, u"highlight"_s);
        row("mark tag", in, u"a <mark class=\"hltr-green\">zielone</mark>"_s, 0, u"</mark>"_s, u"faint"_s);
        row("unpaired mark", in, u"a <mark>bez końca"_s, 0, u"<mark>"_s, QString());
        // Code spans and comments hide the syntax inside them.
        row("code", in, u"kod `x = 1` tu"_s, 0, u"x = 1"_s, u"code"_s);
        row("code's backticks", in, u"kod `x = 1` tu"_s, 0, u"`"_s, u"fence"_s);
        row("no tag in code", in, u"`#tag [[a]] **b**`"_s, 0, u"tag"_s, u"code"_s);
        row("no link in code", in, u"`#tag [[a]] **b**`"_s, 0, u"a"_s, u"code"_s);
        row("double backticks", in, u"``a ` b``"_s, 0, u" ` "_s, u"code"_s);
        row("unclosed code", in, u"`kod bez końca #tag"_s, 0, u"tag"_s, u"tag"_s);
        row("comment", in, u"widać %%ukryte #tag%% dalej"_s, 0, u"tag"_s, u"comment"_s);
        row("html comment", in, u"pytanie <!--SR:!2026-11-20,3,250-->"_s, 0, u"SR"_s, u"comment"_s);
        row("unclosed comment inline", in, u"widać %%ukryte #tag"_s, 0, u"tag"_s, u"tag"_s);
        // Clozes.
        row("cloze answer", in, u"Stolicą jest {{c1::Warszawa::miasto}}."_s, 0, u"Warszawa"_s, u"highlight"_s);
        row("cloze braces", in, u"Stolicą jest {{c1::Warszawa::miasto}}."_s, 0, u"{{c1::"_s, u"marker"_s);
        row("cloze hint", in, u"Stolicą jest {{c1::Warszawa::miasto}}."_s, 0, u"miasto"_s, u"marker"_s);
        row("bare cloze", in, u"{{cloze Kraków}}"_s, 0, u"Kraków"_s, u"highlight"_s);
        // Links, refs, URLs.
        row("wiki link", in, u"zobacz [[Remont łazienki]]"_s, 0, u"Remont"_s, u"link"_s);
        row("wiki brackets", in, u"zobacz [[Remont]]"_s, 0, u"]]"_s, u"marker"_s);
        row("embed", in, u"![[szkic.png]]"_s, 0, u"szkic"_s, u"link"_s);
        row("embed's !", in, u"![[szkic.png]]"_s, 0, u"!"_s, u"marker"_s);
        row("link in bold", in, u"**[[Żółw]]**"_s, 0, u"Żółw"_s, u"link"_s);
        row("block ref", in, u"((abc-123))"_s, 0, u"abc"_s, u"blockRef"_s);
        row("malformed block ref", in, u"((abc 123))"_s, 0, u"abc"_s, QString());
        row("md link text", in, u"[strona](https://example.com)"_s, 0, u"strona"_s, u"link"_s);
        row("md link target", in, u"[strona](https://example.com/ścieżka)"_s, 0, u"example"_s, u"faint"_s);
        row("md link brackets", in, u"[strona](x)"_s, 0, u"]("_s, u"marker"_s);
        row("url", in, u"wejdź na https://example.com/żółw dziś"_s, 0, u"example"_s, u"url"_s);
        row("url after a link", in, u"[a](b) https://po-linku.pl"_s, 0, u"po-linku"_s, u"url"_s);
        row("mailto", in, u"pisz: mailto:jan@example.com"_s, 0, u"jan"_s, u"url"_s);
        row("url after (", in, u"(https://w-nawiasie.pl)"_s, 0, u"nawiasie"_s, QString());
        row("url in a word", in, u"xhttps://example.com"_s, 0, u"example"_s, QString());
        row("url in a mark tag", in, u"<mark class=\"https://x.pl\">tekst</mark>"_s, 0, u"x.pl"_s, u"faint"_s);
        // Tags.
        row("tag", in, u"Kupić #zakupy dziś"_s, 0, u"zakupy"_s, u"tag"_s);
        row("polish tag", in, u"x #żółw"_s, 0, u"żółw"_s, u"tag"_s);
        row("tag after (", in, u"(#nawias)"_s, 0, u"nawias"_s, u"tag"_s);
        row("tag in a word", in, u"a#nie"_s, 0, u"nie"_s, QString());
        row("number is no tag", in, u"pozycja #1"_s, 0, u"1"_s, QString());
        row("nested tag", in, u"#dom/łazienka"_s, 0, u"łazienka"_s, u"tag"_s);
        row("tag with brackets", in, u"#[[Złożony tag]]"_s, 0, u"tag"_s, u"tag"_s);
        // Metadata tokens.
        row("due", in, u"[ ] x 📅 2026-10-16"_s, 0, u"2026"_s, u"due"_s);
        row("scheduled", in, u"x ⏳ 2026-10-16"_s, 0, u"2026"_s, u"scheduled"_s);
        row("start", in, u"x 🛫 2026-10-16"_s, 0, u"2026"_s, u"start"_s);
        row("reminder", in, u"x ⏰ 2026-10-16 09:00"_s, 0, u"09:00"_s, u"reminder"_s);
        row("alarm", in, u"x ⏰! 2026-10-16 07:00"_s, 0, u"07:00"_s, u"alarm"_s);
        row("obsidian reminder", in, u"Spotkanie (@2026-11-11 09:30)"_s, 0, u"09:30"_s, u"reminder"_s);
        row("recurrence", in, u"x 🔁 every week on Monday"_s, 0, u"Monday"_s, u"recurrence"_s);
        row("done", in, u"x ✅ 2026-10-14"_s, 0, u"2026"_s, u"done"_s);
        row("created", in, u"x ➕ 2026-10-14"_s, 0, u"2026"_s, u"done"_s);
        row("cancelled", in, u"x ❌ 2026-10-14"_s, 0, u"2026"_s, u"done"_s);
        row("priority", in, u"x ⏫"_s, 0, u"⏫"_s, u"priority"_s);
        row("logseq priority", in, u"TODO x [#A]"_s, 0, u"[#A]"_s, u"priority"_s);
        row("deadline", in, u"DEADLINE: <2026-10-20 Tue>"_s, 0, u"2026"_s, u"due"_s);
        row("planning scheduled", in, u"SCHEDULED: <2026-10-16 Fri>"_s, 0, u"2026"_s, u"scheduled"_s);
        row("task id", in, u"x 🆔 abc123"_s, 0, u"abc123"_s, u"muted"_s);
        row("depends on", in, u"x ⛔ abc123"_s, 0, u"abc123"_s, u"muted"_s);
        row("date in code", in, u"`📅 2026-10-16`"_s, 0, u"2026"_s, u"code"_s);
        row("block id", in, u"zdanie ^abc-1"_s, 0, u"abc"_s, u"blockId"_s);
        row("block id not at the end", in, u"zdanie ^abc dalej"_s, 0, u"abc"_s, QString());
        row("plain", in, u"zwykły tekst ąćęłńóśźż"_s, 0, u"tekst"_s, QString());
        row("empty line", in, QString(), 0, QString(), QString());

        // Source mode: the frontmatter.
        const QString front = u"---\ntytuł: Remont\n# nie: komentarz\n---\n# Nagłówek"_s;
        row("frontmatter fence", src, front, 0, u"---"_s, u"fence"_s);
        row("frontmatter key", src, front, 1, u"tytuł:"_s, u"property"_s);
        row("frontmatter value", src, front, 1, u"Remont"_s, u"muted"_s);
        row("frontmatter # line", src, front, 2, u"nie"_s, u"muted"_s);
        row("frontmatter closing", src, front, 3, u"---"_s, u"fence"_s);
        row("after the frontmatter", src, front, 4, u"Nagłówek"_s, u"heading"_s);
        row("frontmatter closed by ...", src, u"---\na: b\n...\n#tag"_s, 3, u"tag"_s, u"tag"_s);
        row("--- later is no frontmatter", src, u"x\n---\ntytuł: a"_s, 2, u"tytuł"_s, QString());
        row("frontmatter in inline mode", in, front, 1, u"tytuł"_s, QString());
        // Fences.
        const QString fence = u"```calc\n# nie nagłówek\n```\n# tak"_s;
        row("fence", src, fence, 0, u"calc"_s, u"fence"_s);
        row("fenced code", src, fence, 1, u"nagłówek"_s, u"code"_s);
        row("closing fence", src, fence, 2, u"```"_s, u"fence"_s);
        row("after the fence", src, fence, 3, u"tak"_s, u"heading"_s);
        row("``` inside ~~~", src, u"~~~\n```\n~~~\n#tag"_s, 1, u"```"_s, u"code"_s);
        row("~~~ closes", src, u"~~~\n```\n~~~\n#tag"_s, 3, u"tag"_s, u"tag"_s);
        row("shorter run inside", src, u"````\n```\n````\n#tag"_s, 1, u"```"_s, u"code"_s);
        row("longer run closes", src, u"````\nx\n`````\n#tag"_s, 3, u"tag"_s, u"tag"_s);
        row("text after a closing", src, u"```\n``` nie\n#kod"_s, 2, u"kod"_s, u"code"_s);
        row("four spaces: no fence", src, u"    ```\n#tag"_s, 1, u"tag"_s, u"tag"_s);
        row("unclosed fence", src, u"```\na\nb #nie"_s, 2, u"nie"_s, u"code"_s);
        row("fence in inline mode", in, fence, 1, u"nagłówek"_s, u"heading"_s);
        // A Luau fence: the lexer's state across its lines.
        const QString luau = u"# Tytuł\n```luau\nlocal s = [[\nif\n]] return s\n```\n```js\nlocal x\n```\nlocal po"_s;
        row("luau fence", src, luau, 1, u"luau"_s, u"fence"_s);
        row("luau keyword", src, luau, 2, u"local"_s, u"luau.keyword"_s);
        row("luau long string", src, luau, 3, u"if"_s, u"luau.string"_s);
        row("luau after the string", src, luau, 4, u"return"_s, u"luau.keyword"_s);
        row("luau's plain name", src, luau, 4, u"s"_s, u"code"_s);
        row("luau closing fence", src, luau, 5, u"```"_s, u"fence"_s);
        row("js fence", src, luau, 7, u"local"_s, u"code"_s);
        row("markdown again", src, luau, 9, u"local"_s, QString());
        const QString lua = u"````lua\n-- ```\nlocal y\n````"_s;
        row("lua comment with ```", src, lua, 1, u"```"_s, u"luau.comment"_s);
        row("lua keeps its fence", src, lua, 2, u"local"_s, u"luau.keyword"_s);
        row("lua closing", src, lua, 3, u"````"_s, u"fence"_s);
        row("{.luau} fence", src, u"``` {.luau}\nnil"_s, 1, u"nil"_s, u"luau.constant"_s);
        // %% comments over lines.
        const QString comment = u"Po %%komentarz\nna dwie linie%% #tag"_s;
        row("comment opens", src, comment, 0, u"komentarz"_s, u"comment"_s);
        row("text before it", src, comment, 0, u"Po"_s, QString());
        row("comment continues", src, comment, 1, u"dwie"_s, u"comment"_s);
        row("comment closes", src, comment, 1, u"tag"_s, u"tag"_s);
        row("comment over three lines", src, u"%%\nśrodek #tag\n%%"_s, 1, u"tag"_s, u"comment"_s);
        row("%% in code opens none", src, u"`%%` dalej\nnowa #linia"_s, 1, u"linia"_s, u"tag"_s);
        row("closed %% opens none", src, u"a %%b%% c\n#tag"_s, 1, u"tag"_s, u"tag"_s);
        row("%% over lines inline", in, comment, 1, u"dwie"_s, QString());
        // $$ formulas: styled as usual (only their prose is none).
        row("formula line", src, u"$$\nx^2 #nie\n$$"_s, 1, u"nie"_s, u"tag"_s);

        // Code mode.
        const QString code = u"a = 1 # nie komentarz\n# komentarz\n  // też\nb = a * 2"_s;
        row("code: inline #", u"code"_s, code, 0, u"nie"_s, QString());
        row("code: # comment", u"code"_s, code, 1, u"komentarz"_s, u"comment"_s);
        row("code: // comment", u"code"_s, code, 2, u"też"_s, u"comment"_s);
        row("code: no markdown", u"code"_s, u"**a** #tag"_s, 0, u"a"_s, QString());
        const QString luauCode =
                u"local n = 0 -- licznik\n--[[ długi\nkomentarz ]] b.command(\"x\", f)\n# not a comment"_s;
        row("luau: keyword", u"luau"_s, luauCode, 0, u"local"_s, u"luau.keyword"_s);
        row("luau: number", u"luau"_s, luauCode, 0, u"0"_s, u"luau.number"_s);
        row("luau: comment", u"luau"_s, luauCode, 0, u"licznik"_s, u"luau.comment"_s);
        row("luau: long comment", u"luau"_s, luauCode, 2, u"komentarz"_s, u"luau.comment"_s);
        row("luau: b", u"luau"_s, luauCode, 2, u"b.command"_s, u"luau.builtin"_s);
        row("luau: b's field", u"luau"_s, luauCode, 2, u"command"_s, u"luau.api"_s);
        row("luau: string", u"luau"_s, luauCode, 2, u"\"x\""_s, u"luau.string"_s);
        row("luau: # is no comment", u"luau"_s, luauCode, 3, u"#"_s, QString());
        row("luau: function", u"luau"_s, u"local function walk() end"_s, 0, u"walk"_s, u"luau.function"_s);
        row("luau: interpolation", u"luau"_s, u"local s = `a{n}b`"_s, 0, u"{"_s, u"luau.interpolation"_s);
    }

    void styleNames()
    {
        QFETCH(QString, mode);
        QFETCH(QString, text);
        QFETCH(int, line);
        QFETCH(QString, needle);
        QFETCH(int, occurrence);
        QFETCH(QString, style);
        const QList<LineStyles> styles = styleText(text, optionsFor(mode));
        const QString lineText = text.split(u'\n').value(line);
        const QList<StyleRun> runs = flattenStyles(styles.value(line).ops, int(lineText.size()));
        if (lineText.isEmpty()) {
            QVERIFY(runs.isEmpty());
            QVERIFY(styles.value(line).ops.isEmpty());
            return;
        }
        const int pos = find(lineText, needle, occurrence);
        QVERIFY2(pos >= 0, qPrintable(needle));
        QCOMPARE(nameAt(runs, pos), style);
    }

    /// The operations themselves: set or merge, in the highlighter's order, with the modifiers.
    void exactOps_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("line");
        QTest::addColumn<QList<StyleOp>>("ops");
        using S = MarkdownStyle;
        using M = StyleModifier;
        const auto set = [](int from, int length, S style, M modifier = M::None) {
            return StyleOp{from, length, style, false, modifier, brulion::markdown::HighlightColor::Default};
        };
        const auto merge = [](int from, int length, S style,
                              brulion::markdown::HighlightColor tint = brulion::markdown::HighlightColor::Default) {
            return StyleOp{from, length, style, true, M::None, tint};
        };
        QTest::newRow("plain") << u"inline"_s << u"zwykły tekst"_s << QList<StyleOp>{};
        QTest::newRow("empty") << u"source"_s << QString() << QList<StyleOp>{};
        QTest::newRow("heading") << u"inline"_s << u"# Tytuł ąę"_s
                                 << QList<StyleOp>{set(0, 10, S::Heading), set(0, 1, S::Marker, M::OnHeading)};
        QTest::newRow("bold") << u"inline"_s << u"**żółw**"_s
                              << QList<StyleOp>{merge(0, 8, S::Bold), merge(0, 2, S::Marker), merge(6, 2, S::Marker)};
        QTest::newRow("callout") << u"inline"_s << u"> [!tip] Rada"_s
                                 << QList<StyleOp>{set(0, 2, S::Marker), set(2, 6, S::Muted, M::DemiBold)};
        QTest::newRow("keyword") << u"inline"_s << u"TODO zadanie"_s
                                 << QList<StyleOp>{set(0, 4, S::Marker, M::DemiBold)};
        QTest::newRow("mark") << u"inline"_s << u"<mark class=\"hltr-red\">ą</mark>"_s
                              << QList<StyleOp>{merge(23, 1, S::Highlight, brulion::markdown::HighlightColor::Red),
                                                merge(0, 23, S::Faint), merge(24, 7, S::Faint)};
        QTest::newRow("code span") << u"inline"_s << u"a `b` c"_s
                                   << QList<StyleOp>{set(2, 3, S::Code), set(2, 1, S::Fence), set(4, 1, S::Fence)};
        QTest::newRow("wiki link") << u"inline"_s << u"[[Łódź]]"_s
                                   << QList<StyleOp>{set(0, 8, S::Link), merge(0, 2, S::Marker),
                                                     merge(6, 2, S::Marker)};
        QTest::newRow("md link") << u"inline"_s << u"[a](https://b.pl)"_s
                                 << QList<StyleOp>{set(0, 17, S::Marker), set(1, 1, S::Link), set(4, 12, S::Faint)};
        QTest::newRow("comment over lines") << u"source"_s << u"a %%b"_s << QList<StyleOp>{set(2, 3, S::Comment)};
        QTest::newRow("code comment") << u"code"_s << u"# x"_s << QList<StyleOp>{set(0, 3, S::Comment)};
        QTest::newRow("luau") << u"luau"_s << u"local x = 1"_s
                              << QList<StyleOp>{set(0, 5, S::LuauKeyword), set(10, 1, S::LuauNumber)};
    }

    void exactOps()
    {
        QFETCH(QString, mode);
        QFETCH(QString, line);
        QFETCH(QList<StyleOp>, ops);
        StyleOptions options = optionsFor(mode);
        options.firstLine = true;
        const LineStyles styles = styleMarkdownLine(line, 0, options);
        QCOMPARE(styles.ops.size(), ops.size());
        for (qsizetype i = 0; i < ops.size(); ++i) {
            const StyleOp &a = styles.ops[i];
            const StyleOp &e = ops[i];
            QVERIFY2(a == e,
                     qPrintable(u"op %1: {%2, %3, %4, merge %5, modifier %6} vs {%7, %8, %9, merge %10, modifier %11}"_s
                                        .arg(i)
                                        .arg(a.from)
                                        .arg(a.length)
                                        .arg(markdownStyleName(a.style))
                                        .arg(int(a.merge))
                                        .arg(int(a.modifier))
                                        .arg(e.from)
                                        .arg(e.length)
                                        .arg(markdownStyleName(e.style))
                                        .arg(int(e.merge))
                                        .arg(int(e.modifier))));
        }
    }

    /// What a character looks like after every operation over it (flattenStyles).
    void attributes_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("needle");
        QTest::addColumn<QString>("look");
        const QString in = u"inline"_s;
        QTest::newRow("heading") << in << u"## Remont"_s << u"Remont"_s << u"heading demibold"_s;
        QTest::newRow("heading marks") << in << u"## Remont"_s << u"##"_s << u"marker demibold"_s;
        QTest::newRow("bold in a heading") << in << u"## **Ważne** rzeczy"_s << u"Ważne"_s << u"heading bold"_s;
        QTest::newRow("bold and italic") << in << u"**pogrubione _pochyłe_**"_s << u"pochyłe"_s << u"bold italic"_s;
        QTest::newRow("bold italic marks") << in << u"**pogrubione _pochyłe_**"_s << u"_"_s << u"marker bold italic"_s;
        QTest::newRow("callout title") << in << u"> [!tip] Rada"_s << u"[!tip]"_s << u"muted demibold"_s;
        QTest::newRow("task keyword") << in << u"TODO zadanie"_s << u"TODO"_s << u"marker demibold"_s;
        QTest::newRow("highlight") << in << u"==ąę=="_s << u"ąę"_s << u"background=highlight"_s;
        QTest::newRow("highlight marks") << in << u"==ąę=="_s << u"=="_s << u"marker background=highlight"_s;
        QTest::newRow("tinted mark") << in << u"<mark style=\"background: #FF5582A6;\">róż</mark>"_s << u"róż"_s
                                     << QString(u"background=tint:"_s
                                                + brulion::markdown::highlightTint(
                                                        brulion::markdown::HighlightColor::Red));
        QTest::newRow("mark's tag") << in << u"<mark class=\"hltr-red\">ą</mark>"_s << u"<mark"_s << u"faint"_s;
        QTest::newRow("bare mark") << in << u"<mark>ą</mark>"_s << u"ą"_s << u"background=highlight"_s;
        QTest::newRow("code") << in << u"`kod`"_s << u"kod"_s << u"code background=code mono"_s;
        QTest::newRow("code's backtick") << in << u"`kod`"_s << u"`"_s << u"faint mono"_s;
        QTest::newRow("url") << in << u"https://żółw.pl"_s << u"żółw"_s << u"url underline"_s;
        QTest::newRow("cloze answer") << in << u"{{c1::Kraków}}"_s << u"Kraków"_s << u"background=highlight"_s;
        QTest::newRow("strike") << in << u"~~stare~~"_s << u"stare"_s << u"strike"_s;
        QTest::newRow("strike marks") << in << u"~~stare~~"_s << u"~~"_s << u"marker strike"_s;
        QTest::newRow("alarm") << in << u"x ⏰! 2026-10-16 07:00"_s << u"07:00"_s << u"alarm demibold"_s;
        QTest::newRow("comment") << in << u"%%ukryte%%"_s << u"ukryte"_s << u"faint italic"_s;
        QTest::newRow("link drops the bold") << in << u"**[[Łącze]]**"_s << u"Łącze"_s << u"link"_s;
        QTest::newRow("bold link's brackets") << in << u"**[[Łącze]]**"_s << u"[["_s << u"marker"_s;
        QTest::newRow("italic tag") << in << u"*x #żółw*"_s << u"żółw"_s << u"tag"_s;
        QTest::newRow("no tag after *") << in << u"*#żółw*"_s << u"żółw"_s << u"italic"_s;
        QTest::newRow("bold date") << in << u"**x 📅 2026-10-16**"_s << u"2026"_s << u"due"_s;
        QTest::newRow("luau keyword") << u"luau"_s << u"local x"_s << u"local"_s << u"keyword demibold"_s;
        QTest::newRow("luau function") << u"luau"_s << u"function M.go() end"_s << u"go"_s << u"function demibold"_s;
        QTest::newRow("luau comment") << u"luau"_s << u"-- żółw"_s << u"żółw"_s << u"faint italic"_s;
        QTest::newRow("fenced code") << u"source"_s << u"```\nkod"_s << u"kod"_s << u"code background=code mono"_s;
        QTest::newRow("plain") << in << u"zwykły"_s << u"zwykły"_s << QString();
    }

    void attributes()
    {
        QFETCH(QString, mode);
        QFETCH(QString, line);
        QFETCH(QString, needle);
        QFETCH(QString, look);
        const QList<LineStyles> styles = styleText(line, optionsFor(mode));
        const QStringList lines = line.split(u'\n');
        const QString last = lines.last();
        const QList<StyleRun> runs = flattenStyles(styles.last().ops, int(last.size()));
        const int pos = find(last, needle);
        QVERIFY(pos >= 0);
        const StyleRun *run = runAt(runs, pos);
        QCOMPARE(run ? describe(run->attributes) : QString(), look);
    }

    /// The prose of a line for spell checking, in each mode.
    void spellRanges_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("line");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<bool>("lineStart");
        const QString src = u"source"_s;
        QTest::newRow("inline") << u"inline"_s << u"zwykły tekst"_s << 0 << 0 << -1 << true;
        QTest::newRow("inline ignores fences") << u"inline"_s << u"```\nżółw"_s << 1 << 0 << -1 << true;
        QTest::newRow("code") << u"code"_s << u"tekst"_s << 0 << -1 << -1 << true;
        QTest::newRow("luau") << u"luau"_s << u"-- żółw"_s << 0 << -1 << -1 << true;
        QTest::newRow("source prose") << src << u"zwykły tekst"_s << 0 << 0 << -1 << true;
        QTest::newRow("fence") << src << u"```\nkod\n```\npo"_s << 0 << -1 << -1 << true;
        QTest::newRow("fenced code") << src << u"```\nkod\n```\npo"_s << 1 << -1 << -1 << true;
        QTest::newRow("closing fence") << src << u"```\nkod\n```\npo"_s << 2 << -1 << -1 << true;
        QTest::newRow("after the fence") << src << u"```\nkod\n```\npo"_s << 3 << 0 << -1 << true;
        QTest::newRow("frontmatter") << src << u"---\ntytuł: x\n---\npo"_s << 1 << -1 << -1 << true;
        QTest::newRow("after the frontmatter") << src << u"---\ntytuł: x\n---\npo"_s << 3 << 0 << -1 << true;
        QTest::newRow("comment opens") << src << u"Po %%komentarz\nna dwie%% dalej"_s << 0 << 0 << 3 << true;
        QTest::newRow("comment closes") << src << u"Po %%komentarz\nna dwie%% dalej"_s << 1 << 9 << -1 << false;
        QTest::newRow("inside a comment") << src << u"%%\nśrodek\n%%"_s << 1 << -1 << -1 << true;
        QTest::newRow("formula opens") << src << u"$$\nx^2\n$$\npo"_s << 0 << -1 << -1 << true;
        QTest::newRow("formula") << src << u"$$\nx^2\n$$\npo"_s << 1 << -1 << -1 << true;
        QTest::newRow("formula closes") << src << u"$$\nx^2\n$$\npo"_s << 2 << -1 << -1 << true;
        QTest::newRow("after the formula") << src << u"$$\nx^2\n$$\npo"_s << 3 << 0 << -1 << true;
        QTest::newRow("formula in a list") << src << u"- $$\nx\n  - $$\npo"_s << 1 << -1 << -1 << true;
        QTest::newRow("formula in a quote") << src << u"> $$\n> x\n> $$\npo"_s << 3 << 0 << -1 << true;
        QTest::newRow("$$ after a task box") << src << u"[ ] $$\nżółw"_s << 1 << 0 << -1 << true;
        QTest::newRow("inline $$") << src << u"a $$x$$ b\nżółw"_s << 1 << 0 << -1 << true;
    }

    void spellRanges()
    {
        QFETCH(QString, mode);
        QFETCH(QString, text);
        QFETCH(int, line);
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(bool, lineStart);
        const SpellRange range = styleText(text, optionsFor(mode)).value(line).spell;
        QCOMPARE(range.from, from);
        if (from >= 0) {
            QCOMPARE(range.to, to);
            QCOMPARE(range.lineStart, lineStart);
        }
    }

    /// The line state: none left open is 0; what a line leaves open changes how the next is read,
    /// and inline mode never carries anything.
    void lineStates()
    {
        StyleOptions source = optionsFor(u"source"_s);
        QCOMPARE(styleMarkdownLine(u"# Tytuł"_s, 0, source).state, 0);
        QCOMPARE(styleMarkdownLine(u"# Tytuł"_s, -1, source).state, 0);
        const int fence = styleMarkdownLine(u"```"_s, 0, source).state;
        QVERIFY(fence != 0);
        QCOMPARE(styleMarkdownLine(u"#tag"_s, fence, source).state, fence); // still inside
        QCOMPARE(styleMarkdownLine(u"```"_s, fence, source).state, 0);
        const int tilde = styleMarkdownLine(u"~~~~"_s, 0, source).state;
        QVERIFY(tilde != fence);
        QCOMPARE(styleMarkdownLine(u"```"_s, tilde, source).state, tilde);
        QCOMPARE(styleMarkdownLine(u"~~~"_s, tilde, source).state, tilde); // too short
        QCOMPARE(styleMarkdownLine(u"~~~~~"_s, tilde, source).state, 0);
        // A Luau fence: the lexer's state in the line state, kept across lines and given back.
        const int luau = styleMarkdownLine(u"```luau"_s, 0, source).state;
        QVERIFY(luau != fence);
        const int longString = styleMarkdownLine(u"local s = [==["_s, luau, source).state;
        QVERIFY(longString != luau);
        QCOMPARE(styleMarkdownLine(u"]] nie koniec"_s, longString, source).state, longString);
        QCOMPARE(styleMarkdownLine(u"]==] koniec"_s, longString, source).state, luau);
        // The frontmatter only from the first line.
        source.firstLine = true;
        const int front = styleMarkdownLine(u"---"_s, 0, source).state;
        QVERIFY(front != 0);
        source.firstLine = false;
        QCOMPARE(styleMarkdownLine(u"---"_s, 0, source).state, 0);
        QCOMPARE(styleMarkdownLine(u"a: b"_s, front, source).state, front);
        QCOMPARE(styleMarkdownLine(u"---"_s, front, source).state, 0);
        // Comments and formulas.
        const int comment = styleMarkdownLine(u"a %%b"_s, 0, source).state;
        QVERIFY(comment != 0);
        QCOMPARE(styleMarkdownLine(u"środek"_s, comment, source).state, comment);
        QCOMPARE(styleMarkdownLine(u"b%% c"_s, comment, source).state, 0);
        const int math = styleMarkdownLine(u"$$"_s, 0, source).state;
        QVERIFY(math != 0);
        QCOMPARE(styleMarkdownLine(u"x^2"_s, math, source).state, math);
        QCOMPARE(styleMarkdownLine(u"$$"_s, math, source).state, 0);
        // Inline mode reads none of them.
        const StyleOptions in = optionsFor(u"inline"_s);
        QCOMPARE(styleMarkdownLine(u"```"_s, 0, in).state, 0);
        QCOMPARE(styleMarkdownLine(u"#tag"_s, fence, in).ops, styleMarkdownLine(u"#tag"_s, 0, in).ops);
        // Code mode in Luau: the lexer's state alone.
        const StyleOptions code = optionsFor(u"luau"_s);
        const int open = styleMarkdownLine(u"--[[ długi"_s, 0, code).state;
        QVERIFY(open != 0);
        QCOMPARE(styleMarkdownLine(u"]] koniec"_s, open, code).state, 0);
        QCOMPARE(styleMarkdownLine(u"local"_s, 0, optionsFor(u"code"_s)).state, 0);
    }

    /// The reminder tokens of a line as chips: positions in the line, the alarm, the words.
    void chips_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("today");
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QList<LineChip>>("chips");
        const auto chip = [](const QString &line, const QString &token, bool alarm, const QString &label) {
            const int from = int(line.indexOf(token));
            return LineChip{from, from + int(token.size()), alarm, label};
        };
        const QString two = u"Zadzwonić ⏰ 2026-11-11 18:00 i ⏰! 2026-11-12 06:30"_s;
        QTest::newRow("jutro") << u"Zadzwonić do mamy ⏰ 2026-11-11 18:00"_s << u"2026-11-10"_s << u"pl"_s
                               << QList<LineChip>{chip(u"Zadzwonić do mamy ⏰ 2026-11-11 18:00"_s,
                                                       u"⏰ 2026-11-11 18:00"_s, false, u"⏰ jutro 18:00"_s)};
        QTest::newRow("two") << two << u"2026-11-11"_s << u"pl"_s
                             << QList<LineChip>{chip(two, u"⏰ 2026-11-11 18:00"_s, false, u"⏰ dziś 18:00"_s),
                                                chip(two, u"⏰! 2026-11-12 06:30"_s, true, u"🔔 jutro 06:30"_s)};
        QTest::newRow("tomorrow") << u"Call ⏰ 2026-11-11 18:00"_s << u"2026-11-10"_s << u"en"_s
                                  << QList<LineChip>{chip(u"Call ⏰ 2026-11-11 18:00"_s, u"⏰ 2026-11-11 18:00"_s,
                                                          false, u"⏰ tomorrow 18:00"_s)};
        QTest::newRow("no day") << u"Call ⏰ 2026-11-11 18:00"_s << QString() << u"en"_s
                                << QList<LineChip>{chip(u"Call ⏰ 2026-11-11 18:00"_s, u"⏰ 2026-11-11 18:00"_s, false,
                                                        u"⏰ Nov 11, 2026 18:00"_s)};
        QTest::newRow("obsidian-reminder")
                << u"Spotkanie (@2026-11-11 09:30)"_s << u"2026-11-10"_s << u"pl"_s
                << QList<LineChip>{chip(u"Spotkanie (@2026-11-11 09:30)"_s, u"(@2026-11-11 09:30)"_s, false,
                                        u"⏰ jutro 09:30"_s)};
        QTest::newRow("in code") << u"`⏰ 2026-11-11 09:00` w kodzie"_s << u"2026-11-10"_s << u"pl"_s
                                 << QList<LineChip>{};
        QTest::newRow("other dates") << u"📅 2026-11-12 ⏳ 2026-11-11"_s << u"2026-11-10"_s << u"pl"_s
                                     << QList<LineChip>{};
    }

    void chips()
    {
        QFETCH(QString, line);
        QFETCH(QString, today);
        QFETCH(QString, locale);
        QFETCH(QList<LineChip>, chips);
        StyleOptions options;
        options.reminderChips = true;
        options.chipToday = QDate::fromString(today, Qt::ISODate);
        options.chipLocale = locale == u"en" ? brulion::dates::UiLocale::English : brulion::dates::UiLocale::Polish;
        const LineStyles styles = styleMarkdownLine(line, 0, options);
        QCOMPARE(styles.chips.size(), chips.size());
        for (qsizetype i = 0; i < chips.size(); ++i) {
            QCOMPARE(styles.chips[i].from, chips[i].from);
            QCOMPARE(styles.chips[i].to, chips[i].to);
            QCOMPARE(styles.chips[i].alarm, chips[i].alarm);
            QCOMPARE(styles.chips[i].label, chips[i].label);
        }
        // Off: none, the same styles.
        options.reminderChips = false;
        const LineStyles off = styleMarkdownLine(line, 0, options);
        QVERIFY(off.chips.isEmpty());
        QCOMPARE(off.ops, styles.ops);
    }

    /// Operations flattened into runs: set replaces, merge adds, later wins, cut at the line's
    /// end, neighbours joined only when they look the same and have the same name.
    void flatten_data()
    {
        QTest::addColumn<QList<StyleOp>>("ops");
        QTest::addColumn<int>("length");
        QTest::addColumn<QList<StyleRun>>("runs");
        using S = MarkdownStyle;
        const auto set = [](int from, int length, S style) {
            return StyleOp{from, length, style, false, StyleModifier::None, brulion::markdown::HighlightColor::Default};
        };
        const auto merge = [](int from, int length, S style) {
            return StyleOp{from, length, style, true, StyleModifier::None, brulion::markdown::HighlightColor::Default};
        };
        const auto run = [](int from, int length, S style, StyleAttributes a) {
            return StyleRun{from, length, style, a};
        };
        StyleAttributes headingBold = styleAttributes(S::Heading);
        headingBold.weight = FontWeight::Bold;
        StyleAttributes boldItalic = styleAttributes(S::Bold);
        boldItalic.italic = true;
        StyleAttributes markerBoldItalic = boldItalic;
        markerBoldItalic.color = ColorRole::Marker;
        QTest::newRow("none") << QList<StyleOp>{} << 5 << QList<StyleRun>{};
        QTest::newRow("empty line") << QList<StyleOp>{set(0, 3, S::Tag)} << 0 << QList<StyleRun>{};
        QTest::newRow("merge into a set")
                << QList<StyleOp>{set(0, 10, S::Heading), merge(2, 3, S::Bold)} << 10
                << QList<StyleRun>{run(0, 2, S::Heading, styleAttributes(S::Heading)), run(2, 3, S::Bold, headingBold),
                                   run(5, 5, S::Heading, styleAttributes(S::Heading))};
        QTest::newRow("set over a merge") << QList<StyleOp>{merge(0, 5, S::Bold), set(0, 5, S::Link)} << 5
                                          << QList<StyleRun>{run(0, 5, S::Link, styleAttributes(S::Link))};
        QTest::newRow("merges add up") << QList<StyleOp>{merge(0, 6, S::Bold), merge(2, 4, S::Italic),
                                                         merge(5, 1, S::Marker)}
                                       << 6
                                       << QList<StyleRun>{run(0, 2, S::Bold, styleAttributes(S::Bold)),
                                                          run(2, 3, S::Italic, boldItalic),
                                                          run(5, 1, S::Marker, markerBoldItalic)};
        QTest::newRow("merge into nothing") << QList<StyleOp>{merge(1, 2, S::Italic)} << 4
                                            << QList<StyleRun>{run(1, 2, S::Italic, styleAttributes(S::Italic))};
        QTest::newRow("cut at the end") << QList<StyleOp>{set(3, 10, S::Tag)} << 5
                                        << QList<StyleRun>{run(3, 2, S::Tag, styleAttributes(S::Tag))};
        QTest::newRow("outside the line")
                << QList<StyleOp>{set(7, 5, S::Tag), set(-2, 4, S::Tag), set(1, 0, S::Tag)} << 5 << QList<StyleRun>{};
        QTest::newRow("gaps") << QList<StyleOp>{set(0, 1, S::Marker), set(3, 1, S::Marker)} << 5
                              << QList<StyleRun>{run(0, 1, S::Marker, styleAttributes(S::Marker)),
                                                 run(3, 1, S::Marker, styleAttributes(S::Marker))};
        QTest::newRow("joined") << QList<StyleOp>{set(0, 2, S::Tag), set(2, 2, S::Tag)} << 4
                                << QList<StyleRun>{run(0, 4, S::Tag, styleAttributes(S::Tag))};
        QTest::newRow("same look, other name") << QList<StyleOp>{set(0, 2, S::Link), set(2, 2, S::BlockRef)} << 4
                                               << QList<StyleRun>{run(0, 2, S::Link, styleAttributes(S::Link)),
                                                                  run(2, 2, S::BlockRef, styleAttributes(S::BlockRef))};
    }

    void flatten()
    {
        QFETCH(QList<StyleOp>, ops);
        QFETCH(int, length);
        QFETCH(QList<StyleRun>, runs);
        const QList<StyleRun> flat = flattenStyles(ops, length);
        QCOMPARE(flat.size(), runs.size());
        for (qsizetype i = 0; i < runs.size(); ++i) {
            QCOMPARE(flat[i].from, runs[i].from);
            QCOMPARE(flat[i].length, runs[i].length);
            QCOMPARE(QString(markdownStyleName(flat[i].style)), QString(markdownStyleName(runs[i].style)));
            QCOMPARE(describe(flat[i].attributes), describe(runs[i].attributes));
        }
    }

    /// Every style has a name of its own; the attributes a modifier and a tint give.
    void styleTable()
    {
        QSet<QString> names;
        for (int i = 0; i < MarkdownStyleCount; ++i) {
            const QString name(markdownStyleName(MarkdownStyle(i)));
            QVERIFY(!name.isEmpty());
            QVERIFY2(!names.contains(name), qPrintable(name));
            names.insert(name);
        }
        StyleOp heading{0,
                        1,
                        MarkdownStyle::Marker,
                        false,
                        StyleModifier::OnHeading,
                        brulion::markdown::HighlightColor::Default};
        QCOMPARE(describe(styleAttributes(heading)), u"marker demibold"_s);
        StyleOp tint{
                0, 1, MarkdownStyle::Highlight, true, StyleModifier::None, brulion::markdown::HighlightColor::Blue};
        QCOMPARE(styleAttributes(tint).tintColor(),
                 brulion::markdown::highlightTint(brulion::markdown::HighlightColor::Blue));
        QVERIFY(styleAttributes(MarkdownStyle::Highlight).tintColor().isEmpty());
        QCOMPARE(QString(colorRolePalette(ColorRole::Reminder).key), u"reminder"_s);
        QCOMPARE(QString(colorRolePalette(ColorRole::Reminder, true).key), u"reminderChip"_s);
        QCOMPARE(QString(colorRolePalette(ColorRole::Reminder, true).fallbackKey), u"reminder"_s);
        QVERIFY(colorRolePalette(ColorRole::None).key.isEmpty());
        QVERIFY(backgroundRolePalette(BackgroundRole::Tint).key.isEmpty());
    }

    /// MarkdownHighlighter's formats over a corpus, character by character: the same names and
    /// the same look as the flattened operations (colours by a palette with one colour per key).
    void matchesHighlighter_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("text");
        const QString inlineText =
                QStringList{
                        u"# Nagłówek z **pogrubieniem** i #tagiem"_s,
                        u"## Koszty `kod` ==zaznacz== ~~stare~~ *pochyłe* _też_"_s,
                        u"> [!warning]- Uwaga na **śliską** podłogę"_s,
                        u"> > cytat w cytacie z [[Strona|alias]] i ![[obraz.png]]"_s,
                        u"TODO zadzwonić do pani Żanety ⏰ 2026-11-11 18:00 📅 2026-11-12 ⏳ 2026-11-10 🛫 2026-11-09"_s,
                        u"DONE zrobione ✅ 2026-11-01 ➕ 2026-10-30 🔁 every week on Monday ⏫ 🆔 abc123 ⛔ def456"_s,
                        u"[ ] zadanie z ((blok-1)) i [opis](https://example.com/ścieżka?q=1) oraz https://żółw.pl/x"_s,
                        u"[x] wykonane <mark class=\"hltr-green\">zielone</mark> i <mark>zwykłe</mark> <mark style=\"background: #FF5582A6;\">różowe</mark>"_s,
                        u"status:: w toku #[[Złożony tag]] (#nawias) a#nie #1 #żółć/ąę"_s,
                        u""_s,
                        u"{{c1::Warszawa::miasto}} i {{cloze Kraków}} %%ukryte%% <!--SR:!2026-11-20,3,250-->"_s,
                        u"`#nie [[link]] **bold**` ``a ` b`` **niezamknięte _też ==nie"_s,
                        u"mailto:jan@example.com (https://w-nawiasie.pl) [a](b) https://po-linku.pl ^blok-id"_s,
                        u"SCHEDULED: <2026-10-16 Fri> DEADLINE: <2026-10-20 Tue> [#A] ⏰! 2026-10-16 07:00"_s,
                        u"**[[Link w pogrubieniu]]** __podkreślnik__ ***trzy*** zmienna_nazwa_tu **_razem_**"_s,
                        u"<mark class=\"https://x.pl\">tekst</mark> ==a== ==  == **  ** `` ` niedomknięte"_s,
                        u"#### **Ważne** ==ąę== `x` [[a]] ⏰ 2026-11-11"_s,
                        u"- # Żółw [a](https://b.pl/c?d=(e)) 📅 2026-13-45 ⏰ 99:99 ^"_s,
                        u"> TODO **pilne** ⏰! 2026-11-11 05:00 ((ref)) #tag"_s,
                }
                        .join(u'\n');
        const QString sourceText =
                QStringList{
                        u"---"_s,
                        u"tytuł: Remont łazienki"_s,
                        u"tags: [dom]"_s,
                        u"# nie: komentarz"_s,
                        u"---"_s,
                        u"# Remont **ważny**"_s,
                        u"- [ ] kupić płytki 📅 2026-11-12"_s,
                        u"  1. pierwszy"_s,
                        u"  * drugi #tag"_s,
                        u"```calc"_s,
                        u"# nie nagłówek"_s,
                        u"koszt = 100 zł"_s,
                        u"```"_s,
                        u"~~~"_s,
                        u"```"_s,
                        u"~~~"_s,
                        u"````lua"_s,
                        u"-- ```"_s,
                        u"local y = [["_s,
                        u"długi ]] .. \"x\" .. `a{b}c`"_s,
                        u"````"_s,
                        u"``` {.luau}"_s,
                        u"--[==[ ]] nadal"_s,
                        u"]==] nil"_s,
                        u"```"_s,
                        u"Po %%komentarz"_s,
                        u"na dwie linie%% #tag"_s,
                        u"`%%` dalej %%otwarty **nie**"_s,
                        u"środek"_s,
                        u"koniec%% po **tak**"_s,
                        u"$$"_s,
                        u"x^2 + #tag"_s,
                        u"$$"_s,
                        u"- $$"_s,
                        u"> $$"_s,
                        u"[ ] $$ nie wzór"_s,
                        u"    ```"_s,
                        u"#po"_s,
                        u"> [!tip] Rada ==ąę=="_s,
                        u"TODO zadanie ⏰ 2026-11-11 18:00"_s,
                        u"---"_s,
                        u"tytuł: nie frontmatter"_s,
                }
                        .join(u'\n');
        QTest::newRow("inline") << u"inline"_s << QString() << inlineText;
        QTest::newRow("source") << u"source"_s << QString() << sourceText;
        QTest::newRow("source as inline") << u"inline"_s << QString() << sourceText;
        QTest::newRow("inline as source") << u"source"_s << QString() << inlineText;
        QTest::newRow("code") << u"code"_s << u"calc"_s
                              << u"a = 1 # nie komentarz\n# komentarz\n// też\n  # wcięty\nb = a * 2\n**a** #tag"_s;
        QTest::newRow("luau")
                << u"code"_s << u"luau"_s
                << u"local n = 0 -- licznik\n--[[ długi\nkomentarz ]] b.command(\"x\", f)\nlocal s = `a{n}b`\n"
                   "function M.go() return nil end\n# not a comment\nlocal t = [==[\n]] ]==] 0x1F 2.5e-3 true"_s;
        QTest::newRow("empty") << u"source"_s << QString() << QString();
    }

    void matchesHighlighter()
    {
        QFETCH(QString, mode);
        QFETCH(QString, language);
        QFETCH(QString, text);
        QTextDocument doc;
        doc.setPlainText(text);
        MarkdownHighlighter h;
        const QVariantMap palette = uniquePalette();
        h.setPalette(palette);
        h.setMonoFamily(u"monospace"_s);
        h.setMode(mode);
        h.setLanguage(language);
        h.attach(&doc);

        StyleOptions options = optionsFor(mode);
        options.luau = mode == u"code" && (language == u"luau" || language == u"lua");
        int state = 0;
        int compared = 0;
        for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
            const QString line = block.text();
            options.firstLine = block.blockNumber() == 0;
            const LineStyles styles = styleMarkdownLine(line, state, options);
            state = styles.state;
            QCOMPARE(block.userState() < 0 ? 0 : block.userState(), styles.state);
            const QList<StyleRun> runs = flattenStyles(styles.ops, int(line.size()));
            QList<QTextCharFormat> formats(line.size());
            for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
                for (int i = r.start; i < r.start + r.length && i < line.size(); ++i)
                    formats[i] = r.format;
            }
            for (int i = 0; i < int(line.size()); ++i) {
                const QTextCharFormat &f = formats[i];
                const StyleRun *run = runAt(runs, i);
                const QString where = u"line %1 «%2» at %3 «%4»"_s.arg(QString::number(block.blockNumber()), line,
                                                                       QString::number(i), line.mid(i, 6));
                if (!run) {
                    QVERIFY2(f.properties().isEmpty(), qPrintable(QString(where + u": the highlighter formats it"_s)));
                    continue;
                }
                const QString name = f.property(NameProperty).toString();
                QVERIFY2(name == markdownStyleName(run->style),
                         qPrintable(QString(where + u": highlighter's "_s + name + u" vs "_s
                                            + markdownStyleName(run->style))));
                bool ok = false;
                const StyleAttributes a = attributesOf(f, palette, &ok);
                QVERIFY2(ok, qPrintable(QString(where + u": a colour or weight of no role"_s)));
                QVERIFY2(a == run->attributes,
                         qPrintable(QString(where + u": highlighter «"_s + describe(a) + u"» vs flattened «"_s
                                            + describe(run->attributes) + u"»"_s)));
                ++compared;
            }
        }
        QVERIFY(text.isEmpty() || compared > 0);
    }

    /// The palette's fallbacks give MarkdownHighlighter the colours it always had.
    void defaultColours_data()
    {
        QTest::addColumn<QVariantMap>("palette");
        QTest::addColumn<bool>("chips");
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("needle");
        QTest::addColumn<QColor>("foreground");
        QTest::addColumn<QColor>("background");
        const QVariantMap none;
        QTest::newRow("heading") << none << false << u"# Tytuł"_s << u"Tytuł"_s << QColor(u"#1f2430"_s) << QColor();
        QTest::newRow("marker is faint") << none << false << u"# Tytuł"_s << u"#"_s << QColor(u"#676c7b"_s) << QColor();
        QTest::newRow("marker from faint") << QVariantMap{{u"faint"_s, u"#010101"_s}} << false << u"> a"_s << u">"_s
                                           << QColor(u"#010101"_s) << QColor();
        QTest::newRow("marker's own") << QVariantMap{{u"faint"_s, u"#010101"_s}, {u"marker"_s, u"#020202"_s}} << false
                                      << u"> a"_s << u">"_s << QColor(u"#020202"_s) << QColor();
        QTest::newRow("link") << none << false << u"[[a]]"_s << u"a"_s << QColor(u"#2c6aa3"_s) << QColor();
        QTest::newRow("url from link") << QVariantMap{{u"link"_s, QColor(u"#030303"_s)}} << false << u"https://a.pl"_s
                                       << u"a.pl"_s << QColor(u"#030303"_s) << QColor();
        QTest::newRow("tag") << none << false << u"#żółw"_s << u"żółw"_s << QColor(u"#2b5f8f"_s) << QColor();
        QTest::newRow("code") << none << false << u"`k`"_s << u"k"_s << QColor(u"#1f2430"_s) << QColor(u"#f3eee2"_s);
        QTest::newRow("highlight") << none << false << u"==a=="_s << u"a"_s << QColor() << QColor(242, 179, 61, 97);
        QTest::newRow("muted") << none << false << u"a:: b"_s << u"a"_s << QColor(u"#535a6a"_s) << QColor();
        QTest::newRow("due from link") << none << false << u"📅 2026-10-16"_s << u"2026"_s << QColor(u"#2c6aa3"_s)
                                       << QColor();
        QTest::newRow("scheduled") << none << false << u"⏳ 2026-10-16"_s << u"2026"_s << QColor(u"#6a4f93"_s)
                                   << QColor();
        QTest::newRow("reminder") << none << false << u"⏰ 2026-10-16"_s << u"2026"_s << QColor(u"#7a4f00"_s)
                                  << QColor();
        QTest::newRow("recurrence") << none << false << u"🔁 every day"_s << u"day"_s << QColor(u"#2a6e44"_s)
                                    << QColor();
        QTest::newRow("done from muted") << QVariantMap{{u"muted"_s, u"#040404"_s}} << false << u"✅ 2026-10-16"_s
                                         << u"2026"_s << QColor(u"#040404"_s) << QColor();
        QTest::newRow("priority") << none << false << u"⏫"_s << u"⏫"_s << QColor(u"#a8382d"_s) << QColor();
        QTest::newRow("tinted mark") << none << false << u"<mark class=\"hltr-red\">ą</mark>"_s << u"ą"_s << QColor()
                                     << QColor(brulion::markdown::highlightTint(
                                                brulion::markdown::HighlightColor::Red));
    }

    void defaultColours()
    {
        QFETCH(QVariantMap, palette);
        QFETCH(bool, chips);
        QFETCH(QString, line);
        QFETCH(QString, needle);
        QFETCH(QColor, foreground);
        QFETCH(QColor, background);
        QTextDocument doc;
        doc.setPlainText(line);
        MarkdownHighlighter h;
        h.setPalette(palette);
        h.setReminderChips(chips);
        h.attach(&doc);
        const int pos = find(line, needle);
        QTextCharFormat f;
        for (const QTextLayout::FormatRange &r : doc.firstBlock().layout()->formats()) {
            if (pos >= r.start && pos < r.start + r.length)
                f = r.format;
        }
        QCOMPARE(f.hasProperty(QTextFormat::ForegroundBrush) ? f.foreground().color() : QColor(), foreground);
        QCOMPARE(f.hasProperty(QTextFormat::BackgroundBrush) ? f.background().color() : QColor(), background);
    }

    /// As chips (reminderChips; source mode keeps the token's text visible) reminders take the
    /// chip's colours, else their own.
    void chipColours()
    {
        QTextDocument doc;
        doc.setPlainText(u"Zadzwonić ⏰ 2026-10-16 09:00 i ⏰! 2026-10-16 06:00"_s);
        MarkdownHighlighter h;
        h.setMode(u"source"_s);
        h.setReminderChips(true);
        h.setPalette({{u"reminder"_s, u"#050505"_s}, {u"alarm"_s, u"#060606"_s}, {u"alarmChip"_s, u"#070707"_s}});
        h.attach(&doc);
        const auto colourAt = [&](const QString &needle) {
            const int pos = find(doc.firstBlock().text(), needle);
            for (const QTextLayout::FormatRange &r : doc.firstBlock().layout()->formats()) {
                if (pos >= r.start && pos < r.start + r.length)
                    return r.format.foreground().color();
            }
            return QColor();
        };
        QCOMPARE(colourAt(u"09:00"_s), QColor(u"#050505"_s)); // reminderChip ← reminder
        QCOMPARE(colourAt(u"06:00"_s), QColor(u"#070707"_s)); // alarmChip
        h.setReminderChips(false);
        QCOMPARE(colourAt(u"06:00"_s), QColor(u"#060606"_s));
    }

    /// Luau's colours come from their palette keys, with their fallbacks.
    void luauColours()
    {
        QTextDocument doc;
        doc.setPlainText(u"local function go() return b end"_s);
        MarkdownHighlighter h;
        h.setMode(u"code"_s);
        h.setLanguage(u"luau"_s);
        h.setPalette({{u"scheduled"_s, u"#060606"_s}, {u"heading"_s, u"#070707"_s}, {u"link"_s, u"#080808"_s}});
        h.attach(&doc);
        const auto colourAt = [&](const QString &needle) {
            const int pos = find(doc.firstBlock().text(), needle);
            for (const QTextLayout::FormatRange &r : doc.firstBlock().layout()->formats()) {
                if (pos >= r.start && pos < r.start + r.length)
                    return r.format.foreground().color();
            }
            return QColor();
        };
        QCOMPARE(colourAt(u"local"_s), QColor(u"#060606"_s)); // keyword ← scheduled
        QCOMPARE(colourAt(u"go"_s), QColor(u"#070707"_s));    // function ← heading
        QCOMPARE(colourAt(u"b "_s), QColor(u"#080808"_s));    // builtin ← link
    }
};

QTEST_MAIN(TestMarkdownStyles)
#include "tst_editor_markdownstyles.moc"
