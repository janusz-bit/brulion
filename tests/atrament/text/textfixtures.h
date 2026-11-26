#pragma once

// Brulion's bundled fonts for the text tests, loaded from the source tree.

#include "text/fonts.h"

#include <QString>

#include <memory>
#include <string>
#include <string_view>

namespace textfixtures {

using namespace brulion::atrament;

inline std::string fontPath(std::string_view relative)
{
    return std::string(BRULION_SOURCE_DIR) + "/assets/fonts/" + std::string(relative);
}

inline FaceId loadFace(FontCollection &fonts, std::string_view relative)
{
    const std::vector<FaceId> faces = fonts.addFontFile(fontPath(relative));
    return faces.size() == 1 ? faces.front() : kNoFace;
}

// Inter Regular is the collection's first face (the default for runs without one); emoji and
// math are the fallback families.
struct BundledFonts {
    std::shared_ptr<FontCollection> fonts = std::make_shared<FontCollection>();
    FaceId interRegular = loadFace(*fonts, "ui/BrulionInter-Regular.ttf");
    FaceId interMedium = loadFace(*fonts, "ui/BrulionInter-Medium.ttf");
    FaceId interSemiBold = loadFace(*fonts, "ui/BrulionInter-SemiBold.ttf");
    FaceId interBold = loadFace(*fonts, "ui/BrulionInter-Bold.ttf");
    FaceId interItalic = loadFace(*fonts, "ui/BrulionInter-Italic.ttf");
    FaceId interBoldItalic = loadFace(*fonts, "ui/BrulionInter-BoldItalic.ttf");
    FaceId literataRegular = loadFace(*fonts, "ui/BrulionLiterata-Regular.ttf");
    FaceId literataItalic = loadFace(*fonts, "ui/BrulionLiterata-Italic.ttf");
    FaceId literataBold = loadFace(*fonts, "ui/BrulionLiterata-Bold.ttf");
    FaceId literataBoldItalic = loadFace(*fonts, "ui/BrulionLiterata-BoldItalic.ttf");
    FaceId monoRegular = loadFace(*fonts, "ui/BrulionJetBrainsMono-Regular.ttf");
    FaceId monoBold = loadFace(*fonts, "ui/BrulionJetBrainsMono-Bold.ttf");
    FaceId emoji = loadFace(*fonts, "BrulionEmoji.ttf");
    FaceId math = loadFace(*fonts, "BrulionMath.otf");

    BundledFonts() { fonts->setFallbackFamilies({u"Brulion Emoji", u"Brulion Math"}); }
    bool allLoaded() const
    {
        for (FaceId f :
             {interRegular, interMedium, interSemiBold, interBold, interItalic, interBoldItalic, literataRegular,
              literataItalic, literataBold, literataBoldItalic, monoRegular, monoBold, emoji, math})
            if (f == kNoFace)
                return false;
        return true;
    }
};

inline std::u16string u16(const QString &s)
{
    return s.toStdU16String();
}

inline QString qs(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), qsizetype(s.size()));
}

} // namespace textfixtures
