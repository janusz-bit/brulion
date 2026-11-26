#pragma once

// Styles (docs/ATRAMENT.md D14): what an element looks like — its layout (layout/layout.h), its
// box (background, border, corners, shadows, opacity), its cursor, and the text style its
// descendants inherit. `Styled<Self>` gives every element GPUI's builder methods, Tailwind's
// names in camelCase: `div().flexCol().gap(8).p(12).bg(c).rounded(10)`.
//
// Builders are consumed as they are chained (`&&`-qualified, as Rust's by-value `self`): keep a
// builder in a variable only to `std::move` it on, or use `when()`.

#include "base/color.h"
#include "base/geometry.h"
#include "layout/layout.h"
#include "scene/scene.h"
#include "text/fonts.h"
#include "ui/platform.h"

#include <array>
#include <optional>
#include <string>
#include <utility>

namespace brulion::atrament {

struct BoxShadow {
    Color color;
    Point offset;
    float blurRadius = 0;
    float spreadRadius = 0;
};

enum class TextAlign : std::uint8_t { Left, Center, Right };
enum class WhiteSpace : std::uint8_t { Normal, NoWrap };

struct UnderlineStyle {
    Color color; // transparent: the text's colour
    float thickness = 1;
    bool wavy = false;
};

struct TextStyle {
    std::u16string fontFamily = u"Inter";
    float fontSize = 14;
    FontWeight fontWeight;
    FontStyle fontStyle = FontStyle::Normal;
    Color color = black();
    float lineHeight = 0; // px; 0 with lineHeightScale 0: the font's own
    float lineHeightScale = 0; // × fontSize when > 0 (and lineHeight is 0)
    TextAlign align = TextAlign::Left;
    WhiteSpace whiteSpace = WhiteSpace::Normal;
    std::optional<UnderlineStyle> underline;
    bool strikethrough = false;
    std::optional<Color> background; // highlight behind the text
    std::u16string fontFeatures;

    float resolvedLineHeight() const
    {
        if (lineHeight > 0)
            return lineHeight;
        if (lineHeightScale > 0)
            return lineHeightScale * fontSize;
        return 0;
    }
};

// Partial text style: what an element sets for its descendants.
struct TextStyleRefinement {
    std::optional<std::u16string> fontFamily;
    std::optional<float> fontSize;
    std::optional<FontWeight> fontWeight;
    std::optional<FontStyle> fontStyle;
    std::optional<Color> color;
    std::optional<float> lineHeight;
    std::optional<float> lineHeightScale;
    std::optional<TextAlign> align;
    std::optional<WhiteSpace> whiteSpace;
    std::optional<std::optional<UnderlineStyle>> underline;
    std::optional<bool> strikethrough;
    std::optional<std::optional<Color>> background;
    std::optional<std::u16string> fontFeatures;

    bool isEmpty() const
    {
        return !fontFamily && !fontSize && !fontWeight && !fontStyle && !color && !lineHeight && !lineHeightScale
                && !align && !whiteSpace && !underline && !strikethrough && !background && !fontFeatures;
    }
    void applyTo(TextStyle &s) const;
};

struct Style {
    LayoutStyle layout;
    std::optional<Background> background;
    Color borderColor;
    Corners cornerRadii;
    std::array<BoxShadow, 3> shadows {};
    std::uint8_t shadowCount = 0;
    float opacity = 1;
    std::optional<CursorStyle> cursor;
    TextStyleRefinement text;

