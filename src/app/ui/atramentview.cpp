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
#include "journalview.h"
#include "pageview.h"
#include "streamfind.h"
#include "streamselection.h"
#include "dates/names.h"
#include "tablelayout.h"
#include "queryresultmodel.h"
#include "spellcheck.h"
#include "vimsession.h"
#include "picturecache.h"
#include "drawingimageprovider.h"
#endif

#include <QDate>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

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

// A page the looks and callbacks go to: the "page" view's, or a day of the journal stream.
struct AtramentView::PageHandle {
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    at::Entity<brulion::editor::atr::PageView> entity;
    QPointer<brulion::editor::BlockListModel> model;
    bool day = false; // a day of the journal: its signals make it the active one first
#endif
};

struct AtramentView::Journal {
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    at::Entity<brulion::editor::atr::JournalView> entity;
    // Each day made: its page and the QML items over its foreign rows.
    struct Day {
        at::Entity<brulion::editor::atr::PageView> page;
        std::unique_ptr<brulion::editor::atr::ForeignRowHost> host;
        QPointer<QQuickItem> header; // the zoom's breadcrumb while it is zoomed into a block
        bool foldsRestored = false;
    };
    std::unordered_map<QObject *, Day> days; // by controller
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
    connect(this, &AtramentView::journalChanged, this, &AtramentView::applyJournal);
    connect(this, &AtramentView::journalLookChanged, this, &AtramentView::applyJournalLook);
    connect(this, &AtramentView::dayOptionsChanged, this, &AtramentView::applyDayOptions);
}

AtramentView::~AtramentView()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    // The delegates go with the item (they name the editor around it).
    if (m_page)
        m_page->host.reset();
    if (m_journal)
        for (auto &[controller, day] : m_journal->days)
            day.host.reset();
#endif
    // The window (closed by ~AtramentItem, after this) still names the page's entity: it goes
    // once the window has.
    if (m_page)
        QTimer::singleShot(0, QCoreApplication::instance(), [page = std::shared_ptr<Page>(std::move(m_page))] { });
    // (The stream's days save as they go, with it.)
    if (m_journal)
        QTimer::singleShot(0, QCoreApplication::instance(),
                           [journal = std::shared_ptr<Journal>(std::move(m_journal))] { });
}

bool AtramentView::hasPage() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    return m_page && m_page->entity;
#else
    return false;
#endif
}

template<class F>
void AtramentView::forEachPage(F &&f)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal) {
        for (auto &[controller, day] : m_journal->days) {
            auto *c = qobject_cast<brulion::editor::PageController *>(controller);
            f(PageHandle{day.page, c ? c->blocks() : nullptr, true});
        }
        return;
    }
    if (hasPage())
        f(PageHandle{m_page->entity, qobject_cast<brulion::editor::BlockListModel *>(m_pageModel.data()), false});
#else
    Q_UNUSED(f)
#endif
}

void AtramentView::setPageModel(QObject *model)
{
    if (model == m_pageModel)
        return;
    if (m_view == u"journal") {
        // (The stream's days have their own models: the editor's, which follows the active day, is
        // none of the view's.)
        m_pageModel = model;
        return;
    }
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
    if (m_view == u"journal")
        return;
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
    if (hasPage())
        return m_page->entity.read(runtime().app).editState().row;
#endif
    return -1;
}

QString AtramentView::editText() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (hasPage()) {
        const std::u16string text = m_page->entity.read(runtime().app).editState().text;
        return QString(reinterpret_cast<const QChar *>(text.data()), qsizetype(text.size()));
    }
#endif
    return {};
}

int AtramentView::editCursor() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (hasPage())
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
    if (hasPage()) {
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
    if (hasPage())
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
    if (hasPage())
        if (const auto r = m_page->entity.read(runtime().app).rowBounds(row))
            return r->top();
#endif
    Q_UNUSED(row);
    return -1;
}

qreal AtramentView::rowHeight(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (hasPage())
        if (const auto r = m_page->entity.read(runtime().app).rowBounds(row))
            return r->height();
#endif
    Q_UNUSED(row);
    return -1;
}

QPointF AtramentView::linkPoint(int row, const QString &hrefPart) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (hasPage())
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
    if (hasPage())
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
    if (hasPage())
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
    if (hasPage())
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
    if (hasPage())
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
    if (hasPage())
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
    if (m_journal) {
        // (Every day, also those made later.)
        m_journal->entity.update(runtime().app, [this](brulion::editor::atr::JournalView &journal,
                                                       at::Context<brulion::editor::atr::JournalView> &cx) {
            journal.setFind(m_findQuery, m_findMatchCase, m_findMatchDiacritics, m_findWholeWord, atColor(m_findColor), cx);
        });
        return;
    }
    if (!hasPage())
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
    if (!hasPage() || m_journal)
        return;
    using namespace brulion::editor::atr;
    auto *controller = qobject_cast<brulion::editor::PageController *>(m_pageController.data());
    // (A controller of another model than the page's is none.)
    if (controller && controller->blocks() != m_pageModel.data())
        controller = nullptr;
    connectEditing(PageHandle{m_page->entity, qobject_cast<brulion::editor::BlockListModel *>(m_pageModel.data()), false});
    m_page->entity.update(runtime().app,
                          [controller](PageView &page, at::Context<PageView> &cx) { page.setController(controller, cx); });
#endif
}

