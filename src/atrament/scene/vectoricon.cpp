#include "scene/vectoricon.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace brulion::atrament {

namespace {

// The value of an SVG number the scanner below checked ([sign] digits [. digits] [e [sign]
// digits]), computed here: std::from_chars for floating point is missing from the C++ libraries
// of Android's NDK and of Emscripten, and strtof follows the C locale's decimal point.
double decimalValue(std::string_view s)
{
    std::size_t i = 0;
    double sign = 1;
    if (i < s.size() && (s[i] == '-' || s[i] == '+'))
        sign = s[i++] == '-' ? -1 : 1;
    double value = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9')
        value = value * 10 + (s[i++] - '0');
    if (i < s.size() && s[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            value += (s[i++] - '0') * scale;
            scale /= 10;
        }
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        int expSign = 1;
        if (i < s.size() && (s[i] == '-' || s[i] == '+'))
            expSign = s[i++] == '-' ? -1 : 1;
        int exponent = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && exponent < 400)
            exponent = exponent * 10 + (s[i++] - '0');
        value *= std::pow(10.0, expSign * exponent);
    }
    return sign * value;
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

// A cursor over path data or a list of numbers.
class Numbers {
public:
    explicit Numbers(std::string_view s) : m_s(s) { }

    void skipSeparators()
    {
        while (m_i < m_s.size() && (isSpace(m_s[m_i]) || m_s[m_i] == ','))
            ++m_i;
    }
    void skipSpaces()
    {
        while (m_i < m_s.size() && isSpace(m_s[m_i]))
            ++m_i;
    }
    bool atEnd()
    {
        skipSeparators();
        return m_i >= m_s.size();
    }
    // The next character without taking it ('\0' at the end).
    char peek()
    {
        skipSeparators();
        return m_i < m_s.size() ? m_s[m_i] : '\0';
    }
    char take() { return m_i < m_s.size() ? m_s[m_i++] : '\0'; }
    // Whether a number starts here.
    bool numberAhead()
    {
        const char c = peek();
        return isDigit(c) || c == '-' || c == '+' || c == '.';
    }
    // SVG's number: [sign] digits [. digits] [e [sign] digits], or [sign] . digits …
    std::optional<float> number()
    {
        skipSeparators();
        const std::size_t start = m_i;
        std::size_t i = m_i;
        if (i < m_s.size() && (m_s[i] == '-' || m_s[i] == '+'))
            ++i;
        const std::size_t digitsStart = i;
        while (i < m_s.size() && isDigit(m_s[i]))
            ++i;
        bool digits = i > digitsStart;
        if (i < m_s.size() && m_s[i] == '.') {
            ++i;
            const std::size_t fraction = i;
            while (i < m_s.size() && isDigit(m_s[i]))
                ++i;
            digits = digits || i > fraction;
        }
        if (!digits)
            return std::nullopt;
        if (i < m_s.size() && (m_s[i] == 'e' || m_s[i] == 'E')) {
            std::size_t j = i + 1;
            if (j < m_s.size() && (m_s[j] == '-' || m_s[j] == '+'))
                ++j;
            const std::size_t expDigits = j;
            while (j < m_s.size() && isDigit(m_s[j]))
                ++j;
            if (j > expDigits)
                i = j;
        }
        const float value = float(decimalValue(m_s.substr(start, i - start)));
        if (!std::isfinite(value))
            return std::nullopt;
        m_i = i;
        return value;
    }
    // An arc's flag: one character, '0' or '1', separators optional around it.
    std::optional<bool> flag()
    {
        skipSeparators();
        if (m_i < m_s.size() && (m_s[m_i] == '0' || m_s[m_i] == '1'))
            return m_s[m_i++] == '1';
        return std::nullopt;
    }

private:
    std::string_view m_s;
    std::size_t m_i = 0;
};

// --- the XML subset -----------------------------------------------------------------------------

struct Attribute {
    std::string_view name;
    std::string value; // entities &amp; &lt; &gt; &quot; &apos; decoded
};

struct Tag {
    std::string_view name;
    std::vector<Attribute> attributes;
    bool closing = false;     // </name>
    bool selfClosing = false; // <name … />
    const std::string *value(std::string_view key) const
    {
        for (const Attribute &a : attributes)
            if (a.name == key)
                return &a.value;
        return nullptr;
    }
};

std::string decodeEntities(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        const std::size_t semi = s.find(';', i);
        const std::string_view e = semi == std::string_view::npos ? std::string_view() : s.substr(i + 1, semi - i - 1);
        const char c = e == "amp" ? '&' : e == "lt" ? '<' : e == "gt" ? '>' : e == "quot" ? '"' : e == "apos" ? '\'' : 0;
        if (c) {
            out += c;
            i = semi;
        } else {
            out += '&';
        }
    }
    return out;
}

