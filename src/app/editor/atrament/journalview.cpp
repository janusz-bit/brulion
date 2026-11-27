// JournalView (journalview.h): the journal stream as one Atrament list — the days' sections, the
// days made near the view and released far from it, the active day, ↑/↓ across days, scrolling to
// a day or a date and back to a view state.
#include "journalview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "pageicons.h"
#include "streamfind.h"
#include "streamselection.h"

#include <QDate>

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// An unmade day with a note: its heading and a few blocks; one without: its heading and prompt.
constexpr float kDayEstimate = 160;
constexpr float kEmptyDayEstimate = 110;

std::u16string key(const QString &date)
{
    return std::u16string(u16View(date));
}

} // namespace

struct JournalView::Day {
    std::unique_ptr<PageController> controller;
    at::Entity<PageView> page;
    std::vector<QMetaObject::Connection> connections;
    ~Day()
    {
        for (const QMetaObject::Connection &c : connections)
            QObject::disconnect(c);
        // The page first (it lets go of the controller's model), then the controller (it saves).
        page = {};
        controller.reset();
    }
};

JournalView::JournalView(at::Context<JournalView> &cx, QAbstractItemModel *days, PageMetrics metrics,
                         PagePalette palette)
    : m_days(days)
    , m_metrics(metrics)
    , m_palette(std::move(palette))
    , m_documentGutter(metrics.gutter)
{
    m_app = &cx.app();
    m_selfId = cx.entityId();
    if (days) {
        const QHash<int, QByteArray> names = days->roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it) {
            if (it.value() == "date")
                m_dateRole = it.key();
            else if (it.value() == "path")
                m_pathRole = it.key();
            else if (it.value() == "exists")
                m_existsRole = it.key();
            else if (it.value() == "isToday")
                m_todayRole = it.key();
        }
        at::App *app = m_app;
        const at::EntityId self = m_selfId;
        auto update = [app, self](auto f) {
            app->updateEntity<JournalView>(self, [&f](JournalView &v, at::Context<JournalView> &c) {
                f(v);
                c.notify();
            });
        };
        m_connections.push_back(QObject::connect(
                days, &QAbstractItemModel::rowsInserted, days, [update](const QModelIndex &parent, int first, int last) {
                    if (!parent.isValid())
                        update([first, last](JournalView &v) { v.daysInserted(first, last); });
                }));
        m_connections.push_back(QObject::connect(
                days, &QAbstractItemModel::rowsRemoved, days, [update](const QModelIndex &parent, int first, int last) {
                    if (!parent.isValid())
                        update([first, last](JournalView &v) { v.daysRemoved(first, last); });
                }));
        // Moves, resets and new layouts: the days are found again by their dates.
        m_connections.push_back(QObject::connect(days, &QAbstractItemModel::rowsMoved, days,
                                                 [update] { update([](JournalView &v) { v.rebuildDays(); }); }));
        m_connections.push_back(QObject::connect(days, &QAbstractItemModel::modelReset, days,
                                                 [update] { update([](JournalView &v) { v.rebuildDays(); }); }));
        m_connections.push_back(QObject::connect(days, &QAbstractItemModel::layoutChanged, days,
                                                 [update] { update([](JournalView &v) { v.rebuildDays(); }); }));
        m_connections.push_back(QObject::connect(
                days, &QAbstractItemModel::dataChanged, days,
                [update](const QModelIndex &first, const QModelIndex &last) {
                    update([f = first.row(), l = last.row()](JournalView &v) { v.daysChanged(f, l); });
                }));
    }
    rebuildDays();
}

JournalView::~JournalView()
{
    for (const QMetaObject::Connection &c : m_connections)
        QObject::disconnect(c);
    for (const QMetaObject::Connection &c : m_streamConnections)
        QObject::disconnect(c);
    for (int d = 0; d < dayCount(); ++d)
        releaseDay(d);
}

// --- The days -----------------------------------------------------------------------------------

JournalView::DayInfo JournalView::readDay(int row) const
{
    DayInfo info;
    if (!m_days)
        return info;
    const QModelIndex i = m_days->index(row, 0);
    info.date = i.data(m_dateRole).toString();
    info.path = i.data(m_pathRole).toString();
    info.exists = i.data(m_existsRole).toBool();
    info.isToday = i.data(m_todayRole).toBool();
    return info;
}

float JournalView::estimateFor(const DayInfo &info) const
{
    return info.exists ? kDayEstimate : kEmptyDayEstimate;
}

void JournalView::rebuildDays()
{
    // The view stays on its day (the rows around may come and go).
    const std::shared_ptr<at::ListHandle::State> st = m_handle.shared();
    QString anchorDate;
    if (st->anchorRow < m_slots.size())
        anchorDate = m_slots[st->anchorRow].info.date;
    const float anchorInto = st->anchorInto;
    const bool atTop = st->atTop;

    std::unordered_map<std::u16string, Slot> old;
    for (Slot &s : m_slots)
        old.emplace(key(s.info.date), std::move(s));
    m_slots.clear();
    const int n = m_days ? m_days->rowCount() : 0;
    m_slots.reserve(std::size_t(n));
    for (int r = 0; r < n; ++r) {
        const DayInfo info = readDay(r);
        Slot s;
        if (auto it = old.find(key(info.date)); it != old.end()) {
            s = std::move(it->second);
            old.erase(it);
        }
        const bool moved = s.day && s.day->controller && s.info.path != info.path;
        s.info = info;
        if (moved)
            s.day->controller->setPath(info.path);
        m_slots.push_back(std::move(s));
    }
    // Days no longer listed go.
    for (auto &[date, slot] : old) {
        if (slot.day) {
            if (onDayReleased)
                onDayReleased(-1, slot.day->controller.get());
            if (m_stream && slot.day->controller)
                m_stream->detach(slot.day->controller.get());
            if (m_find && slot.day->controller)
                m_find->detach(slot.day->controller.get());
        }
    }
    old.clear();
    m_heights.reset(std::size_t(n), kDayEstimate);
    for (int r = 0; r < n; ++r) {
        const auto known = m_known.find(key(m_slots[std::size_t(r)].info.date));
        if (known != m_known.end())
            m_heights.setHeight(std::size_t(r), known->second, true);
        else
            m_heights.setHeight(std::size_t(r), estimateFor(m_slots[std::size_t(r)].info), false);
    }
    if (!atTop && !anchorDate.isEmpty()) {
        const int row = exactRowOfDate(anchorDate);
        if (row >= 0)
            m_handle.scrollToAnchor(std::size_t(row), anchorInto);
    }
    // The active day by its page (its row may have moved; a day no longer listed is none).
    const int before = m_active;
    m_active = m_activeDayPage ? dayOfPage(m_activeDayPage) : -1;
    if (m_active < 0)
        m_activeDayPage = 0;
    if (before >= 0 && m_active < 0 && onActiveDayChanged)
        onActiveDayChanged(-1);
}