void AtramentView::connectEditing(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor::atr;
    QPointer<AtramentView> guard = this;
    // (A day of the journal: the day written in first.)
    const std::uint64_t pid = handle.day ? handle.entity.id() : 0;
    auto act = [guard, pid] {
        if (guard && pid)
            guard->activatePage(pid);
    };
    handle.entity.update(runtime().app, [guard, act](PageView &page, at::Context<PageView> &) {
        page.onEditRequest = [guard, act](int row, int cursor, const at::Modifiers &modifiers) {
            if (!guard)
                return false;
            act();
            Q_EMIT guard->editRequested(row, cursor, qtModifiers(modifiers));
            return true;
        };
        page.onEditingStarted = [guard, act] {
            act();
            // The keyboard comes here when a block Atrament edits is edited (a new block, Enter,
            // ↑/↓ from the QML around) — the QML around decides (the find bar keeps it).
            if (guard)
                Q_EMIT guard->editingStarted();
        };
        page.onEscape = [guard, act] {
            act();
            if (guard)
                Q_EMIT guard->escapePressed();
        };
        page.onUndo = [guard, act](bool redo) {
            act();
            if (guard)
                Q_EMIT guard->undoRequested(redo);
        };
        page.onFollowLink = [guard, act](int row, int cursor, bool newPane) {
            act();
            if (guard)
                Q_EMIT guard->followLinkRequested(row, cursor, newPane);
        };
        page.onExtendSelection = [guard, act](int row, int step) {
            act();
            if (guard)
                Q_EMIT guard->selectionExtendRequested(row, step);
        };
        page.onSelectAllBlocks = [guard, act] {
            act();
            if (guard)
                Q_EMIT guard->selectAllBlocksRequested();
        };
        page.onEditStateChanged = [guard] {
            if (guard)
                Q_EMIT guard->editStateChanged();
        };
        page.onSelectionCommand = [guard, act](PageView::SelectionCommand command, int row) {
            if (!guard)
                return false;
            act();
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
    });
#else
    Q_UNUSED(handle)
#endif
}

void AtramentView::applyStyle()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal) {
        // The days' geometry is the stream's (each day's note mode its own gutter).
        using namespace brulion::editor::atr;
        PageMetrics m;
        m.fontSize = float(m_fontSize);
        m.lineHeight = float(m_lineHeight);
        m.indentStep = float(m_indentStep);
        m.gutter = float(m_gutter);
        m.bulletColumn = float(m_bulletColumn);
        m.documentMode = false;
        m.listIndent = float(m_listIndent);
        m.contentX = float(m_contentX);
        m.contentWidth = float(m_contentWidth);
        m.arrowColumn = float(m_arrowColumn);
        m.touch = m_touchUi;
        m.headingSizes[0] = m.fontSize;
        PagePalette p;
        p.text = atColor(m_textColor);
        p.code = p.text;
        p.muted = atColor(m_mutedColor);
        p.link = atColor(m_linkColor);
        p.guide = atColor(m_guideColor);
        if (m_ruleColor.isValid())
            p.rule = atColor(m_ruleColor);
        p.background = atColor(m_backgroundColor);
        m_journal->entity.update(runtime().app, [&](JournalView &journal, at::Context<JournalView> &cx) {
            journal.setPageStyle(m, p, float(m_documentGutter), cx);
        });
    }
    forEachPage([this](const PageHandle &page) { styleOne(page); });
    applyTableStyle();
#endif
}

