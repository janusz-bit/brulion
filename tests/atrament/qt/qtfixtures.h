#pragma once

// Shared by Atrament's Qt host tests: a test pattern, Brulion's bundled fonts, and a view with the
// kinds of things Brulion's views paint.

#include "text/fonts.h"
#include "ui/atrament.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace qtfixtures {

using namespace brulion::atrament;

inline Pixel px(int r, int g, int b, int a)
{
    return {std::uint8_t(r), std::uint8_t(g), std::uint8_t(b), std::uint8_t(a)};
}

// A premultiplied test pattern: hues across, alpha falling down.
inline std::vector<Pixel> pattern(int w, int h)
{
    std::vector<Pixel> pixels;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int a = 255 - y * 160 / std::max(1, h - 1);
            const int r = (x * 255 / std::max(1, w - 1)) * a / 255;
            const int g = ((x + y) % 3 == 0 ? 255 : 40) * a / 255;
            const int b = (255 - x * 255 / std::max(1, w - 1)) * a / 255;
            pixels.push_back(px(r, g, b, a));
        }
    }
    return pixels;
}

inline std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        f->addFontFile(dir + "ui/BrulionInter-Regular.ttf");
        f->addFontFile(dir + "ui/BrulionInter-Bold.ttf");
        f->addFontFile(dir + "ui/BrulionLiterata-Regular.ttf");
        f->addFontFile(dir + "BrulionEmoji.ttf");
        f->setFallbackFamilies({u"Brulion Emoji"});
        return f;
    }();
    return fonts;
}

// A card of the kinds of things Brulion's views paint: text in two families with an underline and
// a strike, a wavy spelling mark, a shadowed rounded card with a border, a gradient, a picture
// with round corners, a path.
struct Card {
    std::shared_ptr<const ImageData> picture = ImageData::make(24, 16, pattern(24, 16));

    AnyElement render(Window &, Context<Card> &)
    {
        return div()
                .sizeFull()
                .p(12)
                .bg(rgb(0xf4f1ea))
                .child(div().flexCol()
                               .gap(6)
                               .p(10)
                               .rounded(9)
                               .border(1.5f)
                               .borderColor(rgb(0xd5cfc3))
                               .bg(white())
                               .shadowMd()
                               .child(text(u"Zażółć gęślą jaźń — notatki"))
                               .child(div().textSize(17).fontBold().child(text(u"Nagłówek ĄĘ")))
                               .child(div().fontFamily(u"Literata")
                                              .underline(UnderlineStyle{rgb(0xd04040), 1, true})
                                              .child(text(u"błąd ortograficzny")))
                               .child(div().lineThrough().textColor(rgb(0x707070)).child(text(u"skreślone")))
                               .child(div().h(14).rounded(7).bg(
                                       Background::linear(70, rgb(0x8b93ff), rgba(0xf0716b80))))
                               .child(div().flexRow().gap(8).h(26).child(
                                       canvas([this](Rect b, Window &w, App &) {
                                           w.paintPicture(Rect::fromXYWH(b.left(), b.top(), 39, 26), Corners::all(6),
                                                          picture, 0.9f);
                                           w.paintPath(rgba(0x2f7d4fcc), [&](Window::PathPainter &p) {
                                               p.moveTo({b.left() + 50, b.top() + 2});
                                               p.cubicTo({b.left() + 80, b.top() - 6}, {b.left() + 90, b.top() + 30},
                                                         {b.left() + 60, b.top() + 24});
                                               p.arcTo({b.left() + 56, b.top() + 13}, 9, 1.2f, 4.4f);
                                               p.close();
                                           });
                                           w.paintUnderline({b.left() + 100, b.top() + 20}, 40, 1.5f, rgb(0x3060c0),
                                                            false);
                                       }).flex1())));
    }
};

} // namespace qtfixtures
