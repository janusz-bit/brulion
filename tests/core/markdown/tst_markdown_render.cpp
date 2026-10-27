// InlineRenderer: exact Qt rich text for inline Markdown, chips, links, hidden syntax, escaping
// of hostile input, block-level rendering.
#include "markdown/inline.h"
#include "markdown/inlinerenderer.h"
#include "markdown/math.h"
#include "markdown/tokens.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

/// A context with short colour names so the expected HTML stays readable.
RenderContext context(dates::UiLocale locale = dates::UiLocale::Polish)
{
    RenderContext c;
    c.today = QDate(2026, 10, 14);
    c.locale = locale;
    Palette p;
    p.link = u"L"_s;
    p.unresolved = u"U"_s;
    p.muted = u"M"_s;
    p.codeBackground = u"C"_s;
    p.highlightBackground = u"H"_s;
    p.tag = {u"TF"_s, u"TB"_s};
    p.due = {u"DF"_s, u"DB"_s};
    p.overdue = {u"OF"_s, u"OB"_s};
    p.scheduled = {u"SF"_s, u"SB"_s};
    p.reminder = {u"RF"_s, u"RB"_s};
    p.alarm = {u"AF"_s, u"AB"_s};
    p.recurrence = {u"EF"_s, u"EB"_s};
    p.done = {u"XF"_s, u"XB"_s};
    p.neutral = {u"NF"_s, u"NB"_s};
    c.palette = p;
    c.resolvePage = [](const QString &target) -> std::optional<QString> {
        if (target == u"Brak")
            return std::nullopt;
        return target + u".md"_s;
    };
    c.blockText = [](const QString &id) -> std::optional<QString> {
        if (id == u"abc")
            return u"Treść <b>bloku</b>"_s;
        if (id == u"znaczniki")
            return u"Umowa z **sąsiadami** z [[2026-10-22]] i [[Dom|domem]]"_s;
        return std::nullopt;
    };
    // The block a [[target#^id]] leads to: per page ("Inna" has its own ^abc).
    c.linkedBlockText = [](const QString &target, const QString &id) -> std::optional<QString> {
        if (target == u"Inna" && id == u"abc")
            return u"Inny blok, to samo id"_s;
        if (id == u"abc")
            return u"Treść <b>bloku</b>"_s;
        if (id == u"dlugi")
            return u"Zażółć gęślą jaźń\n  i dalej: bardzo długi tekst bloku, który nie mieści się w odnośniku wcale"_s;
        if (id == u"emoji") // 📅 (a surrogate pair) at UTF-16 58–59, across the cut
            return QString(57, u'a') + u" 📅 i dalej tekst"_s;
        if (id == u"akcent") // "é" as e + a combining acute accent at 58–59
            return QString(58, u'b') + u"e\u0301 i dalej tekst"_s;
        if (id == u"znaczniki") // the index keeps a block's Markdown
            return u"Ekipa o 9:00 (umowa z **sąsiadami** z [[2026-10-22]] i [[Dom|domem]], `kod`)"_s;
        return std::nullopt;
    };
    c.inlineCalc = [](const QString &expr) { return expr == u"2 + 2" ? u"4"_s : QString(); };
    return c;
}

QString render(const QString &text)
{
    return InlineRenderer(context()).render(text);
}

QString link(const QString &href, const QString &color, const QString &label)
{
    return u"<a href=\"%1\" style=\"color:%2;text-decoration:none\">%3</a>"_s.arg(href, color, label);
}

QString chip(const QString &fg, const QString &bg, const QString &text)
{
    QString t = text;
    return u"<span style=\"background-color:%1;color:%2\">&nbsp;%3&nbsp;</span>"_s.arg(bg, fg,
                                                                                       t.replace(u' ', u"&nbsp;"_s));
}

QString reminderChip(int from, int to, const QString &fg, const QString &bg, const QString &text)
{
    return u"<a href=\"reminder:%1-%2\" style=\"text-decoration:none\">"_s.arg(from).arg(to) + chip(fg, bg, text)
           + u"</a>"_s;
}

QString tag(const QString &href, const QString &name)
{
    return u"<a href=\"tag:%1\" style=\"color:TF;background-color:TB;text-decoration:none\">#%2</a>"_s.arg(href, name);
}

const QString Code = u"<code style=\"background-color:C\">"_s;

} // namespace

