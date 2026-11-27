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
    connect(this, &AtramentView::findChanged, this, &AtramentView::applyFind);
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
        auto colour = [this](const char *key, at::Color fallback) {
            const QVariant v = m_codeLook.value(QString::fromLatin1(key));
            const QColor c = v.metaType() == QMetaType::fromType<QColor>() ? v.value<QColor>() : QColor::fromString(v.toString());
            return c.isValid() ? atColor(c) : fallback;
        };
        auto number = [this](const char *key, float fallback) {
            bool ok = false;
            const double d = m_codeLook.value(QString::fromLatin1(key)).toDouble(&ok);
            return ok && d > 0 ? float(d) : fallback;
        };
        code.background = colour("background", code.background);
        code.border = colour("border", code.border);
        code.label = colour("label", code.label);
        code.radius = number("radius", code.radius);
        code.fontSize = number("fontSize", code.fontSize);
        code.lineHeight = number("lineHeight", code.lineHeight);
        code.labelSize = number("labelSize", code.labelSize);
        page.setDrawsCode(m_drawsCode, code, cx);
        page.setEditLook(atColor(m_caretColor), atColor(m_selectionColor),
                         std::u16string(u16View(m_placeholderText)), std::u16string(u16View(m_editLabel)), cx);
        // The grips (BlockSelectionGrip: Theme.link on Theme.bg) and the drop line (Theme.focusRing).
        page.setDragLook(atColor(m_linkColor), atColor(m_backgroundColor), atColor(m_ringColor), cx);
        cx.notify();
    });
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

void AtramentView::revealRow(int row)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_page)
        m_page->entity.update(runtime().app, [row](brulion::editor::atr::PageView &page,
                                                   at::Context<brulion::editor::atr::PageView> &cx) {
            page.revealRow(row, cx);
        });
#else
    Q_UNUSED(row)
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
                    page.onLinkAt = [guard](int row, const QString &href, at::Point p) {
                        if (guard)
                            Q_EMIT guard->linkClicked(row, href, p.x, p.y);
                    };
                    page.onToggleTask = [guard](int row) {
                        if (guard)
                            Q_EMIT guard->taskToggled(row);
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
                });
        applyStyle();
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