// The tags of a document in order (comments, declarations, processing instructions and text left
// out); stops at the first malformed tag.
std::vector<Tag> readTags(std::string_view s)
{
    std::vector<Tag> tags;
    std::size_t i = 0;
    while (i < s.size()) {
        const std::size_t lt = s.find('<', i);
        if (lt == std::string_view::npos)
            break;
        if (s.substr(lt, 4) == "<!--") {
            const std::size_t end = s.find("-->", lt + 4);
            if (end == std::string_view::npos)
                break;
            i = end + 3;
            continue;
        }
        if (lt + 1 < s.size() && (s[lt + 1] == '?' || s[lt + 1] == '!')) {
            const std::size_t end = s.find('>', lt + 2);
            if (end == std::string_view::npos)
                break;
            i = end + 1;
            continue;
        }
        Tag tag;
        std::size_t j = lt + 1;
        if (j < s.size() && s[j] == '/') {
            tag.closing = true;
            ++j;
        }
        const std::size_t nameStart = j;
        while (j < s.size() && !isSpace(s[j]) && s[j] != '>' && s[j] != '/')
            ++j;
        tag.name = s.substr(nameStart, j - nameStart);
        if (tag.name.empty())
            break;
        bool ended = false;
        while (j < s.size()) {
            while (j < s.size() && isSpace(s[j]))
                ++j;
            if (j >= s.size())
                break;
            if (s[j] == '>') {
                ++j;
                ended = true;
                break;
            }
            if (s[j] == '/' && j + 1 < s.size() && s[j + 1] == '>') {
                tag.selfClosing = true;
                j += 2;
                ended = true;
                break;
            }
            const std::size_t keyStart = j;
            while (j < s.size() && !isSpace(s[j]) && s[j] != '=' && s[j] != '>' && s[j] != '/')
                ++j;
            const std::string_view key = s.substr(keyStart, j - keyStart);
            while (j < s.size() && isSpace(s[j]))
                ++j;
            if (key.empty() || j >= s.size() || s[j] != '=')
                return tags; // an attribute without a value: not the subset
            ++j;
            while (j < s.size() && isSpace(s[j]))
                ++j;
            if (j >= s.size() || (s[j] != '"' && s[j] != '\''))
                return tags;
            const char quote = s[j];
            const std::size_t valueEnd = s.find(quote, j + 1);
            if (valueEnd == std::string_view::npos)
                return tags;
            tag.attributes.push_back({key, decodeEntities(s.substr(j + 1, valueEnd - j - 1))});
            j = valueEnd + 1;
        }
        if (!ended)
            break;
        tags.push_back(std::move(tag));
        i = j;
    }
    return tags;
}

// What a shape inherits from the elements around it.
struct Presentation {
    bool fill = true; // SVG's initial fill is black
    bool stroke = false;
    StrokeStyle style;
};