void AtramentView::styleOne(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor::atr;
    const bool day = handle.day;
    handle.entity.update(runtime().app, [this, day](PageView &page, at::Context<PageView> &cx) {
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
        if (!day)
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
#else
    Q_UNUSED(handle)
#endif
}

void AtramentView::applyTouchSelection()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    forEachPage([this](const PageHandle &page) { touchSelectionOf(page); });
#endif
}

void AtramentView::touchSelectionOf(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor::atr;
    // (A day of the journal: the touch selection mode is the active day's.)
    const bool grips = !handle.day || (hasPage() && m_page->entity.id() == handle.entity.id());
    handle.entity.update(runtime().app, [this, grips](PageView &page, at::Context<PageView> &cx) {
        page.setTouchSelection(m_selectionGrips && grips, m_selectionPicking && grips, cx);
        page.setDragBottomInset(float(m_dragBottomInset));
    });
#else
    Q_UNUSED(handle)
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
    // ("journal": the page's header and footer, set before, are none of the stream's.)
    if (m_view == u"journal") {
        for (QQuickItem *item : {m_header.data(), m_footer.data()}) {
            if (item) {
                item->setVisible(false);
                item->setEnabled(false);
            }
        }
    }
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
        connectPageSignals(PageHandle{m_page->entity, model, false});
        connectHoverSignals(PageHandle{m_page->entity, model, false});
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
    if (m_view == u"journal") {
        attachJournal();
        return;
    }
#endif
    if (attachOtherView())
        return;
    qWarning() << "AtramentView: no view named" << m_view;
}

// The page's hover cues: a link's preview, a table cell's tooltip (hovering activates no day of the
// journal: the link and its file come resolved).
void AtramentView::connectHoverSignals(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    QPointer<AtramentView> guard = this;
    handle.entity.update(runtime().app, [guard](brulion::editor::atr::PageView &page,
                                                at::Context<brulion::editor::atr::PageView> &) {
        page.onLinkHover = [guard](int row, const QString &href, const QString &fromPath, at::Point p) {
            if (guard)
                Q_EMIT guard->linkHovered(row, href, fromPath, p.x, p.y);
        };
        page.onToolTip = [guard](const QString &text, at::Rect r) {
            if (guard)
                Q_EMIT guard->toolTipRequested(text, r.left(), r.top(), r.width(), r.height());
        };
    });
#else
    Q_UNUSED(handle)
#endif
}

void AtramentView::connectPageSignals(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    QPointer<AtramentView> guard = this;
    const std::uint64_t pid = handle.day ? handle.entity.id() : 0;
    // (A day of the journal: the day written in first, so the editor acts on it.)
    auto act = [guard, pid] {
        if (guard && pid)
            guard->activatePage(pid);
    };
    {
        handle.entity.update(
                runtime().app, [guard, act](brulion::editor::atr::PageView &page, at::Context<brulion::editor::atr::PageView> &) {
                    page.onLink = [guard, act](int row, const QString &href) {
                        act();
                        if (guard)
                            Q_EMIT guard->linkActivated(row, href);
                    };
                    page.onLinkAt = [guard, act](int row, const QString &href, at::Point p) {
                        act();
                        if (guard)
                            Q_EMIT guard->linkClicked(row, href, p.x, p.y);
                    };
                    page.onLinkMenu = [guard, act](int row, const QString &href, at::Point p) {
                        act();
                        if (guard)
                            Q_EMIT guard->linkMenuRequested(row, href, p.x, p.y);
                    };
                    page.onToggleTask = [guard, act](int row) {
                        act();
                        if (guard)
                            Q_EMIT guard->taskToggled(row);
                    };
                    page.onTaskMenu = [guard, act](int row, at::Point p) {
                        act();
                        if (guard)
                            Q_EMIT guard->taskMenuRequested(row, p.x, p.y);
                    };
                    page.onTaskMenuAtCaret = [guard, act](int row, at::Point p) {
                        act();
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
                    page.onDragSelect = [guard, act](int anchor, int focus) {
                        act();
                        if (guard)
                            Q_EMIT guard->dragSelectRequested(anchor, focus);
                    };
                    page.onLongPressRow = [guard, act](int row) {
                        act();
                        if (guard)
                            Q_EMIT guard->rowLongPressed(row);
                    };
                    page.onBulletClick = [guard, act](int row) {
                        act();
                        if (guard)
                            Q_EMIT guard->bulletClicked(row);
                    };
                    page.onBlockMenu = [guard, act](int row, at::Point p) {
                        act();
                        if (!guard)
                            return;
                        const QPointF scene = guard->mapToScene(QPointF(p.x, p.y));
                        Q_EMIT guard->blockMenuRequested(row, scene.x(), scene.y());
                    };
                    // Pictures, files, embedded blocks, queries (docs/ATRAMENT.md M2 step 5).
                    page.onEmbedClick = [guard, act](int row, const QVariantMap &embed) {
                        act();
                        if (guard)
                            Q_EMIT guard->embedClicked(row, embed);
                    };
                    page.onEmbedMenu = [guard, act](int row, const QVariantMap &embed, at::Point p) {
                        act();
                        if (guard)
                            Q_EMIT guard->embedMenuRequested(row, embed, p.x, p.y);
                    };
                    page.onWebPicture = [guard, act](int row, const QVariantMap &embed, bool save) {
                        act();
                        if (guard)
                            Q_EMIT guard->webPictureRequested(row, embed.value(u"target"_s).toString(), save);
                    };
                    page.onEmbeddedLink = [guard, act](int row, const QString &link, const QString &path, int line,
                                                  const QString &raw) {
                        act();
                        if (guard)
                            Q_EMIT guard->embeddedLinkActivated(row, link, path, line, raw);
                    };
                    page.onQueryLink = [guard, act](int row, int item, const QString &link, const QString &path, int line,
                                               const QString &raw) {
                        act();
                        if (guard)
                            Q_EMIT guard->queryLinkActivated(row, item, link, path, line, raw);
                    };
                    page.onQueryAnswer = [guard, act](int row, const QVariantMap &answer) {
                        act();
                        if (guard)
                            Q_EMIT guard->queryAnswered(row, answer);
                    };
                    page.onSpellingMenu = [guard, act](const brulion::editor::atr::PageView::Misspelling &m, at::Point p) {
                        act();
                        if (!guard)
                            return;
                        const QVariantMap spelling{{u"from"_s, m.from}, {u"to"_s, m.to}, {u"word"_s, m.word},
                                                   {u"text"_s, m.text}};
                        Q_EMIT guard->spellingMenuRequested(m.row, m.edited, spelling, p.x, p.y);
                    };
                    page.onTextMenu = [guard, act](int row, const std::optional<brulion::editor::atr::PageView::Misspelling> &m,
                                                   at::Point p) {
                        act();
                        if (!guard)
                            return;
                        QVariantMap spelling;
                        if (m)
                            spelling = QVariantMap{{u"from"_s, m->from}, {u"to"_s, m->to}, {u"word"_s, m->word},
                                                   {u"text"_s, m->text}};
                        Q_EMIT guard->textMenuRequested(row, spelling, p.x, p.y);
                    };
                    page.onEmbeddedLinkMenu = [guard, act](int row, const QString &link, const QString &path, int line,
                                                           const QString &raw, at::Point p) {
                        act();
                        if (guard)
                            Q_EMIT guard->embeddedLinkMenuRequested(row, link, path, line, raw, p.x, p.y);
                    };
                    page.onQueryLinkMenu = [guard, act](int row, int item, const QString &link, const QString &path, int line,
                                                        const QString &raw, at::Point p) {
                        act();
                        if (guard)
                            Q_EMIT guard->queryLinkMenuRequested(row, item, link, path, line, raw, p.x, p.y);
                    };
                    page.onReminderChip = [guard, act](int row, int from, int to, bool menu, at::Point p) {
                        act();
                        if (guard)
                            Q_EMIT guard->reminderChipActivated(row, from, to, menu, p.x, p.y);
                    };
                    page.onVimKey = [guard] {
                        if (guard)
                            Q_EMIT guard->vimKeyHandled();
                    };
                });
    }
#else
    Q_UNUSED(handle)
#endif
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
    if (m_journal)
        for (auto &[controller, day] : m_journal->days)
            if (day.host)
                day.host->setDelegate(delegate);
#endif
    Q_EMIT delegateChanged();
}

void AtramentView::setDayHeader(QQmlComponent *component)
{
    if (component == m_dayHeader)
        return;
    m_dayHeader = component;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal) {
        std::vector<QObject *> controllers;
        for (auto &[controller, day] : m_journal->days) {
            // (Made again from the new component.)
            if (day.host)
                day.host->setHeader(nullptr);
            if (day.header)
                day.header->deleteLater();
            day.header = nullptr;
            controllers.push_back(controller);
        }
        for (QObject *controller : controllers)
            updateDayHeader(controller);
    }
#endif
    Q_EMIT dayHeaderChanged();
}

void AtramentView::updateDayHeader(QObject *controller)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor;
    if (!m_journal)
        return;
    const auto it = m_journal->days.find(controller);
    auto *page = qobject_cast<PageController *>(controller);
    if (it == m_journal->days.end() || !it->second.host || !page)
        return;
    Journal::Day &day = it->second;
    const bool zoomed = page->zoomBlock() >= 0 && m_dayHeader && m_dayHeader->isReady();
    if (zoomed == bool(day.header))
        return;
    if (!zoomed) {
        day.host->setHeader(nullptr);
        day.header->deleteLater();
        day.header = nullptr;
        return;
    }
    QQmlContext *context = m_dayHeader->creationContext() ? m_dayHeader->creationContext() : qmlContext(this);
    QObject *object = context ? m_dayHeader->beginCreate(context) : nullptr;
    auto *item = qobject_cast<QQuickItem *>(object);
    if (!item) {
        delete object;
        return;
    }
    m_dayHeader->setInitialProperties(item, {{u"controller"_s, QVariant::fromValue<QObject *>(page)}});
    item->setParentItem(this);
    m_dayHeader->completeCreate();
    day.header = item;
    day.host->setHeader(item);
#else
    Q_UNUSED(controller)
#endif
}

