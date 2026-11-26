#include "gallery.h"

#include "qthost.h"
#include "text/fonts.h"

#include <QDir>
#include <QString>
#include <QStringList>

#include <cmath>
#include <cstdio>

namespace brulion::atrament::gallery {

namespace {

std::u16string u16(const char *ascii)
{
    std::u16string s;
    while (*ascii)
        s += char16_t(*ascii++);
    return s;
}

std::u16string number(double v, int decimals = 0)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return u16(buf);
}

const char16_t *const kSections[] = { u"Typografia", u"Kontrolki", u"Długa lista", u"Rysowanie" };
const char16_t *const kSectionIcons[] = { u"Aa", u"◉", u"≡", u"✏" };

Div card()
{
    const Theme &t = theme();
    return div().flexCol().gap(10).p(16).bg(t.surface).border(1).borderColor(t.border).rounded(14).shadowMd();
}

Div heading(std::u16string_view title, std::u16string_view subtitle)
{
    const Theme &t = theme();
    return div()
            .flexCol()
            .gap(4)
            .child(div().textSize(22).fontSemibold().textColor(t.text).child(title))
            .child(div().textSize(13).textColor(t.muted).child(subtitle));
}

} // namespace

const Theme &theme()
{
    static const Theme t;
    return t;
}

std::shared_ptr<FontCollection> loadFonts()
{
    auto fonts = std::make_shared<FontCollection>();
    QStringList files;
    for (const QString &dir : { QStringLiteral(":/atrament/fonts/ui"), QStringLiteral(":/atrament/fonts") }) {
        const QDir d(dir);
        for (const QString &f : d.entryList({ QStringLiteral("*.ttf"), QStringLiteral("*.otf") }, QDir::Files))
            files << d.filePath(f);
    }
    addQtFonts(*fonts, files);
    fonts->setFallbackFamilies({ u"Brulion Emoji", u"Brulion Math", u"Brulion Inter" });
    return fonts;
}

Gallery::Gallery(Context<Gallery> &cx)
    : m_focus(cx.newFocusHandle())
    , m_search(cx.newEntity<TextInput>(TextInputOptions { u"Szukaj w notatkach…", false, InputHandler::Purpose::Text, {} }))
    , m_note(cx.newEntity<TextInput>(TextInputOptions { u"Napisz coś…", true, InputHandler::Purpose::Multiline,
              u"Jutro o 9 zadzwonić do Łucji.\nKupić: chleb, masło, żurek." }))
{
}

AnyElement Gallery::render(Window &window, Context<Gallery> &cx)
{
    const Theme &t = theme();
    AnyElement content;
    switch (section) {
    case 0:
        content = typography(window, cx);
        break;
    case 1:
        content = controls(window, cx);
        break;
    case 2:
        content = longList(window, cx);
        break;
    default:
        content = drawing(window, cx);
        break;
    }
    return div()
            .sizeFull()
            .flexCol()
            .bg(t.background)
            .fontFamily(u"Brulion Inter")
            .textColor(t.text)
            .textSize(14)
            .child(div()
                            .flexRow()
                            .flex1()
                            .minH(0)
                            .child(sidebar(window, cx))
                            .child(div()
                                            .id(u"content")
                                            .flex1()
                                            .minW0()
                                            .flexCol()
                                            .overflowYScroll()
                                            .p(28)
                                            .gap(20)
                                            .child(std::move(content))))
            .child(footer(window, cx));
}

AnyElement Gallery::sidebar(Window &, Context<Gallery> &cx)
{
    const Theme &t = theme();
    ArenaVector<AnyElement> items;
    for (int i = 0; i < 4; ++i) {
        const bool selected = section == i;
        items.push_back(div()
                        .id(ElementId(u"section", std::uint64_t(i)))
                        .flexRow()
                        .itemsCenter()
                        .gap(10)
                        .px(12)
                        .h(38)
                        .rounded(10)
                        .cursorPointer()
                        .textColor(selected ? t.text : t.muted)
                        .when(selected, [&](Div &&d) { return std::move(d).bg(t.raised); })
                        .hover([](Style &s) { s.background = Background::solid(theme().hover); })
                        .onClick(cx.listener([i](Gallery &g, const ClickEvent &, Window &, Context<Gallery> &cx) {
                            g.section = i;
                            cx.notify();
                        }))
                        .child(div().w(22).textCenter().textColor(selected ? t.accent : t.faint).child(kSectionIcons[i]))
                        .child(div().fontMedium().child(kSections[i])));
    }
    return div()
            .w(220)
            .flexShrink0()
            .flexCol()
            .gap(4)
            .p(14)
            .bg(t.surface)
            .borderR(1)
            .borderColor(t.border)
            .child(div()
                            .flexRow()
                            .itemsCenter()
                            .gap(10)
                            .px(8)
                            .pb(18)
                            .pt(4)
                            .child(div().size(30).rounded(9).bg(Background::linear(135, t.accent, rgb(0xc58bff))))
                            .child(div().flexCol()
                                            .child(div().textSize(16).fontBold().child(u"Atrament"))
                                            .child(div().textSize(11).textColor(t.faint).child(u"Brulion UI · M0"))))
            .children(items);
}

