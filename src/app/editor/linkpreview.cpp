#include "linkpreview.h"

#include "blocklistmodel.h"
#include "blockrender_p.h"
#include "editorhost.h"

#include "dates/local.h"

#include <QColor>
#include <QDateTime>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace detail {

namespace {

QString colorString(const QVariant &v, const QString &fallback)
{
    if (!v.isValid())
        return fallback;
    QColor c;
    if (v.metaType() == QMetaType::fromType<QColor>())
        c = v.value<QColor>();
    else
        c = QColor::fromString(v.toString());
    if (!c.isValid())
        return fallback;
    return c.alpha() == 255 ? c.name(QColor::HexRgb) : c.name(QColor::HexArgb);
}

} // namespace

markdown::Palette rendererPalette(const QVariantMap &map)
{
    markdown::Palette p;
    const auto get = [&map](const char *key, const QString &fallback) {
        return colorString(map.value(QString::fromLatin1(key)), fallback);
    };
    p.link = get("link", p.link);
    p.unresolved = get("unresolved", p.unresolved);
    p.muted = get("muted", p.muted);
    p.codeBackground = get("codeBackground", p.codeBackground);
    p.highlightBackground = get("highlightBackground", p.highlightBackground);
    const auto chip = [&](const char *name, markdown::ChipColors &c) {
        c.foreground = get(QByteArray(QByteArray(name) + "Fg").constData(), c.foreground);
        c.background = get(QByteArray(QByteArray(name) + "Bg").constData(), c.background);
    };
    chip("tag", p.tag);
    chip("due", p.due);
    chip("overdue", p.overdue);
    chip("scheduled", p.scheduled);
    chip("reminder", p.reminder);
    chip("alarm", p.alarm);
    chip("recurrence", p.recurrence);
    chip("done", p.done);
    chip("neutral", p.neutral);
    return p;
}

} // namespace detail

LinkPreview::LinkPreview(QObject *parent) : QObject(parent), m_model(std::make_unique<BlockListModel>()) { }

LinkPreview::~LinkPreview() = default;

void LinkPreview::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    m_host = host;
    m_model->setHost(host);
    update();
    Q_EMIT hostChanged();
}

void LinkPreview::setHref(const QString &href)
{
    if (m_href == href)
        return;
    m_href = href;
    update();
    Q_EMIT hrefChanged();
}

void LinkPreview::setSourcePath(const QString &path)
{
    if (m_sourcePath == path)
        return;
    m_sourcePath = path;
    update();
    Q_EMIT sourcePathChanged();
}

void LinkPreview::setPalette(const QVariantMap &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    m_model->setPalette(detail::rendererPalette(palette));
    update();
    Q_EMIT paletteChanged();
}

void LinkPreview::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    m_model->setEnglish(locale == u"en");
    update();
    Q_EMIT localeChanged();
}

void LinkPreview::setNow(const QString &now)
{
    if (m_now == now)
        return;
    m_now = now;
    update();
    Q_EMIT nowChanged();
}

void LinkPreview::classBegin()
{
    m_deferred = true;
}

void LinkPreview::componentComplete()
{
    m_deferred = false;
    update();
}

bool LinkPreview::isPreviewable(const QString &url)
{
    return (url.startsWith(u"page:") && url.size() > 5) || (url.startsWith(u"block:") && url.size() > 6);
}

void LinkPreview::refresh()
{
    update();
}

void LinkPreview::update()
{
    if (m_deferred)
        return;
    QVariantMap out;
    if (m_host && isPreviewable(m_href)) {
        QDate today = QDate::currentDate();
        if (!m_now.isEmpty()) {
            if (const auto spec = dates::parseDateSpec(m_now))
                today = spec->toLocalDateTime().date();
        }
        m_model->setToday(today);
        // Read anew each time: the notes may have changed since the last preview.
        m_model->invalidateRendering();
        out = m_model->linkPreview(m_href, m_sourcePath);
    }
    if (out != m_preview) {
        m_preview = out;
        Q_EMIT previewChanged();
    }
}

bool LinkPreview::openLink(const QString &url, const QString &fromPath, bool newPane)
{
    const qsizetype colon = url.indexOf(u':');
    if (colon <= 0 || !m_host)
        return false;
    const QString scheme = url.left(colon);
    const QString rest = url.mid(colon + 1);
    if (scheme == u"ext") {
        m_host->openUrl(QUrl(rest));
        return true;
    }
    if (scheme == u"tag") {
        m_host->navigate({{u"view"_s, u"tag"_s}, {u"tag"_s, QUrl::fromPercentEncoding(rest.toUtf8())}}, newPane);
        return true;
    }
    if (scheme == u"block") {
        m_host->navigate({{u"view"_s, u"page"_s}, {u"blockId"_s, rest}}, newPane);
        return true;
    }
    if (scheme == u"part") {
        const QVariantMap route = linkPartRoute(url, fromPath);
        if (route.isEmpty())
            return false;
        m_host->navigate(route, newPane);
        return true;
    }
    if (scheme != u"page")
        return false;
    QString target = rest;
    QString fragment;
    if (const qsizetype hash = rest.indexOf(u'#'); hash >= 0) {
        target = rest.left(hash);
        fragment = QUrl::fromPercentEncoding(rest.mid(hash + 1).toUtf8());
    }
    target = QUrl::fromPercentEncoding(target.toUtf8());
    QVariantMap route{{u"view"_s, u"page"_s}};
    if (target.isEmpty()) {
        if (fromPath.isEmpty())
            return false;
        route.insert(u"path"_s, fromPath);
    } else {
        // A page that does not exist yet opens ready to be written (the shell's resolveRoute).
        const QString resolved = m_host->resolveLink(target, fromPath);
        if (!resolved.isEmpty())
            route.insert(u"path"_s, resolved);
        route.insert(u"target"_s, target);
    }
    if (fragment.startsWith(u'^'))
        route.insert(u"blockId"_s, fragment.mid(1));
    else if (!fragment.isEmpty())
        route.insert(u"heading"_s, fragment);
    m_host->navigate(route, newPane);
    return true;
}

} // namespace brulion::editor
