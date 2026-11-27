#include "atramentview.h"

#include "gallery.h"
#include "ui/app.h"

#include <QCoreApplication>
#include <QDebug>
#include <QTimer>

#ifdef BRULION_HAS_ATRAMENT_EDITOR
#include "blocklistmodel.h"
#include "editorhost.h"
#include "foreignhost.h"
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
    // The QML items over the foreign rows, the header and the footer.
    std::unique_ptr<brulion::editor::atr::ForeignRowHost> host;
#endif
};

AtramentView::AtramentView(QQuickItem *parent)
    : AtramentItem(parent)
{
    connect(this, &AtramentView::styleChanged, this, &AtramentView::applyStyle);
}

AtramentView::~AtramentView()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    // The delegates go with the item (they name the editor around it).
    if (m_page)
        m_page->host.reset();
#endif
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
        QPointer<AtramentView> guard = this;
        m_page->entity.update(
                r.app, [guard](brulion::editor::atr::PageView &page, at::Context<brulion::editor::atr::PageView> &) {
                    page.onLink = [guard](int row, const QString &href) {
                        if (guard)
                            Q_EMIT guard->linkActivated(row, href);
                    };
                    page.onToggleTask = [guard](int row) {
                        if (guard)
                            Q_EMIT guard->taskToggled(row);
                    };
                });
        applyStyle();
        m_page->host = std::make_unique<brulion::editor::atr::ForeignRowHost>(this, r.app, m_page->entity, model);
        m_page->host->setDelegate(m_delegate);
        m_page->host->setHeader(m_header);
        m_page->host->setFooter(m_footer);
        attach(r.app, at::AnyView{m_page->entity.id(), &at::renderView<brulion::editor::atr::PageView>});
        m_attached = true;
        return;
    }
#endif
    qWarning() << "AtramentView: no view named" << m_view;
}

// --- "page": the QML items over the rows Atrament does not draw (editor/atrament/foreignhost.h)

void AtramentView::setDelegate(QQmlComponent *delegate)
{
    if (delegate == m_delegate)
        return;
    m_delegate = delegate;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page && m_page->host)
        m_page->host->setDelegate(delegate);
#endif
    Q_EMIT delegateChanged();
}

void AtramentView::setHeader(QQuickItem *item)
{
    if (item == m_header)
        return;
    m_header = item;
    if (item)
        item->setParentItem(this);
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page && m_page->host)
        m_page->host->setHeader(item);
#endif
    Q_EMIT headerChanged();
}

void AtramentView::setFooter(QQuickItem *item)
{
    if (item == m_footer)
        return;
    m_footer = item;
    if (item)
        item->setParentItem(this);
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page && m_page->host)
        m_page->host->setFooter(item);
#endif
    Q_EMIT footerChanged();
}

QQuickItem *AtramentView::rowItem(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page && m_page->host)
        return m_page->host->rowItem(row);
#endif
    Q_UNUSED(row);
    return nullptr;
}

void AtramentView::placeHostedChildren()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page && m_page->host)
        m_page->host->place();
#endif
}