void AtramentView::setHeader(QQuickItem *item)
{
    if (item == m_header)
        return;
    m_header = item;
    // ("journal": the page's header is none of the stream's — its own is streamHeader.)
    if (item && m_view == u"journal") {
        item->setVisible(false);
        item->setEnabled(false);
        Q_EMIT headerChanged();
        return;
    }
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
    if (item && m_view == u"journal") {
        item->setVisible(false);
        item->setEnabled(false);
        Q_EMIT footerChanged();
        return;
    }
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
    if (m_journal && hasPage())
        for (const auto &[controller, day] : m_journal->days)
            if (day.page.id() == m_page->entity.id() && day.host)
                return day.host->rowItem(row);
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
            hasPage() ? m_page->entity.read(runtime().app).queryResults(row) : nullptr;
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
    if (m_journal) {
        // (Copied first: placing may make or release delegates, not days.)
        std::vector<brulion::editor::atr::ForeignRowHost *> hosts;
        for (auto &[controller, day] : m_journal->days)
            if (day.host)
                hosts.push_back(day.host.get());
        for (brulion::editor::atr::ForeignRowHost *host : hosts)
            host->place();
        placeStreamEdges();
    }
    // Where the frame put the selection's band (the selection's tools may follow it).
    if (hasPage()) {
        const std::optional<at::Rect> band = m_page->entity.read(runtime().app).selectionBounds();
        const QRectF rect = band ? QRectF(band->left(), band->top(), band->width(), band->height()) : QRectF();
        if (rect != m_selectionRect) {
            m_selectionRect = rect;
            Q_EMIT selectionRectChanged();
        }
    }
#endif
}

// --- "journal": the journal stream as one list (editor/atrament/journalview.h) ------------------

void AtramentView::setDaysModel(QObject *model)
{
    if (setBeforeView(m_daysModel, model, "the days' model"))
        Q_EMIT journalChanged();
}

// A model the view is made over, set before it is (once it is: a warning, nothing changed); the
// view made when the item is complete. False when nothing changed.
bool AtramentView::setBeforeView(QPointer<QObject> &slot, QObject *model, const char *what)
{
    if (model == slot)
        return false;
    if (m_attached) {
        qWarning() << "AtramentView:" << what << "is set before the view; ignoring";
        return false;
    }
    slot = model;
    if (isComponentComplete())
        attachView();
    return true;
}

void AtramentView::setStreamHeader(QQuickItem *item)
{
    if (item == m_streamHeader)
        return;
    if (m_streamHeader)
        disconnect(m_streamHeader, nullptr, this, nullptr);
    m_streamHeader = item;
    if (item) {
        item->setParentItem(this);
        connect(item, &QQuickItem::heightChanged, this, &AtramentView::tellStreamEdges);
    }
    Q_EMIT journalChanged();
}

void AtramentView::setStreamFooter(QQuickItem *item)
{
    if (item == m_streamFooter)
        return;
    if (m_streamFooter)
        disconnect(m_streamFooter, nullptr, this, nullptr);
    m_streamFooter = item;
    if (item) {
        item->setParentItem(this);
        connect(item, &QQuickItem::heightChanged, this, &AtramentView::tellStreamEdges);
    }
    Q_EMIT journalChanged();
}

