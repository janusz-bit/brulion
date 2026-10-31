#include "layoutdump.h"

#include <QColor>
#include <QFont>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QPalette>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

double round2(double v)
{
    return std::round(v * 100.0) / 100.0;
}

QJsonArray rectJson(const QRectF &r)
{
    return { round2(r.x()), round2(r.y()), round2(r.width()), round2(r.height()) };
}

QString hexColor(const QColor &c)
{
    const QColor rgb = c.toRgb();
    return QString::asprintf("#%02x%02x%02x%02x", rgb.red(), rgb.green(), rgb.blue(), rgb.alpha());
}

/// "BText" for "BText_QMLTYPE_3", "Text" for "QQuickText".
QString typeName(const QObject *object)
{
    QString name = QString::fromLatin1(object->metaObject()->className());
    for (const auto &marker : { u"_QMLTYPE_"_s, u"_QML_"_s }) {
        const qsizetype i = name.indexOf(marker);
        if (i > 0)
            name.truncate(i);
    }
    if (name.startsWith(u"QQuick"))
        name = name.mid(6);
    return name;
}

QString collapsed(const QString &text)
{
    return text.simplified();
}

QString firstChars(const QString &text, qsizetype n)
{
    return text.size() > n ? text.left(n) : text;
}

bool isAncestor(const QQuickItem *ancestor, const QQuickItem *item)
{
    for (const QQuickItem *p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
        if (p == ancestor)
            return true;
    }
    return false;
}

bool sameRect(const QRectF &a, const QRectF &b)
{
    return std::abs(a.left() - b.left()) <= 1.0 && std::abs(a.top() - b.top()) <= 1.0
        && std::abs(a.right() - b.right()) <= 1.0 && std::abs(a.bottom() - b.bottom()) <= 1.0;
}

/// The Accessible.name of `object` (the attached object is a child of the item).
QString accessibleName(const QObject *object)
{
    for (const QObject *child : object->children()) {
        if (child->inherits("QQuickAccessibleAttached"))
            return child->property("name").toString();
    }
    return {};
}

/// The first text shown inside `item` (depth first), for a target without a label of its own.
QString innerText(const QQuickItem *item, int depth = 0)
{
    if (!item || depth > 6)
        return {};
    for (const QQuickItem *child : item->childItems()) {
        if (!child->isVisible())
            continue;
        if (child->inherits("QQuickText") || child->inherits("QQuickTextEdit") || child->inherits("QQuickTextInput")) {
            const QString t = collapsed(child->property("text").toString());
            if (!t.isEmpty() && !Qt::mightBeRichText(t))
                return t;
            if (!t.isEmpty()) {
                QTextDocument doc;
                doc.setHtml(t);
                return collapsed(doc.toPlainText());
            }
        }
        const QString inner = innerText(child, depth + 1);
        if (!inner.isEmpty())
            return inner;
    }
    return {};
}

/// A plain Item that only takes taps on a text it exactly covers (BlockText's overlay on a
/// rendered block: a tap edits the text where it lands): the text itself is the target, sized by
/// its line height — WCAG 2.5.8's inline exception, like a web editor's text, where a tap puts the
/// caret into the line under it (Obsidian's CodeMirror content is one such field). The text: a
/// Text, or the read-only TextEdit a block with chips is shown by (in its Loader).
bool tapsOnItsText(const QQuickItem *item, const QRectF &scene)
{
    if (qstrcmp(item->metaObject()->className(), "QQuickItem") != 0 || !item->parentItem())
        return false;
    const auto isText = [](const QQuickItem *t) {
        return t && (t->inherits("QQuickText") || (t->inherits("QQuickTextEdit") && t->property("readOnly").toBool()));
    };
    for (const QQuickItem *sibling : item->parentItem()->childItems()) {
        if (sibling == item || !sibling->isVisible())
            continue;
        const QQuickItem *text = sibling;
        if (sibling->inherits("QQuickLoader"))
            text = qobject_cast<const QQuickItem *>(sibling->property("item").value<QObject *>());
        if (!isText(text))
            continue;
        if (sameRect(text->mapRectToScene(QRectF(0, 0, text->width(), text->height())), scene))
            return true;
    }
    return false;
}