void applyPresentation(const Tag &tag, Presentation &p)
{
    auto paint = [](const std::string &v) {
        std::string_view t = v;
        while (!t.empty() && isSpace(t.front()))
            t.remove_prefix(1);
        while (!t.empty() && isSpace(t.back()))
            t.remove_suffix(1);
        return !t.empty() && t != "none" && t != "transparent";
    };
    if (const std::string *v = tag.value("fill"))
        p.fill = paint(*v);
    if (const std::string *v = tag.value("stroke"))
        p.stroke = paint(*v);
    if (const std::string *v = tag.value("stroke-width")) {
        Numbers n(*v);
        if (const std::optional<float> w = n.number(); w && *w >= 0)
            p.style.width = *w;
    }
    if (const std::string *v = tag.value("stroke-linecap"))
        p.style.cap = *v == "round" ? LineCap::Round : *v == "square" ? LineCap::Square : LineCap::Butt;
    if (const std::string *v = tag.value("stroke-linejoin"))
        p.style.join = *v == "round" ? LineJoin::Round : *v == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
    if (const std::string *v = tag.value("stroke-miterlimit")) {
        Numbers n(*v);
        if (const std::optional<float> m = n.number(); m && *m >= 1)
            p.style.miterLimit = *m;
    }
}

float attribute(const Tag &tag, std::string_view key, float fallback = 0)
{
    const std::string *v = tag.value(key);
    if (!v)
        return fallback;
    Numbers n(*v);
    return n.number().value_or(fallback);
}

std::vector<Point> pointList(const std::string &text)
{
    std::vector<Point> points;
    Numbers n(text);
    while (!n.atEnd() && points.size() < VectorIcon::kMaxPathCommands) {
        const std::optional<float> x = n.number();
        const std::optional<float> y = x ? n.number() : std::nullopt;
        if (!y)
            break;
        points.push_back({*x, *y});
    }
    return points;
}

} // namespace

bool parseSvgPathData(std::string_view d, VectorPath &path)
{
    Numbers n(d);
    char command = 0;
    Point current;
    Point start;
    Point lastControl; // the previous curve's second control point (S, T reflect it)
    char lastCommand = 0;
    std::size_t commands = 0;
    while (!n.atEnd()) {
        if (++commands > VectorIcon::kMaxPathCommands)
            return false;
        const char next = n.peek();
        if ((next >= 'A' && next <= 'Z') || (next >= 'a' && next <= 'z')) {
            command = n.take();
        } else if (!command || command == 'Z' || command == 'z' || !n.numberAhead()) {
            return false; // numbers before any command, or after Z
        }
        const bool relative = command >= 'a' && command <= 'z';
        const char upper = char(relative ? command - 'a' + 'A' : command);
        const Point base = relative ? current : Point{};
        auto num = [&n]() { return n.number(); };
        switch (upper) {
        case 'M': {
            const auto x = num(), y = x ? num() : std::nullopt;
            if (!y)
                return false;
            current = base + Point{*x, *y};
            start = current;
            path.moveTo(current);
            // Further pairs are lines.
            command = relative ? 'l' : 'L';
            lastCommand = 'M';
            continue;
        }
        case 'L': {
            const auto x = num(), y = x ? num() : std::nullopt;
            if (!y)
                return false;
            current = base + Point{*x, *y};
            path.lineTo(current);
            break;
        }
        case 'H': {
            const auto x = num();
            if (!x)
                return false;
            current = {(relative ? current.x : 0) + *x, current.y};
            path.lineTo(current);
            break;
        }
        case 'V': {
            const auto y = num();
            if (!y)
                return false;
            current = {current.x, (relative ? current.y : 0) + *y};
            path.lineTo(current);
            break;
        }
        case 'C': {
            float v[6];
            for (float &x : v) {
                const auto r = num();
                if (!r)
                    return false;
                x = *r;
            }
            const Point c1 = base + Point{v[0], v[1]};
            const Point c2 = base + Point{v[2], v[3]};
            current = base + Point{v[4], v[5]};
            path.cubicTo(c1, c2, current);
            lastControl = c2;
            lastCommand = 'C';
            continue;
        }
        case 'S': {
            float v[4];
            for (float &x : v) {
                const auto r = num();
                if (!r)
                    return false;
                x = *r;
            }
            const Point c1 = lastCommand == 'C' ? current * 2 - lastControl : current;
            const Point c2 = base + Point{v[0], v[1]};
            current = base + Point{v[2], v[3]};
            path.cubicTo(c1, c2, current);
            lastControl = c2;
            lastCommand = 'C';
            continue;
        }
        case 'Q': {
            float v[4];
            for (float &x : v) {
                const auto r = num();
                if (!r)
                    return false;
                x = *r;
            }
            const Point c = base + Point{v[0], v[1]};
            current = base + Point{v[2], v[3]};
            path.quadTo(c, current);
            lastControl = c;
            lastCommand = 'Q';
            continue;
        }
        case 'T': {
            const auto x = num(), y = x ? num() : std::nullopt;
            if (!y)
                return false;
            const Point c = lastCommand == 'Q' ? current * 2 - lastControl : current;
            current = base + Point{*x, *y};
            path.quadTo(c, current);
            lastControl = c;
            lastCommand = 'Q';
            continue;
        }
        case 'A': {
            const auto rx = num(), ry = rx ? num() : std::nullopt, rot = ry ? num() : std::nullopt;
            const auto large = rot ? n.flag() : std::nullopt, sweep = large ? n.flag() : std::nullopt;
            const auto x = sweep ? num() : std::nullopt, y = x ? num() : std::nullopt;
            if (!y)
                return false;
            current = base + Point{*x, *y};
            path.arcTo(*rx, *ry, *rot, *large, *sweep, current);
            break;
        }
        case 'Z':
            path.close();
            current = start;
            break;
        default: return false;
        }
        lastCommand = upper;
    }
    return true;
}

