#include "diagram/svg.h"

#include "diagram/common_p.h"

#include <QSet>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::diagram {

namespace {

/// Text for an XML text node or attribute: & < > " ' escaped; characters XML forbids (controls
/// but tab, lone surrogates, U+FFFE/U+FFFF) left out.
QString xmlText(QStringView text)
{
    QString out;
    out.reserve(text.size() + 8);
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text[i];
        const char16_t u = c.unicode();
        if (c.isHighSurrogate() && i + 1 < text.size() && text[i + 1].isLowSurrogate()) {
            out += c;
            out += text[++i];
            continue;
        }
        if (c.isSurrogate() || u == 0xfffe || u == 0xffff || (u < 0x20 && u != u'\t'))
            continue;
        switch (u) {
        case u'&': out += u"&amp;"_s; break;
        case u'<': out += u"&lt;"_s; break;
        case u'>': out += u"&gt;"_s; break;
        case u'"': out += u"&quot;"_s; break;
        case u'\'': out += u"&apos;"_s; break;
        default: out += c;
        }
    }
    return out;
}

QString num(qreal v)
{
    if (!std::isfinite(v))
        return u"0"_s;
    QString s = QString::number(std::round(v * 100) / 100, 'f', 2);
    while (s.endsWith(u'0'))
        s.chop(1);
    if (s.endsWith(u'.'))
        s.chop(1);
    return s == u"-0" ? u"0"_s : s;
}

QString pathData(const QList<PathOp> &path)
{
    QString d;
    for (const PathOp &op : path) {
        if (!d.isEmpty())
            d += u' ';
        switch (op.type) {
        case PathOp::Type::Move: d += u'M' + num(op.p.x()) + u' ' + num(op.p.y()); break;
        case PathOp::Type::Line: d += u'L' + num(op.p.x()) + u' ' + num(op.p.y()); break;
        case PathOp::Type::Cubic:
            d += u'C' + num(op.c1.x()) + u' ' + num(op.c1.y()) + u' ' + num(op.c2.x()) + u' ' + num(op.c2.y()) + u' '
                 + num(op.p.x()) + u' ' + num(op.p.y());
            break;
        case PathOp::Type::Close: d += u'Z'; break;
        }
    }
    return d;
}

class Writer {
public:
    Writer(const Palette &light, const Palette *dark) : m_light(light), m_dark(dark) { }

    /// ` fill="#…"` (+ opacity) for `paint`; the role's class noted in `classes`.
    QString paint(const char *property, const Paint &p, QStringList &classes)
    {
        const QString prop = QString::fromLatin1(property);
        if (p.isNone())
            return u' ' + prop + u"=\"none\""_s;
        const QString color = colorOf(p, m_light);
        const auto rgba = detail::parseHex(color);
        if (!rgba)
            return u' ' + prop + u"=\"none\""_s;
        QString out = u' ' + prop + u"=\""_s + detail::toHex(*rgba) + u'"';
        if (rgba->a < 1)
            out += u' ' + prop + u"-opacity=\""_s + num(rgba->a) + u'"';
        if (m_dark && p.color.isEmpty() && p.role != Role::None) {
            const QChar kind = prop == u"stroke" ? u's' : u'f';
            classes.append(u"bmd-"_s + kind + QString::number(int(p.role)));
            (kind == u's' ? m_strokes : m_fills).insert(int(p.role));
        }
        return out;
    }

