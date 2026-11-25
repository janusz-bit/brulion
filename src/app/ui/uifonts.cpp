#include "uifonts.h"

#include <QByteArray>
#include <QFontDatabase>
#include <QResource>

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
            // Straight from the program's own data: the resources are stored as they are
            // (CMakeLists.txt: --no-compress), so the font engine reads the tables it needs from
            // there and nothing is copied to the heap — addApplicationFont() reads a resource
            // whole into memory, 1.5 MB for the twelve files, for as long as the app runs. (The
            // data of a resource compiled into the program lasts as long as the program.)
            int id = -1;
            if (const QResource resource(file); resource.isValid() && resource.size() > 0
                                                && resource.compressionAlgorithm() == QResource::NoCompression) {
                id = QFontDatabase::addApplicationFontFromData(QByteArray::fromRawData(
                        reinterpret_cast<const char *>(resource.data()), qsizetype(resource.size())));
            }
            if (id < 0)
                id = QFontDatabase::addApplicationFont(file);
            if (id >= 0)
                out += QFontDatabase::applicationFontFamilies(id);
        }
        out.removeDuplicates(); // each family once, where it first came
        return out;
    }();
    return families;
}

} // namespace brulion::ui