QString targetLabel(const QQuickItem *item)
{
    for (const char *prop : { "text", "label", "title" }) {
        const QVariant v = item->property(prop);
        if (v.metaType().id() == QMetaType::QString && !v.toString().trimmed().isEmpty())
            return firstChars(collapsed(v.toString()), 60);
    }
    const QString name = collapsed(accessibleName(item));
    return firstChars(name.isEmpty() ? innerText(item) : name, 60);
}

/// One run of characters of a rich text with the same colours.
struct Run {
    QString text;
    QColor color;
    std::optional<QColor> bg;
};

/// The colour runs of `doc`: the characters' own formats, then the layouts' additional formats
/// (QSyntaxHighlighter) over them. Anchors without a colour of their own (the HTML import gives
/// them the palette's link colour) are drawn in the item's `linkColor`.
QList<Run> colourRuns(const QTextDocument *doc, const QColor &color, const QColor &linkColor)
{
    const QColor paletteLink = QGuiApplication::palette().color(QPalette::Link);
    QList<Run> runs;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        const QString text = block.text();
        const qsizetype n = text.size();
        if (n == 0)
            continue;
        QList<QColor> fg(n, color);
        QList<std::optional<QColor>> bg(n);
        auto apply = [&](qsizetype start, qsizetype length, const QTextCharFormat &format) {
            const qsizetype end = std::min(n, start + length);
            for (qsizetype i = std::max<qsizetype>(0, start); i < end; ++i) {
                const bool anchor = format.isAnchor() || !format.anchorHref().isEmpty();
                const bool own = format.foreground().style() != Qt::NoBrush;
                if (anchor && linkColor.isValid() && (!own || format.foreground().color() == paletteLink))
                    fg[i] = linkColor;
                else if (own)
                    fg[i] = format.foreground().color();
                if (format.background().style() != Qt::NoBrush)
                    bg[i] = format.background().color();
            }
        };
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (fragment.isValid())
                apply(fragment.position() - block.position(), fragment.length(), fragment.charFormat());
        }
        if (const QTextLayout *layout = block.layout()) {
            for (const QTextLayout::FormatRange &range : layout->formats())
                apply(range.start, range.length, range.format);
        }
        for (qsizetype i = 0; i < n;) {
            qsizetype j = i + 1;
            while (j < n && fg[j] == fg[i] && bg[j] == bg[i])
                ++j;
            Run run{ text.mid(i, j - i), fg[i], bg[i] };
            if (!runs.isEmpty() && runs.last().color == run.color && runs.last().bg == run.bg)
                runs.last().text += u' ' + run.text;
            else
                runs.append(run);
            i = j;
        }
    }
    return runs;
}

/// The lines a text shows: Text's lineCount stays 1 for rich text, so also its content height over
/// one line's height (Text.FixedHeight: lineHeight px; else the font's line spacing × lineHeight).
int textLines(const QQuickItem *item, const QFont &font)
{
    const QVariant counted = item->property("lineCount");
    double line = QFontMetricsF(font).lineSpacing();
    const QVariant lineHeight = item->property("lineHeight");
    if (lineHeight.isValid() && lineHeight.toDouble() > 0) {
        if (item->property("lineHeightMode").toInt() == 1) // Text.FixedHeight
            line = lineHeight.toDouble();
        else
            line *= lineHeight.toDouble();
    }
    const double contentHeight = item->property("contentHeight").toDouble();
    const int estimated = line > 0 ? int(std::lround(contentHeight / line)) : 1;
    return std::max({ 1, counted.isValid() ? counted.toInt() : 1, estimated });
}

struct Record {
    QJsonObject json;
    QQuickItem *item = nullptr;
    QRectF rect;
    int paint = 0;
};