void AtramentView::attachJournal()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor;
    using namespace brulion::editor::atr;
    auto *days = qobject_cast<QAbstractItemModel *>(m_daysModel.data());
    if (!days)
        return; // (attached once the days are set)
    Runtime &r = runtime();
    m_page = std::make_unique<Page>(); // the active day's page, once there is one
    m_journal = std::make_unique<Journal>();
    m_journal->entity = r.app.newEntity<JournalView>(days);
    QPointer<AtramentView> guard = this;
    m_journal->entity.update(r.app, [guard](JournalView &j, at::Context<JournalView> &) {
        // A day's controller as ContentJournalDay makes it.
        j.makeController = [guard](const JournalView::DayInfo &info) -> std::unique_ptr<PageController> {
            if (!guard)
                return nullptr;
            auto c = std::make_unique<PageController>();
            QQmlEngine::setObjectOwnership(c.get(), QQmlEngine::CppOwnership);
            c->setObjectName(u"journalDayController"_s);
            c->setHost(qobject_cast<brulion::EditorHost *>(guard->m_editorHost.data()));
            c->setActive(guard->m_daysActive);
            c->setAllowNewBlockIds(guard->m_dayAllowNewBlockIds);
            c->setReadOnly(guard->m_dayReadOnly);
            if (!guard->m_dayPalette.isEmpty())
                c->setPalette(guard->m_dayPalette);
            if (!guard->m_dayLocale.isEmpty())
                c->setLocale(guard->m_dayLocale);
            c->setNow(guard->m_dayNow);
            PageController *page = c.get();
            QObject::connect(page, &PageController::foldStateChanged, guard.data(), [guard, page] {
                if (!guard || !guard->m_journal)
                    return;
                const auto it = guard->m_journal->days.find(page);
                if (it != guard->m_journal->days.end() && it->second.foldsRestored && page->loaded() && !page->loading())
                    Q_EMIT guard->dayFoldStateChanged(page);
            });
            QObject::connect(page, &PageController::externallyChanged, guard.data(), [guard, page](bool merged) {
                if (guard)
                    Q_EMIT guard->dayExternallyChanged(page, merged);
            });
            QObject::connect(page, &PageController::loadingChanged, guard.data(), [guard, page] {
                if (guard && page->loaded() && !page->loading())
                    Q_EMIT guard->dayLoaded(page);
            });
            c->setPath(info.path);
            return c;
        };
        // Its page: the page's signals (through the day written in), looks and hosted rows.
        j.setUpPage = [guard](int, const at::Entity<PageView> &page, PageController *controller) {
            if (!guard || !guard->m_journal)
                return;
            Journal::Day &day = guard->m_journal->days[controller];
            day.page = page;
            const PageHandle handle{page, controller->blocks(), true};
            guard->connectPageSignals(handle);
            guard->connectHoverSignals(handle);
            guard->connectEditing(handle);
            guard->connectTablesOf(handle);
            guard->styleOne(handle);
            guard->styleTablesOf(handle);
            guard->extrasOf(handle);
            guard->touchSelectionOf(handle);
            day.host = std::make_unique<ForeignRowHost>(guard.data(), runtime().app, page, controller->blocks());
            day.host->setDelegate(guard->m_delegate);
            QObject::connect(controller, &PageController::zoomChanged, guard.data(), [guard, controller] {
                if (guard)
                    guard->updateDayHeader(controller);
            });
            guard->updateDayHeader(controller);
            Q_EMIT guard->dayMade(controller);
        };
        j.onDayReleased = [guard](int, PageController *controller) {
            if (!guard || !guard->m_journal)
                return;
            const auto it = guard->m_journal->days.find(controller);
            if (it == guard->m_journal->days.end())
                return;
            it->second.host.reset();
            if (it->second.header)
                it->second.header->deleteLater();
            guard->m_journal->days.erase(it);
            Q_EMIT guard->dayReleased(controller);
        };
        j.formatDate = [guard](const QString &date) {
            const bool english = guard && guard->m_dayLocale == u"en";
            return brulion::dates::longDate(QDate::fromString(date, Qt::ISODate),
                                            english ? brulion::dates::UiLocale::English
                                                    : brulion::dates::UiLocale::Polish);
        };
        j.onOpenAsPage = [guard](const QString &date, bool source) {
            if (guard)
                Q_EMIT guard->openAsPageRequested(date, source);
        };
        j.onBeginRequested = [guard](int day) {
            if (!guard)
                return false;
            Q_EMIT guard->beginRequested(day);
            return true;
        };
        j.onActiveDayChanged = [guard](int day) {
            if (guard)
                guard->journalActiveChanged(day);
        };
    });
    applyJournal();
    applyJournalLook();
    applyDayOptions();
    applyStyle();
    applyFind();
    attach(r.app, at::AnyView{m_journal->entity.id(), &at::renderView<JournalView>});
    m_attached = true;
#endif
}

void AtramentView::applyJournal()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    using namespace brulion::editor;
    auto *selection = qobject_cast<StreamSelection *>(m_streamSelection.data());
    auto *find = qobject_cast<StreamFind *>(m_streamFind.data());
    m_journal->entity.update(runtime().app, [&](atr::JournalView &j, at::Context<atr::JournalView> &cx) {
        j.setStreamSelection(selection);
        j.setStreamFind(find);
        cx.notify();
    });
    tellStreamEdges();
#endif
}

