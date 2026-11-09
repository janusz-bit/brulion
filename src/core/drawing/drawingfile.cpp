#include "drawing/drawingfile.h"

#include "drawing/excalidraw.h"
#include "drawing/svgwrite.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

/// A `.excalidraw` file as Excalidraw's serializeAsJSON writes it: JSON.stringify(scene, null, 2)
/// with its keys in Excalidraw's order — "type", "version", "source", "elements", "appState",
/// "files", then any other — so "type": "excalidraw" opens the file (looksLikeExcalidrawJson()
/// looks at its first HeadBytes, however large the scene); a line break at the end.
QByteArray excalidrawFileJson(const QJsonObject &scene)
{
    static constexpr QStringView First[] = {u"type", u"version", u"source", u"elements", u"appState", u"files"};
    // Those first, in that order; the others after them, as the object keeps them.
    const auto rank = [](const QString &key) { return std::ranges::find(First, QStringView(key)) - First; };
    QStringList keys = scene.keys();
    std::ranges::stable_sort(keys, {}, rank);
    if (keys.isEmpty())
        return "{}\n";
    QByteArray out = "{";
    for (const QString &key : std::as_const(keys)) {
        out += out.size() == 1 ? "\n  " : ",\n  ";
        out += stringifyJson(QJsonValue(key), "  ");
        out += ": ";
        // (A value's lines one level deeper: JSON's strings hold no line breaks.)
        out += stringifyJson(scene.value(key), "  ").replace("\n", "\n  ");
    }
    out += "\n}\n";
    return out;
}

} // namespace

QString formatName(FileFormat format)
{
    switch (format) {
    case FileFormat::Excalidraw: return u"excalidraw"_s;
    case FileFormat::ExcalidrawMarkdown: return u"excalidrawMd"_s;
    case FileFormat::Svg: break;
    }
    return u"svg"_s;
}

std::optional<DrawingFile> readDrawingFile(QByteArrayView bytes, ReadError *error)
{
    const auto fail = [error](ReadError why) -> std::optional<DrawingFile> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (error)
        *error = ReadError::None;
    QByteArrayView text = bytes;
    if (text.startsWith("\xEF\xBB\xBF"))
        text = text.sliced(3);
    if (text.startsWith("---")) {
        // Markdown with frontmatter: an Excalidraw plugin file, or no drawing at all.
        ReadError why = ReadError::None;
        std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes, &why);
        if (!read)
            return fail(why);
        DrawingFile file;
        file.format = FileFormat::ExcalidrawMarkdown;
        file.drawing = std::move(read->drawing);
        file.writable = read->layout.encoding != ExcalidrawMarkdown::Encoding::Legacy;
        file.markdown = std::make_shared<const ExcalidrawMarkdown>(std::move(read->layout));
        return file;
    }
    const auto first =
            std::ranges::find_if_not(text, [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; });
    if (first != text.end() && *first == '{') {
        // A .excalidraw file.
        std::optional<Drawing> d = parseScene(text);
        if (!d)
            return fail(ReadError::NoScene);
        return DrawingFile{FileFormat::Excalidraw, std::move(*d), nullptr, true};
    }
    bool corrupt = false;
    const std::optional<QByteArray> scene = svgPayload(text, &corrupt);
    if (!scene)
        return fail(corrupt ? ReadError::Corrupt : ReadError::NoScene);
    std::optional<Drawing> d = parseScene(*scene);
    if (!d)
        return fail(ReadError::Corrupt);
    return DrawingFile{FileFormat::Svg, std::move(*d), nullptr, true};
}

QByteArray writeDrawingFile(const DrawingFile &file, const Drawing &drawing, qint64 now, const SvgOptions &options)
{
    if (!file.writable)
        return {};
    switch (file.format) {
    case FileFormat::Excalidraw: return excalidrawFileJson(sceneObject(drawing));
    case FileFormat::ExcalidrawMarkdown:
        return file.markdown ? writeExcalidrawMarkdown(*file.markdown, drawing, now) : QByteArray();
    case FileFormat::Svg: break;
    }
    return toSvg(drawing, options);
}

bool readsBack(const DrawingFile &file, const Drawing &drawing, qint64 now)
{
    if (!file.writable)
        return false;
    const QByteArray json = sceneJson(drawing);
    const qsizetype around = file.markdown ? file.markdown->bytes.size() : 0;
    if (json.size() + around <= MaxSceneBytes / 8)
        return true;
    if (file.format != FileFormat::Svg)
        return readDrawingFile(writeDrawingFile(file, drawing, now)).has_value();
    // An SVG: what its reader limits (svgPayload(), sceneRoot()) — not the picture around it.
    if (json.size() > MaxSceneBytes
        || std::ranges::count_if(json, [](char c) { return c == '[' || c == '{'; }) > MaxSceneContainers)
        return false;
    return encodePayload(json).size() <= MaxSceneBytes;
}

bool looksLikeDrawingFile(QStringView path, QByteArrayView head)
{
    if (path.endsWith(u".svg", Qt::CaseInsensitive))
        return looksLikeExcalidrawSvg(head);
    if (path.endsWith(u".excalidraw", Qt::CaseInsensitive))
        return looksLikeExcalidrawJson(head);
    if (path.endsWith(u".md", Qt::CaseInsensitive))
        return looksLikeExcalidrawMarkdown(head);
    return false;
}

Drawing previewDrawing(const Drawing &drawing)
{
    if (drawing.foreign.isEmpty())
        return drawing;
    Drawing out = drawing;
    out.foreign = QJsonArray();
    // (No box for an element whose box is not finite or beyond CoordinateLimit.)
    const auto far = [](double c) { return !std::isfinite(c) || std::abs(c) > CoordinateLimit; };
    qsizetype n = 0;
    for (const auto &v : drawing.foreign) {
        const QJsonObject e = v.toObject();
        const auto number = [&e](QStringView key) { return e.value(key).toDouble(std::nan("")); };
        const QRectF r = QRectF(number(u"x"), number(u"y"), number(u"width"), number(u"height")).normalized();
        if (far(r.left()) || far(r.top()) || far(r.right()) || far(r.bottom()))
            continue;
        Stroke box;
        box.type = ElementType::Rectangle;
        box.id = u"~foreign-"_s + QString::number(n++);
        box.box = r;
        const double angle = number(u"angle");
        box.angle = std::isfinite(angle) ? angle : 0;
        box.color = ForeignBoxColor.toString();
        box.strokeStyle = StrokeStyle::Dashed;
        out.strokes.append(std::move(box));
    }
    return out;
}

} // namespace brulion::drawing
