#include "drawing/drawingfile.h"

#include "drawing/excalidraw.h"
#include "drawing/svgwrite.h"

#include <QStringList>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::drawing {

namespace {

bool endsWith(QStringView path, QStringView suffix)
{
    return path.endsWith(suffix, Qt::CaseInsensitive);
}

/// A `.excalidraw` file as Excalidraw's serializeAsJSON writes it: JSON.stringify(scene, null, 2)
/// with its keys in Excalidraw's order — "type", "version", "source", "elements", "appState",
/// "files", then any other — so "type": "excalidraw" opens the file (looksLikeExcalidrawJson()
/// looks at its first HeadBytes, however large the scene); a line break at the end.
QByteArray excalidrawFileJson(const QJsonObject &scene)
{
    static constexpr QStringView First[] = {u"type", u"version", u"source", u"elements", u"appState", u"files"};
    QStringList keys;
    for (const QStringView key : First) {
        if (scene.contains(key))
            keys.append(key.toString());
    }
    for (const QString &key : scene.keys()) {
        if (!keys.contains(key))
            keys.append(key);
    }
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
    return out + "\n}\n";
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
    qsizetype i = 0;
    while (i < text.size() && (text.at(i) == ' ' || text.at(i) == '\n' || text.at(i) == '\r' || text.at(i) == '\t'))
        ++i;
    if (i < text.size() && text.at(i) == '{') {
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
    if (json.size() > MaxSceneBytes)
        return false;
    qsizetype containers = 0;
    for (const char c : json) {
        if ((c == '[' || c == '{') && ++containers > MaxSceneContainers)
            return false;
    }
    return encodePayload(json).size() <= MaxSceneBytes;
}

bool looksLikeDrawingFile(QStringView path, QByteArrayView head)
{
    if (endsWith(path, u".svg"))
        return looksLikeExcalidrawSvg(head);
    if (endsWith(path, u".excalidraw"))
        return looksLikeExcalidrawJson(head);
    if (endsWith(path, u".md"))
        return looksLikeExcalidrawMarkdown(head);
    return false;
}

Drawing previewDrawing(const Drawing &drawing)
{
    if (drawing.foreign.isEmpty())
        return drawing;
    Drawing out = drawing;
    out.foreign = QJsonArray();
    qsizetype n = 0;
    for (const QJsonValue &v : drawing.foreign) {
        const QJsonObject e = v.toObject();
        const auto number = [&e](QStringView key) {
            const QJsonValue value = e.value(key);
            return value.isDouble() && std::isfinite(value.toDouble()) ? value.toDouble() : std::nan("");
        };
        double x = number(u"x"), y = number(u"y"), w = number(u"width"), h = number(u"height");
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h))
            continue;
        if (w < 0) {
            x += w;
            w = -w;
        }
        if (h < 0) {
            y += h;
            h = -h;
        }
        if (std::abs(x) > CoordinateLimit || std::abs(y) > CoordinateLimit || std::abs(x + w) > CoordinateLimit
            || std::abs(y + h) > CoordinateLimit)
            continue;
        Stroke box;
        box.type = ElementType::Rectangle;
        box.id = u"~foreign-"_s + QString::number(n++);
        box.box = QRectF(x, y, w, h);
        const double angle = number(u"angle");
        box.angle = std::isfinite(angle) ? angle : 0;
        box.color = ForeignBoxColor.toString();
        box.strokeStyle = StrokeStyle::Dashed;
        box.width = 1;
        out.strokes.append(box);
    }
    return out;
}

} // namespace brulion::drawing
