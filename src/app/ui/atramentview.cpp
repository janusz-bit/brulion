#include "atramentview.h"

#include "gallery.h"
#include "ui/app.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QDebug>
#include <QTimer>

#include <cmath>

#ifdef BRULION_HAS_ATRAMENT_EDITOR
#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "pagecontroller.h"
#include "foreignhost.h"
#include "pageview.h"
#include "tablelayout.h"
#include "queryresultmodel.h"
#include "spellcheck.h"
#include "vimsession.h"
#include "picturecache.h"
#include "drawingimageprovider.h"
#endif

#include <QKeyEvent>

#include <algorithm>

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
    connect(this, &AtramentView::findChanged, this, &AtramentView::applyFind);
    connect(this, &AtramentView::spellCheckChanged, this, &AtramentView::applyExtras);
    connect(this, &AtramentView::vimSessionChanged, this, &AtramentView::applyExtras);
    connect(this, &AtramentView::touchSelectionChanged, this, &AtramentView::applyTouchSelection);
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

int qtModifiers(const at::Modifiers &m)
{
    Qt::KeyboardModifiers q;
    if (m.control)
        q |= Qt::ControlModifier;
    if (m.alt)
        q |= Qt::AltModifier;
    if (m.shift)
        q |= Qt::ShiftModifier;
    if (m.platform)
        q |= Qt::MetaModifier;
    return int(q.toInt());
}
} // namespace
#endif

void AtramentView::setPageController(QObject *controller)
{
    if (controller == m_pageController)
        return;
    m_pageController = controller;
    Q_EMIT pageControllerChanged();
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_pageModel) {
        if (auto *c = qobject_cast<brulion::editor::PageController *>(controller)) {
            if (!m_attached)
                setPageModel(c->blocks());
        }
    }
    connectController();
#endif
}

int AtramentView::editRow() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        return m_page->entity.read(runtime().app).editState().row;
#endif
    return -1;
}

QString AtramentView::editText() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page) {
        const std::u16string text = m_page->entity.read(runtime().app).editState().text;
        return QString(reinterpret_cast<const QChar *>(text.data()), qsizetype(text.size()));
    }
#endif
    return {};
}

int AtramentView::editCursor() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        return m_page->entity.read(runtime().app).editState().cursor;
#endif
    return 0;
}

bool AtramentView::editProse() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        return m_page->entity.read(runtime().app).editState().prose;
#endif
    return true;
}

QRectF AtramentView::editCaretRect() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page) {
        const at::Rect r = m_page->entity.read(runtime().app).editState().caret;
        return {r.left(), r.top(), r.width(), r.height()};
    }
#endif
    return {};
}

bool AtramentView::revealRow(int row)
{
    bool moved = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [&](brulion::editor::atr::PageView &page,
                                                 at::Context<brulion::editor::atr::PageView> &cx) {
            moved = page.revealRow(row, cx);
        });
#else
    Q_UNUSED(row)
#endif
    return moved;
}

qreal AtramentView::rowTop(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        if (const auto r = m_page->entity.read(runtime().app).rowBounds(row))
            return r->top();
#endif
    Q_UNUSED(row);
    return -1;
}

qreal AtramentView::rowHeight(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        if (const auto r = m_page->entity.read(runtime().app).rowBounds(row))
            return r->height();
#endif
    Q_UNUSED(row);
    return -1;
}

QPointF AtramentView::linkPoint(int row, const QString &hrefPart) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        if (const auto p = m_page->entity.read(runtime().app).linkPoint(row, hrefPart))
            return {p->x, p->y};
#endif
    Q_UNUSED(row);
    Q_UNUSED(hrefPart);
    return {-1, -1};
}

QPointF AtramentView::editPoint(int offset) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        if (const auto p = m_page->entity.read(runtime().app).editPoint(offset))
            return {p->x, p->y};
#endif
    Q_UNUSED(offset);
    return {-1, -1};
}

QString AtramentView::misspelledWordAt(qreal x, qreal y) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page) {
        const auto &page = m_page->entity.read(runtime().app);
        const at::Point p{float(x), float(y)};
        const int row = page.editedRow() >= 0 ? page.editedRow() : page.rowAtY(float(y));
        if (const auto m = page.misspelledAt(row, p))
            return m->word;
    }
#endif
    Q_UNUSED(x);
    Q_UNUSED(y);
    return {};
}

QPointF AtramentView::taskBoxPoint(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        if (const auto p = m_page->entity.read(runtime().app).taskBoxPoint(row))
            return {p->x, p->y};
#endif
    Q_UNUSED(row);
    return {-1, -1};
}

void AtramentView::commitComposition()
{
    // The input method's own word first (it keeps it outside the text), then the field's.
    QGuiApplication::inputMethod()->commit();
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [](brulion::editor::atr::PageView &page,
                                                at::Context<brulion::editor::atr::PageView> &cx) {
            page.commitComposition(cx);
        });
#endif
}

bool AtramentView::insertAtCaret(const QString &text, bool separate)
{
    bool done = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [&](brulion::editor::atr::PageView &page,
                                                 at::Context<brulion::editor::atr::PageView> &cx) {
            done = page.insertAtCaret(std::u16string(brulion::editor::atr::u16View(text)), separate, cx);
        });
#else
    Q_UNUSED(text)
    Q_UNUSED(separate)
#endif
    return done;
}

bool AtramentView::revealArea(qreal x, qreal y, qreal w, qreal h, qreal margin)
{
    bool moved = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [&](brulion::editor::atr::PageView &page,
                                                 at::Context<brulion::editor::atr::PageView> &cx) {
            moved = page.reveal(at::Rect::fromXYWH(float(x), float(y), float(w), float(h)), float(margin),
                                float(height()), cx);
        });
#else
    Q_UNUSED(x)
    Q_UNUSED(y)
    Q_UNUSED(w)
    Q_UNUSED(h)
    Q_UNUSED(margin)
#endif
    return moved;
}

bool AtramentView::selectInEdited(int row, int from, int to)
{
    bool done = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [&](brulion::editor::atr::PageView &page,
                                                 at::Context<brulion::editor::atr::PageView> &cx) {
            done = page.selectInEdited(row, from, to, cx);
        });