    void addShadow(const BoxShadow &s)
    {
        if (shadowCount < shadows.size())
            shadows[shadowCount++] = s;
    }
};

constexpr Length px(float v)
{
    return Length::px(v);
}
constexpr Length pct(float v)
{
    return Length::percent(v);
}

template<class Self>
class Styled {
public:
    // --- layout
    Self &&flex() && { return set([](Style &s) { s.layout.display = Display::Flex; }); }
    Self &&hidden() && { return set([](Style &s) { s.layout.display = Display::None; }); }
    Self &&flexRow() && { return set([](Style &s) { s.layout.flexDirection = FlexDirection::Row; }); }
    Self &&flexCol() && { return set([](Style &s) { s.layout.flexDirection = FlexDirection::Column; }); }
    Self &&flexRowReverse() && { return set([](Style &s) { s.layout.flexDirection = FlexDirection::RowReverse; }); }
    Self &&flexColReverse() && { return set([](Style &s) { s.layout.flexDirection = FlexDirection::ColumnReverse; }); }
    Self &&flexWrap() && { return set([](Style &s) { s.layout.flexWrap = FlexWrap::Wrap; }); }
    Self &&flexGrow(float g = 1) && { return set([g](Style &s) { s.layout.flexGrow = g; }); }
    Self &&flexShrink(float f = 1) && { return set([f](Style &s) { s.layout.flexShrink = f; }); }
    Self &&flexShrink0() && { return set([](Style &s) { s.layout.flexShrink = 0; }); }
    Self &&flexBasis(Length l) && { return set([l](Style &s) { s.layout.flexBasis = l; }); }
    // flex: 1 1 0%
    Self &&flex1() &&
    {
        return set([](Style &s) {
            s.layout.flexGrow = 1;
            s.layout.flexShrink = 1;
            s.layout.flexBasis = Length::percent(0);
        });
    }
    Self &&flexNone() &&
    {
        return set([](Style &s) {
            s.layout.flexGrow = 0;
            s.layout.flexShrink = 0;
        });
    }
    Self &&itemsStart() && { return set([](Style &s) { s.layout.alignItems = AlignItems::FlexStart; }); }
    Self &&itemsEnd() && { return set([](Style &s) { s.layout.alignItems = AlignItems::FlexEnd; }); }
    Self &&itemsCenter() && { return set([](Style &s) { s.layout.alignItems = AlignItems::Center; }); }
    Self &&itemsStretch() && { return set([](Style &s) { s.layout.alignItems = AlignItems::Stretch; }); }
    Self &&itemsBaseline() && { return set([](Style &s) { s.layout.alignItems = AlignItems::Baseline; }); }
    Self &&selfStart() && { return set([](Style &s) { s.layout.alignSelf = AlignItems::FlexStart; }); }
    Self &&selfEnd() && { return set([](Style &s) { s.layout.alignSelf = AlignItems::FlexEnd; }); }
    Self &&selfCenter() && { return set([](Style &s) { s.layout.alignSelf = AlignItems::Center; }); }
    Self &&selfStretch() && { return set([](Style &s) { s.layout.alignSelf = AlignItems::Stretch; }); }
    Self &&justifyStart() && { return set([](Style &s) { s.layout.justifyContent = JustifyContent::FlexStart; }); }
    Self &&justifyEnd() && { return set([](Style &s) { s.layout.justifyContent = JustifyContent::FlexEnd; }); }
    Self &&justifyCenter() && { return set([](Style &s) { s.layout.justifyContent = JustifyContent::Center; }); }
    Self &&justifyBetween() && { return set([](Style &s) { s.layout.justifyContent = JustifyContent::SpaceBetween; }); }
    Self &&justifyAround() && { return set([](Style &s) { s.layout.justifyContent = JustifyContent::SpaceAround; }); }
    Self &&justifyEvenly() && { return set([](Style &s) { s.layout.justifyContent = JustifyContent::SpaceEvenly; }); }
    Self &&contentStart() && { return set([](Style &s) { s.layout.alignContent = AlignContent::FlexStart; }); }
    Self &&contentCenter() && { return set([](Style &s) { s.layout.alignContent = AlignContent::Center; }); }
    Self &&contentBetween() && { return set([](Style &s) { s.layout.alignContent = AlignContent::SpaceBetween; }); }
    Self &&gap(float g) &&
    {
        return set([g](Style &s) {
            s.layout.rowGap = g;
            s.layout.columnGap = g;
        });
    }
    Self &&gapX(float g) && { return set([g](Style &s) { s.layout.columnGap = g; }); }
    Self &&gapY(float g) && { return set([g](Style &s) { s.layout.rowGap = g; }); }

    Self &&w(float v) && { return set([v](Style &s) { s.layout.width = Length::px(v); }); }
    Self &&w(Length l) && { return set([l](Style &s) { s.layout.width = l; }); }
    Self &&h(float v) && { return set([v](Style &s) { s.layout.height = Length::px(v); }); }
    Self &&h(Length l) && { return set([l](Style &s) { s.layout.height = l; }); }
    Self &&size(float v) &&
    {
        return set([v](Style &s) {
            s.layout.width = Length::px(v);
            s.layout.height = Length::px(v);
        });
    }
    Self &&wFull() && { return set([](Style &s) { s.layout.width = Length::percent(100); }); }
    Self &&hFull() && { return set([](Style &s) { s.layout.height = Length::percent(100); }); }
    Self &&sizeFull() &&
    {
        return set([](Style &s) {
            s.layout.width = Length::percent(100);
            s.layout.height = Length::percent(100);
        });
    }
    Self &&minW(float v) && { return set([v](Style &s) { s.layout.minWidth = Length::px(v); }); }
    Self &&minH(float v) && { return set([v](Style &s) { s.layout.minHeight = Length::px(v); }); }
    Self &&maxW(float v) && { return set([v](Style &s) { s.layout.maxWidth = Length::px(v); }); }
    Self &&maxH(float v) && { return set([v](Style &s) { s.layout.maxHeight = Length::px(v); }); }
    Self &&minW0() && { return set([](Style &s) { s.layout.minWidth = Length::px(0); }); }
    Self &&aspectRatio(float r) && { return set([r](Style &s) { s.layout.aspectRatio = r; }); }

