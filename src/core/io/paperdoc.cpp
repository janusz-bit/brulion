#include "io/paperdoc_p.h"

#include "io/exporters_p.h"
#include "io/htmlrender_p.h"
#include "io/messages_p.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QImageReader>
#include <QPagedPaintDevice>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

constexpr qreal CssDpi = 96;
constexpr qreal MmPerInch = 25.4;

const QString kUnchecked = u"☐ "_s;
const QString kChecked = u"☑ "_s;

/// The text of escaped HTML (what HtmlRenderer::escape produced, plus numeric references).
QString unescape(QStringView html)
{
    QString out;
    out.reserve(html.size());
    for (qsizetype i = 0; i < html.size(); ++i) {
        if (html[i] != u'&') {
            out += html[i];
            continue;
        }
        const qsizetype end = html.indexOf(u';', i);
        if (end < 0 || end - i > 10) {
            out += html[i];
            continue;
        }
        const QStringView entity = html.sliced(i + 1, end - i - 1);
        QString text;
        if (entity == u"amp")
            text = u"&"_s;
        else if (entity == u"lt")
            text = u"<"_s;
        else if (entity == u"gt")
            text = u">"_s;
        else if (entity == u"quot")
            text = u"\""_s;
        else if (entity == u"apos")
            text = u"'"_s;
        else if (entity.startsWith(u'#')) {
            bool ok = false;
            const uint code = entity.startsWith(u"#x") || entity.startsWith(u"#X") ? entity.sliced(2).toUInt(&ok, 16)
                                                                                   : entity.sliced(1).toUInt(&ok, 10);
            if (ok && code > 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF)) {
                const char32_t c = code;
                text = QString::fromUcs4(&c, 1);
            }
        }
        if (text.isEmpty()) {
            out += html[i];
            continue;
        }
        out += text;
        i = end;
    }
    return out;
}

QString esc(const QString &text)
{
    return HtmlRenderer::escape(text);
}

/// The value of attribute `name` of a tag of ours (`name="…"`, escaped), unescaped; "" if none.
QString attribute(QStringView tag, QStringView name)
{
    const QString key = u' ' + name.toString() + u"=\""_s;
    const qsizetype at = tag.indexOf(key);
    if (at < 0)
        return {};
    const qsizetype from = at + key.size();
    const qsizetype to = tag.indexOf(u'"', from);
    return to < 0 ? QString() : unescape(tag.sliced(from, to - from));
}

bool isExternalLink(const QString &href)
{
    return href.startsWith(u"http://", Qt::CaseInsensitive) || href.startsWith(u"https://", Qt::CaseInsensitive)
           || href.startsWith(u"mailto:", Qt::CaseInsensitive);
}

