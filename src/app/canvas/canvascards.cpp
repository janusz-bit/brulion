#include "canvascards.h"

#include "blocklistmodel.h"
#include "blockrender_p.h"
#include "canvasimageprovider.h"
#include "editorhost.h"
#include "linkpreview.h"

#include "canvas/geometry.h"
#include "dates/local.h"
#include "util/paths.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::canvasview {

namespace {

QString encoded(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

} // namespace

CanvasCards::CanvasCards(QObject *parent) : QObject(parent), m_model(std::make_unique<editor::BlockListModel>()) { }

CanvasCards::~CanvasCards() = default;

void CanvasCards::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    if (m_host)
        disconnect(m_host, nullptr, this, nullptr);
    m_host = host;
    m_model->setHost(host);
    if (m_host) {
        connect(m_host, &EditorHost::indexChanged, this, &CanvasCards::invalidate);
        connect(m_host, &EditorHost::fileChanged, this, [this](const QString &changed) {
            // A note a card shows (or one its text embeds) changed.
            Q_UNUSED(changed);
            invalidate();
        });
        connect(m_host, &EditorHost::pdfThumbnailChanged, this, [this] {
            ++m_revision;
            Q_EMIT revisionChanged();
        });
    }
    invalidate();
    Q_EMIT hostChanged();
}

void CanvasCards::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    m_model->setPath(path);
    invalidate();
    Q_EMIT pathChanged();
}

void CanvasCards::setPalette(const QVariantMap &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    m_model->setPalette(editor::detail::rendererPalette(palette));
    invalidate();
    Q_EMIT paletteChanged();
}

void CanvasCards::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    m_model->setEnglish(locale == u"en");
    invalidate();
    Q_EMIT localeChanged();
}

void CanvasCards::setNow(const QString &now)
{
    if (m_now == now)
        return;
    m_now = now;
    invalidate();
    Q_EMIT nowChanged();
}

void CanvasCards::invalidate()
{
    m_texts.clear();
    m_notes.clear();
    m_model->invalidateRendering();
    ++m_revision;
    Q_EMIT revisionChanged();
}

void CanvasCards::refresh()
{
    invalidate();
}

void CanvasCards::prepareModel()
{
    const auto fixed = dates::parseDateSpec(m_now);
    m_model->setToday(fixed ? fixed->toLocalDateTime().date() : QDate::currentDate());
}

QVariantMap CanvasCards::text(const QString &markdown)
{
    if (const auto it = m_texts.constFind(markdown); it != m_texts.cend())
        return *it;
    if (m_texts.size() >= MaxCached)
        m_texts.clear();
    prepareModel();
    QVariantMap out = m_model->textPreview(markdown, m_path);
    m_texts.insert(markdown, out);
    return out;
}

QVariantMap CanvasCards::note(const QString &path, const QString &subpath)
{
    const QString key = path + u'\n' + subpath;
    if (const auto it = m_notes.constFind(key); it != m_notes.cend())
        return *it;
    if (m_notes.size() >= MaxCached)
        m_notes.clear();
    prepareModel();
    QString fragment = subpath.trimmed();
    if (fragment.startsWith(u'#'))
        fragment.remove(0, 1);
    const QString href = u"page:"_s + encoded(path) + (fragment.isEmpty() ? QString() : u'#' + encoded(fragment));
    QVariantMap out = m_model->linkPreview(href, m_path);
    if (out.isEmpty())
        out = {{u"found"_s, false}, {u"message"_s, u"missingPage"_s}, {u"blocks"_s, QVariantList()}};
    out.insert(u"href"_s, href);
    m_notes.insert(key, out);
    return out;
}

QVariantMap CanvasCards::pdf(const QString &path) const
{
    return m_host ? m_host->pdfThumbnail(path, 1) : QVariantMap{{u"state"_s, u"unsupported"_s}};
}

QUrl CanvasCards::pictureUrl(const QString &path) const
{
    return m_host ? m_host->assetUrl(path) : QUrl();
}

QUrl CanvasCards::drawingUrl(const QString &path, bool dark) const
{
    return m_host ? m_host->drawingUrl(path, dark) : QUrl();
}

QUrl CanvasCards::canvasUrl(const QString &path, bool dark) const
{
    const QUrl file = m_host ? m_host->assetUrl(path) : QUrl();
    return file.isLocalFile() ? CanvasImageProvider::url(file.toLocalFile(), dark) : QUrl();
}

QString CanvasCards::fileTitle(const QString &path) const
{
    return paths::isMarkdown(path) ? paths::pageTitle(path) : paths::basename(path);
}

bool CanvasCards::openFile(const QString &file, const QString &subpath)
{
    if (!m_host)
        return false;
    const QString path = m_host->resolveLink(paths::normalize(file), m_path);
    if (path.isEmpty())
        return false;
    QString fragment = subpath.trimmed();
    if (fragment.startsWith(u'#'))
        fragment.remove(0, 1);
    if (!fragment.isEmpty() && paths::isMarkdown(path))
        return openLink(u"page:"_s + encoded(path) + u'#' + encoded(fragment));
    m_host->navigate({{u"view"_s, u"page"_s}, {u"path"_s, path}});
    return true;
}

bool CanvasCards::openNode(const QVariantMap &node)
{
    const QString type = node.value(u"type"_s).toString();
    if (type == u"link") {
        const QString url = node.value(u"url"_s).toString().trimmed();
        if (m_host && !url.isEmpty())
            m_host->openUrl(QUrl(url));
        return true;
    }
    if (type == u"file")
        return openFile(node.value(u"file"_s).toString(), node.value(u"subpath"_s).toString());
    return true;
}