#else
    Q_UNUSED(row)
    Q_UNUSED(from)
    Q_UNUSED(to)
#endif
    return done;
}

void AtramentView::applyFind()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_page)
        return;
    m_page->entity.update(runtime().app, [this](brulion::editor::atr::PageView &page,
                                                at::Context<brulion::editor::atr::PageView> &cx) {
        page.setFind(m_findQuery, m_findMatchCase, m_findMatchDiacritics, m_findWholeWord, atColor(m_findColor), cx);
    });
#endif
}

void AtramentView::connectController()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_page)
        return;
    using namespace brulion::editor::atr;
    auto *controller = qobject_cast<brulion::editor::PageController *>(m_pageController.data());
    // (A controller of another model than the page's is none.)
    if (controller && controller->blocks() != m_pageModel.data())
        controller = nullptr;
    QPointer<AtramentView> guard = this;
    m_page->entity.update(runtime().app, [guard, controller](PageView &page, at::Context<PageView> &cx) {
        page.onEditRequest = [guard](int row, int cursor, const at::Modifiers &modifiers) {
            if (!guard)
                return false;
            Q_EMIT guard->editRequested(row, cursor, qtModifiers(modifiers));
            return true;
        };
        page.onEditingStarted = [guard] {
            // The keyboard comes here when a block Atrament edits is edited (a new block, Enter,
            // ↑/↓ from the QML around) — the QML around decides (the find bar keeps it).
            if (guard)
                Q_EMIT guard->editingStarted();
        };
        page.onEscape = [guard] {
            if (guard)
                Q_EMIT guard->escapePressed();
        };
        page.onUndo = [guard](bool redo) {
            if (guard)
                Q_EMIT guard->undoRequested(redo);
        };
        page.onFollowLink = [guard](int row, int cursor, bool newPane) {
            if (guard)
                Q_EMIT guard->followLinkRequested(row, cursor, newPane);
        };
        page.onExtendSelection = [guard](int row, int step) {
            if (guard)
                Q_EMIT guard->selectionExtendRequested(row, step);
        };
        page.onSelectAllBlocks = [guard] {
            if (guard)
                Q_EMIT guard->selectAllBlocksRequested();
        };
        page.onEditStateChanged = [guard] {
            if (guard)
                Q_EMIT guard->editStateChanged();
        };
        page.onSelectionCommand = [guard](PageView::SelectionCommand command, int row) {
            if (!guard)
                return false;
            const std::u16string_view name = PageView::commandName(command);
            guard->m_commandTaken = false;
            Q_EMIT guard->selectionCommand(QString(reinterpret_cast<const QChar *>(name.data()), qsizetype(name.size())),
                                           row);
            return guard && guard->m_commandTaken;
        };
        page.onSuggestionKey = [guard](std::u16string_view key) {
            if (!guard || !guard->m_suggestionsOpen)
                return false;
            Q_EMIT guard->suggestionKeyPressed(QString(reinterpret_cast<const QChar *>(key.data()), qsizetype(key.size())));
            return true;
        };
        page.setController(controller, cx);
    });