class TestRender : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void inlineMarkdown_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain polish") << u"zwykły tekst ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ"_s << u"zwykły tekst ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ"_s;
        QTest::newRow("emphasis")
                << u"**pogrubienie** i *kursywa* oraz _też_"_s
                << u"<b>pogrubienie</b> i\u00a0<i>kursywa</i> oraz <i>też</i>"_s; // "i" keeps the next word
        QTest::newRow("strike highlight") << u"~~skreślone~~ ==ważne=="_s
                                          << u"<s>skreślone</s> <span style=\"background-color:H\">ważne</span>"_s;
        QTest::newRow("triple") << u"***oba***"_s << u"<i><b>oba</b></i>"_s;
        QTest::newRow("nested") << u"**bold *it* bold**"_s << u"<b>bold <i>it</i> bold</b>"_s;
        QTest::newRow("intraword underscore")
                << u"snake_case_name i 2 * 3 * 4"_s << u"snake_case_name i\u00a02 * 3 * 4"_s;
        QTest::newRow("unclosed") << u"**niezamknięte"_s << u"**niezamknięte"_s;
        QTest::newRow("single tilde and equals") << u"~a~ a = b =c="_s << u"~a~ a = b =c="_s;
        QTest::newRow("code") << u"`kod <b>`"_s << Code + u"kod &lt;b&gt;</code>"_s;
        QTest::newRow("code keeps markup") << u"`**x** [[y]] #z`"_s << Code + u"**x** [[y]] #z</code>"_s;
        QTest::newRow("double backticks") << u"`` a ` b ``"_s << Code + u"a ` b</code>"_s;
        QTest::newRow("inline calc") << u"wynik `= 2 + 2`"_s << u"wynik "_s + chip(u"NF"_s, u"NB"_s, u"4"_s);
        QTest::newRow("inline calc unknown") << u"`= nieznane`"_s << Code + u"= nieznane</code>"_s;
        QTest::newRow("html escaped") << u"<script>alert(1)</script> & \"cudzysłów\""_s
                                      << u"&lt;script&gt;alert(1)&lt;/script&gt; &amp; &quot;cudzysłów&quot;"_s;
        QTest::newRow("html comment hidden") << u"a <!-- ukryte --> b"_s << u"a  b"_s;
        QTest::newRow("comment hidden") << u"Widoczne %%ukryte [[X]]%% dalej"_s << u"Widoczne  dalej"_s;
        QTest::newRow("line breaks") << u"linia 1\nlinia 2"_s << u"linia 1<br/>linia 2"_s;
        QTest::newRow("escapes") << u"\\*nie kursywa\\* \\[[nie link]]"_s << u"*nie kursywa* [[nie link]]"_s;
        QTest::newRow("hard line break") << u"linia 1\\\nlinia 2\\\r\nkoniec \\"_s
                                         << u"linia 1<br/>linia 2<br/>koniec \\"_s;
        QTest::newRow("footnote") << u"tekst[^1]"_s << u"tekst<sup>1</sup>"_s;
        QTest::newRow("javascript text") << u"javascript:alert(1)"_s << u"javascript:alert(1)"_s;
    }

    void inlineMarkdown()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input), expected);
    }

    void links_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("wiki") << u"[[Strona]]"_s << link(u"page:Strona"_s, u"L"_s, u"Strona"_s);
        QTest::newRow("unresolved") << u"[[Brak]]"_s << link(u"page:Brak"_s, u"U"_s, u"Brak"_s);
        QTest::newRow("alias heading") << u"[[Projekty/Dom#Plan|dom]]"_s
                                       << link(u"page:Projekty%2FDom#Plan"_s, u"L"_s, u"dom"_s);
        QTest::newRow("block link") << u"[[Strona#^blok1]]"_s
                                    << link(u"page:Strona#^blok1"_s, u"L"_s, u"Strona › ^blok1"_s);
        // A known block: its text (escaped), like ((id)); cut after 60 characters.
        QTest::newRow("block link text") << u"[[Strona#^abc]]"_s
                                         << link(u"page:Strona#^abc"_s, u"L"_s,
                                                 u"Strona › Treść &lt;b&gt;bloku&lt;/b&gt;"_s);
        QTest::newRow("block link long text")
                << u"[[Strona#^dlugi]]"_s
                << link(u"page:Strona#^dlugi"_s, u"L"_s,
                        u"Strona › Zażółć gęślą jaźń i dalej: bardzo długi tekst bloku, który…"_s);
        // Cut at a whole character: never inside a surrogate pair or before a combining mark.
        QTest::newRow("block link cut before an emoji")
                << u"[[Strona#^emoji]]"_s
                << link(u"page:Strona#^emoji"_s, u"L"_s, u"Strona › "_s + QString(57, u'a') + u"…"_s);
        QTest::newRow("block link cut before an accent")
                << u"[[Strona#^akcent]]"_s
                << link(u"page:Strona#^akcent"_s, u"L"_s, u"Strona › "_s + QString(58, u'b') + u"…"_s);
        // The block's Markdown as the text it shows: links by their labels, no emphasis marks —
        // never raw "[[" cut by the 60 characters.
        QTest::newRow("block link text without markup")
                << u"[[Strona#^znaczniki]]"_s
                << link(u"page:Strona#^znaczniki"_s, u"L"_s,
                        u"Strona › Ekipa o 9:00 (umowa z sąsiadami z 2026-10-22 i domem, kod)"_s);
        // Another page's block with the same id is another block.
        QTest::newRow("block link same id elsewhere")
                << u"[[Inna#^abc]]"_s << link(u"page:Inna#^abc"_s, u"L"_s, u"Inna › Inny blok, to samo id"_s);
        QTest::newRow("same page block link")
                << u"[[#^abc]]"_s << link(u"page:#^abc"_s, u"L"_s, u"Treść &lt;b&gt;bloku&lt;/b&gt;"_s);
        QTest::newRow("block link alias")
                << u"[[Strona#^abc|tutaj]]"_s << link(u"page:Strona#^abc"_s, u"L"_s, u"tutaj"_s);
        QTest::newRow("same page") << u"[[#Sekcja]]"_s << link(u"page:#Sekcja"_s, u"L"_s, u"Sekcja"_s);
        QTest::newRow("polish") << u"[[Żółw]]"_s << link(u"page:%C5%BB%C3%B3%C5%82w"_s, u"L"_s, u"Żółw"_s);
        QTest::newRow("embed file") << u"![[obraz.png]]"_s << link(u"page:obraz.png"_s, u"L"_s, u"📎 obraz.png"_s);
        QTest::newRow("embed page") << u"![[Strona]]"_s << link(u"page:Strona"_s, u"L"_s, u"⧉ Strona"_s);
        QTest::newRow("external") << u"[strona](https://example.com/a?b=c&d=e)"_s
                                  << link(u"ext:https://example.com/a?b=c&amp;d=e"_s, u"L"_s, u"strona"_s);
        QTest::newRow("label markup") << u"[**pogrubiony** link](https://example.com)"_s
                                      << link(u"ext:https://example.com"_s, u"L"_s, u"<b>pogrubiony</b> link"_s);
        QTest::newRow("label no links") << u"[a [[X]] #t](https://e.pl)"_s
                                        << link(u"ext:https://e.pl"_s, u"L"_s, u"a\u00a0[[X]] #t"_s);
        QTest::newRow("mailto") << u"[x](mailto:jan@example.com)"_s
                                << link(u"ext:mailto:jan@example.com"_s, u"L"_s, u"x"_s);
        QTest::newRow("relative") << u"[notatka](Folder/Moja%20notatka.md#Sekcja)"_s
                                  << link(u"page:Folder%2FMoja%20notatka.md#Sekcja"_s, u"L"_s, u"notatka"_s);
        QTest::newRow("image") << u"![wykres](https://x.pl/a.png)"_s
                               << link(u"ext:https://x.pl/a.png"_s, u"L"_s, u"🖼&nbsp;wykres"_s);
        QTest::newRow("bare url") << u"https://example.com/x."_s
                                  << link(u"ext:https://example.com/x"_s, u"L"_s, u"https://example.com/x"_s) + u"."_s;
        QTest::newRow("autolink") << u"<https://a.pl>"_s << link(u"ext:https://a.pl"_s, u"L"_s, u"https://a.pl"_s);
        QTest::newRow("email autolink") << u"<jan@example.com>"_s
                                        << link(u"ext:mailto:jan@example.com"_s, u"L"_s, u"jan@example.com"_s);
        QTest::newRow("angle destination")
                << u"[x](<https://a.pl/x y>)"_s << link(u"ext:https://a.pl/x%20y"_s, u"L"_s, u"x"_s);
        QTest::newRow("tag") << u"#zakupy/spożywcze"_s << tag(u"zakupy%2Fspo%C5%BCywcze"_s, u"zakupy/spożywcze"_s);
        QTest::newRow("bracket tag") << u"#[[dwa słowa]]"_s << tag(u"dwa%20s%C5%82owa"_s, u"dwa słowa"_s);
        QTest::newRow("block ref") << u"((abc))"_s << link(u"block:abc"_s, u"L"_s, u"Treść &lt;b&gt;bloku&lt;/b&gt;"_s);
        QTest::newRow("block ref without markup")
                << u"((znaczniki))"_s
                << link(u"block:znaczniki"_s, u"L"_s, u"Umowa z sąsiadami z 2026-10-22 i domem"_s);
        QTest::newRow("unknown block ref") << u"((nieznany))"_s << link(u"block:nieznany"_s, u"U"_s, u"((nieznany))"_s);
        QTest::newRow("block embed") << u"!((abc))"_s
                                     << link(u"block:abc"_s, u"L"_s, u"⧉ Treść &lt;b&gt;bloku&lt;/b&gt;"_s);
        QTest::newRow("emphasis around link")
                << u"**pogrubiony [[Link]] tekst**"_s
                << u"<b>pogrubiony "_s + link(u"page:Link"_s, u"L"_s, u"Link"_s) + u" tekst</b>"_s;
        QTest::newRow("italic link") << u"*[[Link]]*"_s
                                     << u"<i>"_s + link(u"page:Link"_s, u"L"_s, u"Link"_s) + u"</i>"_s;
    }

    void links()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input), expected);
    }

    void linkTitles()
    {
        // RenderContext::linkTitle changes what a link shows, never where it goes.
        RenderContext c = context();
        c.linkTitle = [](const QString &target) -> std::optional<QString> {
            if (target == u"Strona")
                return u"Strona <główna>"_s;
            return std::nullopt;
        };
        const InlineRenderer r(c);
        QCOMPARE(r.render(u"[[Strona]]"_s), link(u"page:Strona"_s, u"L"_s, u"Strona &lt;główna&gt;"_s));
        QCOMPARE(r.render(u"[[Strona#Plan]]"_s),
                 link(u"page:Strona#Plan"_s, u"L"_s, u"Strona &lt;główna&gt; › Plan"_s));
        QCOMPARE(r.render(u"[[Strona|moja]]"_s), link(u"page:Strona"_s, u"L"_s, u"moja"_s)); // an alias wins
        QCOMPARE(r.render(u"[[Inna]]"_s), link(u"page:Inna"_s, u"L"_s, u"Inna"_s));
        QCOMPARE(r.render(u"![[Strona]]"_s), link(u"page:Strona"_s, u"L"_s, u"⧉ Strona &lt;główna&gt;"_s));

        // Daily notes by their date, in the UI language; the year when it is not this year's.
        const QString nb = QString(QChar(0x00A0));
        c.linkTitle = journalLinkTitle(VaultConfig(), dates::UiLocale::Polish, QDate(2026, 10, 14));
        const InlineRenderer pl(c);
        QCOMPARE(pl.render(u"[[2026-10-13]]"_s),
                 link(u"page:2026-10-13"_s, u"L"_s, u"wt"_s + nb + u"13"_s + nb + u"paź"_s));
        QCOMPARE(pl.render(u"[[2025-12-31]]"_s),
                 link(u"page:2025-12-31"_s, u"L"_s, u"31"_s + nb + u"gru"_s + nb + u"2025"_s));
        QCOMPARE(pl.render(u"[[2026-10-13|wtorek]]"_s), link(u"page:2026-10-13"_s, u"L"_s, u"wtorek"_s));
        QCOMPARE(pl.render(u"[[Żółw]]"_s), link(u"page:%C5%BB%C3%B3%C5%82w"_s, u"L"_s, u"Żółw"_s));
        c.linkTitle = journalLinkTitle(VaultConfig(), dates::UiLocale::English, QDate(2026, 10, 14));
        QCOMPARE(InlineRenderer(c).render(u"[[2026-10-13]]"_s),
                 link(u"page:2026-10-13"_s, u"L"_s, u"Tue,"_s + nb + u"Oct"_s + nb + u"13"_s));
        // No "today": always with the year.
        c.linkTitle = journalLinkTitle(VaultConfig(), dates::UiLocale::Polish, QDate());
        QCOMPARE(InlineRenderer(c).render(u"[[2026-10-13]]"_s),
                 link(u"page:2026-10-13"_s, u"L"_s, u"13"_s + nb + u"paź"_s + nb + u"2026"_s));
    }

    void oneLetterWordsKeepTheNextWord_data()
    {
        // Polish typography: "w", "z", "i", "a", "o", "u" never end a line — a no-break space
        // (U+00A0, written "~" here) joins them to the next word. Display only.
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("words") << u"Spotkanie w domu i z Łucją"_s << u"Spotkanie w~domu i~z~Łucją"_s;
        QTest::newRow("start") << u"W piątek o 9"_s << u"W~piątek o~9"_s;
        QTest::newRow("all letters") << u"a b, i c, o d, u e, w f, z g"_s << u"a~b, i~c, o~d, u~e, w~f, z~g"_s;
        QTest::newRow("capitals") << u"A potem U nas"_s << u"A~potem U~nas"_s;
        QTest::newRow("brackets and quotes") << u"(w domu) „z ogrodem”"_s << u"(w~domu) „z~ogrodem”"_s;
        QTest::newRow("not inside words") << u"aw b, ż c, ów d"_s << u"aw b, ż c, ów d"_s;
        QTest::newRow("before a link") << u"w [[Domu]]"_s << u"w~"_s + link(u"page:Domu"_s, u"L"_s, u"Domu"_s);
        QTest::newRow("before emphasis") << u"i **dalej**"_s << u"i~<b>dalej</b>"_s;
        QTest::newRow("before a formula") << u"i $x$"_s << u"i~<i>x</i>"_s;
        QTest::newRow("line end") << u"jest w\ndomu"_s << u"jest w<br/>domu"_s;
        QTest::newRow("last word") << u"mam i"_s << u"mam i"_s;
        QTest::newRow("two spaces") << u"w  domu"_s << u"w  domu"_s;
        QTest::newRow("code untouched") << u"`w domu` i"_s << Code + u"w domu</code> i"_s;
        QTest::newRow("after digits") << u"5 z 7"_s << u"5 z~7"_s;
    }
    void oneLetterWordsKeepTheNextWord()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input).replace(QChar(0x00A0), u'~'), expected);
    }

    void formulas_data()
    {
        // Formulas in a block body (docs/SYNTAX.md §11): the LaTeX source becomes rich text, the
        // delimiters vanish; text that only looks like a formula stays as written.
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("inline") << u"Pole koła: $\\pi r^2$."_s << u"Pole koła: <i>π</i><i>r</i><sup>2</sup>."_s;
        QTest::newRow("display inline") << u"$$\\frac{1}{2}$$"_s << u"½"_s;
        QTest::newRow("display keeps lines") << u"$$\na \\\\ b\n$$"_s << u"<i>a</i><br/><i>b</i>"_s;
        QTest::newRow("emphasis is math") << u"$a*b*c$"_s << u"<i>a</i> ∗ <i>b</i> ∗ <i>c</i>"_s;
        QTest::newRow("underscore is a subscript") << u"$x_1$ i $y$"_s << u"<i>x</i><sub>1</sub> i\u00a0<i>y</i>"_s;
        QTest::newRow("two formulas") << u"$x$ oraz $y^2$"_s << u"<i>x</i> oraz <i>y</i><sup>2</sup>"_s;
        QTest::newRow("in a link label") << u"[$x^2$](https://e.pl)"_s
                                         << link(u"ext:https://e.pl"_s, u"L"_s, u"<i>x</i><sup>2</sup>"_s);

        // Not formulas.
        QTest::newRow("prices") << u"kosztuje $5 i $10"_s << u"kosztuje $5 i\u00a0$10"_s;
        QTest::newRow("escaped dollar") << u"cena \\$100, a $x$ tyle"_s << u"cena $100, a\u00a0<i>x</i> tyle"_s;
        QTest::newRow("prices after the number") << u"Netflix 15$, Spotify 10$"_s << u"Netflix 15$, Spotify 10$"_s;
        QTest::newRow("hourly rates around a link")
                << u"Stawka 10$/h — patrz [[Plan]] w weekend 15$/h."_s
                << u"Stawka 10$/h — patrz "_s + link(u"page:Plan"_s, u"L"_s, u"Plan"_s) + u" w\u00a0weekend 15$/h."_s;
        QTest::newRow("shell variables") << u"export PATH=$PATH:$HOME/bin"_s << u"export PATH=$PATH:$HOME/bin"_s;
        QTest::newRow("dollar after a space") << u"$5 a wzór $x$"_s << u"$5 a\u00a0wzór <i>x</i>"_s;
        QTest::newRow("lone display") << u"Drogo $$ i wzór $x^2$"_s << u"Drogo $$ i\u00a0wzór <i>x</i><sup>2</sup>"_s;

        // `\(…\)` and `\[…\]` are escaped brackets (CommonMark), as the importers write them for
        // literal brackets: the brackets are shown, a later formula still renders.
        QTest::newRow("escaped parens") << u"\\(x \\le y\\)"_s << u"(x \\le y)"_s;
        QTest::newRow("escaped brackets") << u"\\[x = y\\]"_s << u"[x = y]"_s;
        QTest::newRow("importer index") << u"Indeks\\[2\\] i H2O"_s << u"Indeks[2] i\u00a0H2O"_s;
        QTest::newRow("importer double brackets") << u"\\[\\[Nie link\\]\\]"_s << u"[[Nie link]]"_s;
        QTest::newRow("importer link label") << u"Zob. [\\[1\\]](https://example.org)"_s
                                             << u"Zob. "_s + link(u"ext:https://example.org"_s, u"L"_s, u"[1]"_s);
        QTest::newRow("importer parens") << u"\\((x)) i $x$"_s << u"((x)) i\u00a0<i>x</i>"_s;
        QTest::newRow("importer footnote")
                << u"Zobacz \\[^1\\] i $x$ potem"_s << u"Zobacz [^1] i\u00a0<i>x</i> potem"_s;
        QTest::newRow("code span") << u"`$x$`"_s << Code + u"$x$</code>"_s;
        QTest::newRow("comment") << u"a %%$x$%% b"_s << u"a  b"_s;
        QTest::newRow("property line") << u"autor:: $x$"_s << u"<span style=\"color:M\">autor:</span> <i>x</i>"_s;
    }

    void formulas()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input), expected);
    }

    // The index (extractInline) counts exactly the tags and links the editor shows next to a
    // formula: what starts inside one is part of it (SYNTAX §11).
    void indexAgreesAboutFormulas_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("tag and link in formulas") << u"$x \\in A #B$ oraz $[[Strona]]$ i #poza [[Łąka]]"_s;
        QTest::newRow("link starts before the formula") << u"[[a $b]] c$ #d"_s;
        QTest::newRow("formula in a link label") << u"[$\\alpha$ i #tag](Wzory.md) #źródło"_s;
        QTest::newRow("url in a formula") << u"$f(https://example.org)$ i https://brulion.pl"_s;
        QTest::newRow("not over a blank line") << u"$a\n\n#b$"_s;
        QTest::newRow("display") << u"$$\n#b [[S]]\n$$ #d ((abc))"_s;
    }

    void indexAgreesAboutFormulas()
    {
        QFETCH(QString, input);
        const QString html = render(input);
        const InlineScan scan = extractInline(input);
        const qsizetype tags = html.count(u"<a href=\"tag:"_s);
        QCOMPARE(tags, scan.tags.size());
        QCOMPARE(html.count(u"<a href=\""_s) - tags, scan.links.size());
    }

    // A quote shows "- x" lines as bullets, but not inside a display formula between `$$` lines
    // (SYNTAX §11): there "- b" is part of the formula.
    void quotedDisplayFormulaKeepsItsLines()
    {
        const QString html =
                InlineRenderer(context()).renderBlock(u"> $$\n> a\n> - b\n> $$\n> - punkt"_s, BlockKind::Quote);
        QVERIFY2(html.contains(renderMath(u"\na\n- b\n"_s, true)), qPrintable(html));
        QCOMPARE(html.count(u"•"_s), 1);
        QVERIFY(html.contains(u"• punkt"_s));
        // Without a closing `$$` line the lines are bullets as before.
        QCOMPARE(InlineRenderer(context()).renderBlock(u"> $$\n> - b"_s, BlockKind::Quote).count(u"•"_s), 1);
    }

    void hostileInput_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("javascript link") << u"[x](javascript:alert(1))"_s << u"[x](javascript:alert(1))"_s;
        QTest::newRow("javascript upper") << u"[x](JaVaScRiPt:alert(1))"_s << u"[x](JaVaScRiPt:alert(1))"_s;
        QTest::newRow("data link") << u"[x](data:text/html;base64,PHNjcmlwdD4=)"_s
                                   << u"[x](data:text/html;base64,PHNjcmlwdD4=)"_s;
        QTest::newRow("file link") << u"[x](file:///etc/passwd)"_s << u"[x](file:///etc/passwd)"_s;
        QTest::newRow("javascript autolink") << u"<javascript:alert(1)>"_s << u"&lt;javascript:alert(1)&gt;"_s;
        QTest::newRow("entity trick") << u"[x](jav&#x09;ascript:alert(1))"_s
                                      << link(u"page:jav%26#x09%3Bascript%3Aalert%281%29"_s, u"L"_s, u"x"_s);
        QTest::newRow("alias html") << u"[[Strona|<img src=x onerror=alert(1)>]]"_s
                                    << link(u"page:Strona"_s, u"L"_s, u"&lt;img src=x onerror=alert(1)&gt;"_s);
        QTest::newRow("quote in target") << u"[[a\"b]]"_s << link(u"page:a%22b"_s, u"L"_s, u"a&quot;b"_s);
        QTest::newRow("html in label") << u"[<b>x</b>](https://e.pl)"_s
                                       << link(u"ext:https://e.pl"_s, u"L"_s, u"&lt;b&gt;x&lt;/b&gt;"_s);
        QTest::newRow("quote in url") << u"[x](https://e.pl/\"onmouseover=\"alert(1))"_s
                                      << link(u"ext:https://e.pl/&quot;onmouseover=&quot;alert(1)"_s, u"L"_s, u"x"_s);
        QTest::newRow("img tag") << u"<img src=\"https://tracker.example/p.gif\">"_s
                                 << u"&lt;img src=&quot;"_s
                                            + link(u"ext:https://tracker.example/p.gif"_s, u"L"_s,
                                                   u"https://tracker.example/p.gif"_s)
                                            + u"&quot;&gt;"_s;
        QTest::newRow("style tag") << u"<style>*{display:none}</style>"_s
                                   << u"&lt;style&gt;*{display:none}&lt;/style&gt;"_s;
    }

    void hostileInput()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input), expected);
    }

    void tasksAndChips_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        const QString todo = link(u"task:toggle"_s, u"L"_s, u"☐"_s) + u"&nbsp;"_s;
        QTest::newRow("overdue") << u"[ ] Zadanie 📅 2026-10-13"_s
                                 << todo + u"Zadanie "_s + chip(u"OF"_s, u"OB"_s, u"📅 wczoraj"_s);
        QTest::newRow("due tomorrow") << u"[/] Zadanie 📅 2026-10-15 10:00"_s
                                      << link(u"task:toggle"_s, u"L"_s, u"◐"_s) + u"&nbsp;Zadanie "_s
                                                 + chip(u"DF"_s, u"DB"_s, u"📅 jutro 10:00"_s);
        QTest::newRow("done not overdue")
                << u"[x] Zrobione 📅 2026-10-13 ✅ 2026-10-14"_s
                << link(u"task:toggle"_s, u"XF"_s, u"☑"_s) + u"&nbsp;Zrobione "_s
                           + chip(u"DF"_s, u"DB"_s, u"📅 wczoraj"_s) + u" "_s + chip(u"XF"_s, u"XB"_s, u"✅ dziś"_s);
        QTest::newRow("cancelled") << u"[-] Odwołane ❌ 2026-10-14"_s
                                   << link(u"task:toggle"_s, u"XF"_s, u"☒"_s) + u"&nbsp;Odwołane "_s
                                              + chip(u"XF"_s, u"XB"_s, u"❌ dziś"_s);
        // A code span right after the checkbox is shown, not taken with the marker's whitespace.
        QTest::newRow("code span first") << u"[ ] `npm i` w katalogu"_s
                                         << todo + Code + u"npm i</code> w\u00a0katalogu"_s;
        QTest::newRow("no task") << u"Zwykły blok 📅 2026-10-13"_s
                                 << u"Zwykły blok "_s + chip(u"DF"_s, u"DB"_s, u"📅 wczoraj"_s);
        QTest::newRow("reminder") << u"Spotkanie ⏰ 2026-10-14 09:00"_s
                                  << u"Spotkanie "_s + reminderChip(10, 28, u"RF"_s, u"RB"_s, u"⏰ dziś 09:00"_s);
        QTest::newRow("alarm") << u"Pobudka ⏰! 2026-10-15 06:30"_s
                               << u"Pobudka "_s + reminderChip(8, 27, u"AF"_s, u"AB"_s, u"🔔 jutro 06:30"_s);
        QTest::newRow("compat reminder") << u"(@2026-10-14 18:00) dzwonić"_s
                                         << reminderChip(0, 19, u"RF"_s, u"RB"_s, u"⏰ dziś 18:00"_s) + u" dzwonić"_s;
        QTest::newRow("recurrence") << u"Podlać 🔁 every week on Monday"_s
                                    << u"Podlać "_s + chip(u"EF"_s, u"EB"_s, u"🔁 co tydzień w poniedziałek"_s);
        QTest::newRow("polish recurrence")
                << u"Czynsz 🔁 co miesiąc"_s << u"Czynsz "_s + chip(u"EF"_s, u"EB"_s, u"🔁 co miesiąc"_s);
        // A 🔁 whose text is no rule stays text (it never swallows the rest of the line in a chip).
        QTest::newRow("raw recurrence") << u"x 🔁 every blue moon"_s << u"x 🔁 every blue moon"_s;
        QTest::newRow("other dates") << u"Plan ⏳ 2026-10-15 🛫 2026-10-14 ➕ 2026-10-01"_s
                                     << u"Plan "_s + chip(u"SF"_s, u"SB"_s, u"⏳ jutro"_s) + u" "_s
                                                + chip(u"NF"_s, u"NB"_s, u"🛫 dziś"_s) + u" "_s
                                                + chip(u"NF"_s, u"NB"_s, u"➕ czw 1 paź"_s);
        QTest::newRow("priorities") << u"Ważne ⏫ [#A]"_s << u"Ważne ⏫ "_s + chip(u"NF"_s, u"NB"_s, u"A"_s);
        QTest::newRow("logseq task") << u"TODO Zadzwonić\nSCHEDULED: <2026-10-15 Thu .+1w>"_s
                                     << todo + u"Zadzwonić<br/>"_s + chip(u"SF"_s, u"SB"_s, u"⏳ jutro"_s) + u" "_s
                                                + chip(u"EF"_s, u"EB"_s, u"🔁 co tydzień po wykonaniu"_s);
        QTest::newRow("deadline overdue") << u"LATER x\nDEADLINE: <2026-10-01 Thu>"_s
                                          << todo + u"x<br/>"_s + chip(u"OF"_s, u"OB"_s, u"📅 czw 1 paź"_s);
        QTest::newRow("task ids")
                << u"🆔 abc123 zadanie ⛔ x1,y2"_s
                << u"<span style=\"color:M\">🆔 abc123</span> zadanie <span style=\"color:M\">⛔ x1,y2</span>"_s;
        QTest::newRow("token in code") << u"`📅 2026-10-13` tekst"_s << Code + u"📅 2026-10-13</code> tekst"_s;
    }

    void recurrenceChips_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty rule") << u"Podlać 🔁"_s << u"Podlać 🔁"_s;
        QTest::newRow("no rule") << u"Spotkanie 🔁 kiedyś tam, zobaczymy"_s << u"Spotkanie 🔁 kiedyś tam, zobaczymy"_s;
        QTest::newRow("rule then text")
                << u"Podlać 🔁 co tydzień kwiaty na balkonie"_s
                << u"Podlać "_s + chip(u"EF"_s, u"EB"_s, u"🔁 co tydzień"_s) + u" kwiaty na balkonie"_s;
        QTest::newRow("english rule then text")
                << u"Water 🔁 every day the żółte plants"_s
                << u"Water "_s + chip(u"EF"_s, u"EB"_s, u"🔁 codziennie"_s) + u" the żółte plants"_s;
        QTest::newRow("comma after the rule")
                << u"🔁 co miesiąc, czynsz"_s << chip(u"EF"_s, u"EB"_s, u"🔁 co miesiąc"_s) + u", czynsz"_s;
        QTest::newRow("rule before a tag")
                << u"x 🔁 every day #dom"_s << u"x "_s + chip(u"EF"_s, u"EB"_s, u"🔁 codziennie"_s) + u" "_s + tag(u"dom"_s, u"dom"_s);
        QTest::newRow("text before a due date")
                << u"x 🔁 nie wiem 📅 2026-10-15"_s << u"x 🔁 nie wiem "_s + chip(u"DF"_s, u"DB"_s, u"📅 jutro"_s);
        QTest::newRow("escaped text after the rule")
                << u"🔁 every week <b>x</b>"_s << chip(u"EF"_s, u"EB"_s, u"🔁 co tydzień"_s) + u" &lt;b&gt;x&lt;/b&gt;"_s;
    }

    void recurrenceChips()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input), expected);
    }

    void longChipsWrapBetweenWords()
    {
        // A long chip may wrap at word boundaries (never inside a word); the emoji and short
        // words ("co", "w", "2") stay with the next word.
        const QString html = render(u"Raport 🔁 every 2 weeks on Wednesday, Saturday, Sunday"_s);
        QCOMPARE(html, u"Raport <span style=\"background-color:EB;color:EF\">&nbsp;🔁&nbsp;co&nbsp;2&nbsp;tygodnie "
                       u"w&nbsp;środę, sobotę, niedzielę&nbsp;</span>"_s);
        // Short chips never wrap: no breakable space in their visible text.
        QString visible = render(u"📅 2026-10-20"_s);
        visible.remove(QRegularExpression(u"<[^>]*>"_s));
        QCOMPARE(visible, u"&nbsp;📅&nbsp;wt&nbsp;20&nbsp;paź&nbsp;"_s);
    }

    void tasksAndChips()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input), expected);
    }

    void hiddenAndProperties_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("property") << u"kwota:: 1200 zł"_s << u"<span style=\"color:M\">kwota:</span> 1200 zł"_s;
        QTest::newRow("property link") << u"autor:: [[Lem]]"_s
                                       << u"<span style=\"color:M\">autor:</span> "_s
                                                  + link(u"page:Lem"_s, u"L"_s, u"Lem"_s);
        QTest::newRow("empty property") << u"tekst\npusty::"_s << u"tekst<br/><span style=\"color:M\">pusty:</span>"_s;
        QTest::newRow("id lines") << u"tekst\nid:: 64f0a1b2\ncollapsed:: true"_s << u"tekst"_s;
        QTest::newRow("id line in middle") << u"a\nid:: x\nb"_s << u"a<br/>b"_s;
        QTest::newRow("block id") << u"tekst ^abc123"_s << u"tekst"_s;
        QTest::newRow("block id lines") << u"pierwsza ^a1\ndruga ^b2"_s << u"pierwsza<br/>druga"_s;
        QTest::newRow("drawer") << u"Zrobione\n:LOGBOOK:\nCLOCK: x\n:END:\nnotatka"_s << u"Zrobione<br/>notatka"_s;
    }

    void hiddenAndProperties()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(render(input), expected);
    }

    void transclusionsHidden()
    {
        // The editor shows embedded pages, sections and blocks in frames: their syntax is hidden,
        // attachments and plain links stay.
        RenderContext c = context();
        c.hideTransclusions = true;
        const InlineRenderer r(c);
        QCOMPARE(r.render(u"Zobacz ![[Budżet#Październik]] i !((abc)) tutaj"),
                 u"Zobacz  i  tutaj"_s);
        QCOMPARE(r.render(u"![[Strona]] ![[Strona#^id1]]"), u" "_s);
        QVERIFY(r.render(u"![[plan.png]]"_s).contains(u"📎"_s));
        QVERIFY(r.render(u"[[Strona]] ((abc))"_s).contains(u"block:abc"_s));
        QVERIFY(r.render(u"[[Strona]] ((abc))"_s).contains(u"page:Strona"_s));
        // Off by default (query results, table cells): placeholder links.
        QVERIFY(render(u"![[Budżet#Październik]]"_s).contains(u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s));
    }

    void taskCheckboxHidden()
    {
        RenderContext c = context();
        c.taskCheckbox = false;
        QCOMPARE(InlineRenderer(c).render(u"[ ] Zadanie"), u"Zadanie"_s);
        QCOMPARE(InlineRenderer(c).render(u"DONE Zrobione"), u"Zrobione"_s);
    }

    void englishLocale()
    {
        const InlineRenderer r(context(dates::UiLocale::English));
        QCOMPARE(r.render(u"x 📅 2026-10-15 🔁 every week on Monday"),
                 u"x "_s + chip(u"DF"_s, u"DB"_s, u"📅 tomorrow"_s) + u" "_s
                         + chip(u"EF"_s, u"EB"_s, u"🔁 every week on Monday"_s));
        QCOMPARE(r.render(u"⏰ 2026-10-20 08:00"), reminderChip(0, 18, u"RF"_s, u"RB"_s, u"⏰ Tue, Oct 20 08:00"_s));
    }

    void withoutCallbacksOrToday()
    {
        RenderContext c;
        c.palette.link = u"L"_s;
        const InlineRenderer r(c);
        QCOMPARE(r.render(u"[[Brak]] `= 2 + 2`"),
                 link(u"page:Brak"_s, u"L"_s, u"Brak"_s)
                         + u" <code style=\"background-color:#f3eee2\">= 2 + 2</code>"_s);
        QCOMPARE(r.render(u"((abc))"),
                 u"<a href=\"block:abc\" style=\"color:#7a5ea3;text-decoration:none\">((abc))</a>"_s);
        QVERIFY(r.render(u"📅 2026-10-15").contains(u"15&nbsp;paź&nbsp;2026"));
    }

    void paletteIsSanitised()
    {
        RenderContext c = context();
        c.palette.link = u"red\" onclick=\"alert(1)"_s;
        const QString out = InlineRenderer(c).render(u"[[Strona]]");
        QVERIFY(!out.contains(u"onclick=\""));
        QCOMPARE(out.count(u'"'), 4);
    }

    void escapeHelper() { QCOMPARE(InlineRenderer::escape(u"a<b\nc & \"d\""), u"a&lt;b<br/>c &amp; &quot;d&quot;"_s); }

    void renderBlocks()
    {
        const InlineRenderer r(context());
        QCOMPARE(r.renderBlock(u"## Tytuł ##", BlockKind::Heading), u"Tytuł"_s);
        QCOMPARE(r.renderBlock(u"# **Ważne** #tag", BlockKind::Heading), u"<b>Ważne</b> "_s + tag(u"tag"_s, u"tag"_s));
        QCOMPARE(r.renderBlock(u"Tytuł\n===", BlockKind::Heading), u"Tytuł"_s);
        QCOMPARE(r.renderBlock(u"> a\n> b", BlockKind::Quote), u"a<br/>b"_s);
        QCOMPARE(r.renderBlock(u"> - punkt\n> - [[Link]]", BlockKind::Quote),
                 u"• punkt<br/>• "_s + link(u"page:Link"_s, u"L"_s, u"Link"_s));
        QCOMPARE(r.renderBlock(u"> [!note] Uwaga\n> treść", BlockKind::Callout), u"<b>Uwaga</b><br/>treść"_s);
        QCOMPARE(r.renderBlock(u"> [!tip]\n> x", BlockKind::Callout), u"<b>Tip</b><br/>x"_s);
        QCOMPARE(r.renderBlock(u"> [!warning]- Zwinięte", BlockKind::Callout), u"<b>Zwinięte</b>"_s);
        QCOMPARE(r.renderBlock(u"```js\nif (a < b) {}\n```", BlockKind::Code),
                 u"<pre>```js\nif (a &lt; b) {}\n```</pre>"_s);
        QCOMPARE(r.renderBlock(u"---", BlockKind::Rule), u"<hr/>"_s);
        QCOMPARE(r.renderBlock(u"<div onclick=\"x\">a</div>", BlockKind::Html),
                 u"&lt;div onclick=&quot;x&quot;&gt;a&lt;/div&gt;"_s);
        QCOMPARE(r.renderBlock(u"**p**", BlockKind::Paragraph), u"<b>p</b>"_s);
        QCOMPARE(r.renderBlock(u"[ ] t", BlockKind::ListItem), link(u"task:toggle"_s, u"L"_s, u"☐"_s) + u"&nbsp;t"_s);
        // Only list items are tasks (SYNTAX §5): elsewhere a leading marker is text.
        QCOMPARE(r.renderBlock(u"[ ] t", BlockKind::Paragraph), u"[ ] t"_s);
        QCOMPARE(r.renderBlock(u"TODO zadanie?", BlockKind::Paragraph), u"TODO zadanie?"_s);
        QCOMPARE(r.renderBlock(u"# DONE nagłówek", BlockKind::Heading), u"DONE nagłówek"_s);
        QCOMPARE(r.renderBlock(u"> [x] cytat", BlockKind::Quote), u"[x] cytat"_s);
        QCOMPARE(r.renderBlock(u"> [!note] TODO tytuł\n> LATER treść", BlockKind::Callout),
                 u"<b>TODO tytuł</b><br/>LATER treść"_s);
        QCOMPARE(r.render(u"TODO t"), link(u"task:toggle"_s, u"L"_s, u"☐"_s) + u"&nbsp;t"_s);
    }

    void reminderRangesPointIntoTheBody_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("chips");
        QTest::newRow("paragraph") << u"Spotkanie ⏰ 2026-10-15 09:00\ndruga ⏰! 2026-10-16 06:30"_s
                                   << int(BlockKind::Paragraph) << 2;
        QTest::newRow("list item") << u"[ ] zadanie 😀 ⏰ 2026-10-15 09:00"_s << int(BlockKind::ListItem) << 1;
        QTest::newRow("atx heading") << u"## Spotkanie ⏰ 2026-10-15 09:00 ##"_s << int(BlockKind::Heading) << 1;
        QTest::newRow("setext heading") << u"Spotkanie (@2026-10-15 09:00)\n==="_s << int(BlockKind::Heading) << 1;
        QTest::newRow("quote") << u"> Cytat ⏰ 2026-10-15 09:00\n> > głębiej ⏰ 2026-10-16"_s << int(BlockKind::Quote)
                               << 2;
        QTest::newRow("quote with list") << u"> - punkt ⏰ 2026-10-15 09:00\r\n>   * pod ⏰! 2026-10-16 07:00"_s
                                         << int(BlockKind::Quote) << 2;
        QTest::newRow("callout")
                << u"> [!todo] Tytuł ⏰ 2026-10-14 08:00\n> - [ ] zadanie ⏰ 2026-10-15 09:00\n> tekst ⏰ 2026-10-16"_s
                << int(BlockKind::Callout) << 3;
        QTest::newRow("callout without space")
                << u">[!note]⏰ 2026-10-15\n>żółw ⏰ 2026-10-16"_s << int(BlockKind::Callout) << 2;
    }

    void reminderRangesPointIntoTheBody()
    {
        // Review regression: the UI edits [from, to) of the block body when a reminder chip is
        // clicked, so the range must point at the token in the body even when renderBlock() strips
        // heading markers or quote / callout prefixes first.
        QFETCH(QString, body);
        QFETCH(int, kind);
        QFETCH(int, chips);
        const QString out = InlineRenderer(context()).renderBlock(body, BlockKind(kind));
        static const QRegularExpression re(u"reminder:(\\d+)-(\\d+)"_s);
        int found = 0;
        for (auto it = re.globalMatch(out); it.hasNext(); ++found) {
            const auto m = it.next();
            const qsizetype from = m.captured(1).toLongLong();
            const qsizetype to = m.captured(2).toLongLong();
            QVERIFY2(from >= 0 && from < to && to <= body.size(), qPrintable(out));
            const QString token = body.sliced(from, to - from);
            const QList<Token> tokens = findTokens(token);
            QVERIFY2(tokens.size() == 1 && tokens.first().from == 0 && tokens.first().to == token.size()
                             && (tokens.first().kind == TokenKind::Reminder
                                 || tokens.first().kind == TokenKind::ReminderCompat),
                     qPrintable(token + u" in "_s + out));
        }
        QCOMPARE(found, chips);
    }

    void dateLinksPointIntoTheBody_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QStringList>("kinds"); // the kinds of the date: links, in order

        QTest::newRow("task fields") << u"[ ] Zapłacić za żółte światło 📅 2026-10-20 ⏳ 2026-10-18 09:00 🛫 2026-10-17 ➕ 2026-10-14"_s
                                     << int(BlockKind::ListItem)
                                     << QStringList{u"due"_s, u"scheduled"_s, u"start"_s, u"created"_s};
        QTest::newRow("done and cancelled") << u"[x] gotowe ✅ 2026-10-15\n[-] nie ❌ 2026-10-16"_s
                                            << int(BlockKind::ListItem) << QStringList{u"done"_s, u"cancelled"_s};
        QTest::newRow("heading") << u"## Plan 📅 2026-10-20"_s << int(BlockKind::Heading) << QStringList{u"due"_s};
        QTest::newRow("quote") << u"> cytat ⏳ 2026-10-18"_s << int(BlockKind::Quote) << QStringList{u"scheduled"_s};
        QTest::newRow("reminder is no date: link") << u"tekst ⏰ 2026-10-16 09:00"_s << int(BlockKind::Paragraph) << QStringList{};
        QTest::newRow("logseq planning is no date: link") << u"TODO zadanie\nSCHEDULED: <2026-10-15 Thu>"_s
                                                          << int(BlockKind::ListItem) << QStringList{};
        QTest::newRow("in code") << u"`📅 2026-10-20` poza kodem 📅 2026-10-21"_s << int(BlockKind::Paragraph)
                                 << QStringList{u"due"_s};
    }

    void dateLinksPointIntoTheBody()
    {
        QFETCH(QString, body);
        QFETCH(int, kind);
        QFETCH(QStringList, kinds);
        RenderContext ctx = context();
        QVERIFY(!InlineRenderer(ctx).renderBlock(body, BlockKind(kind)).contains(u"date:"_s)); // off by default
        ctx.dateLinks = true;
        const QString out = InlineRenderer(ctx).renderBlock(body, BlockKind(kind));
        static const QRegularExpression re(u"href=\"date:(\\d+)-(\\d+)\""_s);
        QStringList found;
        for (auto it = re.globalMatch(out); it.hasNext();) {
            const auto m = it.next();
            const qsizetype from = m.captured(1).toLongLong();
            const qsizetype to = m.captured(2).toLongLong();
            QVERIFY2(from >= 0 && from < to && to <= body.size(), qPrintable(out));
            const QString token = body.sliced(from, to - from);
            const QList<Token> tokens = findTokens(token);
            QVERIFY2(tokens.size() == 1 && tokens.first().from == 0 && tokens.first().to == token.size(),
                     qPrintable(token + u" in "_s + out));
            const TokenKind k = tokens.first().kind;
            found.append(k == TokenKind::Due         ? u"due"_s
                         : k == TokenKind::Scheduled ? u"scheduled"_s
                         : k == TokenKind::Start     ? u"start"_s
                         : k == TokenKind::Created   ? u"created"_s
                         : k == TokenKind::Done      ? u"done"_s
                         : k == TokenKind::Cancelled ? u"cancelled"_s
                                                     : u"?"_s);
        }
        QCOMPARE(found, kinds);
    }

    void tokenLinksPointIntoTheBody_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QStringList>("links"); // "scheme:text of the range", in order

        QTest::newRow("recurrence") << u"[ ] Podlać kwiaty 🔁 co tydzień w poniedziałek 📅 2026-10-20"_s
                                    << int(BlockKind::ListItem)
                                    << QStringList{u"recur:🔁 co tydzień w poniedziałek"_s, u"date:📅 2026-10-20"_s};
        QTest::newRow("recurrence prefix") << u"Zakupy 🔁 co tydzień kupić mleko"_s << int(BlockKind::Paragraph)
                                           << QStringList{u"recur:🔁 co tydzień"_s};
        QTest::newRow("no rule is no link") << u"Notatka 🔁 kiedyś tam"_s << int(BlockKind::Paragraph) << QStringList{};
        QTest::newRow("priorities") << u"[ ] Pilne ⏫ zadanie [#B]"_s << int(BlockKind::ListItem)
                                    << QStringList{u"prio:⏫"_s, u"prio:[#B]"_s};
        QTest::newRow("journal days") << u"Spotkanie [[2026-10-20]] i ![[2026-10-21|środa]] oraz [[Strona]]"_s
                                      << int(BlockKind::Paragraph)
                                      << QStringList{u"day:[[2026-10-20]]"_s}; // (an embed is a frame of its own)
        QTest::newRow("journal heading link stays a page") << u"[[2026-10-20#Plan]]"_s << int(BlockKind::Paragraph)
                                                           << QStringList{};
        QTest::newRow("heading offsets") << u"## Żółw 🔼 🔁 every day"_s << int(BlockKind::Heading)
                                         << QStringList{u"prio:🔼"_s, u"recur:🔁 every day"_s};
        QTest::newRow("quote offsets") << u"> cytat [[2026-10-20]] 🔽"_s << int(BlockKind::Quote)
                                       << QStringList{u"day:[[2026-10-20]]"_s, u"prio:🔽"_s};
        QTest::newRow("in code") << u"`⏫ [[2026-10-20]]` 🔺"_s << int(BlockKind::Paragraph) << QStringList{u"prio:🔺"_s};
    }

    void tokenLinksPointIntoTheBody()
    {
        QFETCH(QString, body);
        QFETCH(int, kind);
        QFETCH(QStringList, links);
        RenderContext ctx = context();
        const QString plain = InlineRenderer(ctx).renderBlock(body, BlockKind(kind));
        for (const QString &scheme : {u"recur:"_s, u"prio:"_s, u"day:"_s})
            QVERIFY2(!plain.contains(scheme), qPrintable(plain)); // off by default
        ctx.dateLinks = true;
        ctx.dayLinks = true;
        const QString out = InlineRenderer(ctx).renderBlock(body, BlockKind(kind));
        static const QRegularExpression re(u"href=\"(date|recur|prio|day):(\\d+)-(\\d+)\""_s);
        QStringList found;
        for (auto it = re.globalMatch(out); it.hasNext();) {
            const auto m = it.next();
            const qsizetype from = m.captured(2).toLongLong();
            const qsizetype to = m.captured(3).toLongLong();
            QVERIFY2(from >= 0 && from < to && to <= body.size(), qPrintable(out));
            found.append(m.captured(1) + u':' + body.sliced(from, to - from));
        }
        QCOMPARE(found, links);
        // Without the flags nothing else changes: the same text, colours and chips.
        ctx.dateLinks = false;
        ctx.dayLinks = false;
        QCOMPARE(InlineRenderer(ctx).renderBlock(body, BlockKind(kind)), plain);
    }

    void onlyWhitelistedTags()
    {
        const QStringList pieces{u"**"_s,
                                 u"*"_s,
                                 u"_"_s,
                                 u"~~"_s,
                                 u"=="_s,
                                 u"`"_s,
                                 u"[["_s,
                                 u"]]"_s,
                                 u"[x]("_s,
                                 u")"_s,
                                 u"<script>"_s,
                                 u"<img src=x>"_s,
                                 u"\""_s,
                                 u"'"_s,
                                 u"&"_s,
                                 u"#tag "_s,
                                 u"((abc))"_s,
                                 u"📅 2026-10-15"_s,
                                 u"⏰ 2026-10-15 09:00"_s,
                                 u"🔁 every day"_s,
                                 u"%%"_s,
                                 u"\\"_s,
                                 u"\n"_s,
                                 u"key:: "_s,
                                 u"https://e.pl/a "_s,
                                 u"javascript:"_s,
                                 u"![["_s,
                                 u"^id"_s,
                                 u" "_s,
                                 u"żółw"_s,
                                 u"[ ] "_s,
                                 u"<!--"_s,
                                 u"-->"_s,
                                 u"[^1]"_s};
        static const QRegularExpression tagRe(u"<(/?)([A-Za-z]+)"_s);
        const QSet<QString> allowed{u"a"_s, u"b"_s, u"i"_s, u"s"_s, u"code"_s, u"span"_s, u"sup"_s, u"br"_s};
        quint32 seed = 5;
        const auto rnd = [&](quint32 n) {
            seed = (seed * 1103515245u + 12345u) % 2147483648u;
            return seed % n;
        };
        const InlineRenderer r(context());
        for (int k = 0; k < 400; ++k) {
            QString s;
            const quint32 len = 1 + rnd(40);
            for (quint32 i = 0; i < len; ++i)
                s += pieces[rnd(quint32(pieces.size()))];
            const QString out = r.render(s);
            auto it = tagRe.globalMatch(out);
            QStringList open;
            while (it.hasNext()) {
                const auto m = it.next();
                const QString name = m.captured(2).toLower();
                QVERIFY2(allowed.contains(name), qPrintable(s + u" -> "_s + out));
                if (name == u"br")
                    continue;
                if (m.captured(1).isEmpty()) {
                    open.append(name);
                } else {
                    QVERIFY2(!open.isEmpty() && open.last() == name,
                             qPrintable(u"bad nesting: "_s + s + u" -> "_s + out));
                    open.removeLast();
                }
            }
            QVERIFY2(open.isEmpty(), qPrintable(u"unclosed: "_s + s + u" -> "_s + out));
            QVERIFY(!out.contains(u"href=\"javascript", Qt::CaseInsensitive));
        }
    }

    void unmatchedDelimitersAreLinear()
    {
        QString s;
        for (int i = 0; i < 20000; ++i)
            s += u"*a "_s;
        for (int i = 0; i < 20000; ++i)
            s += u"b_ ~~c "_s;
        QElapsedTimer t;
        t.start();
        const QString out = render(s);
        QVERIFY(out.startsWith(u"*a *a"));
        QVERIFY2(t.elapsed() < 5000, qPrintable(QString::number(t.elapsed())));
    }

    void largeInputIsFast()
    {
        QString s;
        for (int i = 0; i < 5000; ++i)
            s += u"**a* _b_ [[c]] #d `e` 📅 2026-10-15 ~~f "_s;
        QElapsedTimer t;
        t.start();
        const QString out = render(s);
        QVERIFY(!out.isEmpty());
        QVERIFY2(t.elapsed() < 5000, qPrintable(QString::number(t.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestRender)
#include "tst_markdown_render.moc"
