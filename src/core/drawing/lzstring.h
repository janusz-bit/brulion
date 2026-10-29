// LZ-String's base64 form (pieroxy/lz-string 1.5, MIT; the algorithm re-implemented, not its
// code): how the Obsidian Excalidraw plugin stores a drawing's scene in its `*.excalidraw.md`
// files (the "```compressed-json" block, excalidrawmd.h). It works on the UTF-16 code units of a
// string, as JavaScript strings hold them — lone surrogates included.
//
//   const QString b64 = lzstring::compressToBase64(json);          // "N4IgLgngDgpiBcIYA8DGB…"
//   const std::optional<QString> back = lzstring::decompressFromBase64(b64);
//
// The output is the same as lz-string's, character for character (tested against its own test
// vectors, tests/data/drawing/lzstring). Decompression is stricter than JavaScript's: input it would
// read as zero bits or half a string is refused (nullopt).
#pragma once

#include "drawing/excalidraw.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::drawing::lzstring {

/// LZString.compressToBase64: the code units of `input` compressed, as base64 characters
/// ("A–Z a–z 0–9 + /"), padded with '=' to a multiple of four.
QString compressToBase64(QStringView input);

/// LZString.decompressFromBase64. ASCII whitespace is skipped (the plugin's 256-character lines,
/// CRLF files). nullopt — never a partial string — for: empty input, a character outside
/// "A–Z a–z 0–9 + / =" (JavaScript would read it as zero bits), a stream that ends without its
/// end mark (JavaScript: ""), a bad back-reference (JavaScript: null), more than `maxUnits` code
/// units of output.
std::optional<QString> decompressFromBase64(QStringView input, qsizetype maxUnits = MaxSceneBytes);

} // namespace brulion::drawing::lzstring