#endif
}

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
        m.arrowColumn = float(m_arrowColumn);
        m.touch = m_touchUi;
        m.headingSizes[0] = m.fontSize;
        PagePalette p = page.palette();
        p.text = atColor(m_textColor);
        p.code = p.text;
        p.muted = atColor(m_mutedColor);
        p.link = atColor(m_linkColor);
        p.guide = atColor(m_guideColor);
        if (m_ruleColor.isValid())
            p.rule = atColor(m_ruleColor);
        p.background = atColor(m_backgroundColor);
        page.setStyle(m, p);
        page.setSelectionLook({atColor(m_bandColor), atColor(m_ringColor), float(m_bandRadius)});
        PageView::CalloutLook callouts;
        callouts.info = atColor(m_calloutInfo);
        callouts.success = atColor(m_calloutSuccess);
        callouts.warning = atColor(m_calloutWarning);
        callouts.danger = atColor(m_calloutDanger);
        callouts.quote = atColor(m_calloutQuote);
        callouts.fill = float(m_calloutFill);
        callouts.radius = float(m_calloutRadius);
        page.setCalloutLook(callouts);
        // The edited text coloured by the highlighter's rules (markdownstyles.h) in the code face.
        page.setEditStyles(std::make_shared<const EditStyler>(m_highlighterPalette, p.codeFamily), cx);
        PageView::CodeLook code;
        const QVariantMap *look = &m_codeLook;
        auto colour = [&look](const char *key, at::Color fallback) {
            const QVariant v = look->value(QString::fromLatin1(key));
            const QColor c = v.metaType() == QMetaType::fromType<QColor>() ? v.value<QColor>() : QColor::fromString(v.toString());
            return c.isValid() ? atColor(c) : fallback;
        };
        auto number = [&look](const char *key, float fallback) {
            bool ok = false;
            const double d = look->value(QString::fromLatin1(key)).toDouble(&ok);
            return ok && d > 0 ? float(d) : fallback;
        };
        code.background = colour("background", code.background);
        code.border = colour("border", code.border);
        code.label = colour("label", code.label);
        code.radius = number("radius", code.radius);
        code.fontSize = number("fontSize", code.fontSize);
        code.lineHeight = number("lineHeight", code.lineHeight);
        code.labelSize = number("labelSize", code.labelSize);
        code.lineNumbers = m_codeLook.value(QStringLiteral("lineNumbers")).toBool();
        page.setDrawsCode(m_drawsCode, code, cx);
        look = &m_calcLook;
        PageView::CalcLook calc;
        calc.background = colour("background", calc.background);
        calc.frame = colour("frame", calc.frame);
        calc.separator = colour("separator", calc.separator);
        calc.rule = colour("rule", calc.rule);
        calc.label = colour("label", calc.label);
        calc.muted = colour("muted", calc.muted);
        calc.error = colour("error", calc.error);
        calc.radius = number("radius", calc.radius);
        calc.labelSize = number("labelSize", calc.labelSize);
        calc.errorSize = number("errorSize", calc.errorSize);
        if (const QString family = m_calcLook.value(u"uiFamily"_s).toString(); !family.isEmpty())
            calc.uiFamily = std::u16string(u16View(family));
        if (const QString title = m_calcLook.value(u"title"_s).toString(); !title.isEmpty())
            calc.title = std::u16string(u16View(title));
        page.setDrawsCalc(m_drawsCalc, calc, cx);
        // Charts, Mermaid diagrams and formulas (diagramrows.cpp, mathrows.cpp).
        PageView::ViewLook views;
        auto viewColour = [this](const char *key, at::Color fallback) {
            const QVariant v = m_diagramLook.value(QString::fromLatin1(key));
            const QColor c = v.metaType() == QMetaType::fromType<QColor>() ? v.value<QColor>() : QColor::fromString(v.toString());
            return c.isValid() ? atColor(c) : fallback;
        };
        auto viewNumber = [this](const char *key, float fallback) {
            bool ok = false;
            const double d = m_diagramLook.value(QString::fromLatin1(key)).toDouble(&ok);
            return ok && d > 0 ? float(d) : fallback;
        };
        views.panel = viewColour("panel", views.panel);
        views.panelBorder = viewColour("panelBorder", views.panelBorder);
        views.editingBorder = viewColour("editingBorder", views.editingBorder);
        views.label = viewColour("label", views.label);
        views.text = viewColour("text", views.text);
        views.muted = viewColour("muted", views.muted);
        views.border = viewColour("border", views.border);
        views.accent = viewColour("accent", views.accent);
        views.danger = viewColour("danger", views.danger);
        views.dark = m_diagramLook.value(QStringLiteral("dark")).toBool();
        views.english = m_diagramLook.value(QStringLiteral("english")).toBool();
        views.radius = viewNumber("radius", views.radius);
        views.framedPad = viewNumber("framedPad", views.framedPad);
        views.codeSize = viewNumber("codeSize", views.codeSize);
        views.codeLineHeight = viewNumber("codeLineHeight", views.codeLineHeight);
        views.labelSize = viewNumber("labelSize", views.labelSize);
        views.chartFontSize = viewNumber("chartFontSize", views.chartFontSize);
        views.mermaidFontSize = viewNumber("mermaidFontSize", views.mermaidFontSize);
        if (const QString family = m_diagramLook.value(QStringLiteral("family")).toString(); !family.isEmpty())
            views.family = std::u16string(u16View(family));
        page.setViewLook(views, cx);
        page.setDrawsDiagrams(m_drawsDiagrams, cx);
        page.setDrawsMath(m_drawsMath, cx);
        // Pictures, files, embedded blocks, queries (mediarows.cpp, queryrows.cpp).
        PageView::MediaLook media;
        auto mediaColour = [this](const char *key, at::Color fallback) {
            const QVariant v = m_mediaLook.value(QString::fromLatin1(key));
            const QColor c = v.metaType() == QMetaType::fromType<QColor>() ? v.value<QColor>() : QColor::fromString(v.toString());
            return c.isValid() ? atColor(c) : fallback;
        };
        auto mediaNumber = [this](const char *key, float fallback) {
            bool ok = false;
            const double d = m_mediaLook.value(QString::fromLatin1(key)).toDouble(&ok);
            return ok && d > 0 ? float(d) : fallback;
        };
        media.frame = mediaColour("frame", media.frame);
        media.border = mediaColour("border", media.border);
        media.borderStrong = mediaColour("borderStrong", media.borderStrong);
        media.faint = mediaColour("faint", media.faint);
        media.linkMissing = mediaColour("linkMissing", media.linkMissing);
        media.danger = mediaColour("danger", media.danger);
        media.backing = mediaColour("backing", media.backing);
        media.chipBackground = mediaColour("chipBackground", media.chipBackground);
        media.chipText = mediaColour("chipText", media.chipText);
        media.embedFill = mediaColour("embedFill", media.embedFill);
        media.queryBackground = mediaColour("queryBackground", media.queryBackground);
        media.hover = mediaColour("hover", media.hover);
        media.focusRing = mediaColour("focusRing", media.focusRing);
        media.radius = mediaNumber("radius", media.radius);
        media.radiusSm = mediaNumber("radiusSm", media.radiusSm);
        media.spacing = mediaNumber("spacing", media.spacing);
        media.smallSize = mediaNumber("smallSize", media.smallSize);
        media.tinySize = mediaNumber("tinySize", media.tinySize);
        media.bodySize = mediaNumber("bodySize", media.bodySize);
        media.h1Size = mediaNumber("h1Size", media.h1Size);
        media.h2Size = mediaNumber("h2Size", media.h2Size);
        media.headerHeight = mediaNumber("headerHeight", media.headerHeight);
        media.indentStep = mediaNumber("indentStep", media.indentStep);
        media.taskBoxWidth = mediaNumber("taskBoxWidth", media.taskBoxWidth);
        media.queryItems = int(mediaNumber("queryItems", float(media.queryItems)));
        media.frames = m_mediaLook.value(u"frames"_s).toBool();
        media.lightBacking = m_mediaLook.value(u"lightBacking"_s).toBool();
        media.dark = m_mediaLook.value(u"dark"_s).toBool();
        media.uiFamily = std::u16string(u16View(m_mediaLook.value(u"uiFamily"_s).toString()));
        media.embeddedName = QCoreApplication::translate("TransclusionView", "Embedded: %1");
        page.setDrawsMedia((m_drawsPictures ? PageView::Pictures : 0u) | (m_drawsFiles ? PageView::Files : 0u)
                                   | (m_drawsTransclusions ? PageView::Transclusions : 0u)
                                   | (m_drawsQueries ? PageView::Queries : 0u),
                           media, cx);
        page.setEditLook(atColor(m_caretColor), atColor(m_selectionColor),
                         std::u16string(u16View(m_placeholderText)), std::u16string(u16View(m_editLabel)), cx);
        // The grips (BlockSelectionGrip: Theme.link on Theme.bg) and the drop line (Theme.focusRing).
        page.setDragLook(atColor(m_linkColor), atColor(m_backgroundColor), atColor(m_ringColor), cx);
        cx.notify();
    });
    applyTableStyle();
#endif
}

