// The files of the Obsidian Excalidraw plugin (zsviczian/obsidian-excalidraw-plugin, `*.excalidraw.md`;
// docs/SYNTAX.md §2 "Drawings"): Markdown whose frontmatter has the key `excalidraw-plugin`
// ("parsed" or "raw"), with the scene in a code block under "## Drawing":
//
//   ---
//   excalidraw-plugin: parsed
//   tags: [excalidraw]
//   ---
//   ==⚠  Switch to EXCALIDRAW VIEW in the MORE OPTIONS menu of this document. ⚠== …
//   (the back of the card: any Markdown)
//   # Excalidraw Data
//   ## Text Elements
//   Zażółć gęślą jaźń ^Zt8xQw1a
//
//   ## Element Links            (optional: "id: [[link]]")
//   ## Embedded Files           (optional: "fileId: [[picture.png]]")
//   %%
//   ## Drawing
//   ```compressed-json
//   N4KAkARALgngDgUwgLgAQQQDwMYEMA2AlgCYBOuA7hADTgQBuCpAzoQPYB2KqATLZMzYBXUtiRoIACyhQ4zZAHoFAc…
//
//   …(LZ-String base64, lzstring.h, in lines of 256 characters with an empty line between)
//   ```
//   %%
//
// or "```json" with the scene as JSON.stringify(scene, null, "\t"). The plugin reads a text
// element's text from its "## Text Elements" entry (its Markdown, links included) before the
// scene's. Brulion rewrites only the block (in the file's own encoding) and the entries of text
// elements it changed (their new text) or removed (gone); every other byte stays as it was —
// frontmatter, the back of the card, element links, embedded files, `%%` markers. Elements read
// from the file and gone from the drawing are written as deleted ones (`isDeleted: true`, a
// "tombstone"): the plugin, showing the drawing while the file changes under it, removes only
// those. The plugin's oldest form (the scene as one line under "# Drawing", no code block) is read,
// never written.
#pragma once

#include "drawing/drawing.h"
#include "drawing/excalidraw.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::drawing {

/// The frontmatter key of the Obsidian Excalidraw plugin.
inline constexpr QStringView ExcalidrawPluginKey = u"excalidraw-plugin";

/// The start of a file (e.g. its first HeadBytes) is an Excalidraw plugin file: YAML frontmatter
/// first (after a BOM; a `---` line, CRLF allowed) with the key `excalidraw-plugin` (quoted or not)
/// at the start of a line before the closing `---` (or the end of `head`).
bool looksLikeExcalidrawMarkdown(QByteArrayView head);
/// The start of a `.excalidraw` file: '{' first (a BOM and whitespace aside) and `"excalidraw"` in
/// it (its "type").
bool looksLikeExcalidrawJson(QByteArrayView head);

/// Where an Excalidraw plugin file keeps its scene and its text elements (byte offsets into
/// `bytes`), and what of the scene read must survive a rewrite.
struct ExcalidrawMarkdown {
    enum class Encoding {
        CompressedJson,
        Json,
        /// The plugin's oldest form: the scene as one line after "# Drawing" — read, never written.
        Legacy
    };
    Encoding encoding = Encoding::CompressedJson;
    /// The whole file as read.
    QByteArray bytes;
    /// The block's content: from after the opening fence's line break up to the closing "```"
    /// line (the line break before it included).
    qsizetype sceneFrom = 0;
    qsizetype sceneTo = 0;
    /// "\r\n" when the opening fence's line ends so (the block is written with it).
    QByteArray eol = "\n";
    /// An entry of "## Text Elements": its raw text [from, to), then " ^id" and its line breaks up
    /// to `end`.
    struct TextEntry {
        QString id;
        qsizetype from = 0;
        qsizetype to = 0;
        qsizetype end = 0;
    };
    QList<TextEntry> textEntries;
    /// The elements the scene marks deleted, as read.
    QJsonArray tombstones;
    /// The other elements, as read, in the scene's order.
    QJsonArray elements;
};

/// What readExcalidrawMarkdown() found: the file's layout and its drawing.
struct ExcalidrawMarkdownRead {
    ExcalidrawMarkdown layout;
    Drawing drawing;
};

/// The drawing of an Excalidraw plugin file. nullopt with `error` NoScene: no plugin file (no key
/// in the frontmatter) or larger than MaxSceneBytes; Corrupt: the key but no "## Drawing" block, or
/// one that cannot be read (not base64, no LZ-String end mark, not JSON, not a scene).
std::optional<ExcalidrawMarkdownRead> readExcalidrawMarkdown(QByteArrayView bytes, ReadError *error = nullptr);

/// The file with `drawing` as its scene (see above): the block's content in the file's encoding
/// (compressed: lines of 256 characters with an empty line between; json: tab-indented), the
/// "## Text Elements" entries of text elements whose version changed (their text, which also
/// becomes their `rawText`) or that are gone (removed); every other byte as read. Elements read
/// from the file and absent from `drawing` are written as tombstones (`isDeleted: true`, version
/// + 1, a versionNonce from the id and `now`, `updated: now`); the file's tombstones are kept
/// (but those whose id is in the drawing again). Text elements new or changed in `drawing` get
/// `rawText` = their originalText. Precondition: layout.encoding != Legacy.
QByteArray writeExcalidrawMarkdown(const ExcalidrawMarkdown &layout, const Drawing &drawing, qint64 now);

/// JSON.stringify(value, null, indent)'s layout: one member or item a line, `"key": value`, "{}"
/// and "[]" when empty; scalars as QJsonDocument writes them, object keys in QJsonObject's order
/// (sorted).
QByteArray stringifyJson(const QJsonValue &value, QByteArrayView indent);

} // namespace brulion::drawing