    Self &&p(float v) && { return set([v](Style &s) { s.layout.padding = Edges::all(v); }); }
    Self &&px(float v) &&
    {
        return set([v](Style &s) {
            s.layout.padding.left = v;
            s.layout.padding.right = v;
        });
    }
    Self &&py(float v) &&
    {
        return set([v](Style &s) {
            s.layout.padding.top = v;
            s.layout.padding.bottom = v;
        });
    }
    Self &&pt(float v) && { return set([v](Style &s) { s.layout.padding.top = v; }); }
    Self &&pr(float v) && { return set([v](Style &s) { s.layout.padding.right = v; }); }
    Self &&pb(float v) && { return set([v](Style &s) { s.layout.padding.bottom = v; }); }
    Self &&pl(float v) && { return set([v](Style &s) { s.layout.padding.left = v; }); }
    Self &&m(float v) && { return set([v](Style &s) { s.layout.margin = LengthEdges::all(Length::px(v)); }); }
    Self &&mx(float v) &&
    {
        return set([v](Style &s) {
            s.layout.margin.left = Length::px(v);
            s.layout.margin.right = Length::px(v);
        });
    }
    Self &&my(float v) &&
    {
        return set([v](Style &s) {
            s.layout.margin.top = Length::px(v);
            s.layout.margin.bottom = Length::px(v);
        });
    }
    Self &&mt(float v) && { return set([v](Style &s) { s.layout.margin.top = Length::px(v); }); }
    Self &&mr(float v) && { return set([v](Style &s) { s.layout.margin.right = Length::px(v); }); }
    Self &&mb(float v) && { return set([v](Style &s) { s.layout.margin.bottom = Length::px(v); }); }
    Self &&ml(float v) && { return set([v](Style &s) { s.layout.margin.left = Length::px(v); }); }
    Self &&mxAuto() &&
    {
        return set([](Style &s) {
            s.layout.margin.left = Length::autoLength();
            s.layout.margin.right = Length::autoLength();
        });
    }
    Self &&mlAuto() && { return set([](Style &s) { s.layout.margin.left = Length::autoLength(); }); }

    Self &&relative() && { return set([](Style &s) { s.layout.position = PositionType::Relative; }); }
    Self &&absolute() && { return set([](Style &s) { s.layout.position = PositionType::Absolute; }); }
    Self &&top(float v) && { return set([v](Style &s) { s.layout.inset.top = Length::px(v); }); }
    Self &&right(float v) && { return set([v](Style &s) { s.layout.inset.right = Length::px(v); }); }
    Self &&bottom(float v) && { return set([v](Style &s) { s.layout.inset.bottom = Length::px(v); }); }
    Self &&left(float v) && { return set([v](Style &s) { s.layout.inset.left = Length::px(v); }); }
    Self &&inset(float v) && { return set([v](Style &s) { s.layout.inset = LengthEdges::all(Length::px(v)); }); }

    Self &&overflowHidden() &&
    {
        return set([](Style &s) {
            s.layout.overflowX = Overflow::Hidden;
            s.layout.overflowY = Overflow::Hidden;
        });
    }
    Self &&overflowXHidden() && { return set([](Style &s) { s.layout.overflowX = Overflow::Hidden; }); }
    Self &&overflowYHidden() && { return set([](Style &s) { s.layout.overflowY = Overflow::Hidden; }); }
    Self &&overflowScroll() &&
    {
        return set([](Style &s) {
            s.layout.overflowX = Overflow::Scroll;
            s.layout.overflowY = Overflow::Scroll;
        });
    }
    Self &&overflowXScroll() && { return set([](Style &s) { s.layout.overflowX = Overflow::Scroll; }); }
    Self &&overflowYScroll() && { return set([](Style &s) { s.layout.overflowY = Overflow::Scroll; }); }

