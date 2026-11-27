// PageView's block selection (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 3
// "Selection"): the keys while blocks are selected and no block is edited — the page keeps the
// keyboard, as the QML editor's list does (BlockEditor.handleSelectionKeyCode) — the band under the
// selected rows (BlockPartSelectionBand's geometry, also under foreign rows: their delegates draw no
// band of their own here) and the touch selection mode's grips at its ends (BlockSelectionGrip).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

#include <QCoreApplication>

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// BlockSelectionGrip: a round grip of 18 px in a hit area of Theme.touchTarget, its centre 9 px in
// from the band's corner (and 12 px in from the window's edge).
constexpr float kGripArea = 44;
constexpr float kGripDot = 18;
constexpr float kGripInset = 9;
constexpr float kGripEdge = 12;
// Theme.space2: the band reaches this far past the text column on the right (and a document's on
// the left).
constexpr float kBandMargin = 8;

} // namespace

std::u16string_view PageView::commandName(SelectionCommand command)
{
    switch (command) {
    case SelectionCommand::Copy: return u"copy";
    case SelectionCommand::Cut: return u"cut";
    case SelectionCommand::Delete: return u"delete";
    case SelectionCommand::Paste: return u"paste";
    case SelectionCommand::Escape: return u"escape";
    case SelectionCommand::Back: return u"back";
    case SelectionCommand::Edit: return u"edit";
    case SelectionCommand::TaskMenu: return u"taskMenu";
    case SelectionCommand::SourceMode: return u"sourceMode";
    case SelectionCommand::CopyReference: return u"copyReference";
    case SelectionCommand::Indent: return u"indent";
    case SelectionCommand::Outdent: return u"outdent";
    case SelectionCommand::MoveUp: return u"moveUp";
    case SelectionCommand::MoveDown: return u"moveDown";
    case SelectionCommand::CycleTasks: return u"cycleTasks";
    case SelectionCommand::ExtendUp: return u"extendUp";
    case SelectionCommand::ExtendDown: return u"extendDown";
    case SelectionCommand::ExtendPastTop: return u"extendPastTop";
    case SelectionCommand::ExtendPastBottom: return u"extendPastBottom";
    case SelectionCommand::Extended: return u"extended";
    }
    return {};
}