AnyElement Gallery::typography(Window &, Context<Gallery> &cx)
{
    const Theme &t = theme();
    const std::u16string_view paragraph
            = u"Zażółć gęślą jaźń. Brulion to notatki w zwykłych plikach Markdown: dziennik, zadania, przypomnienia "
              u"i obliczenia — wszystko offline. Ten akapit łamie się w kolumnie, której szerokość zmieniasz "
              u"przyciskami poniżej; tekst kształtuje HarfBuzz, a glify rysuje własny rasteryzator.";
    auto widthButton = [&](const char16_t *label, float delta) {
        return div()
                .id(label)
                .px(10)
                .py(5)
                .rounded(8)
                .bg(t.raised)
                .border(1)
                .borderColor(t.border)
                .cursorPointer()
                .hover([](Style &s) { s.background = Background::solid(theme().hover); })
                .onClick(cx.listener([delta](Gallery &g, const ClickEvent &, Window &, Context<Gallery> &cx) {
                    g.columnWidth = std::clamp(g.columnWidth + delta, 160.0f, 900.0f);
                    cx.notify();
                }))
                .child(label);
    };
    TextStyleRefinement bold;
    bold.fontWeight = FontWeight { FontWeight::Bold };
    TextStyleRefinement accent;
    accent.color = t.accent;
    accent.underline = UnderlineStyle {};
    TextStyleRefinement misspelt;
    misspelt.underline = UnderlineStyle { t.danger, 1, true };
    TextStyleRefinement mark;
    mark.background = rgba(0xf2c14e40);
    TextStyleRefinement struck;
    struck.strikethrough = true;
    struck.color = t.muted;
    const std::u16string_view rich = u"Pogrubienie, link, błont ortograficzny, zakreślenie i skreślenie w jednym akapicie.";
    return div()
            .flexCol()
            .gap(20)
            .child(heading(u"Typografia", u"Inter, Literata i JetBrains Mono · kształtowanie HarfBuzz · łamanie wierszy"))
            .child(card()
                            .child(div().flexRow().itemsCenter().gap(8)
                                            .child(widthButton(u"− węziej", -40))
                                            .child(widthButton(u"+ szerzej", 40))
                                            .child(div().textColor(t.muted).textSize(12).child(
                                                    number(columnWidth) + u" px")))
                            .child(div().w(columnWidth).textSize(15).leading(1.55f).child(paragraph))
                            .child(div().w(columnWidth)
                                            .fontFamily(u"Brulion Literata")
                                            .textSize(17)
                                            .leading(1.5f)
                                            .textColor(rgb(0xd8d4cc))
                                            .child(u"„Pisz, ile wlezie — brulion wszystko zniesie.” Literata, "
                                                   u"krój do czytania długich notatek.")))
            .child(card()
                            .child(div().textSize(12).textColor(t.faint).child(u"Style w jednym akapicie"))
                            .child(Text(rich)
                                            .span({ 0, 11 }, bold)
                                            .span({ 13, 17 }, accent)
                                            .span({ 19, 24 }, misspelt)
                                            .span({ 41, 51 }, mark)
                                            .span({ 54, 64 }, struck)))
            .child(card()
                            .child(div().textSize(12).textColor(t.faint).child(u"Rozmiary"))
                            .child(div().textSize(32).fontBold().child(u"Nagłówek 32"))
                            .child(div().textSize(24).fontSemibold().child(u"Nagłówek 24"))
                            .child(div().textSize(18).fontMedium().child(u"Nagłówek 18"))
                            .child(div().textSize(12).textColor(t.muted).child(u"Drobny tekst 12 · ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ")))
            .child(card()
                            .child(div().textSize(12).textColor(t.faint).child(u"Kod"))
                            .child(div()
                                            .p(12)
                                            .rounded(8)
                                            .bg(rgb(0x111214))
                                            .fontFamily(u"Brulion JetBrains Mono")
                                            .textSize(13)
                                            .leading(1.5f)
                                            .textColor(rgb(0xc9d1ff))
                                            .whitespaceNowrap()
                                            .child(u"div().flexCol().gap(8).p(16)\n    .child(u\"Cześć, świecie!\");")))
            .child(card()
                            .child(div().textSize(12).textColor(t.faint).child(u"Emoji i symbole (zapasowe kroje)"))
                            .child(div().textSize(20).child(u"✅ ⭐ 📅 🔔 💡 🚀 · ∑ ∫ √ ≈ ≠ ∞")));
}