bool CanvasCards::openLink(const QString &url, bool newPane)
{
    if (!m_host)
        return false;
    editor::LinkPreview links;
    links.setHost(m_host);
    return links.openLink(url, m_path, newPane);
}

// ---- CanvasColors ----------------------------------------------------------------------------------

QString CanvasColors::hex(const QString &canvasColor, bool dark) const
{
    return canvas::colorHex(canvasColor, dark);
}

QString CanvasColors::normalize(const QString &color) const
{
    return canvas::normalizeColor(color);
}

// ---- CanvasCardContent ----------------------------------------------------------------------------

CanvasCardContent::CanvasCardContent(QObject *parent) : QObject(parent)
{
    connect(this, &CanvasCardContent::inputChanged, this, &CanvasCardContent::update);
}

void CanvasCardContent::setCards(CanvasCards *cards)
{
    if (m_cards == cards)
        return;
    if (m_cards)
        disconnect(m_cards, nullptr, this, nullptr);
    m_cards = cards;
    if (m_cards)
        connect(m_cards, &CanvasCards::revisionChanged, this, &CanvasCardContent::update);
    update();
    Q_EMIT cardsChanged();
}

void CanvasCardContent::setKind(const QString &kind)
{
    if (m_kind == kind)
        return;
    m_kind = kind;
    Q_EMIT inputChanged();
}

void CanvasCardContent::setText(const QString &text)
{
    if (m_text == text)
        return;
    m_text = text;
    Q_EMIT inputChanged();
}

void CanvasCardContent::setFile(const QString &file)
{
    if (m_file == file)
        return;
    m_file = file;
    Q_EMIT inputChanged();
}

void CanvasCardContent::setSubpath(const QString &subpath)
{
    if (m_subpath == subpath)
        return;
    m_subpath = subpath;
    Q_EMIT inputChanged();
}

void CanvasCardContent::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    Q_EMIT inputChanged();
}

void CanvasCardContent::setDark(bool dark)
{
    if (m_dark == dark)
        return;
    m_dark = dark;
    Q_EMIT inputChanged();
}

void CanvasCardContent::update()
{
    QVariantMap out;
    QUrl picture;
    QString pdfState;
    int pdfPages = 0;
    if (m_cards && m_active) {
        if (m_kind == u"text")
            out = m_cards->text(m_text);
        else if (m_kind == u"note" && !m_file.isEmpty())
            out = m_cards->note(m_file, m_subpath);
        else if (m_kind == u"picture")
            picture = m_cards->pictureUrl(m_file);
        else if (m_kind == u"drawing")
            picture = m_cards->drawingUrl(m_file, m_dark);
        else if (m_kind == u"canvas")
            picture = m_cards->canvasUrl(m_file, m_dark);
        else if (m_kind == u"pdf") {
            const QVariantMap thumb = m_cards->pdf(m_file);
            pdfState = thumb.value(u"state"_s, u"unsupported"_s).toString();
            pdfPages = thumb.value(u"pages"_s).toInt();
            if (pdfState == u"ready")
                picture = thumb.value(u"url"_s).toUrl();
        }
    }
    const bool pictureChanged = picture != m_pictureUrl || pdfState != m_pdfState || pdfPages != m_pdfPages;
    m_pictureUrl = picture;
    m_pdfState = pdfState;
    m_pdfPages = pdfPages;
    // Every key the delegate reads, in every row (a missing one would not convert).
    QVariantList rows;
    const QVariantList blocks = out.value(u"blocks"_s).toList();
    rows.reserve(blocks.size());
    for (const QVariant &v : blocks) {
        QVariantMap m = v.toMap();
        static const QStringList strings{u"kind"_s, u"view"_s, u"task"_s, u"title"_s, u"html"_s, u"raw"_s, u"path"_s};
        for (const QString &k : strings) {
            if (!m.contains(k))
                m.insert(k, QString());
        }
        m.insert(u"headingLevel"_s, m.value(u"headingLevel"_s).toInt());
        m.insert(u"depth"_s, m.value(u"depth"_s).toInt());
        m.insert(u"line"_s, m.value(u"line"_s, -1).toInt());
        // A list item's marker as the card shows it ("•", "3."; "" for others and for tasks: their
        // box shows instead).
        static const QRegularExpression item(u"^\\s*([-*+]|(\\d{1,9})[.)])\\s"_s);
        const QRegularExpressionMatch match = item.match(m.value(u"raw"_s).toString());
        QString bullet;
        if (match.hasMatch() && m.value(u"task"_s).toString().isEmpty() && m.value(u"kind"_s).toString() != u"embed")
            bullet = match.captured(2).isEmpty() ? u"•"_s : match.captured(2) + u'.';
        m.insert(u"bullet"_s, bullet);
        rows.append(m);
    }
    const QString title = out.value(u"title"_s).toString();
    const bool found = out.value(u"found"_s).toBool();
    const QString message = out.value(u"message"_s).toString();
    const int more = out.value(u"more"_s).toInt();
    if (!pictureChanged && rows == m_rows && title == m_title && found == m_found && message == m_message
        && more == m_more)
        return;
    m_rows = std::move(rows);
    m_title = title;
    m_found = found;
    m_message = message;
    m_more = more;
    Q_EMIT contentChanged();
}

} // namespace brulion::canvasview