bool PageView::selectionKey(const at::Keystroke &k, at::Context<PageView> &cx)
{
    PageController *ctl = m_controller.data();
    // (While a block is edited its keys are the field's and the editor's: editorKey.)
    if (!ctl || !m_model || ctl->editingRow() >= 0 || ctl->sourceMode())
        return false;
    const at::Modifiers &m = k.modifiers;
    const bool ctrl = m.control || m.platform;
    const bool shift = m.shift;
    const bool alt = m.alt;
    const int row = ctl->selectedRow();
    // Blocks from one end to another, or picked one by one (a block selected alone — with the
    // children it brings along — keeps the block's own keys: Ctrl+Enter changes it, not them).
    const bool range = row >= 0
                       && ((ctl->selectionAnchorRow() >= 0 && ctl->selectionAnchorRow() != row)
                           || ctl->selectionPicked());
    auto host = [&](SelectionCommand c) { return onSelectionCommand && onSelectionCommand(c, row); };
    auto reveal = [&](int r) {
        if (r >= 0)
            revealRow(r, cx);
    };

    if (ctrl) {
        if (k.key == u"z" || k.key == u"y") {
            const bool redo = k.key == u"y" || shift;
            if (onUndo)
                onUndo(redo);
            else if (redo)
                ctl->redo();
            else
                ctl->undo();
            return true;
        }
        if (k.key == u"e" && !shift && !alt)
            return host(SelectionCommand::SourceMode);
        if (k.key == u"." && !alt) {
            if (row >= 0)
                ctl->toggleFold(row);
            return true;
        }
        if (k.key == u"enter") {
            if (row >= 0 && shift) {
                host(SelectionCommand::TaskMenu);
            } else if (row >= 0 && !host(SelectionCommand::CycleTasks)) {
                if (range)
                    ctl->cycleSelectionTasks();
                else
                    ctl->cycleTask(row);
            }
            return true;
        }
        if (k.key == u"c" && shift)
            return host(SelectionCommand::CopyReference);
        if ((k.key == u"c" || k.key == u"insert") && !shift) {
            if (row < 0)
                return false;
            if (!host(SelectionCommand::Copy))
                ctl->copySelection(false);
            return true;
        }
        if (k.key == u"x" && !shift) {
            if (row < 0)
                return false;
            if (!host(SelectionCommand::Cut))
                reveal(ctl->cutSelection().value(QStringLiteral("row"), -1).toInt());
            return true;
        }
        if (k.key == u"v" && !shift) {
            if (row < 0)
                return false;
            if (!host(SelectionCommand::Paste))
                reveal(ctl->pasteOverSelection().value(QStringLiteral("row"), -1).toInt());
            return true;
        }
        if (k.key == u"a" && !shift) {
            if (onSelectAllBlocks)
                onSelectAllBlocks();
            else
                ctl->selectAll();
            return true;
        }
        return false;
    }
    if (row < 0) {
        // Nothing selected: ↓ selects the first block, ↑ the last.
        if (k.key == u"down" || k.key == u"up") {
            const int first = k.key == u"down" ? ctl->nextVisibleRow(-1) : ctl->lastVisibleRow();
            if (first >= 0) {
                ctl->setSelectedRow(first);
                reveal(first);
            }
            return true;
        }
        return false;
    }
    if (shift && !alt) {
        if (k.key == u"up" || k.key == u"down") {
            const int step = k.key == u"down" ? 1 : -1;
            if (host(step > 0 ? SelectionCommand::ExtendDown : SelectionCommand::ExtendUp))
                return true;
            if (!ctl->extendSelection(step)
                && host(step > 0 ? SelectionCommand::ExtendPastBottom : SelectionCommand::ExtendPastTop))
                return true;
            host(SelectionCommand::Extended);
            reveal(ctl->selectedRow());
            return true;
        }
        if (k.key == u"insert") {
            if (!host(SelectionCommand::Paste))
                reveal(ctl->pasteOverSelection().value(QStringLiteral("row"), -1).toInt());
            return true;
        }
        if (k.key == u"delete") {
            if (!host(SelectionCommand::Cut))
                reveal(ctl->cutSelection().value(QStringLiteral("row"), -1).toInt());
            return true;
        }
    }
    if (alt) {
        if (k.key == u"up" || k.key == u"down") {
            const bool up = k.key == u"up";
            if (!host(up ? SelectionCommand::MoveUp : SelectionCommand::MoveDown)) {
                if (!range && up)
                    ctl->moveUp(row);
                else if (!range)
                    ctl->moveDown(row);
                else if (up)
                    ctl->moveSelectionUp();
                else
                    ctl->moveSelectionDown();
            }
            reveal(ctl->selectedRow());
            return true;
        }
        return false;
    }
    if (k.key == u"up" || k.key == u"down") {
        const int next = k.key == u"up" ? ctl->previousVisibleRow(row) : ctl->nextVisibleRow(row);
        if (next >= 0) {
            ctl->setSelectedRow(next);
            reveal(next);
        }
        return true;
    }
    if (k.key == u"enter") {
        if (!host(SelectionCommand::Edit))
            ctl->startEditing(row, -1);
        return true;
    }
    if (k.key == u"delete" || k.key == u"backspace") {
        if (!host(SelectionCommand::Delete))
            reveal(ctl->deleteSelection());
        return true;
    }
    if (k.key == u"tab") {
        if (host(shift ? SelectionCommand::Outdent : SelectionCommand::Indent))
            return true;
        if (shift && range)
            ctl->outdentSelection();
        else if (shift)
            ctl->outdent(row);
        else if (range)
            ctl->indentSelection();
        else
            ctl->indent(row);
        return true;
    }
    if (k.key == u"escape") {
        if (!host(SelectionCommand::Escape))
            ctl->clearSelection();
        return true;
    }
    if (k.key == u"back")
        return host(SelectionCommand::Back);
    return false;
}

at::AnyElement PageView::keyLayer()
{
    // (Embedded: only the stream's active day takes the keys nothing else took.)
    if (!m_keysActive)
        return at::div().absolute().size(0);
    // (A child of the page's root, which pushes no dispatch node of its own: its listener is the
    // root node's, on the path of every key — also while nothing in the window has the focus.)
    const at::EntityId id = m_selfId;
    return at::canvas([id](at::Rect, at::Window &window, at::App &) {
               window.onKeyEvent([id](const at::PlatformInput &input, at::DispatchPhase phase, at::Window &w,
                                      at::App &app) {
                   const auto *down = std::get_if<at::KeyDownEvent>(&input);
                   if (!down || phase != at::DispatchPhase::Bubble)
                       return;
                   bool handled = false;
                   app.updateEntity<PageView>(id, [&](PageView &v, at::Context<PageView> &c) {
                       handled = v.cellRangeKey(down->keystroke, c) || v.selectionKey(down->keystroke, c);
                   });
                   if (handled)
                       w.stopPropagation();
               });
           })
            .absolute()
            .size(0);
}

void PageView::setTouchSelection(bool grips, bool picking, at::Context<PageView> &cx)
{
    if (grips == m_touchGrips && picking == m_touchPicking)
        return;
    m_touchGrips = grips;
    m_touchPicking = picking;
    cx.notify();
}

void PageView::setDragLook(at::Color grip, at::Color gripBorder, at::Color dropLine, at::Context<PageView> &cx)
{
    m_gripColor = grip;
    m_gripBorder = gripBorder;
    m_dropColor = dropLine;
    cx.notify();
}

