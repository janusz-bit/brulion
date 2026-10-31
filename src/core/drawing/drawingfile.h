// One API for the files a drawing lives in (docs/SYNTAX.md §2 "Drawings"): Brulion's SVG with the
// scene embedded (svgwrite.h; also excalidraw.com's "Embed scene" exports), a `.excalidraw` JSON
// file (excalidraw.com's "Save to disk", the Obsidian plugin's compatibility mode, Logseq) and an
// Obsidian Excalidraw plugin file `*.excalidraw.md` (excalidrawmd.h). Each is read and written back
// in its own format.
//
//   std::optional<DrawingFile> file = readDrawingFile(bytes, &error);
//   … edit file->drawing …
//   const QByteArray written = writeDrawingFile(*file, file->drawing, nowMs);
#pragma once

#include "drawing/drawing.h"
#include "drawing/excalidrawmd.h"
#include "drawing/svgwrite.h"

#include <QByteArrayView>
#include <QString>
#include <QStringView>

#include <memory>
#include <optional>

namespace brulion::drawing {

enum class FileFormat { Svg, Excalidraw, ExcalidrawMarkdown };
/// "svg", "excalidraw", "excalidrawMd".
QString formatName(FileFormat format);

/// A drawing as read from its file, with what writing it back in that format needs.
struct DrawingFile {
    FileFormat format = FileFormat::Svg;
    Drawing drawing;
    /// ExcalidrawMarkdown: the file's layout (everything around the scene).
    std::shared_ptr<const ExcalidrawMarkdown> markdown;
    /// false: shown, never written (the plugin's oldest form).
    bool writable = true;
};

/// The drawing in a file's bytes: '{' first (a BOM and whitespace aside) a `.excalidraw` file;
/// "---" first with the plugin's key an Excalidraw plugin file; else an SVG with an embedded scene.
/// nullopt with `error`: NoScene (no drawing: a picture, a note) or Corrupt (a scene that cannot
/// be read).
std::optional<DrawingFile> readDrawingFile(QByteArrayView bytes, ReadError *error = nullptr);

/// The file of `drawing` in `file`'s format: Svg — toSvg(drawing, options); Excalidraw — the scene
/// as Excalidraw writes it (JSON with two spaces, "type", "version", "source", "elements",
/// "appState", "files" in that order, a line break at the end); ExcalidrawMarkdown —
/// writeExcalidrawMarkdown(*file.markdown, drawing, now). Empty when `file` is not writable.
QByteArray writeDrawingFile(const DrawingFile &file, const Drawing &drawing, qint64 now = 0,
                            const SvgOptions &options = {});

/// `drawing` written as `file` (writeDrawingFile()) reads back (readDrawingFile()): no limit of
/// the readers is crossed — MaxSceneBytes and MaxSceneContainers for the scene, MaxSceneBytes for
/// an SVG's embedded payload (pictures grow it some 1.6 times: deflate, the payload's string
/// escapes, base64) and for a plugin file. A scene within an eighth of MaxSceneBytes always does
/// (no format grows one eightfold); a larger one is encoded (an SVG: its payload only) to know.
/// For edits that add picture bytes (a backdrop) and merges.
bool readsBack(const DrawingFile &file, const Drawing &drawing, qint64 now = 0);
/// A drawing file by its name and its start (HeadBytes): ".svg" with an embedded scene,
/// ".excalidraw" with a scene's JSON, ".md" with the plugin's frontmatter key. Cheap: for lists.
bool looksLikeDrawingFile(QStringView path, QByteArrayView head);

/// The drawing as a preview shows one Brulion does not edit: its elements and, for each foreign
/// one (a frame, an embedded page, an unknown kind), a dashed grey box where it lies — never a
/// blank picture. An editable drawing is returned as it is.
Drawing previewDrawing(const Drawing &drawing);
/// The colour of those boxes (Excalidraw's grey; the dark theme maps it as any ink).
inline constexpr QStringView ForeignBoxColor = u"#868e96";

} // namespace brulion::drawing