void AtramentView::applyTouchSelection()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_page)
        return;
    using namespace brulion::editor::atr;
    m_page->entity.update(runtime().app, [this](PageView &page, at::Context<PageView> &cx) {
        page.setTouchSelection(m_selectionGrips, m_selectionPicking, cx);
        page.setDragBottomInset(float(m_dragBottomInset));
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
        // Drawings' previews (image://drawing/…, EditorHost::drawingUrl) for the pages' pictures: the
        // provider QML's EmbedView reads them from (stateless: called on the cache's threads).
        static const bool drawings = [] {
            auto provider = std::make_shared<brulion::drawingview::DrawingImageProvider>();
            brulion::editor::atr::PictureCache::setImageProvider(
                    u"drawing"_s, [provider](const QString &id, QSize *size, const QSize &requested) {
                        return provider->requestImage(id, size, requested);
                    });
            return true;
        }();
        Q_UNUSED(drawings)
        m_page = std::make_unique<Page>();
        m_page->entity = r.app.newEntity<brulion::editor::atr::PageView>(model);
        QPointer<AtramentView> guard = this;
        m_page->entity.update(
                r.app, [guard](brulion::editor::atr::PageView &page, at::Context<brulion::editor::atr::PageView> &) {
                    page.onLink = [guard](int row, const QString &href) {
                        if (guard)
                            Q_EMIT guard->linkActivated(row, href);
                    };
                    page.onLinkAt = [guard](int row, const QString &href, at::Point p) {
                        if (guard)
                            Q_EMIT guard->linkClicked(row, href, p.x, p.y);
                    };
                    page.onLinkMenu = [guard](int row, const QString &href, at::Point p) {
                        if (guard)
                            Q_EMIT guard->linkMenuRequested(row, href, p.x, p.y);
                    };
                    page.onToggleTask = [guard](int row) {
                        if (guard)
                            Q_EMIT guard->taskToggled(row);
                    };
                    page.onTaskMenu = [guard](int row, at::Point p) {
                        if (guard)
                            Q_EMIT guard->taskMenuRequested(row, p.x, p.y);
                    };
                    page.onTaskMenuAtCaret = [guard](int row, at::Point p) {
                        if (guard)
                            Q_EMIT guard->taskMenuAtCaretRequested(row, p.x, p.y);
                    };
                    page.onContentHeight = [guard](float height) {
                        // (Told in the frame's paint: the item changes size after it.)
                        if (!guard)
                            return;
                        QMetaObject::invokeMethod(
                                guard.data(),
                                [guard, height] {
                                    if (guard && std::abs(guard->m_contentHeight - height) >= 0.5) {
                                        guard->m_contentHeight = height;
                                        Q_EMIT guard->contentHeightChanged();
                                    }
                                },
                                Qt::QueuedConnection);
                    };
                    page.onDragSelect = [guard](int anchor, int focus) {
                        if (guard)
                            Q_EMIT guard->dragSelectRequested(anchor, focus);
                    };
                    page.onLongPressRow = [guard](int row) {
                        if (guard)
                            Q_EMIT guard->rowLongPressed(row);
                    };
                    page.onBulletClick = [guard](int row) {
                        if (guard)
                            Q_EMIT guard->bulletClicked(row);
                    };
                    page.onBlockMenu = [guard](int row, at::Point p) {
                        if (!guard)
                            return;
                        const QPointF scene = guard->mapToScene(QPointF(p.x, p.y));
                        Q_EMIT guard->blockMenuRequested(row, scene.x(), scene.y());
                    };
                    // Pictures, files, embedded blocks, queries (docs/ATRAMENT.md M2 step 5).
                    page.onEmbedClick = [guard](int row, const QVariantMap &embed) {
                        if (guard)
                            Q_EMIT guard->embedClicked(row, embed);
                    };
                    page.onEmbedMenu = [guard](int row, const QVariantMap &embed, at::Point p) {
                        if (guard)
                            Q_EMIT guard->embedMenuRequested(row, embed, p.x, p.y);
                    };
                    page.onWebPicture = [guard](int row, const QVariantMap &embed, bool save) {
                        if (guard)
                            Q_EMIT guard->webPictureRequested(row, embed.value(u"target"_s).toString(), save);
                    };
                    page.onEmbeddedLink = [guard](int row, const QString &link, const QString &path, int line,
                                                  const QString &raw) {
                        if (guard)
                            Q_EMIT guard->embeddedLinkActivated(row, link, path, line, raw);
                    };
                    page.onQueryLink = [guard](int row, int item, const QString &link, const QString &path, int line,
                                               const QString &raw) {
                        if (guard)
                            Q_EMIT guard->queryLinkActivated(row, item, link, path, line, raw);
                    };
                    page.onQueryAnswer = [guard](int row, const QVariantMap &answer) {
                        if (guard)
                            Q_EMIT guard->queryAnswered(row, answer);
                    };
                    page.onSpellingMenu = [guard](const brulion::editor::atr::PageView::Misspelling &m, at::Point p) {
                        if (!guard)
                            return;
                        const QVariantMap spelling{{u"from"_s, m.from}, {u"to"_s, m.to}, {u"word"_s, m.word},
                                                   {u"text"_s, m.text}};
                        Q_EMIT guard->spellingMenuRequested(m.row, m.edited, spelling, p.x, p.y);
                    };
                    page.onTextMenu = [guard](int row, const std::optional<brulion::editor::atr::PageView::Misspelling> &m,
                                              at::Point p) {
                        if (!guard)
                            return;
                        QVariantMap spelling;
                        if (m)
                            spelling = QVariantMap{{u"from"_s, m->from}, {u"to"_s, m->to}, {u"word"_s, m->word},
                                                   {u"text"_s, m->text}};
                        Q_EMIT guard->textMenuRequested(row, spelling, p.x, p.y);
                    };
                    page.onEmbeddedLinkMenu = [guard](int row, const QString &link, const QString &path, int line,
                                                      const QString &raw, at::Point p) {
                        if (guard)
                            Q_EMIT guard->embeddedLinkMenuRequested(row, link, path, line, raw, p.x, p.y);
                    };
                    page.onQueryLinkMenu = [guard](int row, int item, const QString &link, const QString &path, int line,
                                                   const QString &raw, at::Point p) {
                        if (guard)
                            Q_EMIT guard->queryLinkMenuRequested(row, item, link, path, line, raw, p.x, p.y);
                    };
                    page.onReminderChip = [guard](int row, int from, int to, bool menu, at::Point p) {
                        if (guard)
                            Q_EMIT guard->reminderChipActivated(row, from, to, menu, p.x, p.y);
                    };
                    page.onVimKey = [guard] {
                        if (guard)
                            Q_EMIT guard->vimKeyHandled();
                    };
                });
        applyStyle();
        connectTables();
        applyExtras();
        applyTouchSelection();
        m_page->host = std::make_unique<brulion::editor::atr::ForeignRowHost>(this, r.app, m_page->entity, model);
        m_page->host->setDelegate(m_delegate);
        m_page->host->setHeader(m_header);
        m_page->host->setFooter(m_footer);
        applyFind();
        connectController();
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

QVariantMap AtramentView::activateQueryLink(int row, int item, const QString &link, bool newPane)
{
    QVariantMap answer;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    // (Outside of an update of the page: following the link may open another page.)
    brulion::editor::QueryResultModel *results =
            m_page ? m_page->entity.read(runtime().app).queryResults(row) : nullptr;
    if (results)
        answer = link == u"open:"_s ? results->open(item, newPane) : results->activateLink(item, link, newPane);
#else
    Q_UNUSED(row)
    Q_UNUSED(item)
    Q_UNUSED(link)
    Q_UNUSED(newPane)
#endif
    return answer;
}

QVariantMap AtramentView::textCommand(const QString &command)
{
    QVariantMap answer;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using brulion::editor::atr::PageView;
    std::optional<PageView::TextCommand> c;
    if (command == u"cut")
        c = PageView::TextCommand::Cut;
    else if (command == u"copy")
        c = PageView::TextCommand::Copy;
    else if (command == u"paste")
        c = PageView::TextCommand::Paste;
    else if (command == u"selectAll")
        c = PageView::TextCommand::SelectAll;
    // The input method's own word first (it keeps it outside the text).
    QGuiApplication::inputMethod()->commit();
    bool done = false;
    if (m_page && c)
        m_page->entity.update(runtime().app, [&](PageView &page, at::Context<PageView> &cx) {
            done = page.textCommand(*c, cx, &answer);
        });
    answer.insert(u"done"_s, done);
#else
    Q_UNUSED(command)
#endif
    return answer;
}

void AtramentView::placeHostedChildren()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page && m_page->host)
        m_page->host->place();
    // Where the frame put the selection's band (the selection's tools may follow it).
    if (m_page) {
        const std::optional<at::Rect> band = m_page->entity.read(runtime().app).selectionBounds();
        const QRectF rect = band ? QRectF(band->left(), band->top(), band->width(), band->height()) : QRectF();
        if (rect != m_selectionRect) {
            m_selectionRect = rect;
            Q_EMIT selectionRectChanged();
        }
    }
#endif
}

