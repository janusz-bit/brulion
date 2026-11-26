#include "atramentview.h"

#include "gallery.h"
#include "ui/app.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#ifdef BRULION_HAS_ATRAMENT_EDITOR
#include "blocklistmodel.h"
#include "editorhost.h"
#include "pageview.h"
#endif

using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

struct Runtime {
    std::shared_ptr<at::QtPlatform> platform = std::make_shared<at::QtPlatform>();
    at::App app { platform, at::gallery::loadFonts({ u":/fonts/ui"_s, u":/fonts"_s }) };
    at::Entity<at::gallery::Gallery> gallery;
};

Runtime &runtime()
{
    // Made on first use and kept for the process: items detach their windows before it goes.
    static Runtime *r = new Runtime;
    return *r;
}

} // namespace

struct AtramentView::Page {
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    at::Entity<brulion::editor::atr::PageView> entity;
#endif
};

AtramentView::AtramentView(QQuickItem *parent)
    : AtramentItem(parent)
{
    connect(this, &AtramentView::styleChanged, this, &AtramentView::applyStyle);
}

AtramentView::~AtramentView()
{
    // The window (closed by ~AtramentItem, after this) still names the page's entity: it goes
    // once the window has.
    if (m_page)
        QTimer::singleShot(0, QCoreApplication::instance(), [page = std::shared_ptr<Page>(std::move(m_page))] { });
}

void AtramentView::setPageModel(QObject *model)
{
    if (model == m_pageModel)
        return;
    if (m_attached) {
        qWarning() << "AtramentView: the page model is set before the view; ignoring";
        return;
    }
    m_pageModel = model;
    Q_EMIT pageModelChanged();
    if (isComponentComplete())
        attachView();
}

#ifdef BRULION_HAS_ATRAMENT_EDITOR
namespace {
at::Color atColor(const QColor &c)
{
    return {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())};
}
} // namespace
#endif

void AtramentView::applyStyle()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_page)
        return;
    using namespace brulion::editor::atr;
    m_page->entity.update(runtime().app, [this](PageView &page, at::Context<PageView> &cx) {
        PageMetrics m = page.metrics();
        m.fontSize = float(m_fontSize);
        m.lineHeight = float(m_lineHeight);
        m.indentStep = float(m_indentStep);
        m.gutter = float(m_gutter);
        m.bulletColumn = float(m_bulletColumn);
        m.documentMode = m_documentMode;
        m.listIndent = float(m_listIndent);
        m.contentX = float(m_contentX);
        m.contentWidth = float(m_contentWidth);
        m.headingSizes[0] = m.fontSize;
        PagePalette p = page.palette();
        p.text = atColor(m_textColor);
        p.code = p.text;
        p.muted = atColor(m_mutedColor);
        p.link = atColor(m_linkColor);
        p.guide = atColor(m_guideColor);
        p.background = atColor(m_backgroundColor);
        page.setStyle(m, p);
        cx.notify();
    });
#endif
}

at::App &AtramentView::atramentApp()
{
    return runtime().app;
}

void AtramentView::setView(const QString &view)
{
    if (view == m_view)
        return;
    if (m_attached) {
        qWarning() << "AtramentView: the view is set once; ignoring" << view;
        return;
    }
    m_view = view;
    Q_EMIT viewChanged();
    if (isComponentComplete())
        attachView();
}

void AtramentView::componentComplete()
{
    AtramentItem::componentComplete();
    attachView();
}

void AtramentView::attachView()
{
    if (m_attached || m_view.isEmpty())
        return;
    Runtime &r = runtime();
    if (m_view == u"gallery") {
        if (!r.gallery)
            r.gallery = r.app.newEntity<at::gallery::Gallery>();
        attach(r.app, at::AnyView { r.gallery.id(), &at::renderView<at::gallery::Gallery> });
        m_attached = true;
        return;
    }
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_view == u"page") {
        auto *model = qobject_cast<brulion::editor::BlockListModel *>(m_pageModel.data());
        if (!model) {
            if (m_pageModel)
                qWarning() << "AtramentView: the page view needs a BlockListModel as pageModel";
            return; // (attached once the model is set)
        }
        m_page = std::make_unique<Page>();
        m_page->entity = r.app.newEntity<brulion::editor::atr::PageView>(model);
        applyStyle();
        attach(r.app, at::AnyView{m_page->entity.id(), &at::renderView<brulion::editor::atr::PageView>});
        m_attached = true;
        return;
    }
#endif
    qWarning() << "AtramentView: no view named" << m_view;
}