void AtramentView::applyJournalLook()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    using namespace brulion::editor::atr;
    const QVariantMap &l = m_journalLook;
    auto colour = [&l](const char *key, at::Color fallback) {
        const QVariant v = l.value(QString::fromLatin1(key));
        const QColor c = v.metaType() == QMetaType::fromType<QColor>() ? v.value<QColor>() : QColor::fromString(v.toString());
        return c.isValid() ? atColor(c) : fallback;
    };
    auto number = [&l](const char *key, float fallback) {
        bool ok = false;
        const double d = l.value(QString::fromLatin1(key)).toDouble(&ok);
        return ok && d > 0 ? float(d) : fallback;
    };
    auto word = [&l](const char *key, const QString &fallback) {
        const QString w = l.value(QString::fromLatin1(key)).toString();
        return w.isEmpty() ? fallback : w;
    };
    JournalView::Look look;
    look.text = colour("text", look.text);
    look.muted = colour("muted", look.muted);
    look.faint = colour("faint", look.faint);
    look.accentText = colour("accentText", look.accentText);
    look.accent = colour("accent", look.accent);
    look.onAccent = colour("onAccent", look.onAccent);
    look.neutralBadge = colour("neutralBadge", look.neutralBadge);
    look.separator = colour("separator", look.separator);
    look.hover = colour("hover", look.hover);
    look.pressed = colour("pressed", look.pressed);
    look.background = colour("background", look.background);
    look.dateFamily = std::u16string(u16View(l.value(u"dateFamily"_s).toString()));
    look.uiFamily = std::u16string(u16View(l.value(u"uiFamily"_s).toString()));
    look.dateSize = number("dateSize", look.dateSize);
    look.todaySize = number("todaySize", look.todaySize);
    look.compactDateSize = number("compactDateSize", look.compactDateSize);
    look.captionSize = number("captionSize", look.captionSize);
    look.buttonSize = number("buttonSize", look.buttonSize);
    look.badgeSize = number("badgeSize", look.badgeSize);
    look.lineHeightTight = number("lineHeightTight", look.lineHeightTight);
    look.gap = number("gap", look.gap);
    look.space2 = number("space2", look.space2);
    look.space3 = number("space3", look.space3);
    look.touchTarget = number("touchTarget", look.touchTarget);
    look.radius = number("radius", look.radius);
    look.iconSize = number("iconSize", look.iconSize);
    look.compact = l.value(u"compact"_s).toBool();
    look.touch = l.value(u"touch"_s).toBool();
    look.readOnly = l.value(u"readOnly"_s).toBool();
    look.todayWord = word("today", look.todayWord);
    look.yesterdayWord = word("yesterday", look.yesterdayWord);
    look.openAsPage = word("openAsPage", look.openAsPage);
    look.promptToday = word("promptToday", look.promptToday);
    look.promptDay = word("promptDay", look.promptDay);
    look.readOnlyToday = word("readOnlyToday", look.readOnlyToday);
    look.readOnlyDay = word("readOnlyDay", look.readOnlyDay);
    m_journal->entity.update(runtime().app,
                             [&](JournalView &j, at::Context<JournalView> &cx) { j.setLook(look, cx); });
#endif
}

void AtramentView::applyDayOptions()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    using namespace brulion::editor;
    // Every day made follows the options (ContentJournalDay's bindings).
    for (auto &[object, day] : m_journal->days) {
        auto *c = qobject_cast<PageController *>(object);
        if (!c)
            continue;
        c->setActive(m_daysActive);
        c->setAllowNewBlockIds(m_dayAllowNewBlockIds);
        c->setReadOnly(m_dayReadOnly);
        if (!m_dayPalette.isEmpty())
            c->setPalette(m_dayPalette);
        if (!m_dayLocale.isEmpty())
            c->setLocale(m_dayLocale);
        c->setNow(m_dayNow);
    }
    m_journal->entity.update(runtime().app, [this](atr::JournalView &j, at::Context<atr::JournalView> &cx) {
        j.setJournalTemplate(m_journalTemplate);
        j.setBottomInset(float(m_bottomInset));
        cx.notify();
    });
#endif
}

void AtramentView::activatePage(std::uint64_t pageId)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal || (hasPage() && m_page->entity.id() == pageId))
        return;
    m_journal->entity.update(runtime().app, [pageId](brulion::editor::atr::JournalView &j,
                                                     at::Context<brulion::editor::atr::JournalView> &cx) {
        j.activateDay(j.dayOfPage(pageId), cx);
    });
#else
    Q_UNUSED(pageId)
#endif
}

void AtramentView::journalActiveChanged(int day)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal || !m_page)
        return;
    const brulion::editor::atr::JournalView &j = m_journal->entity.read(runtime().app);
    const at::Entity<brulion::editor::atr::PageView> *page = j.page(day);
    m_page->entity = page ? *page : at::Entity<brulion::editor::atr::PageView>{};
    // The touch selection mode and Vim are the active day's.
    applyTouchSelection();
    applyExtras();
    Q_EMIT activeControllerChanged();
    Q_EMIT editStateChanged();
#else
    Q_UNUSED(day)
#endif
}

QObject *AtramentView::activeController() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        return m_journal->entity.read(runtime().app).controller(m_journal->entity.read(runtime().app).activeDay());
#endif
    return nullptr;
}

int AtramentView::activeDay() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        return m_journal->entity.read(runtime().app).activeDay();
#endif
    return -1;
}

int AtramentView::dayCount() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        return m_journal->entity.read(runtime().app).dayCount();
#endif
    return 0;
}

int AtramentView::madeDayCount() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        return int(m_journal->entity.read(runtime().app).madeDays().size());
#endif
    return 0;
}

QObject *AtramentView::dayController(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        return m_journal->entity.read(runtime().app).controller(row);
#endif
    Q_UNUSED(row)
    return nullptr;
}