// --- "page": tables drawn and edited by Atrament (editor/atrament/tablerows.cpp) -----------------

void AtramentView::applyTableStyle()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_page)
        return;
    using namespace brulion::editor::atr;
    m_page->entity.update(runtime().app, [this](PageView &page, at::Context<PageView> &cx) {
        PageView::TableLook look;
        auto colour = [this](const char *key, at::Color fallback) {
            const QVariant v = m_tableLook.value(QString::fromLatin1(key));
            const QColor c = v.metaType() == QMetaType::fromType<QColor>() ? v.value<QColor>() : QColor::fromString(v.toString());
            return c.isValid() ? atColor(c) : fallback;
        };
        auto number = [this](const char *key, float fallback) {
            bool ok = false;
            const double d = m_tableLook.value(QString::fromLatin1(key)).toDouble(&ok);
            return ok && d >= 0 ? float(d) : fallback;
        };
        auto family = [this](const char *key, const std::u16string &fallback) {
            const QString f = m_tableLook.value(QString::fromLatin1(key)).toString();
            return f.isEmpty() ? fallback : std::u16string(u16View(f));
        };
        look.background = colour("background", look.background);
        look.headerBackground = colour("header", look.headerBackground);
        look.border = colour("border", look.border);
        look.borderStrong = colour("borderStrong", look.borderStrong);
        look.borderSubtle = colour("borderSubtle", look.borderSubtle);
        look.focusRing = colour("focusRing", look.focusRing);
        look.formula = colour("formula", look.formula);
        look.error = colour("error", look.error);
        look.input = colour("input", look.input);
        look.hover = colour("hover", look.hover);
        look.picked = colour("picked", look.picked);
        look.accent = colour("accent", look.accent);
        look.muted = colour("muted", look.muted);
        look.filterHeight = number("filterHeight", look.filterHeight);
        look.fieldTextSize = number("fieldTextSize", look.fieldTextSize);
        look.statusTextSize = number("statusTextSize", look.statusTextSize);
        look.fontFamily = family("fontFamily", look.fontFamily);
        look.monoFamily = family("monoFamily", look.monoFamily);
        look.fontSize = number("fontSize", look.fontSize);
        look.lineHeight = number("lineHeight", look.lineHeight);
        look.rowHeight = number("rowHeight", look.rowHeight);
        look.cellPadding = number("cellPadding", look.cellPadding);
        look.radius = number("radius", look.radius);
        look.buttonHeight = number("buttonHeight", look.buttonHeight);
        look.buttonTextSize = number("buttonTextSize", look.buttonTextSize);
        page.setDrawsTables(m_drawsTables, look, cx);
    });
#endif
}

