#include "uifonts.h"

#include <QFontDatabase>

using namespace Qt::StringLiterals;

namespace brulion::ui {

QStringList bundledFontFiles()
{
    static const QStringList files{
            u":/fonts/ui/BrulionInter-Regular.ttf"_s,       u":/fonts/ui/BrulionInter-Medium.ttf"_s,
            u":/fonts/ui/BrulionInter-SemiBold.ttf"_s,      u":/fonts/ui/BrulionInter-Bold.ttf"_s,
            u":/fonts/ui/BrulionInter-Italic.ttf"_s,        u":/fonts/ui/BrulionLiterata-Regular.ttf"_s,
            u":/fonts/ui/BrulionLiterata-Italic.ttf"_s,     u":/fonts/ui/BrulionLiterata-Bold.ttf"_s,
            u":/fonts/ui/BrulionLiterata-BoldItalic.ttf"_s, u":/fonts/ui/BrulionJetBrainsMono-Regular.ttf"_s,
            u":/fonts/ui/BrulionJetBrainsMono-Bold.ttf"_s,
    };
    return files;
}

QStringList installBundledFonts()
{
    static const QStringList families = [] {
        QStringList out;
        for (const QString &file : bundledFontFiles()) {
            const int id = QFontDatabase::addApplicationFont(file);
            if (id < 0)
                continue;
            for (const QString &family : QFontDatabase::applicationFontFamilies(id)) {
                if (!out.contains(family))
                    out.append(family);
            }
        }
        return out;
    }();
    return families;
}

} // namespace brulion::ui
