#include "uifonts.h"

#include <QFontDatabase>

using namespace Qt::StringLiterals;

namespace brulion::ui {

QStringList bundledFontFiles()
{
    static const QStringList files{
            u":/fonts/ui/BrulionInter-Regular.ttf"_s,         u":/fonts/ui/BrulionInter-Medium.ttf"_s,
            u":/fonts/ui/BrulionInter-SemiBold.ttf"_s,        u":/fonts/ui/BrulionInter-Bold.ttf"_s,
            u":/fonts/ui/BrulionInter-Italic.ttf"_s,          u":/fonts/ui/BrulionInter-BoldItalic.ttf"_s,
            u":/fonts/ui/BrulionLiterata-Regular.ttf"_s,      u":/fonts/ui/BrulionLiterata-Italic.ttf"_s,
            u":/fonts/ui/BrulionLiterata-Bold.ttf"_s,         u":/fonts/ui/BrulionLiterata-BoldItalic.ttf"_s,
            u":/fonts/ui/BrulionJetBrainsMono-Regular.ttf"_s, u":/fonts/ui/BrulionJetBrainsMono-Bold.ttf"_s,
    };
    return files;
}

QStringList installBundledFonts()
{
    static const QStringList families = [] {
        QStringList out;
        const QStringList files = bundledFontFiles();
        for (const QString &file : files) {
            if (const int id = QFontDatabase::addApplicationFont(file); id >= 0)
                out += QFontDatabase::applicationFontFamilies(id);
        }
        out.removeDuplicates(); // each family once, where it first came
        return out;
    }();
    return families;
}

} // namespace brulion::ui