void AtramentView::connectTables()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_page)
        return;
    using namespace brulion::editor::atr;
    QPointer<AtramentView> guard = this;
    m_page->entity.update(runtime().app, [guard](PageView &page, at::Context<PageView> &) {
        page.onCellEditRequest = [guard](int row, int tableRow, int column, int cursor, const at::Modifiers &modifiers) {
            if (!guard)
                return false;
            Q_EMIT guard->tableCellTapped(row, tableRow, column, cursor, qtModifiers(modifiers));
            return true;
        };
        page.onCellLink = [guard](int row, int tableRow, int column, const QString &href, at::Point p) {
            if (guard)
                Q_EMIT guard->tableLinkClicked(row, tableRow, column, href, p.x, p.y);
        };
        page.onCellMenu = [guard](int row, int tableRow, int column, at::Point p) {
            if (!guard)
                return;
            // The table's size and the column's alignment, for the menu's Row and Column items.
            auto *model = qobject_cast<brulion::editor::BlockListModel *>(guard->m_pageModel.data());
            const TableGrid grid = model ? readTableGrid(model->index(row, 0)
                                                                 .data(brulion::editor::BlockListModel::TableDataRole)
                                                                 .toMap())
                                         : TableGrid{};
            QString align;
            switch (grid.align(column)) {
            case TableAlign::Left: align = QStringLiteral("left"); break;
            case TableAlign::Center: align = QStringLiteral("center"); break;
            case TableAlign::Right: align = QStringLiteral("right"); break;
            case TableAlign::Default: break;
            }
            const QPointF scene = guard->mapToScene(QPointF(p.x, p.y));
            Q_EMIT guard->tableCellMenuRequested(row, tableRow, column, grid.rowCount(), grid.columnCount(), align,
                                                 scene.x(), scene.y());
        };
        page.onNotice = [guard](const QString &text, const QString &level) {
            if (guard)
                Q_EMIT guard->noticeRequested(text, level);
        };
        page.onCellSelectionChanged = [guard](bool selected) {
            if (!guard)
                return;
            guard->m_cellsSelected = selected;
            Q_EMIT guard->cellSelectionChanged();
            for (const auto &kv : guard->m_tableItems)
                if (kv.second)
                    Q_EMIT kv.second->changed();
        };
        page.onCellEditingChanged = [guard](int row) {
            if (!guard || guard->m_editedTableRow == row)
                return;
            guard->m_editedTableRow = row;
            Q_EMIT guard->tableEditingChanged();
            for (const auto &kv : guard->m_tableItems)
                if (kv.second)
                    Q_EMIT kv.second->changed();
        };
        page.onCellLinkMenu = [guard](int row, int tableRow, int column, const QString &href, at::Point p) {
            if (guard)
                Q_EMIT guard->tableLinkMenuRequested(row, tableRow, column, href, p.x, p.y);
        };
    });
#endif
}

bool AtramentView::editFirstTableCell(int row)
{
    bool done = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [&](brulion::editor::atr::PageView &page,
                                                 at::Context<brulion::editor::atr::PageView> &cx) {
            done = page.editFirstCell(row, cx);
        });
#else
    Q_UNUSED(row)
#endif
    return done;
}

int AtramentView::rowOfUid(quint64 uid) const
{
    auto *model = qobject_cast<brulion::editor::BlockListModel *>(m_pageModel.data());
    return model && uid ? model->rowOfUid(uid) : -1;
}

QQuickItem *AtramentView::tableAt(int row)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    auto *model = qobject_cast<brulion::editor::BlockListModel *>(m_pageModel.data());
    if (!m_page || !model || row < 0 || row >= model->rowCount() || !m_drawsTables
        || model->index(row, 0).data(brulion::editor::BlockListModel::ViewRole).toString() != u"table"
        || !m_page->entity.read(runtime().app).drawsRow(row))
        return nullptr;
    const quint64 uid = model->uidAt(row);
    // (Tables gone from the page take their items with them.)
    for (auto it = m_tableItems.begin(); it != m_tableItems.end();) {
        if (!it->second || model->rowOfUid(it->first) < 0) {
            if (it->second && it->first != uid)
                it->second->deleteLater();
            it = m_tableItems.erase(it);
        } else {
            ++it;
        }
    }
    QPointer<AtramentTableItem> &item = m_tableItems[uid];
    if (!item)
        item = new AtramentTableItem(this, uid);
    return item.data();
#else
    Q_UNUSED(row)
    return nullptr;
#endif
}

QQuickItem *AtramentView::editedTable()
{
    return m_editedTableRow >= 0 ? tableAt(m_editedTableRow) : nullptr;
}

bool AtramentView::editTableCell(int row, int tableRow, int column, int cursor)
{
    bool done = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [&](brulion::editor::atr::PageView &page,
                                                 at::Context<brulion::editor::atr::PageView> &cx) {
            done = page.editCell(row, tableRow, column, cursor, cx);
        });
#else
    Q_UNUSED(row)
    Q_UNUSED(tableRow)
    Q_UNUSED(column)
    Q_UNUSED(cursor)
#endif
    return done;
}

// --- "page": the edited text's extras (spell marks, Vim)

void AtramentView::setSpellCheck(QObject *spell)
{
    if (spell == m_spellCheck)
        return;
    m_spellCheck = spell;
    Q_EMIT spellCheckChanged();
}

void AtramentView::setVimSession(QObject *vim)
{
    if (vim == m_vimSession)
        return;
    m_vimSession = vim;
    Q_EMIT vimSessionChanged();
}

void AtramentView::applyExtras()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_page)
        return;
    using namespace brulion::editor;
    auto *spell = qobject_cast<SpellCheck *>(m_spellCheck.data());
    auto *vim = qobject_cast<VimSession *>(m_vimSession.data());
    m_page->entity.update(runtime().app, [&](atr::PageView &page, at::Context<atr::PageView> &cx) {
        page.setSpellCheck(spell, atColor(m_spellColor), cx);
        page.setVim(vim, cx);
    });
#endif
}

int AtramentView::editSelectionStart() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page) {
        const auto st = m_page->entity.read(runtime().app).editState();
        return std::min(st.anchor, st.cursor);
    }
#endif
    return 0;
}

int AtramentView::editSelectionEnd() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page) {
        const auto st = m_page->entity.read(runtime().app).editState();
        return std::max(st.anchor, st.cursor);
    }
#endif
    return 0;
}