    // --- the box
    Self &&bg(Color c) && { return set([c](Style &s) { s.background = Background::solid(c); }); }
    Self &&bg(Background b) && { return set([b](Style &s) { s.background = b; }); }
    Self &&border(float w = 1) && { return set([w](Style &s) { s.layout.border = Edges::all(w); }); }
    Self &&borderT(float w = 1) && { return set([w](Style &s) { s.layout.border.top = w; }); }
    Self &&borderR(float w = 1) && { return set([w](Style &s) { s.layout.border.right = w; }); }
    Self &&borderB(float w = 1) && { return set([w](Style &s) { s.layout.border.bottom = w; }); }
    Self &&borderL(float w = 1) && { return set([w](Style &s) { s.layout.border.left = w; }); }
    Self &&borderColor(Color c) && { return set([c](Style &s) { s.borderColor = c; }); }
    Self &&rounded(float r) && { return set([r](Style &s) { s.cornerRadii = Corners::all(r); }); }
    Self &&roundedFull() && { return set([](Style &s) { s.cornerRadii = Corners::all(1e6f); }); }
    Self &&roundedT(float r) &&
    {
        return set([r](Style &s) {
            s.cornerRadii.topLeft = r;
            s.cornerRadii.topRight = r;
        });
    }
    Self &&roundedB(float r) &&
    {
        return set([r](Style &s) {
            s.cornerRadii.bottomLeft = r;
            s.cornerRadii.bottomRight = r;
        });
    }
    Self &&shadow(BoxShadow b) && { return set([b](Style &s) { s.addShadow(b); }); }
    Self &&shadowSm() && { return set([](Style &s) { s.addShadow({ rgba(0x0000001f), { 0, 1 }, 2, 0 }); }); }
    Self &&shadowMd() &&
    {
        return set([](Style &s) {
            s.addShadow({ rgba(0x0000001a), { 0, 4 }, 6, -1 });
            s.addShadow({ rgba(0x00000014), { 0, 2 }, 4, -2 });
        });
    }
    Self &&shadowLg() &&
    {
        return set([](Style &s) {
            s.addShadow({ rgba(0x0000001a), { 0, 10 }, 15, -3 });
            s.addShadow({ rgba(0x0000000d), { 0, 4 }, 6, -4 });
        });
    }
    Self &&opacity(float o) && { return set([o](Style &s) { s.opacity = o; }); }
    Self &&cursor(CursorStyle c) && { return set([c](Style &s) { s.cursor = c; }); }
    Self &&cursorPointer() && { return set([](Style &s) { s.cursor = CursorStyle::PointingHand; }); }
    Self &&cursorText() && { return set([](Style &s) { s.cursor = CursorStyle::IBeam; }); }

    // --- text, for this element's descendants
    Self &&textColor(Color c) && { return set([c](Style &s) { s.text.color = c; }); }
    Self &&textSize(float v) && { return set([v](Style &s) { s.text.fontSize = v; }); }
    Self &&fontFamily(std::u16string f) && { return set([f = std::move(f)](Style &s) { s.text.fontFamily = f; }); }
    Self &&fontWeight(std::uint16_t w) && { return set([w](Style &s) { s.text.fontWeight = FontWeight { w }; }); }
    Self &&fontMedium() && { return set([](Style &s) { s.text.fontWeight = FontWeight { FontWeight::Medium }; }); }
    Self &&fontSemibold() && { return set([](Style &s) { s.text.fontWeight = FontWeight { FontWeight::SemiBold }; }); }
    Self &&fontBold() && { return set([](Style &s) { s.text.fontWeight = FontWeight { FontWeight::Bold }; }); }
    Self &&italic() && { return set([](Style &s) { s.text.fontStyle = FontStyle::Italic; }); }
    Self &&lineHeight(float v) && { return set([v](Style &s) { s.text.lineHeight = v; }); }
    Self &&leading(float scale) && { return set([scale](Style &s) { s.text.lineHeightScale = scale; }); }
    Self &&textLeft() && { return set([](Style &s) { s.text.align = TextAlign::Left; }); }
    Self &&textCenter() && { return set([](Style &s) { s.text.align = TextAlign::Center; }); }
    Self &&textRight() && { return set([](Style &s) { s.text.align = TextAlign::Right; }); }
    Self &&whitespaceNowrap() && { return set([](Style &s) { s.text.whiteSpace = WhiteSpace::NoWrap; }); }
    Self &&underline(UnderlineStyle u = {}) && { return set([u](Style &s) { s.text.underline = u; }); }
    Self &&lineThrough() && { return set([](Style &s) { s.text.strikethrough = true; }); }
    Self &&fontFeatures(std::u16string f) && { return set([f = std::move(f)](Style &s) { s.text.fontFeatures = f; }); }

    // --- composition
    // Applies `f` when `condition` holds: `f` takes Self&& and returns the builder (by value or as
    // the same reference).
    template<class F>
    Self &&when(bool condition, F &&f) &&
    {
        if (condition)
            adopt(std::forward<F>(f)(std::move(self())));
        return std::move(self());
    }
    template<class T, class F>
    Self &&whenSome(const std::optional<T> &value, F &&f) &&
    {
        if (value)
            adopt(std::forward<F>(f)(std::move(self()), *value));
        return std::move(self());
    }
    // Edits the style directly.
    template<class F>
    Self &&style(F &&f) &&
    {
        f(self().styleRef());
        return std::move(self());
    }

private:
    Self &self() { return static_cast<Self &>(*this); }
    void adopt(Self &&result)
    {
        if (&result != &self())
            self() = std::move(result);
    }
    template<class F>
    Self &&set(F &&f)
    {
        f(self().styleRef());
        return std::move(self());
    }
};

} // namespace brulion::atrament