std::shared_ptr<const VectorIcon> VectorIcon::parseSvg(std::string_view svg)
{
    if (svg.size() > kMaxSvgBytes)
        return nullptr;
    const std::vector<Tag> tags = readTags(svg);
    auto icon = std::make_shared<VectorIcon>();
    std::vector<Presentation> stack;
    int skipped = 0; // inside an element whose content is never drawn (<defs>, <clipPath>, …)
    bool inSvg = false;
    std::size_t commands = 0;
    for (const Tag &tag : tags) {
        const std::string_view name = tag.name;
        if (tag.closing) {
            if (skipped > 0) {
                --skipped;
                continue;
            }
            if ((name == "g" || name == "svg" || name == "a") && !stack.empty())
                stack.pop_back();
            continue;
        }
        if (skipped > 0) {
            if (!tag.selfClosing)
                ++skipped;
            continue;
        }
        if (name == "svg") {
            Presentation p = stack.empty() ? Presentation{} : stack.back();
            applyPresentation(tag, p);
            if (!inSvg) {
                inSvg = true;
                const float w = attribute(tag, "width", 24);
                const float h = attribute(tag, "height", 24);
                icon->m_viewBox = Rect::fromXYWH(0, 0, w > 0 ? w : 24, h > 0 ? h : 24);
                if (const std::string *vb = tag.value("viewBox")) {
                    Numbers numbers(*vb);
                    const auto x = numbers.number(), y = x ? numbers.number() : std::nullopt;
                    const auto vw = y ? numbers.number() : std::nullopt, vh = vw ? numbers.number() : std::nullopt;
                    if (vh && *vw > 0 && *vh > 0)
                        icon->m_viewBox = Rect::fromXYWH(*x, *y, *vw, *vh);
                }
            }
            if (!tag.selfClosing)
                stack.push_back(p);
            continue;
        }
        if (!inSvg)
            continue;
        if (name == "g" || name == "a") {
            Presentation p = stack.empty() ? Presentation{} : stack.back();
            applyPresentation(tag, p);
            if (!tag.selfClosing)
                stack.push_back(p);
            continue;
        }
        const bool shape = name == "path" || name == "rect" || name == "circle" || name == "ellipse" || name == "line"
                           || name == "polyline" || name == "polygon";
        if (!shape) {
            // defs, clipPath, mask, symbol, text, style…: nothing of it is drawn here.
            if (!tag.selfClosing)
                ++skipped;
            continue;
        }
        Presentation p = stack.empty() ? Presentation{} : stack.back();
        applyPresentation(tag, p);
        Shape s;
        s.filled = p.fill;
        s.stroked = p.stroke && p.style.width > 0;
        s.stroke = p.style;
        if (name == "path") {
            if (const std::string *d = tag.value("d")) {
                commands += d->size() / 2;
                (void)parseSvgPathData(*d, s.path);
            }
        } else if (name == "rect") {
            const float w = attribute(tag, "width"), h = attribute(tag, "height");
            float rx = attribute(tag, "rx", -1), ry = attribute(tag, "ry", -1);
            if (rx < 0)
                rx = ry;
            if (ry < 0)
                ry = rx;
            s.path.addRect(Rect::fromXYWH(attribute(tag, "x"), attribute(tag, "y"), w, h), std::max(0.0f, rx),
                           std::max(0.0f, ry));
        } else if (name == "circle") {
            const float r = attribute(tag, "r");
            s.path.addEllipse({attribute(tag, "cx"), attribute(tag, "cy")}, r, r);
        } else if (name == "ellipse") {
            s.path.addEllipse({attribute(tag, "cx"), attribute(tag, "cy")}, attribute(tag, "rx"), attribute(tag, "ry"));
        } else if (name == "line") {
            s.path.moveTo({attribute(tag, "x1"), attribute(tag, "y1")});
            s.path.lineTo({attribute(tag, "x2"), attribute(tag, "y2")});
            s.filled = false; // a line has no inside
        } else if (const std::string *pts = tag.value("points")) { // polyline, polygon
            const std::vector<Point> points = pointList(*pts);
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (i == 0)
                    s.path.moveTo(points[i]);
                else
                    s.path.lineTo(points[i]);
            }
            if (name == "polygon" && !points.empty())
                s.path.close();
        }
        if (!tag.selfClosing)
            ++skipped; // (a shape's children — <title>, <animate> — are not drawn)
        if (!s.path.isEmpty() && (s.filled || s.stroked))
            icon->m_shapes.push_back(std::move(s));
        if (commands > kMaxPathCommands)
            break;
    }
    if (icon->m_shapes.empty())
        return nullptr;
    return icon;
}

