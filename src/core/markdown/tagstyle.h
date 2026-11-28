// Styles of blocks by tag (an extension's b.app.tagStyle, docs/EXTENSIONS.md "Styles of blocks by
// tag"): which style a block gets from its tags — one decision for the editor's rows
// (BlockListModel) and the exports (io's HTML renderer, and through it PDF and DOCX).
//
//   const TagStyles styles{{u"pilne"_s, {u"danger"_s}}, {u"projekt"_s, {u"#12ab34"_s, false, true}}};
//   tagStyleOf(BlockKind::ListItem, u"Remont #projekt/kuchnia"_s, styles) → {"#12ab34", false, true}
#pragma once

#include "types.h"

#include <QHash>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::markdown {

/// How blocks carrying a tag are drawn: a tint of the theme ("accent", "success", "danger",
/// "warning", "info", "neutral") or a colour "#rrggbb", as a background and / or a bar at the
/// block's left edge.
struct TagStyle {
    QString tint;
    bool fill = true;
    bool bar = true;
    bool operator==(const TagStyle &other) const = default;
};

/// The styles by tag: the tag in lower case (NFC, without '#').
using TagStyles = QHash<QString, TagStyle>;

/// The style of a block of `kind` with the text `text`: that of its first tag that has one — the
/// tag itself, else its nearest parent that has one (`#projekt/dom` by `projekt`), in any case;
/// nullopt for none, and always for code, the frontmatter, HTML and a rule.
std::optional<TagStyle> tagStyleOf(BlockKind kind, QStringView text, const TagStyles &styles);

} // namespace brulion::markdown
