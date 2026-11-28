#include "markdown/tagstyle.h"

#include "markdown/inline.h"

namespace brulion::markdown {

std::optional<TagStyle> tagStyleOf(BlockKind kind, QStringView text, const TagStyles &styles)
{
    if (styles.isEmpty() || kind == BlockKind::Code || kind == BlockKind::Frontmatter || kind == BlockKind::Html
        || kind == BlockKind::Rule)
        return std::nullopt;
    for (const TagSpan &tag : findTags(text)) {
        // The tag itself, else its parents (`#projekt/dom` by the style of `projekt`).
        QString name = tag.name.toLower();
        while (!name.isEmpty()) {
            const auto it = styles.constFind(name);
            if (it != styles.cend())
                return *it;
            const qsizetype slash = name.lastIndexOf(u'/');
            if (slash < 0)
                break;
            name.truncate(slash);
        }
    }
    return std::nullopt;
}

} // namespace brulion::markdown