std::shared_ptr<const VectorIcon::Outline> VectorIcon::outline(float side) const
{
    if (!(side > 0) || !std::isfinite(side))
        return std::make_shared<const Outline>();
    for (std::size_t i = 0; i < m_outlines.size(); ++i) {
        if (m_outlines[i].first == side) {
            // The most recent last.
            std::rotate(m_outlines.begin() + std::ptrdiff_t(i), m_outlines.begin() + std::ptrdiff_t(i) + 1,
                        m_outlines.end());
            return m_outlines.back().second;
        }
    }
    const Rect vb = m_viewBox;
    const float scale = side / std::max(vb.width(), vb.height());
    PathTransform t;
    t.scale = scale;
    t.offset = {(side - vb.width() * scale) / 2 - vb.left() * scale, (side - vb.height() * scale) / 2 - vb.top() * scale};
    // Small icons need finer curves than a page's paths.
    const float tolerance = 0.08f;
    PathBuilder fill;
    PathBuilder stroke;
    for (const Shape &s : m_shapes) {
        const std::vector<FlatContour> contours = s.path.flatten(t, tolerance);
        if (s.filled)
            fillContours(contours, fill);
        if (s.stroked) {
            StrokeStyle style = s.stroke;
            style.width *= scale;
            for (float &d : style.dashes)
                d *= scale;
            style.dashOffset *= scale;
            strokeContours(contours, style, stroke, tolerance);
        }
    }
    auto out = std::make_shared<Outline>();
    out->fill = std::move(fill).build(transparent(), {});
    out->stroke = std::move(stroke).build(transparent(), {});
    m_outlines.emplace_back(side, out);
    constexpr std::size_t kSizesKept = 4;
    if (m_outlines.size() > kSizesKept)
        m_outlines.erase(m_outlines.begin());
    return out;
}

} // namespace brulion::atrament