int AtramentView::dayOfController(QObject *controller) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        return m_journal->entity.read(runtime().app).dayOfController(
                qobject_cast<brulion::editor::PageController *>(controller));
#endif
    Q_UNUSED(controller)
    return -1;
}

void AtramentView::scrollToDay(int row, bool contain)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        m_journal->entity.update(runtime().app, [&](brulion::editor::atr::JournalView &j,
                                                    at::Context<brulion::editor::atr::JournalView> &cx) {
            j.scrollToDay(row, contain ? at::ListPosition::Nearest : at::ListPosition::Start, cx);
        });
#else
    Q_UNUSED(row)
    Q_UNUSED(contain)
#endif
}

bool AtramentView::scrollToDate(const QString &date)
{
    bool done = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        m_journal->entity.update(runtime().app, [&](brulion::editor::atr::JournalView &j,
                                                    at::Context<brulion::editor::atr::JournalView> &cx) {
            done = j.scrollToDate(date, cx);
        });
#else
    Q_UNUSED(date)
#endif
    return done;
}

void AtramentView::scrollBy(qreal dy)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        m_journal->entity.update(runtime().app, [dy](brulion::editor::atr::JournalView &j,
                                                     at::Context<brulion::editor::atr::JournalView> &cx) {
            if (j.handle().scrollBy(float(dy)))
                cx.notify();
        });
#else
    Q_UNUSED(dy)
#endif
}

void AtramentView::activateDay(int row)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        m_journal->entity.update(runtime().app, [row](brulion::editor::atr::JournalView &j,
                                                      at::Context<brulion::editor::atr::JournalView> &cx) {
            j.scrollToDay(row, at::ListPosition::Nearest, cx);
            j.activateDay(row, cx);
        });
#else
    Q_UNUSED(row)
#endif
}

void AtramentView::activateController(QObject *controller)
{
    const int day = dayOfController(controller);
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal && day >= 0)
        m_journal->entity.update(runtime().app, [day](brulion::editor::atr::JournalView &j,
                                                      at::Context<brulion::editor::atr::JournalView> &cx) {
            j.activateDay(day, cx);
        });
#else
    Q_UNUSED(day)
#endif
}

bool AtramentView::beginDay(int row)
{
    bool done = false;
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        m_journal->entity.update(runtime().app, [&](brulion::editor::atr::JournalView &j,
                                                    at::Context<brulion::editor::atr::JournalView> &cx) {
            done = j.beginDay(row, cx);
        });
#else
    Q_UNUSED(row)
#endif
    return done;
}

QVariantMap AtramentView::viewState() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal) {
        const auto st = m_journal->entity.read(runtime().app).viewState();
        if (st.date.isEmpty())
            return {};
        return {{u"date"_s, st.date}, {u"offset"_s, int(std::lround(st.offset))}};
    }
#endif
    return {};
}

void AtramentView::restoreViewState(const QVariantMap &state)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    brulion::editor::atr::JournalView::ViewState st;
    st.date = state.value(u"date"_s).toString();
    st.offset = float(state.value(u"offset"_s).toDouble());
    m_journal->entity.update(runtime().app, [&](brulion::editor::atr::JournalView &j,
                                                at::Context<brulion::editor::atr::JournalView> &cx) {
        j.restoreViewState(st, cx);
    });
#else
    Q_UNUSED(state)
#endif
}

qreal AtramentView::dayTop(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        if (const auto b = m_journal->entity.read(runtime().app).dayBounds(row))
            return b->top();
#endif
    Q_UNUSED(row)
    return -1;
}

qreal AtramentView::dayHeight(int row) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        if (const auto b = m_journal->entity.read(runtime().app).dayBounds(row))
            return b->height();
#endif
    Q_UNUSED(row)
    return -1;
}

qreal AtramentView::dayRowTop(int row, int block) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        if (const auto b = m_journal->entity.read(runtime().app).rowBounds(row, block))
            return b->top();
#endif
    Q_UNUSED(row)
    Q_UNUSED(block)
    return -1;
}

qreal AtramentView::dayRowHeight(int row, int block) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal)
        if (const auto b = m_journal->entity.read(runtime().app).rowBounds(row, block))
            return b->height();
#endif
    Q_UNUSED(row)
    Q_UNUSED(block)
    return -1;
}

QPointF AtramentView::dayPoint(int row, const QString &part) const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (m_journal) {
        const brulion::editor::atr::JournalView &j = m_journal->entity.read(runtime().app);
        const std::optional<at::Point> p = part == u"date"   ? j.datePoint(row)
                                           : part == u"open" ? j.openAsPagePoint(row)
                                                             : j.promptPoint(row);
        if (p)
            return {p->x, p->y};
    }
#endif
    Q_UNUSED(row)
    Q_UNUSED(part)
    return {-1, -1};
}

void AtramentView::followRename(const QString &oldPath, const QString &newPath)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    for (auto &[object, day] : m_journal->days)
        if (auto *c = qobject_cast<brulion::editor::PageController *>(object); c && c->path() == oldPath)
            c->followRename(oldPath, newPath);
#else
    Q_UNUSED(oldPath)
    Q_UNUSED(newPath)
#endif
}

void AtramentView::refreshModes()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    for (auto &[object, day] : m_journal->days)
        if (auto *c = qobject_cast<brulion::editor::PageController *>(object))
            c->refreshMode();
#endif
}

void AtramentView::markFoldsRestored(QObject *controller)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    const auto it = m_journal->days.find(controller);
    if (it != m_journal->days.end())
        it->second.foldsRestored = true;