bool AtramentView::event(QEvent *e)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (e->type() == QEvent::ShortcutOverride && m_page) {
        auto *vim = qobject_cast<brulion::editor::VimSession *>(m_vimSession.data());
        auto *key = static_cast<QKeyEvent *>(e);
        const Qt::KeyboardModifiers mods = key->modifiers();
        // BlockEditor.vimOwnsShortcut: Ctrl+R, Ctrl+D, Ctrl+U, Ctrl+A, Ctrl+X, Ctrl+[ in Vim's
        // command modes, Ctrl+W and Ctrl+H in its command line.
        if (vim && vim->enabled() && vim->commands() && editRow() >= 0 && (mods & Qt::ControlModifier)
            && !(mods & (Qt::AltModifier | Qt::MetaModifier | Qt::ShiftModifier))) {
            const int k = key->key();
            const bool owned = (!vim->commandLine().isEmpty() && (k == Qt::Key_W || k == Qt::Key_H))
                               || ((k == Qt::Key_A || k == Qt::Key_X) && vim->mode() != u"replace")
                               || k == Qt::Key_R || k == Qt::Key_D || k == Qt::Key_U || k == Qt::Key_BracketLeft;
            if (owned) {
                e->accept();
                return true;
            }
        }
    }
#endif
    return AtramentItem::event(e);
}

// --- AtramentTableItem: a table drawn by Atrament as BlockEditor reaches a TableBlockView ---------

AtramentTableItem::AtramentTableItem(AtramentView *view, quint64 uid)
    : QQuickItem(nullptr)
    , m_view(view)
    , m_uid(uid)
{
    setParent(view);
    setVisible(false);
}

int AtramentTableItem::blockIndex() const
{
    return m_view ? m_view->rowOfUid(m_uid) : -1;
}

const brulion::editor::atr::PageView *AtramentTableItem::page() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_view && m_view->m_page)
        return &m_view->m_page->entity.read(runtime().app);
#endif
    return nullptr;
}

void AtramentTableItem::step(const Step &f)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    const int row = blockIndex();
    if (!m_view || !m_view->m_page || row < 0)
        return;
    m_view->m_page->entity.update(runtime().app, [&](brulion::editor::atr::PageView &page,
                                                     at::Context<brulion::editor::atr::PageView> &cx) {
        f(page, cx, row);
    });
#else
    Q_UNUSED(f)
#endif
}

#ifdef BRULION_HAS_ATRAMENT_EDITOR
namespace {
using brulion::editor::atr::PageView;
using brulion::editor::atr::TableGrid;

TableGrid gridOf(const AtramentView *view, int row)
{
    auto *model = view ? qobject_cast<brulion::editor::BlockListModel *>(view->pageModel()) : nullptr;
    if (!model || row < 0 || row >= model->rowCount())
        return {};
    return brulion::editor::atr::readTableGrid(
            model->index(row, 0).data(brulion::editor::BlockListModel::TableDataRole).toMap());
}
} // namespace

int AtramentTableItem::editRow() const
{
    const PageView *p = page();
    const int row = blockIndex();
    const PageView::TableCell cell = p ? p->editedCell() : PageView::TableCell{};
    return row >= 0 && cell.row == row ? cell.tableRow : -2;
}

int AtramentTableItem::editColumn() const
{
    const PageView *p = page();
    const int row = blockIndex();
    const PageView::TableCell cell = p ? p->editedCell() : PageView::TableCell{};
    return row >= 0 && cell.row == row ? cell.column : -1;
}

int AtramentTableItem::columns() const
{
    return gridOf(m_view.data(), blockIndex()).columnCount();
}

int AtramentTableItem::cellRowCount() const
{
    return gridOf(m_view.data(), blockIndex()).rowCount();
}

bool AtramentTableItem::filtering() const
{
    return gridOf(m_view.data(), blockIndex()).filtering;
}

QString AtramentTableItem::cellAlignOf(int c) const
{
    switch (gridOf(m_view.data(), blockIndex()).align(c)) {
    case brulion::editor::atr::TableAlign::Left: return QStringLiteral("left");
    case brulion::editor::atr::TableAlign::Center: return QStringLiteral("center");
    case brulion::editor::atr::TableAlign::Right: return QStringLiteral("right");
    case brulion::editor::atr::TableAlign::Default: break;
    }
    return {};
}

QString AtramentTableItem::rawText(int r, int c) const
{
    const TableGrid grid = gridOf(m_view.data(), blockIndex());
    const brulion::editor::atr::TableGridCell *cell = grid.cell(r, c);
    return cell ? cell->text : QString();
}

QStringList AtramentTableItem::headerTexts() const
{
    QStringList out;
    for (const auto &h : gridOf(m_view.data(), blockIndex()).header)
        out.append(h.text);
    return out;
}

QString AtramentTableItem::headerText(int c) const
{
    const TableGrid grid = gridOf(m_view.data(), blockIndex());
    return c >= 0 && c < grid.columnCount() ? grid.header[std::size_t(c)].text : QString();
}

bool AtramentTableItem::isRowShown(int r) const
{
    return gridOf(m_view.data(), blockIndex()).rowShown(r);
}

int AtramentTableItem::shownRowCount() const
{
    return int(gridOf(m_view.data(), blockIndex()).shownRows(-2).size());
}

bool AtramentTableItem::filterFocused(int c) const
{
    const PageView *p = page();
    return p && p->filterFocused(blockIndex(), c);
}

QString AtramentTableItem::filterStatusText() const
{
    const TableGrid grid = gridOf(m_view.data(), blockIndex());
    return grid.filtering ? PageView::filterStatus(grid) : QString();
}

qreal AtramentTableItem::cellCenterX(int r, int c) const
{
    const QRectF b = cellRect(r, c);
    return b.isEmpty() ? -1 : b.center().x();
}

qreal AtramentTableItem::cellCenterY(int r, int c) const
{
    const QRectF b = cellRect(r, c);
    return b.isEmpty() ? -1 : b.center().y();
}

QList<int> AtramentTableItem::shownRows() const
{
    QList<int> out;
    for (const int r : gridOf(m_view.data(), blockIndex()).shownRows(editRow()))
        out.append(r);
    return out;
}

bool AtramentTableItem::hasSelection() const
{
    const PageView *p = page();
    const int row = blockIndex();
    return p && row >= 0 && p->cellSelection().row == row;
}

bool AtramentTableItem::inSelection(int r, int c) const
{
    const PageView *p = page();
    const int row = blockIndex();
    return p && row >= 0 && p->cellSelected(row, r, c);
}