/// The paper's style sheet (Qt's CSS subset), the HTML export's light colours.
QString styleSheet(const QString &mono)
{
    return uR"css(
body { color: #1f2430; }
h1 { font-size: 19pt; margin-top: 14px; margin-bottom: 8px; }
h1.title { font-size: 22pt; margin-top: 0px; margin-bottom: 6px; }
h2 { font-size: 15.5pt; margin-top: 14px; margin-bottom: 6px; }
h3 { font-size: 13pt; margin-top: 12px; margin-bottom: 4px; }
h4 { font-size: 11.5pt; margin-top: 10px; margin-bottom: 4px; }
h5, h6 { font-size: 10.5pt; margin-top: 8px; margin-bottom: 4px; }
p { margin-top: 0px; margin-bottom: 7px; }
li { margin-bottom: 2px; }
a { color: #2c6aa3; text-decoration: underline; }
.link { color: #2c6aa3; }
.missing { color: #7a5ea3; }
code { font-family: "MONO"; background-color: #f3eee2; }
pre { font-family: "MONO"; font-size: 9pt; background-color: #f3eee2; margin-top: 4px; margin-bottom: 9px; }
blockquote { color: #535a6a; margin-left: 18px; margin-right: 0px; }
.callout { background-color: #f6f2e8; margin-top: 6px; margin-bottom: 8px; }
.callout-title { font-weight: bold; }
.tags { color: #2c6aa3; }
.chip { color: #535a6a; font-size: 9pt; }
.due { color: #2b5f8f; }
.scheduled { color: #6a4f93; }
.reminder { color: #7a4f00; }
.alarm { color: #7a4f00; font-weight: bold; }
.recurrence { color: #2a6e44; }
.prop { color: #535a6a; font-size: 9pt; }
.cancelled { color: #535a6a; text-decoration: line-through; }
.done { color: #535a6a; }
.math { font-family: serif; }
div.math { text-align: center; }
.embed-title { color: #535a6a; font-weight: bold; }
.footnote { color: #535a6a; }
th { font-weight: bold; background-color: #f3eee2; }
.picture { text-align: center; }
)css"_s.replace(u"MONO"_s, mono);
}

/// QTextDocument that shows only the paper document's own pictures.
class PaperTextDocument final : public QTextDocument {
public:
    explicit PaperTextDocument(const QHash<QString, PaperPicture> *pictures) : m_pictures(pictures) { }

protected:
    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource) {
            const auto it = m_pictures->constFind(name.toString());
            if (it != m_pictures->constEnd())
                return it->image;
        }
        // Never a file, the app's resources or the network.
        return {};
    }

private:
    const QHash<QString, PaperPicture> *m_pictures;
};

/// The rows of `table` (our renderer's <table>…</table>) cut to at most `maxRows` rows and
/// `maxCells` cells.
QString limitTable(const QString &table, int maxRows, int maxCells, int *keptRows, int *allRows)
{
    QList<qsizetype> rowStarts;
    for (qsizetype i = table.indexOf(u"<tr"); i >= 0; i = table.indexOf(u"<tr", i + 3)) {
        if (i + 3 < table.size() && (table[i + 3] == u'>' || table[i + 3] == u' '))
            rowStarts.append(i);
    }
    *allRows = int(rowStarts.size());
    *keptRows = *allRows;
    if (rowStarts.isEmpty())
        return table;
    // Columns: the widest of the first rows (a row is one line of cells).
    qsizetype columns = 1;
    for (qsizetype r = 0; r < std::min<qsizetype>(rowStarts.size(), 3); ++r) {
        const qsizetype end = r + 1 < rowStarts.size() ? rowStarts[r + 1] : table.size();
        const QStringView row = QStringView(table).sliced(rowStarts[r], end - rowStarts[r]);
        columns = std::max(columns, row.count(u"<td") + row.count(u"<th"));
    }
    const qsizetype rows = std::clamp<qsizetype>(maxCells / columns, 2, maxRows);
    if (rowStarts.size() <= rows)
        return table;
    *keptRows = int(rows);
    return table.left(rowStarts[rows]) + u"</tbody></table>"_s;
}

/// The words of a date / reminder / priority chip of the HTML ("📅 2026-10-20" -> "due:
/// 2026-10-20"); "" to keep it as it is.
QString chipWords(const QString &classes, const QString &text)
{
    if (classes.startsWith(u"priority")) {
        const QString level = classes.section(u"priority-"_s, 1, 1).section(u' ', 0, 0);
        const QString name = level == u"highest"  ? IoMessages::tr("highest")
                             : level == u"high"   ? IoMessages::tr("high")
                             : level == u"medium" ? IoMessages::tr("medium")
                             : level == u"low"    ? IoMessages::tr("low")
                             : level == u"lowest" ? IoMessages::tr("lowest")
                                                  : QString();
        return name.isEmpty() ? QString() : IoMessages::tr("priority: %1").arg(name);
    }
    // A reminder's own recurrence follows it: "⏰ 2026-10-16 09:00 🔁 every week".
    const QString recurrenceMark = u" 🔁 "_s;
    QString main = text;
    QString repeats;
    if (const qsizetype at = text.indexOf(recurrenceMark); at > 0) {
        main = text.left(at);
        repeats = IoMessages::tr("repeats: %1").arg(text.sliced(at + recurrenceMark.size()).trimmed());
    }
    const auto after = [&](QStringView mark) { return main.sliced(mark.size()).trimmed(); };
    QString words;
    if (main.startsWith(u"📅"))
        words = IoMessages::tr("due: %1").arg(after(u"📅"));
    else if (main.startsWith(u"⏳"))
        words = IoMessages::tr("scheduled: %1").arg(after(u"⏳"));
    else if (main.startsWith(u"🛫"))
        words = IoMessages::tr("starts: %1").arg(after(u"🛫"));
    else if (main.startsWith(u"✅"))
        words = IoMessages::tr("done: %1").arg(after(u"✅"));
    else if (main.startsWith(u"❌"))
        words = IoMessages::tr("cancelled: %1").arg(after(u"❌"));
    else if (main.startsWith(u"🔁"))
        words = IoMessages::tr("repeats: %1").arg(after(u"🔁"));
    else if (main.startsWith(u"⏰!"))
        words = IoMessages::tr("alarm: %1").arg(after(u"⏰!"));
    else if (main.startsWith(u"⏰"))
        words = IoMessages::tr("reminder: %1").arg(after(u"⏰"));
    if (words.isEmpty())
        return {};
    return repeats.isEmpty() ? words : words + u", "_s + repeats;
}

} // namespace

QPageSize pageSizeOf(PaperSize size)
{
    return QPageSize(size == PaperSize::Letter ? QPageSize::Letter : QPageSize::A4);
}

PaperDocument::PaperDocument(const ExportInput &input, const QPageLayout &layout) : m_layout(layout)
{
    const std::unique_ptr<Exporter> html = makeHtmlSingleExporter();
    const ExportResult r = html->run(input);
    m_warnings = r.warnings;
    QString page = r.files.isEmpty() ? QString() : QString::fromUtf8(r.files.first().data);
    const qsizetype from = page.indexOf(u"<main>");
    const qsizetype to = page.lastIndexOf(u"</main>");
    QString body = from >= 0 && to > from ? page.sliced(from + 6, to - from - 6) : QString();
    page.clear();
    m_notes = int(body.count(u"<section class=\"note\""));
    m_title = input.vaultName.trimmed().isEmpty() ? u"Brulion"_s : input.vaultName.trimmed();
    if (m_notes == 1) {
        static const QRegularExpression title(u"<h1 class=\"title\">(.*?)</h1>"_s);
        if (const auto m = title.match(body); m.hasMatch())
            m_title = unescape(m.captured(1));
    }
    m_document = std::make_unique<PaperTextDocument>(&m_pictures);
    m_document->setUndoRedoEnabled(false);
    QFont font = QGuiApplication::font();
    font.setPointSizeF(10.5);
    font.setStyleStrategy(QFont::PreferOutline);
    m_document->setDefaultFont(font);
    QTextOption option = m_document->defaultTextOption();
    option.setUseDesignMetrics(true);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_document->setDefaultTextOption(option);
    m_document->setDocumentMargin(0);
    m_document->setDefaultStyleSheet(styleSheet(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()));
    m_document->setMetaInformation(QTextDocument::DocumentTitle, m_title);
    m_document->setHtml(u"<html><body>"_s + adapt(std::move(body)) + u"</body></html>"_s);
    finish();
}

PaperDocument::~PaperDocument() = default;

QTextDocument *PaperDocument::document() const
{
    return m_document.get();
}

qreal PaperDocument::textWidth() const
{
    return m_layout.paintRect(QPageLayout::Millimeter).width() / MmPerInch * CssDpi;
}

QString PaperDocument::addPicture(QImage image, QByteArray data, QString format, const QString &alt)
{
    if (image.width() > MaxPictureSide || image.height() > MaxPictureSide) {
        image = image.scaled(MaxPictureSide, MaxPictureSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        data.clear();
    }
    if (data.isEmpty() || (format != u"png" && format != u"jpeg")) {
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        const bool opaque = !image.hasAlphaChannel();
        format = opaque && format == u"jpeg" ? u"jpeg"_s : u"png"_s;
        image.save(&buffer, format == u"jpeg" ? "JPEG" : "PNG", format == u"jpeg" ? 90 : -1);
    }
    // Shown at its size in CSS pixels (a picture of a high-density screen at its logical size),
    // at most the text's width and most of a page's height.
    QSizeF size = QSizeF(image.size()) / std::max<qreal>(1, image.devicePixelRatio());
    const QSizeF room(textWidth(), m_layout.paintRect(QPageLayout::Millimeter).height() / MmPerInch * CssDpi * 0.85);
    if (size.width() > room.width() || size.height() > room.height())
        size = size.scaled(room, Qt::KeepAspectRatio);
    const QString name = u"brulion-picture:"_s + QString::number(m_pictures.size() + 1);
    m_pictures.insert(name, PaperPicture{std::move(image), std::move(data), format, size, alt});
    return u"<img src=\""_s + name + u"\" width=\""_s + QString::number(std::max(1, qRound(size.width())))
           + u"\" height=\""_s + QString::number(std::max(1, qRound(size.height()))) + u"\">"_s;
}

QString PaperDocument::svgPicture(const QString &svgText, const QString &alt)
{
    // The dark colours are a style sheet of media rules: the attributes hold the light ones.
    static const QRegularExpression style(u"<style\\b.*?</style>"_s, QRegularExpression::DotMatchesEverythingOption);
    QByteArray svg = QString(svgText).remove(style).toUtf8();
    QBuffer buffer(&svg);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "svg");
    const QSize natural = reader.size();
    if (!reader.canRead() || natural.isEmpty())
        return {};
    // Twice the size for print, at most 4000 px a side and 16 MP.
    qreal scale = std::min({2.0, 4000.0 / std::max(natural.width(), natural.height()),
                            std::sqrt(16e6 / (qreal(natural.width()) * natural.height()))});
    scale = std::max(scale, 0.01);
    reader.setScaledSize((QSizeF(natural) * scale).toSize().expandedTo(QSize(1, 1)));
    reader.setBackgroundColor(Qt::white);
    QImage image = reader.read();
    if (image.isNull())
        return {};
    image.setDevicePixelRatio(qreal(image.width()) / natural.width());
    return addPicture(std::move(image), {}, u"png"_s, alt);
}

QString PaperDocument::pictureHtml(const QString &src, const QString &alt)
{
    if (src.startsWith(u"data:")) {
        const qsizetype comma = src.indexOf(u',');
        const QString header = src.sliced(5, std::max<qsizetype>(0, comma - 5));
        const QString mime = header.section(u';', 0, 0).toLower();
        if (comma > 0 && header.endsWith(u";base64") && mime.startsWith(u"image/")) {
            const QByteArray bytes = QByteArray::fromBase64(QStringView(src).sliced(comma + 1).toLatin1());
            if (mime == u"image/svg+xml") {
                const QString html = svgPicture(QString::fromUtf8(bytes), alt);
                if (!html.isEmpty())
                    return html;
            } else {
                QBuffer buffer;
                buffer.setData(bytes);
                buffer.open(QIODevice::ReadOnly);
                QImageReader reader(&buffer);
                reader.setAutoTransform(true);
                QString format = QString::fromLatin1(reader.format()).toLower();
                if (format == u"jpg")
                    format = u"jpeg"_s;
                const QImage image = reader.read();
                if (!image.isNull()) {
                    // A JPEG turned by its EXIF orientation is embedded as drawn.
                    const bool asIs = (format == u"png" || format == u"jpeg") && !reader.transformation();
                    return addPicture(image, asIs ? bytes : QByteArray(), format, alt);
                }
            }
        }
        m_warnings.append(
                IoMessages::tr("The picture “%1” could not be shown in the document.").arg(alt.isEmpty() ? mime : alt));
        return u"<span class=\"missing\">"_s + esc(IoMessages::tr("Picture: %1").arg(alt.isEmpty() ? mime : alt))
               + u"</span>"_s;
    }
    // A picture from the internet without a copy in the export: its address.
    const QString label = IoMessages::tr("Picture: %1").arg(alt.isEmpty() ? src : alt);
    if (isExternalLink(src))
        return u"<a href=\""_s + esc(src) + u"\">"_s + esc(label) + u"</a>"_s;
    return u"<span class=\"missing\">"_s + esc(label) + u"</span>"_s;
}

QString PaperDocument::adapt(QString body)
{
    using RE = QRegularExpression;
    // One note: no table of contents, no notebook heading.
    if (m_notes <= 1) {
        static const RE head(u"^\\s*<h1>.*?</h1>\\s*<nav class=\"toc\">.*?</nav>"_s, RE::DotMatchesEverythingOption);
        body.remove(head);
    }
    // A first heading that repeats the title: the title alone.
    {
        static const RE repeated(
                u"(<h1 class=\"title\">(.*?)</h1>\\n(?:<p class=\"tags\">.*?</p>)?)<h1 id=\"[^\"]*\">(.*?)</h1>"_s);
        QString out;
        qsizetype pos = 0;
        for (auto it = repeated.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            if (unescape(m.captured(2)).trimmed().compare(unescape(m.captured(3)).trimmed(), Qt::CaseInsensitive) != 0)
                continue;
            out += QStringView(body).sliced(pos, m.capturedStart() - pos);
            out += m.captured(1);
            pos = m.capturedEnd();
        }
        if (pos > 0)
            body = out + QStringView(body).sliced(pos);
    }
    // Each note after the first on a new page.
    {
        qsizetype at = body.indexOf(u"<section class=\"note\"");
        bool first = true;
        while (at >= 0) {
            const qsizetype heading = body.indexOf(u"<h1 class=\"title\"", at);
            if (heading < 0)
                break;
            if (!first) {
                const QString broken = u"<h1 class=\"title\" style=\"page-break-before:always\""_s;
                body.replace(heading, 17, broken);
            }
            first = false;
            at = body.indexOf(u"<section class=\"note\"", heading + 17);
        }
    }
    // Mermaid diagrams: the SVG as a picture, else the source.
    {
        static const RE figure(u"<figure class=\"mermaid\"[^>]*>(<svg\\b.*?</svg>)\\s*<details><summary>.*?</summary>"
                               u"<pre><code class=\"language-mermaid\">(.*?)</code></pre></details></figure>"_s,
                               RE::DotMatchesEverythingOption);
        static const RE svgTitle(u"<title>(.*?)</title>"_s, RE::DotMatchesEverythingOption);
        QString out;
        qsizetype pos = 0;
        for (auto it = figure.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(body).sliced(pos, m.capturedStart() - pos);
            const auto t = svgTitle.match(m.captured(1));
            const QString alt = t.hasMatch() ? unescape(t.captured(1)) : IoMessages::tr("Diagram");
            const QString picture = svgPicture(m.captured(1), alt);
            if (picture.isEmpty()) {
                m_warnings.append(IoMessages::tr("A diagram could not be drawn in the document – its source is "
                                                 "shown instead."));
                out += u"<pre>"_s + m.captured(2) + u"</pre>"_s;
            } else {
                out += u"<p class=\"picture\" align=\"center\">"_s + picture + u"</p>"_s;
            }
            pos = m.capturedEnd();
        }
        if (pos > 0)
            body = out + QStringView(body).sliced(pos);
    }
    // Other inline SVG (charts…).
    {
        static const RE svg(u"<svg\\b.*?</svg>"_s, RE::DotMatchesEverythingOption);
        QString out;
        qsizetype pos = 0;
        for (auto it = svg.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(body).sliced(pos, m.capturedStart() - pos);
            const QString picture = svgPicture(m.captured(0), IoMessages::tr("Picture"));
            if (picture.isEmpty())
                m_warnings.append(IoMessages::tr("A picture could not be shown in the document."));
            out += picture;
            pos = m.capturedEnd();
        }
        if (pos > 0)
            body = out + QStringView(body).sliced(pos);
    }
    // Pictures, audio and video.
    {
        static const RE media(u"<img\\b[^>]*>|<(audio|video)\\b[^>]*></(?:audio|video)>"_s);
        QString out;
        qsizetype pos = 0;
        for (auto it = media.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(body).sliced(pos, m.capturedStart() - pos);
            const QStringView tag = m.capturedView(0);
            if (m.capturedLength(1) > 0) {
                out += u"<span class=\"missing\">"_s
                       + esc(m.captured(1) == u"audio" ? IoMessages::tr("[Audio recording]")
                                                       : IoMessages::tr("[Video]"))
                       + u"</span>"_s;
            } else if (const QString src = attribute(tag, u"src"); src.startsWith(u"brulion-picture:")) {
                out += tag; // a diagram's, made above
            } else {
                out += pictureHtml(src, attribute(tag, u"alt"));
            }
            pos = m.capturedEnd();
        }
        if (pos > 0)
            body = out + QStringView(body).sliced(pos);
    }
    // Links: to the web and mail stay links; inside the document (#…), to embedded attachments
    // (data:) and anything else become text in the link colour.
    {
        static const RE anchor(u"<a\\b[^>]*>|</a>"_s);
        QString out;
        qsizetype pos = 0;
        QList<bool> open;
        for (auto it = anchor.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            out += QStringView(body).sliced(pos, m.capturedStart() - pos);
            if (m.capturedView(0) == u"</a>") {
                out += !open.isEmpty() && !open.takeLast() ? u"</span>"_s : u"</a>"_s;
            } else {
                const QString href = attribute(m.capturedView(0), u"href");
                const bool keep = isExternalLink(href);
                open.append(keep);
                out += keep ? u"<a href=\""_s + esc(href) + u"\">"_s : u"<span class=\"link\">"_s;
            }
            pos = m.capturedEnd();
        }
        if (pos > 0)
            body = out + QStringView(body).sliced(pos);
    }
    // Tables: borders; a huge one keeps its first rows.
    {
        QString out;
        qsizetype pos = 0;
        for (qsizetype at = body.indexOf(u"<table"); at >= 0; at = body.indexOf(u"<table", pos)) {
            const qsizetype end = body.indexOf(u"</table>", at);
            if (end < 0)
                break;
            const qsizetype close = body.indexOf(u'>', at);
            out += QStringView(body).sliced(pos, at - pos);
            QString table = body.sliced(close + 1, end + 8 - close - 1);
            int kept = 0;
            int all = 0;
            table = limitTable(u"<table>"_s + table, MaxTableRows, MaxTableCells, &kept, &all).sliced(7);
            out += u"<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\" "
                   u"style=\"border-collapse:collapse; border-style:solid; border-color:#bdb6a6\""_s
                   + body.sliced(at + 6, close - at - 6) + u'>' + table;
            if (kept < all) {
                out += u"<p class=\"missing\">"_s + esc(IoMessages::tr("(%1 more rows are left out)").arg(all - kept))
                       + u"</p>"_s;
                m_warnings.append(
                        IoMessages::tr("A table is too large for paper: only its first %1 rows are shown.").arg(kept));
            }
            pos = end + 8;
        }
        if (pos > 0)
            body = out + QStringView(body).sliced(pos);
    }
    // Dates, reminders and priorities in words (as the Pandoc export writes them): emoji fonts
    // are colour bitmaps that a PDF does not show.
    {
        static const RE chip(u"<span class=\"chip ([^\"]*)\">([^<]*)</span>"_s);
        QString out;
        qsizetype pos = 0;
        for (auto it = chip.globalMatch(body); it.hasNext();) {
            const auto m = it.next();
            const QString words = chipWords(m.captured(1), unescape(m.captured(2)));
            if (words.isEmpty())
                continue;
            out += QStringView(body).sliced(pos, m.capturedStart() - pos);
            out += u"<span class=\"chip "_s + m.captured(1) + u"\">"_s + esc(words) + u"</span>"_s;
            pos = m.capturedEnd();
        }
        if (pos > 0)
            body = out + QStringView(body).sliced(pos);
    }
    body.replace(u"<input type=\"checkbox\" disabled checked> "_s, kChecked);
    body.replace(u"<input type=\"checkbox\" disabled> "_s, kUnchecked);
    body.replace(u"<mark>"_s, u"<span style=\"background-color:#f9e2a6\">"_s).replace(u"</mark>"_s, u"</span>"_s);
    body.replace(u"<del>"_s, u"<s>"_s).replace(u"</del>"_s, u"</s>"_s);
    return body;
}

void PaperDocument::finish()
{
    QTextDocument *doc = m_document.get();
    const qreal maxMargin = textWidth() * MaxIndentFraction;
    QTextCursor cursor(doc);
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        if (QTextList *list = block.textList()) {
            QTextListFormat lf = list->format();
            if (lf.indent() > MaxListIndent) {
                lf.setIndent(MaxListIndent);
                list->setFormat(lf);
            }
            // A task: the checkbox as the item's marker.
            const QString text = block.text();
            const bool checked = text.startsWith(kChecked);
            if (checked || text.startsWith(kUnchecked)) {
                cursor.setPosition(block.position());
                cursor.setPosition(block.position() + kChecked.size(), QTextCursor::KeepAnchor);
                cursor.removeSelectedText();
                QTextBlockFormat bf = block.blockFormat();
                bf.setMarker(checked ? QTextBlockFormat::MarkerType::Checked : QTextBlockFormat::MarkerType::Unchecked);
                cursor.setBlockFormat(bf);
            }
        }
        QTextBlockFormat bf = block.blockFormat();
        bool changed = false;
        if (bf.leftMargin() > maxMargin) {
            bf.setLeftMargin(maxMargin);
            changed = true;
        }
        if (bf.indent() > MaxListIndent) {
            bf.setIndent(MaxListIndent);
            changed = true;
        }
        if (changed) {
            cursor.setPosition(block.position());
            cursor.setBlockFormat(bf);
        }
    }
}

int PaperDocument::paint(QPagedPaintDevice *device)
{
    QTextDocument *doc = m_document.get();
    if (!device)
        return 0;
    QPainter painter;
    if (!painter.begin(device))
        return 0;
    const qreal width = device->width();
    const qreal height = device->height();
    QFont footerFont = doc->defaultFont();
    footerFont.setPointSizeF(8.5);
    const QFontMetricsF footerMetrics(footerFont, device);
    const qreal footer = footerMetrics.height() * 2.2;
    const qreal bodyHeight = std::max<qreal>(1, height - footer);
    doc->documentLayout()->setPaintDevice(device);
    doc->setPageSize(QSizeF(width, bodyHeight));
    const int pages = std::max(1, doc->pageCount());
    for (int page = 0; page < pages; ++page) {
        if (page > 0 && !device->newPage())
            return page;
        painter.save();
        const QRectF view(0, page * bodyHeight, width, bodyHeight);
        painter.translate(0, -view.top());
        painter.setClipRect(view);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = view;
        context.palette.setColor(QPalette::Text, QColor(0x1f, 0x24, 0x30));
        context.palette.setColor(QPalette::Link, QColor(0x2c, 0x6a, 0xa3));
        doc->documentLayout()->draw(&painter, context);
        painter.restore();
        if (pages > 1) {
            painter.setFont(footerFont);
            painter.setPen(QColor(0x53, 0x5a, 0x6a));
            painter.drawText(QRectF(0, height - footerMetrics.height(), width, footerMetrics.height()),
                             Qt::AlignHCenter | Qt::AlignBottom, u"%1 / %2"_s.arg(page + 1).arg(pages));
        }
    }
    painter.end();
    return pages;
}

} // namespace brulion::io::detail