AnyElement Gallery::controls(Window &window, Context<Gallery> &cx)
{
    const Theme &t = theme();
    auto button = [&](ElementId id, std::u16string_view label, Color bg, Color fg, Color hoverBg) {
        return div()
                .id(id)
                .px(16)
                .h(36)
                .flexRow()
                .itemsCenter()
                .rounded(10)
                .bg(bg)
                .textColor(fg)
                .fontMedium()
                .cursorPointer()
                .hover([hoverBg](Style &s) { s.background = Background::solid(hoverBg); })
                .active([](Style &s) { s.opacity = 0.8f; })
                .child(label);
    };
    ArenaVector<AnyElement> chips;
    const char16_t *const tags[] = { u"#praca", u"#dom", u"#pomysły", u"#zdrowie", u"#książki", u"#podróże", u"#kod" };
    for (std::size_t i = 0; i < std::size(tags); ++i)
        chips.push_back(div()
                        .id(ElementId(u"chip", i))
                        .px(10)
                        .py(4)
                        .roundedFull()
                        .bg(t.raised)
                        .border(1)
                        .borderColor(t.border)
                        .textSize(12)
                        .textColor(t.muted)
                        .hover([](Style &s) {
                            s.borderColor = theme().accent;
                            s.text.color = theme().text;
                        })
                        .child(tags[i]));
    const bool focused = m_focus.isFocused(window);
    return div()
            .flexCol()
            .gap(20)
            .child(heading(u"Kontrolki", u"Najechanie, naciśnięcie, fokus i klawiatura — bez QML"))
            .child(card()
                            .child(div().flexRow().gap(10).flexWrap()
                                            .child(button(u"primary", u"Zapisz", t.accent, t.accentText, t.accentHover)
                                                            .onClick(cx.listener([](Gallery &g, const ClickEvent &,
                                                                                         Window &, Context<Gallery> &cx) {
                                                                ++g.counter;
                                                                cx.notify();
                                                            })))
                                            .child(button(u"secondary", u"Anuluj", t.raised, t.text, t.hover)
                                                            .border(1)
                                                            .borderColor(t.border))
                                            .child(button(u"danger", u"Usuń", rgba(0xf0716b26), t.danger,
                                                    rgba(0xf0716b40)))
                                            .child(button(u"like", liked ? u"❤ Lubię" : u"🤍 Lubię",
                                                    liked ? rgba(0xf0716b33) : t.raised, liked ? t.danger : t.text,
                                                    t.hover)
                                                            .onClick(cx.listener([](Gallery &g, const ClickEvent &,
                                                                                         Window &, Context<Gallery> &cx) {
                                                                g.liked = !g.liked;
                                                                cx.notify();
                                                            }))))
                            .child(div().textColor(t.muted).child(u"Zapisano " + number(counter) + u" razy")))
            .child(card()
                            .child(div().textSize(12).textColor(t.faint).child(u"Fokus i klawisze: kliknij pole, potem Enter lub spacja"))
                            .child(div()
                                            .id(u"focusable")
                                            .trackFocus(m_focus)
                                            .px(14)
                                            .py(10)
                                            .rounded(10)
                                            .bg(t.raised)
                                            .border(2)
                                            .borderColor(focused ? t.accent : t.border)
                                            .cursorPointer()
                                            .onClick(cx.listener([](Gallery &g, const ClickEvent &e, Window &,
                                                                         Context<Gallery> &cx) {
                                                if (e.isKeyboard) {
                                                    ++g.counter;
                                                    cx.notify();
                                                }
                                            }))
                                            .child(focused ? u"Ma fokus — Enter dodaje" : u"Bez fokusu")))
            .child(card()
                            .child(div().textSize(12).textColor(t.faint).child(u"Pola tekstowe (IME, schowek, cofanie)"))
                            .child(div()
                                            .px(12)
                                            .py(9)
                                            .rounded(10)
                                            .bg(t.raised)
                                            .border(1)
                                            .borderColor(m_search.read(cx.app()).focusHandle().isFocused(window) ? t.accent
                                                                                                                 : t.border)
                                            .child(m_search))
                            .child(div()
                                            .px(12)
                                            .py(9)
                                            .rounded(10)
                                            .bg(t.raised)
                                            .border(1)
                                            .borderColor(m_note.read(cx.app()).focusHandle().isFocused(window) ? t.accent
                                                                                                               : t.border)
                                            .leading(1.5f)
                                            .child(m_note)))
            .child(card()
                            .child(div().textSize(12).textColor(t.faint).child(u"Etykiety (zawijanie flex-wrap)"))
                            .child(div().flexRow().flexWrap().gap(8).children(chips)))
            .child(div()
                            .flexRow()
                            .gap(16)
                            .flexWrap()
                            .child(div()
                                            .w(260)
                                            .h(140)
                                            .rounded(16)
                                            .bg(Background::linear(135, rgb(0x6d74ff), rgb(0xd27bff)))
                                            .shadowLg()
                                            .p(18)
                                            .flexCol()
                                            .justifyEnd()
                                            .child(div().textSize(18).fontBold().textColor(white()).child(u"Gradient"))
                                            .child(div().textSize(12).textColor(rgba(0xffffffcc)).child(u"135° · cień lg")))
                            .child(div()
                                            .w(260)
                                            .h(140)
                                            .rounded(16)
                                            .bg(t.surface)
                                            .border(1)
                                            .borderColor(t.border)
                                            .p(18)
                                            .flexRow()
                                            .itemsCenter()
                                            .gap(14)
                                            .child(div().size(56).roundedFull().bg(Background::linear(200, t.success, rgb(0x3aa0ff))))
                                            .child(div().flexCol().gap(2)
                                                            .child(div().fontSemibold().child(u"Janek Brulion"))
                                                            .child(div().textSize(12).textColor(t.muted).child(u"zalogowany · 3 urządzenia")))));
}