void JournalView::daysInserted(int first, int last)
{
    const int n = last - first + 1;
    if (n <= 0)
        return;
    std::vector<Slot> inserted(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        inserted[std::size_t(i)].info = readDay(first + i);
    m_slots.insert(m_slots.begin() + first, std::make_move_iterator(inserted.begin()),
                   std::make_move_iterator(inserted.end()));
    m_heights.insert(std::size_t(first), std::size_t(n), kDayEstimate);
    for (int r = first; r <= last; ++r) {
        const auto known = m_known.find(key(m_slots[std::size_t(r)].info.date));
        if (known != m_known.end())
            m_heights.setHeight(std::size_t(r), known->second, true);
        else
            m_heights.setHeight(std::size_t(r), estimateFor(m_slots[std::size_t(r)].info), false);
    }
    m_handle.rowsInserted(std::size_t(first), std::size_t(n));
    if (m_active >= first)
        m_active += n;
}

void JournalView::daysRemoved(int first, int last)
{
    const int n = last - first + 1;
    if (n <= 0 || first < 0 || last >= dayCount())
        return;
    for (int d = first; d <= last; ++d)
        releaseDay(d);
    m_slots.erase(m_slots.begin() + first, m_slots.begin() + last + 1);
    m_heights.remove(std::size_t(first), std::size_t(n));
    m_handle.rowsRemoved(std::size_t(first), std::size_t(n));
    if (m_active > last) {
        m_active -= n;
    } else if (m_active >= first) {
        m_active = -1;
        m_activeDayPage = 0;
        if (onActiveDayChanged)
            onActiveDayChanged(-1);
    }
}

void JournalView::daysChanged(int first, int last)
{
    for (int r = std::max(0, first); r <= last && r < dayCount(); ++r) {
        Slot &s = m_slots[std::size_t(r)];
        const DayInfo info = readDay(r);
        const bool moved = s.day && s.day->controller && s.info.path != info.path;
        s.info = info;
        if (moved)
            s.day->controller->setPath(info.path);
    }
}

JournalView::DayInfo JournalView::dayInfo(int day) const
{
    return day >= 0 && day < dayCount() ? m_slots[std::size_t(day)].info : DayInfo{};
}

PageController *JournalView::controller(int day) const
{
    if (day < 0 || day >= dayCount() || !m_slots[std::size_t(day)].day)
        return nullptr;
    return m_slots[std::size_t(day)].day->controller.get();
}

const at::Entity<PageView> *JournalView::page(int day) const
{
    if (day < 0 || day >= dayCount() || !m_slots[std::size_t(day)].day)
        return nullptr;
    return &m_slots[std::size_t(day)].day->page;
}

int JournalView::dayOfController(const PageController *controller) const
{
    if (!controller)
        return -1;
    for (std::size_t r = 0; r < m_slots.size(); ++r)
        if (m_slots[r].day && m_slots[r].day->controller.get() == controller)
            return int(r);
    return -1;
}

int JournalView::dayOfPage(at::EntityId page) const
{
    for (std::size_t r = 0; r < m_slots.size(); ++r)
        if (m_slots[r].day && m_slots[r].day->page.id() == page)
            return int(r);
    return -1;
}

std::vector<int> JournalView::madeDays() const
{
    std::vector<int> days;
    for (std::size_t r = 0; r < m_slots.size(); ++r)
        if (m_slots[r].day)
            days.push_back(int(r));
    return days;
}

PageMetrics JournalView::dayMetrics(int day) const
{
    PageMetrics m = m_metrics;
    const PageController *c = controller(day);
    m.documentMode = c && c->documentMode();
    if (m.documentMode)
        m.gutter = m_documentGutter;
    return m;
}

bool JournalView::makeDay(int day, at::Context<JournalView> &cx)
{
    if (day < 0 || day >= dayCount())
        return false;
    if (m_slots[std::size_t(day)].day)
        return true;
    if (!makeController)
        return false;
    std::unique_ptr<PageController> made = makeController(m_slots[std::size_t(day)].info);
    if (!made)
        return false;
    auto d = std::make_unique<Day>();
    d->controller = std::move(made);
    PageController *ctl = d->controller.get();
    d->page = cx.newEntity<PageView>(ctl->blocks(), dayMetrics(day), m_palette);
    d->page.update(cx.app(), [ctl](PageView &v, at::Context<PageView> &pc) { v.setController(ctl, pc); });
    m_slots[std::size_t(day)].day = std::move(d);
    // (The slot's day is read again below: setUpPage may change the model's rows.)
    const at::Entity<PageView> page = m_slots[std::size_t(day)].day->page;
    if (setUpPage)
        setUpPage(day, page, ctl);
    // The stream's own parts of the page: it scrolls the stream, ↑/↓ past its edges go on to the
    // days around, the tap strip under its blocks, the find bar's matches.
    at::App *app = m_app;
    const at::EntityId self = m_selfId;
    QPointer<PageController> guard = ctl;
    const bool keys = m_activeDayPage == page.id();
    page.update(*app, [&](PageView &v, at::Context<PageView> &pc) {
        PageView::PageEmbedding e;
        e.scrollBy = [app, self](float dy) {
            bool moved = false;
            app->updateEntity<JournalView>(self, [&](JournalView &j, at::Context<JournalView> &c) {
                moved = j.m_handle.scrollBy(dy);
                if (moved)
                    c.notify();
            });
            return moved;
        };
        e.scrolledSinceFrame = [app, self] {
            const JournalView *j = app->tryGet<JournalView>(self);
            if (!j)
                return 0.0f;
            const std::shared_ptr<at::ListHandle::State> st = j->m_handle.shared();
            return st->scroll.offset.y - st->lastY;
        };
        e.pastEdge = [app, self, guard](bool up, float x) {
            bool took = false;
            app->updateEntity<JournalView>(self, [&](JournalView &j, at::Context<JournalView> &c) {
                took = j.pastEdge(guard.data(), up, x, c);
            });
            return took;
        };
        v.setEmbedding(std::move(e), pc);
        v.setStyle(dayMetrics(day), m_palette);
        v.setKeysActive(keys, pc);
        // The selection's keys past the day's edges go on into the days around (StreamSelection),
        // when the host does not take them its way (BlockEditor.atramentSelectionCommand).
        std::function<bool(PageView::SelectionCommand, int)> hostCommand = v.onSelectionCommand;
        v.onSelectionCommand = [hostCommand, app, self, guard](PageView::SelectionCommand c, int row) {
            if (hostCommand && hostCommand(c, row))
                return true;
            const JournalView *j = app->tryGet<JournalView>(self);
            StreamSelection *stream = j ? j->m_stream.data() : nullptr;
            if (!stream || !guard)
                return false;
            switch (c) {
            case PageView::SelectionCommand::ExtendUp:
            case PageView::SelectionCommand::ExtendDown:
                if (!stream->active())
                    return false;
                stream->extendBy(guard.data(), c == PageView::SelectionCommand::ExtendDown ? 1 : -1);
                return true;
            case PageView::SelectionCommand::ExtendPastTop:
            case PageView::SelectionCommand::ExtendPastBottom:
                return stream->extendBy(guard.data(), c == PageView::SelectionCommand::ExtendPastBottom ? 1 : -1);
            default:
                return false;
            }
        };
        // Shift+↑/↓ past the edited block's text at the day's edge: into the day around.
        if (!v.onExtendSelection)
            v.onExtendSelection = [app, self, guard](int row, int step) {
                if (!guard)
                    return;
                if (guard->extendSelection(step))
                    return;
                const JournalView *j = app->tryGet<JournalView>(self);
                if (j && j->m_stream && j->m_stream->extendBy(guard.data(), step))
                    return;
                guard->selectRange(row, row);
            };
        v.setFooterHeight(m_look.space3);
        v.setFooterTapHeight(m_look.touch ? m_look.touchTarget : m_look.space3);
        if (!m_findState.query.isEmpty())
            v.setFind(m_findState.query, m_findState.matchCase, m_findState.matchDiacritics, m_findState.wholeWord,
                      m_findState.color, pc);
    });
    Day &made2 = *m_slots[std::size_t(day)].day;
    // The day becomes the one written in when it starts editing, selects blocks or zooms
    // (ContentJournalDay's rules); Ctrl+E opens it as a page in source mode; its note mode its geometry.
    auto activity = [app, self, guard] {
        app->updateEntity<JournalView>(self, [&](JournalView &j, at::Context<JournalView> &c) {
            j.dayActivity(guard.data(), c);
        });
    };
    made2.connections.push_back(QObject::connect(ctl, &PageController::editingRowChanged, ctl, activity));
    made2.connections.push_back(QObject::connect(ctl, &PageController::selectionChanged, ctl, activity));
    made2.connections.push_back(QObject::connect(ctl, &PageController::zoomChanged, ctl, activity));
    made2.connections.push_back(QObject::connect(ctl, &PageController::sourceModeChanged, ctl, [app, self, guard] {
        if (!guard || !guard->sourceMode())
            return;
        guard->setSourceMode(false);
        app->updateEntity<JournalView>(self, [&](JournalView &j, at::Context<JournalView> &) {
            const int d = j.dayOfController(guard.data());
            if (d >= 0 && j.onOpenAsPage)
                j.onOpenAsPage(j.m_slots[std::size_t(d)].info.date, true);
        });
    }));
    made2.connections.push_back(QObject::connect(ctl, &PageController::modeChanged, ctl, [app, self, guard] {
        app->updateEntity<JournalView>(self, [&](JournalView &j, at::Context<JournalView> &c) {
            const int d = j.dayOfController(guard.data());
            if (const at::Entity<PageView> *p = j.page(d))
                p->update(c.app(), [&](PageView &v, at::Context<PageView> &pc) {
                    v.setStyle(j.dayMetrics(d), j.m_palette);
                    pc.notify();
                });
            c.notify();
        });
    }));
    // Loaded, emptied, filled: the prompt comes or goes.
    made2.connections.push_back(QObject::connect(ctl, &PageController::loadingChanged, ctl, [app, self] {
        app->updateEntity<JournalView>(self, [](JournalView &, at::Context<JournalView> &c) { c.notify(); });
    }));
    made2.connections.push_back(
            QObject::connect(ctl->blocks(), &BlockListModel::visibleCountChanged, ctl, [app, self] {
                app->updateEntity<JournalView>(self, [](JournalView &, at::Context<JournalView> &c) { c.notify(); });
            }));
    if (m_stream)
        m_stream->attach(ctl);
    if (m_find)
        m_find->attach(ctl);
    // Today is the day written in until another one is.
    if (m_active < 0 && m_slots[std::size_t(day)].info.isToday)
        activateDay(day, cx);
    // (It may already be editing: a day made by a find's match.)
    dayActivity(ctl, cx);
    cx.notify();
    return true;
}

bool JournalView::keeps(int day) const
{
    const PageController *c = controller(day);
    if (!c)
        return false;
    if (day == m_active || c->editingRow() >= 0 || c->selectionCount() > 0 || c->zoomBlock() >= 0)
        return true;
    if (const at::Entity<PageView> *p = page(day)) {
        const PageView &v = p->read(*m_app);
        if (v.editedCell().row >= 0 || v.dragKind() != PageView::DragKind::None)
            return true;
    }
    return false;
}

void JournalView::releaseDay(int day)
{
    if (day < 0 || day >= dayCount() || !m_slots[std::size_t(day)].day)
        return;
    std::unique_ptr<Day> d = std::move(m_slots[std::size_t(day)].day);
    if (onDayReleased)
        onDayReleased(day, d->controller.get());
    if (m_stream && d->controller)
        m_stream->detach(d->controller.get());
    if (m_find && d->controller)
        m_find->detach(d->controller.get());
    if (m_activeDayPage == d->page.id()) {
        m_activeDayPage = 0;
        m_active = -1;
    }
    d.reset();
}

void JournalView::makeDaysNear(at::Window &window, at::Context<JournalView> &cx)
{
    const std::size_t n = m_heights.count();
    if (!makeController || n == 0)
        return;
    const std::shared_ptr<at::ListHandle::State> st = m_handle.shared();
    const float vh = st->scroll.viewport.height > 0 ? st->scroll.viewport.height : window.viewportSize().height;
    const float lead = st->leading;
    // The rows' coordinates of what the next frame shows: a row asked for, the bottom, the top, a
    // scroll since the last frame, else the anchor.
    float top = 0;
    if (st->scrollTo)
        top = m_heights.offsetOf(std::min(*st->scrollTo, n - 1));
    else if (st->atEnd)
        top = std::max(0.0f, m_heights.total() + st->trailing - vh);
    else if (std::abs(st->scroll.offset.y - st->lastY) > 0.01f)
        top = st->scroll.offset.y - lead;
    else if (!st->atTop)
        top = m_heights.offsetOf(std::min(st->anchorRow, n - 1)) + st->anchorInto;
    // A screen above and below what shows.
    const float from = std::max(0.0f, top - vh);
    const float to = top + 2 * vh;
    for (std::size_t r = m_heights.rowAt(from); r < n && m_heights.offsetOf(r) < to; ++r)
        makeDay(int(r), cx);
    if (st->scrollTo)
        makeDay(int(std::min(*st->scrollTo, n - 1)), cx);
}

void JournalView::releaseFarDays()
{
    m_releaseScheduled = false;
    // The places of days not drawn for a while are forgotten.
    for (auto it = m_parts.begin(); it != m_parts.end();) {
        if (it->second.frame + 2 < m_frame)
            it = m_parts.erase(it);
        else
            ++it;
    }
    const std::size_t n = m_heights.count();
    if (n == 0)
        return;
    const std::shared_ptr<at::ListHandle::State> st = m_handle.shared();
    const float vh = std::max(1.0f, st->scroll.viewport.height);
    const float top = std::max(0.0f, st->lastY - st->leading);
    // Kept within two screens of the view (made within one: a little hysteresis).
    const float from = top - 2 * vh;
    const float to = top + 3 * vh;
    for (std::size_t r = 0; r < n; ++r) {
        if (!m_slots[r].day)
            continue;
        const float a = m_heights.offsetOf(r);
        const float b = a + m_heights.heightOf(r);
        if (b >= from && a <= to)
            continue;
        if (keeps(int(r)))
            continue;
        releaseDay(int(r));
    }
}

void JournalView::dayActivity(PageController *ctl, at::Context<JournalView> &cx)
{
    const int day = dayOfController(ctl);
    if (day < 0 || m_applyingActive)
        return;
    const bool editing = ctl->editingRow() >= 0;
    const bool selected = ctl->selectionCount() > 0 && !(m_stream && m_stream->applying());
    const bool zoomed = ctl->zoomBlock() >= 0;
    if ((editing || selected || zoomed) && day != m_active)
        activateDay(day, cx);
}

void JournalView::activateDay(int day, at::Context<JournalView> &cx)
{
    if (day >= dayCount())
        return;
    if (day >= 0 && !makeDay(day, cx))
        return;
    if (day == m_active)
        return;
    const int old = m_active;
    m_active = day;
    m_activeDayPage = day >= 0 ? m_slots[std::size_t(day)].day->page.id() : 0;
    const bool span = spanActive ? spanActive() : (m_stream && m_stream->active());
    if (old >= 0 && old < dayCount() && m_slots[std::size_t(old)].day) {
        Day &d = *m_slots[std::size_t(old)].day;
        // The day written in before stops editing and its selection goes — unless a selection over
        // several days spans them (each keeps its part).
        m_applyingActive = true;
        if (!span && d.controller) {
            d.controller->stopEditing();
            d.controller->setSelectedRow(-1);
        }
        d.page.update(cx.app(), [span](PageView &v, at::Context<PageView> &pc) {
            if (!span)
                v.stopEditingCell(pc);
            v.setKeysActive(false, pc);
        });
        m_applyingActive = false;
    }
    if (day >= 0)
        m_slots[std::size_t(day)].day->page.update(cx.app(), [](PageView &v, at::Context<PageView> &pc) {
            v.setKeysActive(true, pc);
        });
    cx.notify();
    if (onActiveDayChanged)
        onActiveDayChanged(m_active);
}

QString JournalView::todayDate() const
{
    for (const Slot &s : m_slots)
        if (s.info.isToday)
            return s.info.date;
    return {};
}

bool JournalView::dayEmpty(int day) const
{
    if (const PageController *c = controller(day))
        return c->loaded() && c->blocks()->visibleCount() == 0 && c->editingRow() < 0;
    return !dayInfo(day).exists;
}

bool JournalView::beginDay(int day, at::Context<JournalView> &cx)
{
    if (!makeDay(day, cx))
        return false;
    PageController *c = controller(day);
    if (!c || !c->loaded() || c->readOnly())
        return false;
    activateDay(day, cx);
    if (c->blocks()->visibleCount() > 0) {
        c->startEditing(c->lastVisibleRow(), -1);
        return true;
    }
    const int row = c->startNewPage(c->exists() ? QString() : m_template);
    if (row >= 0)
        c->startEditing(row, -1);
    return row >= 0;
}

bool JournalView::pastEdge(PageController *from, bool up, float x, at::Context<JournalView> &cx)
{
    const int day = dayOfController(from);
    if (day < 0)
        return false;
    at::App &app = *m_app;
    JournalView &j = *this;
    {
        for (int d = day + (up ? -1 : 1); d >= 0 && d < j.dayCount(); d += up ? -1 : 1) {
            if (!j.makeDay(d, cx))
                continue;
            PageController *c = j.controller(d);
            if (!c || !c->loaded() || c->blocks()->visibleCount() == 0)
                continue; // a day with nothing to edit is passed
            BlockListModel *blocks = c->blocks();
            int row = up ? c->lastVisibleRow() : c->nextVisibleRow(-1);
            // (Not into the page's properties: the first block that is none.)
            while (!up && row >= 0 && blocks->index(row, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
                row = c->nextVisibleRow(row);
            if (row < 0 || blocks->index(row, 0).data(BlockListModel::ViewRole).toString() == u"frontmatter")
                continue;
            int cursor = -1;
            if (const at::Entity<PageView> *p = j.page(d)) {
                const PageView &v = p->read(app);
                if (v.editsRow(row))
                    cursor = v.editCursorOnLine(row, x, up);
            }
            if (cursor < 0)
                cursor = up ? -1 : 0;
            // (Its editing makes it the active day: the day left stops editing.)
            c->startEditing(row, cursor);
            return true;
        }
    }
    return false;
}

void JournalView::setLook(const Look &look, at::Context<JournalView> &cx)
{
    m_look = look;
    for (int d : madeDays())
        m_slots[std::size_t(d)].day->page.update(cx.app(), [&](PageView &v, at::Context<PageView> &pc) {
            v.setFooterHeight(m_look.space3);
            v.setFooterTapHeight(m_look.touch ? m_look.touchTarget : m_look.space3);
            pc.notify();
        });
    m_heights.markAllEstimated();
    cx.notify();
}

void JournalView::setPageStyle(const PageMetrics &metrics, const PagePalette &palette, float documentGutter,
                               at::Context<JournalView> &cx)
{
    m_metrics = metrics;
    m_palette = palette;
    m_documentGutter = documentGutter;
    for (int d : madeDays())
        m_slots[std::size_t(d)].day->page.update(cx.app(), [&](PageView &v, at::Context<PageView> &pc) {
            v.setStyle(dayMetrics(d), m_palette);
            pc.notify();
        });
    m_heights.markAllEstimated();
    cx.notify();
}

void JournalView::setStreamSelection(StreamSelection *selection)
{
    if (selection == m_stream)
        return;
    for (const QMetaObject::Connection &c : m_streamConnections)
        QObject::disconnect(c);
    m_streamConnections.clear();
    for (int d : madeDays())
        if (m_stream)
            m_stream->detach(controller(d));
    m_stream = selection;
    for (int d : madeDays())
        if (m_stream)
            m_stream->attach(controller(d));
    if (!selection)
        return;
    at::App *app = m_app;
    const at::EntityId self = m_selfId;
    // The end of a selection over several days moved into a day: it is the active one, the keys
    // are its; `reveal`: shown.
    m_streamConnections.push_back(QObject::connect(
            selection, &StreamSelection::focusMoved, selection,
            [app, self](PageController *page, int row, bool reveal) {
                app->updateEntity<JournalView>(self, [&](JournalView &j, at::Context<JournalView> &cx) {
                    const int d = j.dayOfController(page);
                    if (d < 0)
                        return;
                    j.activateDay(d, cx);
                    if (reveal)
                        if (const at::Entity<PageView> *p = j.page(d))
                            p->update(cx.app(), [row](PageView &v, at::Context<PageView> &pc) { v.revealRow(row, pc); });
                });
            }));
    // Its end in a day scrolled far away: the stream goes there.
    m_streamConnections.push_back(
            QObject::connect(selection, &StreamSelection::revealRequested, selection, [app, self](const QString &path) {
                app->updateEntity<JournalView>(self, [&](JournalView &j, at::Context<JournalView> &cx) {
                    for (int d = 0; d < j.dayCount(); ++d) {
                        if (j.m_slots[std::size_t(d)].info.path == path) {
                            j.scrollToDay(d, at::ListPosition::Nearest, cx);
                            return;
                        }
                    }
                });
            }));
}

void JournalView::setStreamFind(StreamFind *find)
{
    if (find == m_find)
        return;
    for (int d : madeDays())
        if (m_find)
            m_find->detach(controller(d));
    m_find = find;
    for (int d : madeDays())
        if (m_find)
            m_find->attach(controller(d));
}

void JournalView::setFind(const QString &query, bool matchCase, bool matchDiacritics, bool wholeWord, at::Color color,
                          at::Context<JournalView> &cx)
{
    m_findState = {query, matchCase, matchDiacritics, wholeWord, color};
    for (int d : madeDays())
        m_slots[std::size_t(d)].day->page.update(cx.app(), [&](PageView &v, at::Context<PageView> &pc) {
            v.setFind(query, matchCase, matchDiacritics, wholeWord, color, pc);
        });
    cx.notify();
}

// --- Scrolling ----------------------------------------------------------------------------------

void JournalView::scrollToDay(int day, at::ListPosition position, at::Context<JournalView> &cx)
{
    if (day < 0 || day >= dayCount())
        return;
    makeDay(day, cx);
    m_handle.scrollToRow(std::size_t(day), position);
    cx.notify();
}

int JournalView::exactRowOfDate(const QString &date) const
{
    for (std::size_t r = 0; r < m_slots.size(); ++r)
        if (m_slots[r].info.date == date)
            return int(r);
    return -1;
}

int JournalView::rowOfDate(const QString &date) const
{
    // Newest first: the first day not after `date` (dates after the first listed: the first).
    if (m_slots.empty())
        return -1;
    for (std::size_t r = 0; r < m_slots.size(); ++r)
        if (m_slots[r].info.date <= date)
            return int(r);
    return int(m_slots.size()) - 1;
}

bool JournalView::scrollToDate(const QString &date, at::Context<JournalView> &cx)
{
    const int row = rowOfDate(date);
    if (row < 0)
        return false;
    scrollToDay(row, at::ListPosition::Start, cx);
    return true;
}

JournalView::ViewState JournalView::viewState() const
{
    const std::shared_ptr<at::ListHandle::State> st = m_handle.shared();
    if (st->atTop || st->anchorRow >= m_slots.size())
        return {};
    return {m_slots[st->anchorRow].info.date, std::round(st->anchorInto)};
}

void JournalView::restoreViewState(const ViewState &state, at::Context<JournalView> &cx)
{
    if (state.date.isEmpty()) {
        m_handle.scrollToTop();
        cx.notify();
        return;
    }
    const int row = rowOfDate(state.date);
    if (row < 0)
        return;
    makeDay(row, cx);
    m_handle.scrollToAnchor(std::size_t(row), m_slots[std::size_t(row)].info.date == state.date ? state.offset : 0);
    cx.notify();
}

void JournalView::takeReveals()
{
    // (Read straight from the pages: no update inside the frame.)
    for (std::size_t r = 0; r < m_slots.size(); ++r) {
        if (!m_slots[r].day)
            continue;
        const at::EntityId pageId = m_slots[r].day->page.id();
        PageView *v = m_app->tryGet<PageView>(pageId);
        if (!v)
            continue;
        const std::optional<PageView::Reveal> reveal = v->takeReveal();
        if (!reveal)
            continue;
        at::App *app = m_app;
        const int row = reveal->row;
        m_handle.scrollToRowPart(
                r,
                [app, pageId, row](at::Window &window) -> std::optional<at::RowPart> {
                    const PageView *page = app->tryGet<PageView>(pageId);
                    if (!page)
                        return std::nullopt;
                    return page->embeddedPart(row, window);
                },
                reveal->position, m_revealMargin, m_revealMargin);
    }
}

// --- The header and the footer of the stream ----------------------------------------------------

bool JournalView::setHeaderHeight(float height)
{
    height = std::max(0.0f, height);
    if (std::abs(height - m_headerHeight) <= 0.01f)
        return false;
    m_headerHeight = height;
    return true;
}

bool JournalView::setFooterHeight(float height)
{
    height = std::max(0.0f, height);
    if (std::abs(height - m_footerHeight) <= 0.01f)
        return false;
    m_footerHeight = height;
    return true;
}

at::AnyElement JournalView::edgeElement(ForeignFrame::Kind kind)
{
    const float h = kind == ForeignFrame::Kind::Header ? m_headerHeight : m_footerHeight;
    JournalView *self = this;
    return at::div().h(h).ariaHidden().child(at::canvas([self, kind](at::Rect bounds, at::Window &window, at::App &) {
                                                 self->m_edgeFrames.push_back({kind, -1, 0, bounds, window.contentMask()});
                                             }).sizeFull());
}

// --- The last frame -----------------------------------------------------------------------------

std::optional<at::Rect> JournalView::dayBounds(int day) const
{
    if (day < 0 || day >= dayCount())
        return std::nullopt;
    const auto it = m_parts.find(key(m_slots[std::size_t(day)].info.date));
    if (it == m_parts.end() || it->second.frame != m_frame)
        return std::nullopt;
    return it->second.section;
}

std::optional<at::Rect> JournalView::rowBounds(int day, int row) const
{
    const at::Entity<PageView> *p = page(day);
    if (!p || !dayBounds(day))
        return std::nullopt;
    return p->read(*m_app).rowBounds(row);
}

int JournalView::dayAtY(float y) const
{
    for (std::size_t r = 0; r < m_slots.size(); ++r) {
        const auto it = m_parts.find(key(m_slots[r].info.date));
        if (it == m_parts.end() || it->second.frame != m_frame)
            continue;
        if (y >= it->second.section.top() && y < it->second.section.bottom())
            return int(r);
    }
    return -1;
}

std::optional<at::Point> JournalView::datePoint(int day) const
{
    if (!dayBounds(day))
        return std::nullopt;
    const at::Rect r = m_parts.at(key(m_slots[std::size_t(day)].info.date)).date;
    if (r.width() <= 0)
        return std::nullopt;
    return at::Point{r.left() + std::min(20.0f, r.width() / 2), r.top() + r.height() / 2};
}

std::optional<at::Point> JournalView::openAsPagePoint(int day) const
{
    if (!dayBounds(day))
        return std::nullopt;
    const at::Rect r = m_parts.at(key(m_slots[std::size_t(day)].info.date)).open;
    if (r.width() <= 0)
        return std::nullopt;
    return at::Point{r.left() + r.width() / 2, r.top() + r.height() / 2};
}

std::optional<at::Point> JournalView::promptPoint(int day) const
{
    if (!dayBounds(day))
        return std::nullopt;
    const at::Rect r = m_parts.at(key(m_slots[std::size_t(day)].info.date)).prompt;
    if (r.width() <= 0)
        return std::nullopt;
    return at::Point{r.left() + r.width() / 2, r.top() + r.height() / 2};
}

// --- Drawing ------------------------------------------------------------------------------------

at::AnyElement JournalView::render(at::Window &window, at::Context<JournalView> &cx)
{
    m_app = &cx.app();
    m_selfId = cx.entityId();
    m_frame = window.frameCount();
    m_edgeFrames.clear();
    // The days this frame shows (and a screen around) exist before the list lays them out; what
    // their pages asked to show is the list's to scroll to in this frame.
    makeDaysNear(window, cx);
    takeReveals();
    JournalView *self = this;
    const at::EntityId id = m_selfId;
    return at::div()
            .sizeFull()
            .bg(m_look.background)
            .flexCol()
            // A mouse's release anywhere: the days' pressed blocks are let go (a drag that left its
            // day ends here).
            .onMouseUp(at::MouseButton::Left,
                       [id](const at::MouseUpEvent &, at::Window &, at::App &a) {
                           a.updateEntity<JournalView>(id, [&](JournalView &j, at::Context<JournalView> &c) {
                               for (int d : j.madeDays())
                                   j.m_slots[std::size_t(d)].day->page.update(
                                           c.app(), [](PageView &v, at::Context<PageView> &pc) { v.pointerReleased(pc); });
                           });
                       })
            // A mouse's press on a block dragged into another day: the blocks between are selected
            // across the days (StreamSelection).
            .onMouseMove([id](const at::MouseMoveEvent &e, at::Window &, at::App &a) {
                if (e.pointer != at::PointerKind::Mouse || e.pressedButton != at::MouseButton::Left)
                    return;
                a.updateEntity<JournalView>(id, [&](JournalView &j, at::Context<JournalView> &) {
                    j.dragAcross(e.position);
                });
            })
            .child(at::list(u"journal", m_heights,
                            [self](std::size_t row, at::Window &, at::App &) -> at::AnyElement {
                                return self->renderDay(row);
                            })
                           .trackScroll(m_handle)
                           .header([self](at::Window &, at::App &) {
                               return self->edgeElement(ForeignFrame::Kind::Header);
                           })
                           .footer([self](at::Window &, at::App &) {
                               return self->edgeElement(ForeignFrame::Kind::Footer);
                           })
                           .ariaLabel(u"Journals")
                           .flex1()
                           // (The docked touch toolbar covers the bottom: the rows end above it.)
                           .pb(m_bottomInset)
                           .textSize(m_metrics.fontSize)
                           .lineHeight(m_metrics.lineHeight)
                           .textColor(m_palette.text)
                           .fontFamily(m_palette.bodyFamily))
            // After the frame: the days far from the view go.
            .child(at::canvas([self](at::Rect, at::Window &, at::App &app) {
                       if (self->m_releaseScheduled)
                           return;
                       self->m_releaseScheduled = true;
                       const at::EntityId journal = self->m_selfId;
                       app.defer([journal](at::App &a) {
                           a.updateEntity<JournalView>(journal, [](JournalView &j, at::Context<JournalView> &) {
                               j.releaseFarDays();
                           });
                       });
                   })
                           .absolute()
                           .size(0));
}

at::AnyElement JournalView::renderDay(std::size_t row)
{
    if (row >= m_slots.size())
        return at::div().h(1);
    const DayInfo &info = m_slots[row].info;
    const PageMetrics &m = m_metrics;
    const Look &l = m_look;
    const QString longDate = formatDate ? formatDate(info.date) : info.date;
    JournalView *self = this;
    const std::u16string dateKey = key(info.date);
    at::Div section = at::div()
                              .id(at::ElementId(std::u16string_view(dateKey)))
                              .flexCol()
                              .pb(l.space2)
                              .role(at::Role::Group)
                              .ariaLabel(u16View(longDate));
    // Where the frame drew the day (its height known by its date from now on).
    std::move(section).child(at::canvas([self, dateKey](at::Rect bounds, at::Window &, at::App &) {
                                           // (Painted before the heading and the prompt: their
                                           // places are this frame's from here on.)
                                           Parts &p = self->m_parts[dateKey];
                                           p = Parts{};
                                           p.section = bounds;
                                           p.frame = self->m_frame;
                                           self->m_known[dateKey] = bounds.height();
                                       })
                                               .absolute()
                                               .inset(0));
    if (row > 0) {
        // The line between days (ContentJournalDay's separator: the text column's width).
        const float x = m.contentX + m.gutter;
        const float w = std::max(0.0f, (m.contentWidth > 0 ? m.contentWidth : 600.0f) - m.gutter);
        std::move(section).child(at::div().mt(l.gap).mb(l.gap).ml(x).w(w).h(1).bg(l.separator).ariaHidden());
    }
    std::move(section).child(heading(int(row), info));
    if (dayEmpty(int(row)))
        return std::move(section).child(prompt(int(row), info));
    if (const at::Entity<PageView> *p = page(int(row)))
        return std::move(section).child(*p);
    // Not made yet (a jump far away): the room it had, until the next frame makes it.
    const auto known = m_known.find(dateKey);
    const float rest = known != m_known.end() ? std::max(0.0f, known->second - 60) : kDayEstimate - 60;
    return std::move(section).child(at::div().h(rest));
}

at::AnyElement JournalView::heading(int day, const DayInfo &info)
{
    const PageMetrics &m = m_metrics;
    const Look &l = m_look;
    JournalView *self = this;
    const at::EntityId id = m_selfId;
    const QString longDate = formatDate ? formatDate(info.date) : info.date;
    const qsizetype comma = longDate.indexOf(QStringLiteral(", "));
    const QString weekday = comma > 0 ? longDate.left(comma) : QString();
    const QString rest = comma > 0 ? longDate.mid(comma + 2) : longDate;
    const QString today = todayDate();
    const QString yesterday =
            !today.isEmpty()
                            && QDate::fromString(info.date, Qt::ISODate).addDays(1) == QDate::fromString(today, Qt::ISODate)
                    ? l.yesterdayWord
                    : QString();
    const QString relative = info.isToday ? l.todayWord : yesterday;
    const float size = l.compact ? l.compactDateSize : (info.isToday ? l.todaySize : l.dateSize);
    const std::u16string dateKey = key(info.date);
    const QString date = info.date;
    auto openPage = [id, date](const at::ClickEvent &, at::Window &, at::App &a) {
        a.updateEntity<JournalView>(id, [&](JournalView &j, at::Context<JournalView> &) {
            if (j.onOpenAsPage)
                j.onOpenAsPage(date, false);
        });
    };
    // The date: a heading, underlined under the mouse; a click opens the day as a page.
    at::Div dateColumn = at::div().flex1().minW(0).flexCol();
    if (l.compact)
        std::move(dateColumn).child(
                at::div()
                        .textSize(l.captionSize)
                        .lineHeight(l.captionSize * 1.4f)
                        .fontSemibold()
                        .textColor(info.isToday ? l.accentText : l.muted)
                        .ariaHidden()
                        .child(at::text(u16View((relative.isEmpty() ? QString() : relative + QStringLiteral(" · "))
                                                + weekday.toUpper()))));
    at::Div dateText = at::div()
                               .id(u"date")
                               .textSize(size)
                               .lineHeight(std::round(size * l.lineHeightTight))
                               .fontSemibold()
                               .textColor(l.text)
                               .cursorPointer()
                               .role(at::Role::Heading)
                               .ariaLevel(1)
                               .ariaLabel(u16View(longDate))
                               .hover([](at::Style &s) { s.text.underline = at::UnderlineStyle{}; })
                               .onClick(openPage)
                               .child(at::text(u16View(l.compact ? rest : longDate)))
                               .child(at::canvas([self, dateKey](at::Rect b, at::Window &, at::App &) {
                                          self->m_parts[dateKey].date = b;
                                      })
                                              .absolute()
                                              .inset(0));
    if (!l.dateFamily.empty())
        std::move(dateText).fontFamily(l.dateFamily);
    std::move(dateColumn).child(std::move(dateText));
    at::Div row = at::div()
                          .flexRow()
                          .itemsCenter()
                          .gap(l.space2)
                          .ml(m.contentX + m.gutter)
                          .when(m.contentWidth > 0, [&](at::Div &&d) {
                              return std::move(d).maxW(std::max(0.0f, m.contentWidth - m.gutter));
                          })
                          .child(std::move(dateColumn));
    if (!l.compact && !relative.isEmpty()) {
        // The badge: today's in the accent, yesterday's neutral.
        std::move(row).child(at::div()
                                           .px(6)
                                           .py(1)
                                           .rounded(6)
                                           .bg(info.isToday ? l.accent : l.neutralBadge)
                                           .textColor(info.isToday ? l.onAccent : l.muted)
                                           .textSize(l.badgeSize)
                                           .lineHeight(l.badgeSize * 1.4f)
                                           .fontSemibold()
                                           .ariaHidden()
                                           .child(at::text(u16View(relative))));
    }
    // "Open as page" (a ghost button; on phones its icon only).
    at::Div open = at::div()
                           .id(u"open")
                           .flexRow()
                           .itemsCenter()
                           .gap(6)
                           .px(l.compact ? 6 : 8)
                           .h(l.compact ? l.touchTarget * 0.75f : 28)
                           .rounded(6)
                           .cursorPointer()
                           .textColor(l.muted)
                           .textSize(l.buttonSize)
                           .role(at::Role::Button)
                           .ariaLabel(u16View(l.openAsPage))
                           .hover([c = l.hover](at::Style &s) { s.background = at::Background::solid(c); })
                           .onClick(openPage);
    const std::shared_ptr<const at::VectorIcon> icon = pageIcon(QStringLiteral("external-link"));
    const at::Color ink = l.muted;
    const float iconSize = l.iconSize;
    std::move(open).child(at::canvas([icon, ink](at::Rect b, at::Window &w, at::App &) {
                              if (icon)
                                  w.paintIcon(*icon, b, ink);
                          })
                                         .size(iconSize)
                                         .flexShrink0());
    if (!l.compact)
        std::move(open).child(at::text(u16View(l.openAsPage)));
    std::move(open).child(at::canvas([self, dateKey](at::Rect b, at::Window &, at::App &) {
                                     self->m_parts[dateKey].open = b;
                                 })
                                         .absolute()
                                         .inset(0));
    (void)day;
    return std::move(row).child(std::move(open));
}

at::AnyElement JournalView::prompt(int day, const DayInfo &info)
{
    const PageMetrics &m = m_metrics;
    const Look &l = m_look;
    JournalView *self = this;
    const at::EntityId id = m_selfId;
    const std::u16string dateKey = key(info.date);
    const PageController *c = controller(day);
    const bool readOnly = l.readOnly || (c && c->readOnly());
    const QString text = readOnly ? (info.isToday ? l.readOnlyToday : l.readOnlyDay)
                                  : (info.isToday ? l.promptToday : l.promptDay);
    const float x = m.contentX + m.gutter - l.space2;
    const float w = std::max(0.0f, (m.contentWidth > 0 ? m.contentWidth : 600.0f) - m.gutter + 2 * l.space2);
    const std::shared_ptr<const at::VectorIcon> icon = pageIcon(QStringLiteral("square-pen"));
    const at::Color ink = l.muted;
    const float iconSize = l.iconSize;
    const QString date = info.date;
    return at::div()
            .mt(l.space2)
            .ml(x)
            .maxW(w)
            .minH(l.touchTarget)
            .px(l.space3)
            .py(l.space2)
            .flexRow()
            .itemsCenter()
            .gap(l.space2)
            .rounded(l.radius)
            .border(1)
            .borderColor(l.separator)
            .id(u"prompt")
            .role(at::Role::Button)
            .ariaLabel(u16View(text))
            .when(!readOnly, [](at::Div &&d) { return std::move(d).cursorText(); })
            .hover([c = l.hover](at::Style &s) { s.background = at::Background::solid(c); })
            .onClick([id, date](const at::ClickEvent &, at::Window &, at::App &a) {
                a.updateEntity<JournalView>(id, [&](JournalView &j, at::Context<JournalView> &cx) {
                    const int d = j.exactRowOfDate(date);
                    if (d < 0)
                        return;
                    if (j.onBeginRequested && j.onBeginRequested(d))
                        return;
                    j.beginDay(d, cx);
                });
            })
            .child(at::canvas([icon, ink](at::Rect b, at::Window &win, at::App &) {
                       if (icon)
                           win.paintIcon(*icon, b, ink);
                   })
                           .size(iconSize)
                           .flexShrink0())
            .child(at::div().flex1().minW(0).textColor(l.muted).child(at::text(u16View(text))))
            .child(at::canvas([self, dateKey](at::Rect b, at::Window &, at::App &) {
                       self->m_parts[dateKey].prompt = b;
                   })
                           .absolute()
                           .inset(0));
}

// --- A drag across days --------------------------------------------------------------------------

void JournalView::dragAcross(at::Point p)
{
    if (!m_stream)
        return;
    // The day whose block the mouse pressed, and the day under the pointer now.
    int from = -1;
    int fromRow = -1;
    for (int d : madeDays()) {
        const int pressed = m_slots[std::size_t(d)].day->page.read(*m_app).pressedRow();
        if (pressed >= 0) {
            from = d;
            fromRow = pressed;
            break;
        }
    }
    const int to = dayAtY(p.y);
    if (from < 0 || to < 0 || to == from)
        return;
    PageController *target = controller(to);
    PageController *source = controller(from);
    if (!target || !source)
        return;
    const PageView &view = m_slots[std::size_t(to)].day->page.read(*m_app);
    int row = view.rowAtY(p.y);
    if (row < 0)
        row = to < from ? target->lastVisibleRow() : target->nextVisibleRow(-1);
    if (row < 0)
        return;
    if (m_stream->extendFrom(source, fromRow, target, row))
        m_slots[std::size_t(from)].day->page.update(*m_app, [](PageView &v, at::Context<PageView> &) {
            v.markDragSelected();
        });
}

} // namespace brulion::editor::atr
