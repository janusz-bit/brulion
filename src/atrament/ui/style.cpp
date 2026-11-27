#include "ui/style.h"

namespace brulion::atrament {

void TextStyleRefinement::applyTo(TextStyle &s) const
{
    if (fontFamily)
        s.fontFamily = *fontFamily;
    if (fontSize)
        s.fontSize = *fontSize;
    if (fontWeight)
        s.fontWeight = *fontWeight;
    if (fontStyle)
        s.fontStyle = *fontStyle;
    if (color)
        s.color = *color;
    if (lineHeight) {
        s.lineHeight = *lineHeight;
        s.lineHeightScale = 0;
    }
    if (lineHeightScale) {
        s.lineHeightScale = *lineHeightScale;
        s.lineHeight = 0;
    }
    if (align)
        s.align = *align;
    if (whiteSpace)
        s.whiteSpace = *whiteSpace;
    if (underline)
        s.underline = *underline;
    if (strikethrough)
        s.strikethrough = *strikethrough;
    if (background)
        s.background = *background;
    if (fontFeatures)
        s.fontFeatures = *fontFeatures;
    if (overline)
        s.overline = *overline;
    if (baselineShift)
        s.baselineShift = *baselineShift;
}

} // namespace brulion::atrament