AnyElement Gallery::longList(Window &, Context<Gallery> &cx)
{
    const Theme &t = theme();
    const std::size_t count = 100000;
    Gallery *self = this;
    App *app = &cx.app();
    const EntityId id = cx.entityId();
    return div()
            .flexCol()
            .gap(20)
            .h(620)
            .child(heading(u"Długa lista", u"100 000 wierszy — w klatce istnieją tylko widoczne"))
            .child(uniformList(u"rows", count,
                    [self, app, id](std::size_t i, Window &, App &) -> AnyElement {
                        const Theme &th = theme();
                        const bool selected = self->selectedRow == i;
                        const bool task = i % 3 == 0;
                        return div()
                                .id(ElementId(u"row", i))
                                .flexRow()
                                .itemsCenter()
                                .gap(12)
                                .px(14)
                                .h(40)
                                .borderB(1)
                                .borderColor(th.border)
                                .when(selected, [&](Div &&d) { return std::move(d).bg(rgba(0x8b93ff1f)); })
                                .hover([](Style &s) { s.background = Background::solid(theme().raised); })
                                .cursorPointer()
                                .onClick([app, id, i](const ClickEvent &, Window &, App &) {
                                    app->updateEntity<Gallery>(id, [i](Gallery &g, Context<Gallery> &cx) {
                                        g.selectedRow = i;
                                        cx.notify();
                                    });
                                })
                                .child(div()
                                                .size(16)
                                                .rounded(task ? 4 : 8)
                                                .border(task ? 1.5f : 0)
                                                .borderColor(th.muted)
                                                .when(!task, [&](Div &&d) { return std::move(d).size(6).mx(5).bg(th.faint); }))
                                .child(div().flex1().minW0().whitespaceNowrap().overflowHidden().child(
                                        (task ? u"Zadanie " : u"Notatka ") + number(double(i + 1))
                                        + u" — jutro o 9 zadzwonić do Łucji w sprawie żółwia"))
                                .child(div().textSize(12).textColor(th.faint).child(number(double(i % 28 + 1)) + u" lis"));
                    })
                            .itemHeight(40)
                            .flex1()
                            .rounded(14)
                            .bg(t.surface)
                            .border(1)
                            .borderColor(t.border));
}