    QString item(const Item &it)
    {
        QStringList classes;
        QString out;
        if (it.type == Item::Type::Path) {
            if (it.path.isEmpty())
                return {};
            out = u"<path d=\""_s + pathData(it.path) + u'"';
            out += paint("fill", it.fill, classes);
            out += paint("stroke", it.stroke, classes);
            if (!it.stroke.isNone()) {
                out += u" stroke-width=\""_s + num(it.strokeWidth) + u'"';
                if (it.dash == Dash::Dotted)
                    out += u" stroke-dasharray=\"2 3\""_s;
                else if (it.dash == Dash::Dashed)
                    out += u" stroke-dasharray=\"6 4\""_s;
                out += u" stroke-linejoin=\"round\" stroke-linecap=\"round\""_s;
            }
            if (!classes.isEmpty())
                out += u" class=\""_s + classes.join(u' ') + u'"';
            return out + u"/>\n"_s;
        }
        if (!it.background.isNone()) {
            QStringList bg;
            out += u"<rect x=\""_s + num(it.box.x()) + u"\" y=\""_s + num(it.box.y()) + u"\" width=\""_s
                   + num(it.box.width()) + u"\" height=\""_s + num(it.box.height()) + u"\" rx=\"3\""_s
                   + paint("fill", it.background, bg);
            if (!bg.isEmpty())
                out += u" class=\""_s + bg.join(u' ') + u'"';
            out += u"/>\n"_s;
        }
        const QString fill = paint("fill", it.color, classes);
        const qreal total = it.lineHeight * it.lines.size();
        const qreal top = it.box.center().y() - total / 2;
        QString anchor = u"middle"_s;
        qreal x = it.box.center().x();
        if (it.align & Qt::AlignLeft) {
            anchor = u"start"_s;
            x = it.box.left();
        } else if (it.align & Qt::AlignRight) {
            anchor = u"end"_s;
            x = it.box.right();
        }
        for (qsizetype i = 0; i < it.lines.size(); ++i) {
            if (it.lines[i].isEmpty())
                continue;
            out += u"<text x=\""_s + num(x) + u"\" y=\""_s + num(top + (i + 0.5) * it.lineHeight) + u"\" font-size=\""_s
                   + num(it.fontSize) + u"\" text-anchor=\""_s + anchor + u"\" dominant-baseline=\"central\""_s;
            if (it.bold)
                out += u" font-weight=\"bold\""_s;
            out += fill;
            if (!classes.isEmpty())
                out += u" class=\""_s + classes.join(u' ') + u'"';
            out += u" xml:space=\"preserve\">"_s + xmlText(it.lines[i]) + u"</text>\n"_s;
        }
        return out;
    }

    QString style() const
    {
        if (!m_dark || (m_fills.isEmpty() && m_strokes.isEmpty()))
            return {};
        QString css = u"<style>@media (prefers-color-scheme:dark){"_s;
        const auto rules = [&](const QSet<int> &set, QStringView kind, QStringView prop) {
            QList<int> roles(set.cbegin(), set.cend());
            std::sort(roles.begin(), roles.end());
            for (const int r : std::as_const(roles)) {
                const auto rgba = detail::parseHex((*m_dark)[Role(r)]);
                if (!rgba)
                    continue;
                css += u".bmd-"_s + kind + QString::number(r) + u'{' + prop + u':' + detail::toHex(*rgba) + u'}';
            }
        };
        rules(m_fills, u"f", u"fill");
        rules(m_strokes, u"s", u"stroke");
        return css + u"}</style>\n"_s;
    }

private:
    const Palette &m_light;
    const Palette *m_dark;
    QSet<int> m_fills, m_strokes;
};

} // namespace

QString toSvg(const Scene &scene, const Palette &light, const Palette *dark, const SvgOptions &options)
{
    Writer w(light, dark);
    QString body;
    for (const Item &it : scene.items)
        body += w.item(it);
    const int width = int(std::ceil(std::max<qreal>(1, scene.size.width())));
    const int height = int(std::ceil(std::max<qreal>(1, scene.size.height())));
    const QString title = scene.title.isEmpty() ? options.fallbackTitle : scene.title;
    return u"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "_s + QString::number(width) + u' '
           + QString::number(height) + u"\" width=\""_s + QString::number(width) + u"\" height=\""_s
           + QString::number(height) + u"\" role=\"img\" font-family=\""_s + xmlText(options.fontFamily)
           + u"\" class=\"brulion-diagram\">\n<title>"_s + xmlText(title) + u"</title>\n"_s + w.style() + body
           + u"</svg>\n"_s;
}

} // namespace brulion::diagram