struct BoxRecord {
    QJsonObject json;
    QQuickItem *item = nullptr;
    QRectF rect;
    double alpha = 1; // colour alpha × accumulated opacity
    bool overlay = false;
    int paint = 0;
};

class Walker {
public:
    explicit Walker(const QRectF &window) : m_window(window) { }

    void visit(QQuickItem *item, double parentAlpha, const QRectF &clip, bool inOverlay,
               const QRectF *targetAncestor)
    {
        if (!item->isVisible())
            return;
        const double alpha = parentAlpha * item->opacity();
        if (alpha <= 0.01)
            return;
        const bool overlay = inOverlay || item->inherits("QQuickOverlay");
        QRectF childClip = clip;
        if (item->clip())
            childClip = clip.intersected(item->mapRectToScene(item->clipRect()));

        QList<QQuickItem *> children = item->childItems();
        std::stable_sort(children.begin(), children.end(),
                         [](const QQuickItem *a, const QQuickItem *b) { return a->z() < b->z(); });
        auto it = children.cbegin();
        // Negative z: painted before the item itself.
        for (; it != children.cend() && (*it)->z() < 0; ++it) {
            if (!childClip.isEmpty())
                visit(*it, alpha, childClip, overlay, targetAncestor);
        }
        const int paint = m_paint++;
        const std::optional<QRectF> target = record(item, alpha, clip, overlay, paint, targetAncestor);
        const QRectF *childTarget = target ? &*target : targetAncestor;
        for (; it != children.cend(); ++it) {
            if (!childClip.isEmpty())
                visit(*it, alpha, childClip, overlay, childTarget);
        }
    }

