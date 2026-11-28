#include "pageicons.h"

#include <QFile>
#include <QHash>

namespace brulion::editor::atr {

namespace {

struct Icons {
    QString folder = QStringLiteral(":/icons");
    QHash<QString, std::shared_ptr<const atrament::VectorIcon>> byName; // null: none of that name
};

Icons &icons()
{
    static Icons i;
    return i;
}

// IconImageProvider::validName: a file name that stays in the folder.
bool validName(const QString &name)
{
    return !name.isEmpty() && !name.startsWith(u'.') && !name.contains(u'/') && !name.contains(u'\\')
           && !name.contains(QChar(0));
}

// Names kept (a session shows a few hundred icons at most; unknown names are kept too).
constexpr qsizetype kIconsKept = 1024;

} // namespace

std::shared_ptr<const atrament::VectorIcon> pageIcon(const QString &name)
{
    if (!validName(name))
        return nullptr;
    Icons &i = icons();
    if (const auto it = i.byName.constFind(name); it != i.byName.constEnd())
        return it.value();
    std::shared_ptr<const atrament::VectorIcon> icon;
    QFile file(i.folder + u'/' + name + u".svg");
    if (file.size() <= qint64(atrament::VectorIcon::kMaxSvgBytes) && file.open(QIODevice::ReadOnly)) {
        const QByteArray svg = file.readAll();
        icon = atrament::VectorIcon::parseSvg(std::string_view(svg.constData(), std::size_t(svg.size())));
    }
    if (i.byName.size() >= kIconsKept)
        i.byName.clear();
    i.byName.insert(name, icon);
    return icon;
}

void setPageIconFolder(const QString &folder)
{
    icons().folder = folder;
    icons().byName.clear();
}

atrament::AnyElement iconElement(const QString &name, float size, atrament::Color color)
{
    std::shared_ptr<const atrament::VectorIcon> icon = pageIcon(name);
    return atrament::canvas([icon, color](atrament::Rect b, atrament::Window &w, atrament::App &) {
               if (icon)
                   w.paintIcon(*icon, b, color);
           })
            .size(size)
            .flexShrink0();
}

} // namespace brulion::editor::atr