std::optional<at::AnyElement> PageView::selectionBand(int row)
{
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return std::nullopt;
    const QModelIndex index = m_model->index(row, 0);
    const QString edge = index.data(BlockListModel::SelectionEdgeRole).toString();
    if (edge.isEmpty())
        return std::nullopt;
    const PageMetrics &m = m_metrics;
    const bool start = edge == u"single" || edge == u"first";
    const bool end = edge == u"single" || edge == u"last";
    const bool single = edge == u"single";
    const int depth = std::max(0, index.data(BlockListModel::DepthRole).toInt());
    const bool listItem = index.data(BlockListModel::KindRole).toString() == u"listItem";
    // BlockDelegate's bandX: a block alone from its own level, a range from the column's (one
    // straight band over every level); to the text column's right edge and a little past it.
    float left = 0;
    if (m.documentMode)
        left = m.contentX + m.gutter + (single && listItem ? float(depth) * m.listIndent : 0) - kBandMargin;
    else
        left = m.contentX + m.gutter + (single ? float(depth) * m.indentStep : 0) - m.bulletColumn - 2;
    const float columnRight = m.contentWidth > 0 ? m.contentX + m.contentWidth : m_viewWidth;
    const float rowRight = m_viewWidth > 0 ? std::min(m_viewWidth, columnRight) : columnRight;
    const float right = rowRight + kBandMargin;
    const float r = m_selectionLook.radius;
    at::Div band = at::div()
                           .absolute()
                           .left(left)
                           .right(-kBandMargin)
                           .top(start ? 1.0f : 0.0f)
                           .bottom(end ? 1.0f : 0.0f)
                           .bg(m_selectionLook.band)
                           .roundedT(start ? r : 0)
                           .roundedB(end ? r : 0);
    if (single)
        std::move(band).border(1).borderColor(m_selectionLook.ring.fadedBy(0.35f));
    else
        std::move(band).borderL(2).borderColor(m_selectionLook.ring.fadedBy(0.6f));
    // The touch selection mode's grips: at the band's top left and bottom right (inside the
    // window: a document's band starts near the screen's edge on a phone).
    if (m_touchGrips && m_controller && m_controller->selectionContiguous()) {
        if (start) {
            const float centre = std::max(left, kGripEdge) - left;
            std::move(band).child(selectionGrip(true, centre - kGripArea / 2, 0));
        }
        if (end) {
            const float centre = m_viewWidth > 0 ? std::min(right, m_viewWidth - kGripEdge) : right;
            std::move(band).child(selectionGrip(false, 0, right - centre - kGripArea / 2));
        }
    }
    return at::AnyElement(std::move(band));
}

at::AnyElement PageView::selectionGrip(bool atStart, float left, float right)
{
    if (m_gripNames[0].isEmpty()) {
        m_gripNames[0] = QCoreApplication::translate("BlockDelegate", "Start of the selection");
        m_gripNames[1] = QCoreApplication::translate("BlockDelegate", "End of the selection");
    }
    const at::EntityId self = m_selfId;
    at::Div grip = at::div()
                           .id(atStart ? u"gripStart" : u"gripEnd")
                           .absolute()
                           .size(kGripArea)
                           .role(at::Role::Button)
                           .ariaLabel(u16View(m_gripNames[atStart ? 0 : 1]))
                           // The finger moves that end of the selection from its press on: no
                           // scroll, no long press (also over a hosted row's delegate).
                           .claimTouchDrag()
                           .onMouseDown(at::MouseButton::Left,
                                        [self, atStart](const at::MouseDownEvent &e, at::Window &w, at::App &a) {
                                            a.updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &c) {
                                                if (!v.m_controller || v.m_controller->selectionCount() == 0)
                                                    return;
                                                const int fixed = atStart ? v.m_controller->selectionLastRow()
                                                                          : v.m_controller->selectionFirstRow();
                                                v.m_drag.pointer = e.pointer;
                                                v.beginDrag(DragKind::Extend, fixed, e.position, true, c);
                                            });
                                            w.stopPropagation(); // not the row's press under it
                                        })
                           .child(at::div()
                                          .absolute()
                                          .left((kGripArea - kGripDot) / 2)
                                          .top((kGripArea - kGripDot) / 2)
                                          .size(kGripDot)
                                          .rounded(kGripDot / 2)
                                          .bg(m_gripColor)
                                          .border(2)
                                          .borderColor(m_gripBorder));
    if (atStart)
        std::move(grip).left(left).top(kGripInset - kGripArea / 2);
    else
        std::move(grip).right(right).bottom(kGripInset - kGripArea / 2);
    return grip;
}

std::optional<at::Rect> PageView::selectionBounds() const
{
    if (!m_controller || m_controller->selectionCount() == 0)
        return std::nullopt;
    const std::optional<at::Rect> view = listViewport();
    const std::optional<at::Rect> first = rowBounds(m_controller->selectionFirstRow());
    const std::optional<at::Rect> last = rowBounds(m_controller->selectionLastRow());
    if (!view || !first || !last)
        return std::nullopt;
    const float top = std::max(view->top(), first->top());
    const float bottom = std::min(view->bottom(), last->bottom());
    if (bottom <= top)
        return std::nullopt;
    return at::Rect::fromXYWH(first->left(), top, first->width(), bottom - top);
}

} // namespace brulion::editor::atr