    QJsonObject result(QQuickWindow *window, const QObject *theme)
    {
        markOccluded(m_texts);
        markOccluded(m_targets);
        QJsonArray texts, targets, boxes;
        for (const Record &r : std::as_const(m_texts))
            texts.append(r.json);
        for (const Record &r : std::as_const(m_targets))
            targets.append(r.json);
        for (const BoxRecord &b : std::as_const(m_boxes))
            boxes.append(b.json);
        QJsonObject tokens;
        if (theme) {
            for (const char *name : { "fg", "fgMuted", "fgFaint", "bg", "bgElevated", "bgSunken", "accent", "link" }) {
                const QVariant v = theme->property(name);
                if (v.canConvert<QColor>() && v.value<QColor>().isValid())
                    tokens.insert(QString::fromLatin1(name), hexColor(v.value<QColor>()));
            }
        }
        return {
            { u"schema"_s, 1 },
            { u"app"_s, u"brulion"_s },
            { u"viewport"_s,
              QJsonObject{ { u"w"_s, window->width() },
                           { u"h"_s, window->height() },
                           { u"dpr"_s, round2(window->effectiveDevicePixelRatio()) } } },
            { u"texts"_s, texts },
            { u"targets"_s, targets },
            { u"boxes"_s, boxes },
            { u"tokens"_s, tokens },
        };
    }

private:
    /// Records `item` as a text, a target and/or a box; returns its rectangle when it is a target
    /// (the nearest target ancestor of its children).
    std::optional<QRectF> record(QQuickItem *item, double alpha, const QRectF &clip, bool overlay, int paint,
                                 const QRectF *targetAncestor)
    {
        const QRectF visible = clip.intersected(m_window);
        if (visible.isEmpty())
            return std::nullopt;
        const QRectF scene = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        // MouseArea's own `enabled` hides QQuickItem's (isEnabled() stays true when it is off).
        const bool enabled = item->isEnabled() && item->property("enabled").toBool();

        if (item->inherits("QQuickText") || item->inherits("QQuickTextEdit") || item->inherits("QQuickTextInput"))
            recordText(item, alpha, visible, paint, enabled);

        std::optional<QRectF> target;
        QString kind;
        if (item->inherits("QQuickAbstractButton") || item->inherits("QQuickComboBox") || item->inherits("QQuickSlider")
            || item->inherits("QQuickSpinBox")) {
            kind = u"button"_s;
        } else if (item->inherits("QQuickMouseArea")) {
            if (enabled && item->property("acceptedButtons").toInt() != 0)
                kind = u"mouse"_s;
        } else if ((item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit"))
                   && !item->property("readOnly").toBool()) {
            kind = u"field"_s;
        }
        if (kind.isEmpty() && enabled) {
            for (const QObject *child : item->children()) {
                if (child->inherits("QQuickTapHandler") && child->property("enabled").toBool()) {
                    kind = u"tap"_s;
                    break;
                }
            }
        }
        if (!kind.isEmpty()) {
            // The whole hit area (a row scrolled half out of the view is still as large), when any
            // of it is visible.
            const QRectF hit = scene.intersects(visible) ? scene : QRectF();
            if (!hit.isEmpty() && !(targetAncestor && sameRect(hit, *targetAncestor))) {
                Record r;
                r.item = item;
                r.rect = hit;
                r.paint = paint;
                r.json = {
                    { u"type"_s, typeName(item) },
                    { u"name"_s, item->objectName() },
                    { u"kind"_s, kind },
                    { u"label"_s, targetLabel(item) },
                    { u"rect"_s, rectJson(hit) },
                    { u"enabled"_s, enabled },
                    { u"inline"_s, kind == u"tap" && tapsOnItsText(item, scene) },
                };
                m_targets.append(r);
                target = hit;
            } else if (!hit.isEmpty()) {
                target = hit;
            }
        }

        if (item->inherits("QQuickRectangle")) {
            const QColor color = item->property("color").value<QColor>();
            double borderWidth = 0;
            QColor borderColor;
            if (const QObject *border = item->property("border").value<QObject *>()) {
                borderWidth = border->property("width").toDouble();
                borderColor = border->property("color").value<QColor>();
                if (!borderColor.isValid() || borderColor.alphaF() < 0.05)
                    borderWidth = 0;
            }
            const QRectF rect = scene.intersected(visible);
            if (!rect.isEmpty() && (color.alphaF() >= 0.05 || borderWidth > 0)) {
                BoxRecord b;
                b.item = item;
                b.rect = rect;
                b.alpha = color.alphaF() * alpha;
                b.overlay = overlay;
                b.paint = paint;
                b.json = {
                    { u"type"_s, typeName(item) },
                    { u"rect"_s, rectJson(rect) },
                    { u"color"_s, hexColor(color) },
                    { u"alpha"_s, round2(alpha) },
                    { u"border"_s, round2(borderWidth) },
                    { u"borderColor"_s, borderWidth > 0 ? hexColor(borderColor) : QString() },
                    { u"radius"_s, round2(item->property("radius").toDouble()) },
                    { u"paint"_s, paint },
                };
                m_boxes.append(b);
            }
        }
        return target;
    }

    void recordText(QQuickItem *item, double alpha, const QRectF &visible, int paint, bool enabled)
    {
        const bool input = item->inherits("QQuickTextInput");
        const bool edit = item->inherits("QQuickTextEdit");
        QString raw = item->property(input ? "displayText" : "text").toString();
        if (raw.trimmed().isEmpty())
            return;
        const QRectF rect = item->mapRectToScene(item->boundingRect()).intersected(visible);
        if (rect.isEmpty())
            return;
        const QColor color = item->property("color").value<QColor>();
        const QColor linkColor = item->property("linkColor").value<QColor>();

        // Rich text: its colour runs (links, chips, highlighting).
        QList<Run> runs;
        QString plain = raw;
        if (edit) {
            const auto *document = qobject_cast<QQuickTextDocument *>(item->property("textDocument").value<QObject *>());
            if (document && document->textDocument()) {
                runs = colourRuns(document->textDocument(), color, linkColor);
                plain = document->textDocument()->toPlainText();
            }
        } else if (!input) {
            const int format = item->property("textFormat").toInt();
            const bool rich = format == Qt::RichText || format == 4 /* Text.StyledText */
                || (format == Qt::AutoText && Qt::mightBeRichText(raw));
            if (rich) {
                QTextDocument doc;
                doc.setHtml(raw);
                runs = colourRuns(&doc, color, linkColor);
                plain = doc.toPlainText();
            }
        }
        plain = collapsed(plain);
        if (plain.isEmpty())
            return;
        // Fragments only when the text is not of one colour.
        QJsonArray fragments;
        const bool mixed = std::any_of(runs.cbegin(), runs.cend(),
                                       [&](const Run &r) { return r.color != color || r.bg.has_value(); });
        if (mixed) {
            for (const Run &r : std::as_const(runs)) {
                const QString t = collapsed(r.text);
                if (t.isEmpty())
                    continue;
                QJsonObject f{ { u"text"_s, firstChars(t, 40) },
                               { u"chars"_s, int(t.size()) },
                               { u"color"_s, hexColor(r.color) } };
                if (r.bg)
                    f.insert(u"bg"_s, hexColor(*r.bg));
                fragments.append(f);
            }
        }

        const QFont font = item->property("font").value<QFont>();
        const double px = font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF() * 96.0 / 72.0;
        bool placeholder = false;
        if (const QQuickItem *parent = item->parentItem()) {
            if ((parent->inherits("QQuickTextInput") || parent->inherits("QQuickTextEdit"))
                && parent->property("text").toString().isEmpty()
                && parent->property("placeholderText").toString() == item->property("text").toString())
                placeholder = true;
        }
        Record r;
        r.item = item;
        r.rect = rect;
        r.paint = paint;
        r.json = {
            { u"type"_s, typeName(item) },
            { u"name"_s, item->objectName() },
            { u"text"_s, firstChars(plain, 80) },
            { u"chars"_s, int(plain.size()) },
            { u"rect"_s, rectJson(rect) },
            { u"color"_s, hexColor(color) },
            { u"alpha"_s, round2(alpha) },
            { u"px"_s, round2(px) },
            { u"weight"_s, font.weight() },
            { u"family"_s, QFontInfo(font).family() },
            { u"lines"_s, textLines(item, font) },
            { u"enabled"_s, enabled },
            { u"placeholder"_s, placeholder },
            { u"paint"_s, paint },
            { u"fragments"_s, fragments },
        };
        m_texts.append(r);
    }

    void markOccluded(QList<Record> &records) const
    {
        for (Record &r : records) {
            const QPointF centre = r.rect.center();
            bool occluded = false;
            for (const BoxRecord &b : m_boxes) {
                if (b.paint <= r.paint || b.alpha < (b.overlay ? 0.3 : 0.9) || !b.rect.contains(centre))
                    continue;
                // Its own background (a child) or a container (an ancestor) does not hide it.
                if (b.item == r.item || isAncestor(b.item, r.item) || isAncestor(r.item, b.item))
                    continue;
                occluded = true;
                break;
            }
            r.json.insert(u"occluded"_s, occluded);
        }
    }

    QRectF m_window;
    int m_paint = 0;
    QList<Record> m_texts;
    QList<Record> m_targets;
    QList<BoxRecord> m_boxes;
};

} // namespace

QJsonObject layoutDump(QQuickWindow *window, const QObject *theme)
{
    if (!window || !window->contentItem())
        return {};
    const QRectF bounds(0, 0, window->width(), window->height());
    Walker walker(bounds);
    walker.visit(window->contentItem(), 1.0, bounds, false, nullptr);
    return walker.result(window, theme);
}

bool writeLayoutDump(QQuickWindow *window, const QString &path, const QObject *theme, QString *error)
{
    if (!window) {
        if (error)
            *error = u"no window"_s;
        return false;
    }
    const QJsonObject dump = layoutDump(window, theme);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = u"%1: %2"_s.arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(dump).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = u"%1: %2"_s.arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace brulion::ui