#else
    Q_UNUSED(controller)
#endif
}

void AtramentView::placeStreamEdges()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    using brulion::editor::atr::ForeignFrame;
    const std::vector<ForeignFrame> frames = m_journal->entity.read(runtime().app).edgeFrames();
    for (const ForeignFrame &f : frames) {
        QQuickItem *item = f.kind == ForeignFrame::Kind::Header   ? m_streamHeader.data()
                           : f.kind == ForeignFrame::Kind::Footer ? m_streamFooter.data()
                                                                  : nullptr;
        if (!item)
            continue;
        item->setPosition(QPointF(f.bounds.left(), f.bounds.top()));
        item->setVisible(true);
    }
    tellStreamEdges();
#endif
}

void AtramentView::tellStreamEdges()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (!m_journal)
        return;
    const float header = m_streamHeader ? float(m_streamHeader->height()) : 0.0f;
    const float footer = m_streamFooter ? float(m_streamFooter->height()) : 0.0f;
    m_journal->entity.update(runtime().app, [&](brulion::editor::atr::JournalView &j,
                                                at::Context<brulion::editor::atr::JournalView> &cx) {
        bool changed = j.setHeaderHeight(header);
        changed = j.setFooterHeight(footer) || changed;
        if (changed)
            cx.notify();
    });
#endif
}

bool AtramentView::childMouseEventFilter(QQuickItem *child, QEvent *event)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    // A press on a day's hosted row (its properties, a row Atrament does not draw): that day is the
    // one written in before the delegate, which acts through the stream's editor, sees it.
    if (m_journal
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin
            || event->type() == QEvent::KeyPress)) {
        for (auto &[controller, day] : m_journal->days) {
            if (day.host && day.host->hosts(child)) {
                activatePage(day.page.id());
                break;
            }
        }
    }
#endif
    return AtramentItem::childMouseEventFilter(child, event);
}

// --- "page": tables drawn and edited by Atrament (editor/atrament/tablerows.cpp) -----------------

void AtramentView::applyTableStyle()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    forEachPage([this](const PageHandle &page) { styleTablesOf(page); });
#endif
}

void AtramentView::styleTablesOf(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor::atr;
    handle.entity.update(runtime().app, [this](PageView &page, at::Context<PageView> &cx) {
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
#else
    Q_UNUSED(handle)
#endif
}

void AtramentView::connectTables()
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    forEachPage([this](const PageHandle &page) { connectTablesOf(page); });
#endif
}

void AtramentView::connectTablesOf(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor::atr;
    QPointer<AtramentView> guard = this;
    const std::uint64_t pid = handle.day ? handle.entity.id() : 0;
    auto act = [guard, pid] {
        if (guard && pid)
            guard->activatePage(pid);
    };
    QPointer<brulion::editor::BlockListModel> pageModel = handle.model;
    handle.entity.update(runtime().app, [guard, act, pageModel](PageView &page, at::Context<PageView> &) {
        page.onCellEditRequest = [guard, act](int row, int tableRow, int column, int cursor, const at::Modifiers &modifiers) {
            if (!guard)
                return false;
            act();
            Q_EMIT guard->tableCellTapped(row, tableRow, column, cursor, qtModifiers(modifiers));
            return true;
        };
        page.onCellLink = [guard, act](int row, int tableRow, int column, const QString &href, at::Point p) {
            act();
            if (guard)
                Q_EMIT guard->tableLinkClicked(row, tableRow, column, href, p.x, p.y);
        };
        page.onCellMenu = [guard, act, pageModel](int row, int tableRow, int column, at::Point p) {
            if (!guard)
                return;
            act();
            // The table's size and the column's alignment, for the menu's Row and Column items.
            brulion::editor::BlockListModel *model = pageModel.data();
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
        page.onCellLinkMenu = [guard, act](int row, int tableRow, int column, const QString &href, at::Point p) {
            act();
            if (guard)
                Q_EMIT guard->tableLinkMenuRequested(row, tableRow, column, href, p.x, p.y);
        };
    });
#else
    Q_UNUSED(handle)
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
    if (hasPage())
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
    forEachPage([this](const PageHandle &page) { extrasOf(page); });
#endif
}

void AtramentView::extrasOf(const PageHandle &handle)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    using namespace brulion::editor;
    auto *spell = qobject_cast<SpellCheck *>(m_spellCheck.data());
    auto *vim = qobject_cast<VimSession *>(m_vimSession.data());
    // (A day of the journal: Vim is the editor's, which follows the active day.)
    const bool withVim = !handle.day || (hasPage() && m_page->entity.id() == handle.entity.id());
    handle.entity.update(runtime().app, [&](atr::PageView &page, at::Context<atr::PageView> &cx) {
        page.setSpellCheck(spell, atColor(m_spellColor), cx);
        page.setVim(withVim ? vim : nullptr, cx);
    });
#else
    Q_UNUSED(handle)
#endif
}

int AtramentView::editSelectionStart() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (hasPage()) {
        const auto st = m_page->entity.read(runtime().app).editState();
        return std::min(st.anchor, st.cursor);
    }
#endif
    return 0;
}

int AtramentView::editSelectionEnd() const
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (hasPage()) {
        const auto st = m_page->entity.read(runtime().app).editState();
        return std::max(st.anchor, st.cursor);
    }
#endif
    return 0;
}

bool AtramentView::event(QEvent *e)
{
#ifdef BRULION_HAS_ATRAMENT_EDITOR
    if (e->type() == QEvent::ShortcutOverride && hasPage()) {
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