AnyElement Gallery::drawing(Window &, Context<Gallery> &)
{
    const Theme &t = theme();
    return div()
            .flexCol()
            .gap(20)
            .child(heading(u"Rysowanie", u"Ścieżki, łuki i krzywe — wypełnianie bez QPainter"))
            .child(card().child(canvas([](Rect b, Window &w, App &) {
                const Theme &th = theme();
                // A grid, a filled area under a curve, the curve itself as a thin band.
                for (int i = 1; i < 4; ++i) {
                    const float y = b.top() + b.height() * float(i) / 4;
                    w.paintQuad(Rect::fromXYWH(b.left(), y, b.width(), 1), Background::solid(th.border), {},
                            transparent(), {});
                }
                auto f = [&](float x) {
                    const float u = (x - b.left()) / b.width();
                    return b.top() + b.height() * (0.55f - 0.3f * std::sin(u * 6.2f) * std::exp(-u * 0.8f));
                };
                w.paintPath(rgba(0x8b93ff33), [&](Window::PathPainter &p) {
                    p.moveTo({ b.left(), b.bottom() });
                    for (float x = b.left(); x <= b.right(); x += 4)
                        p.lineTo({ x, f(x) });
                    p.lineTo({ b.right(), b.bottom() });
                    p.close();
                });
                w.paintPath(th.accent, [&](Window::PathPainter &p) {
                    p.moveTo({ b.left(), f(b.left()) - 1.25f });
                    for (float x = b.left(); x <= b.right(); x += 4)
                        p.lineTo({ x, f(x) - 1.25f });
                    for (float x = b.right(); x >= b.left(); x -= 4)
                        p.lineTo({ x, f(x) + 1.25f });
                    p.close();
                });
            }).h(220).wFull()))
            .child(div()
                            .flexRow()
                            .gap(16)
                            .child(card().w(240).itemsCenter().child(canvas([](Rect b, Window &w, App &) {
                                // A ring chart from arcs.
                                const Theme &th = theme();
                                const Point c = b.center();
                                const float r = std::min(b.width(), b.height()) / 2 - 4;
                                const float parts[] = { 0.42f, 0.27f, 0.19f, 0.12f };
                                const Color colors[] = { th.accent, th.success, th.warning, th.danger };
                                float a = -float(M_PI) / 2;
                                for (int i = 0; i < 4; ++i) {
                                    const float a1 = a + parts[i] * 2 * float(M_PI) - 0.04f;
                                    w.paintPath(colors[i], [&](Window::PathPainter &p) {
                                        p.arcTo(c, r, a, a1);
                                        p.arcTo(c, r * 0.62f, a1, a);
                                        p.close();
                                    });
                                    a = a1 + 0.04f;
                                }
                            }).size(180)))
                            .child(card().flex1().child(div().textColor(t.muted).leading(1.5f).child(
                                    u"Każdy kształt to ścieżka spłaszczona do odcinków i wypełniona regułą non-zero "
                                    u"z antyaliasingiem pokrycia — tym samym kodem, którym rasteryzujemy glify."))));
}

AnyElement Gallery::footer(Window &window, Context<Gallery> &)
{
    const Theme &t = theme();
    const Window::FrameStats s = window.lastFrameStats();
    return div()
            .flexRow()
            .itemsCenter()
            .gap(18)
            .px(16)
            .h(30)
            .bg(t.surface)
            .borderT(1)
            .borderColor(t.border)
            .textSize(11)
            .textColor(t.faint)
            .fontFamily(u"Brulion JetBrains Mono")
            .child(u"klatka " + number(double(window.frameCount())))
            .child(number(double(s.primitives)) + u" prymitywów")
            .child(number(double(s.layoutNodes)) + u" węzłów układu")
            .child(u"arena " + number(double(s.arenaBytes) / 1024.0, 1) + u" KB")
            .child(u"budowa " + number(s.buildMicros / 1000.0, 2) + u" ms")
            .child(u"raster " + number(s.presentMicros / 1000.0, 2) + u" ms");
}

} // namespace brulion::atrament::gallery