QList<int> AtramentTableItem::selectedRows() const
{
    return hasSelection() ? page()->selectedTableRows() : QList<int>{};
}

int AtramentTableItem::selectionTop() const
{
    return hasSelection() ? page()->cellSelection().top() : -2;
}

int AtramentTableItem::selectionBottom() const
{
    return hasSelection() ? page()->cellSelection().bottom() : -2;
}

int AtramentTableItem::selectionLeft() const
{
    return hasSelection() ? page()->cellSelection().left() : -1;
}

int AtramentTableItem::selectionRight() const
{
    return hasSelection() ? page()->cellSelection().right() : -1;
}

QString AtramentTableItem::cellText() const
{
    const PageView *p = page();
    const auto *input = p && editRow() > -2 ? p->cellInput() : nullptr;
    if (!input)
        return {};
    const std::u16string &t = input->read(runtime().app).text();
    return QString(reinterpret_cast<const QChar *>(t.data()), qsizetype(t.size()));
}

int AtramentTableItem::cellCursor() const
{
    const PageView *p = page();
    const auto *input = p && editRow() > -2 ? p->cellInput() : nullptr;
    return input ? input->read(runtime().app).selection().focus : -1;
}

QRectF AtramentTableItem::cellRect(int r, int c) const
{
    const PageView *p = page();
    const int row = blockIndex();
    const std::optional<at::Rect> b = p && row >= 0 ? p->cellBounds(row, r, c) : std::nullopt;
    return b ? QRectF(b->left(), b->top(), b->width(), b->height()) : QRectF();
}

void AtramentTableItem::startCell(int r, int c)
{
    step([r, c](PageView &page, at::Context<PageView> &cx, int row) { page.editCell(row, r, c, -1, cx); });
}

void AtramentTableItem::focusFirstCell()
{
    step([](PageView &page, at::Context<PageView> &cx, int row) { page.editFirstCell(row, cx); });
}

void AtramentTableItem::openFilter(int c)
{
    // (TableBlockView.openFilter: the filter row shown, the keys in column c's field.)
    step([c](PageView &page, at::Context<PageView> &cx, int row) {
        if (page.controller())
            page.controller()->setTableFilterOpen(row, true);
        page.focusFilter(row, c, cx);
    });
}

void AtramentTableItem::select(int anchorRow, int anchorColumn, int cursorRow, int cursorColumn)
{
    step([=](PageView &page, at::Context<PageView> &cx, int row) {
        page.selectCells(row, anchorRow, anchorColumn, cursorRow, cursorColumn, cx);
    });
}

void AtramentTableItem::clearSelection()
{
    if (hasSelection())
        step([](PageView &page, at::Context<PageView> &cx, int) { page.clearCellSelection(cx); });
}

void AtramentTableItem::nextCell()
{
    if (editRow() > -2)
        step([](PageView &page, at::Context<PageView> &cx, int) { page.nextCell(cx); });
}

void AtramentTableItem::undoInCell()
{
    if (editRow() > -2)
        step([](PageView &page, at::Context<PageView> &cx, int) { page.undoInCell(false, cx); });
}

void AtramentTableItem::redoInCell()
{
    if (editRow() > -2)
        step([](PageView &page, at::Context<PageView> &cx, int) { page.undoInCell(true, cx); });
}

void AtramentTableItem::closeCell()
{
    if (editRow() > -2)
        step([](PageView &page, at::Context<PageView> &cx, int) { page.closeCell(cx); });
}
#else
int AtramentTableItem::editRow() const { return -2; }
int AtramentTableItem::editColumn() const { return -1; }
int AtramentTableItem::columns() const { return 0; }
int AtramentTableItem::cellRowCount() const { return 0; }
bool AtramentTableItem::filtering() const { return false; }
QString AtramentTableItem::cellAlignOf(int) const { return {}; }
QString AtramentTableItem::rawText(int, int) const { return {}; }
QStringList AtramentTableItem::headerTexts() const { return {}; }
QList<int> AtramentTableItem::shownRows() const { return {}; }
QString AtramentTableItem::headerText(int) const { return {}; }
bool AtramentTableItem::isRowShown(int) const { return false; }
int AtramentTableItem::shownRowCount() const { return 0; }
bool AtramentTableItem::filterFocused(int) const { return false; }
QString AtramentTableItem::filterStatusText() const { return {}; }
qreal AtramentTableItem::cellCenterX(int, int) const { return -1; }
qreal AtramentTableItem::cellCenterY(int, int) const { return -1; }
bool AtramentTableItem::hasSelection() const { return false; }
bool AtramentTableItem::inSelection(int, int) const { return false; }
QList<int> AtramentTableItem::selectedRows() const { return {}; }
int AtramentTableItem::selectionTop() const { return -2; }
int AtramentTableItem::selectionBottom() const { return -2; }
int AtramentTableItem::selectionLeft() const { return -1; }
int AtramentTableItem::selectionRight() const { return -1; }
QString AtramentTableItem::cellText() const { return {}; }
int AtramentTableItem::cellCursor() const { return -1; }
QRectF AtramentTableItem::cellRect(int, int) const { return {}; }
void AtramentTableItem::startCell(int, int) { }
void AtramentTableItem::focusFirstCell() { }
void AtramentTableItem::openFilter(int) { }
void AtramentTableItem::select(int, int, int, int) { }
void AtramentTableItem::clearSelection() { }
void AtramentTableItem::nextCell() { }
void AtramentTableItem::undoInCell() { }
void AtramentTableItem::redoInCell() { }
void AtramentTableItem::closeCell() { }
#endif

int AtramentTableItem::selectedCellCount() const
{
    return hasSelection() ? int(selectedRows().size()) * (selectionRight() - selectionLeft() + 1) : 0;
}

bool AtramentTableItem::cellHasKeys() const
{
    return m_view && m_view->hasActiveFocus() && editRow() > -2;
}

int AtramentTableItem::cellLength() const
{
    return editRow() > -2 ? int(cellText().size()) : -1;
}

void AtramentTableItem::commitComposition()
{
    // (The edited cell's word the keyboard composes.)
    if (m_view && editRow() > -2)
        m_view->commitComposition();
}
