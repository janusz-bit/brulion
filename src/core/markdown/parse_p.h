// Internal parse entry points of the markdown module.
#pragma once

#include "markdown/parse.h"
#include "markdown/structure_p.h"
#include "types.h"

#include <functional>

namespace brulion::markdown::detail {

/// parseNote() with structure options (used to parse one block of a document on its own). With
/// `tags`, every inline and property tag occurrence is appended to it (findNoteTags() adds the
/// frontmatter's).
ParsedNote parseNoteWith(const VaultPath &path, QStringView content, const VaultConfig &config,
                         StructureOptions options, QList<NoteTag> *tags = nullptr);
/// The same on a QString: the blocks' raw texts share it.
ParsedNote parseNoteWith(const VaultPath &path, const QString &content, const VaultConfig &config,
                         StructureOptions options, QList<NoteTag> *tags = nullptr);

/// The items of the `tags::` / `tag::` property value `value` that are no `#tag` the inline scan
/// finds (`hashAt`: whether a '#' at that document offset starts one) and no `[[link]]`:
/// Property NoteTags with document offsets (`valueOffset`: offset of value[0]).
QList<NoteTag> propertyTagItems(QStringView value, qsizetype valueOffset, int line,
                                const std::function<bool(qsizetype)> &hashAt);

/// One item of the frontmatter's tag key (frontmatterTagItems()).
struct FrontmatterTagItem {
    NoteTag tag;       ///< form Frontmatter; for a link item the range of its text
    bool link = false; ///< the item is a `[[link]]` (renameLinks() rewrites it)
};

/// The frontmatter's tag key as found in the YAML source.
struct FrontmatterTags {
    QString key;                     ///< the key as written ("tags", "Tag"); empty when none
    QList<FrontmatterTagItem> items; ///< in document order
    /// The items are exactly the names parseNote() reads from the key (namesFrom()), in order;
    /// false when the value has a form the scan does not place (nested lists, escapes, …).
    bool complete = false;
};

/// Locate the items of the frontmatter key parseNote() takes the page's tags from (`tags`, else
/// `tag`, any letter case).
FrontmatterTags frontmatterTagItems(QStringView content);

} // namespace brulion::markdown::detail
