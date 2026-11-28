// JournalView — the journal stream drawn by Atrament as one list (docs/ATRAMENT.md "The journal
// stream as one list", M2 step 4): JournalsView's days without a QML item per day. The list's rows
// are the days of a days' model (JournalListModel: today first, then every earlier day with a
// note) — each row a day's section: the line between days, the day's heading (the date, "Today" /
// "Yesterday", "Open as page"), then the prompt of a day without blocks or the day's page, a
// PageView in embedded mode (pageview.h `setEmbedding`) drawn as a nested view.
//
// Days near the view (the rows the last frame showed, a screen above and below) are made before
// the list lays them out — a PageController from the host's `makeController`, a PageView set up by
// the host's `setUpPage` (looks, callbacks) —, days farther away go after the frame unless they are
// edited, have blocks selected, are zoomed into or are the active day. The stream scrolls by an
// anchor (a day and an offset into it): a day above the view measured later moves nothing on
// screen. ↑ on a day's first line and ↓ on its last go into the day above / below.
//
//   auto journal = app.newEntity<JournalView>(daysModel);
//   journal.update(app, [](JournalView &j, auto &cx) { j.makeController = …; j.setUpPage = …; });
//   app.openWindow(window, journal);   // or AtramentItem::attach
#pragma once

#include "foreignrows.h"
#include "pageview.h"

#include "ui/atrament.h"

#include <QAbstractItemModel>
#include <QMetaObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace brulion::editor {
class PageController;
class StreamSelection;
class StreamFind;

namespace atr {

class JournalView {
public:
    // A row of the days' model.
    struct DayInfo {
        QString date; // "YYYY-MM-DD"
        QString path; // the note (today without one: the path it will get)
        bool exists = false;
        bool isToday = false;
    };
    // The heading's and the prompt's look (ContentJournalDay's Theme values) and words (the host's
    // translations; English by default).
    struct Look {
        atrament::Color text = atrament::rgb(0x1f2328);       // Theme.fg
        atrament::Color muted = atrament::rgb(0x6e7781);      // Theme.fgMuted
        atrament::Color faint = atrament::rgb(0x9aa0a6);      // Theme.fgFaint
        atrament::Color accentText = atrament::rgb(0xc2410c); // Theme.accentText: today's weekday line
        atrament::Color accent = atrament::rgb(0xe8590c);     // the "Today" badge (accent, solid)
        atrament::Color onAccent = atrament::rgb(0xffffff);
        atrament::Color neutralBadge = atrament::rgba(0x8c959f26); // "Yesterday"
        atrament::Color separator = atrament::rgba(0x8c959f33);    // Theme.borderSubtle
        atrament::Color hover = atrament::rgba(0x8c959f1a);        // Theme.bgHover
        atrament::Color pressed = atrament::rgba(0x8c959f33);      // Theme.bgPressed
        atrament::Color background = atrament::rgb(0xffffff);      // Theme.bg
        std::u16string dateFamily;                                 // Theme.fontEditor ("": the page's)
        std::u16string uiFamily;                                   // Theme.fontUi ("": the page's)
        float dateSize = 24;        // Theme.text2xl
        float todaySize = 30;       // Theme.text3xl
        float compactDateSize = 20; // Theme.textXl (phones)
        float captionSize = 12;     // Theme.textXs: the compact weekday line
        float buttonSize = 12;      // Theme.textSm
        float badgeSize = 11;
        float lineHeightTight = 1.25f;
        float gap = 32;             // between days, above and below the line (Theme.space8; compact space5)
        float space2 = 8;
        float space3 = 12;
        float touchTarget = 44;
        float radius = 10;          // Theme.radius: the prompt's box
        float iconSize = 16;
        bool compact = false;       // a phone: the weekday above the date, an icon for "Open as page"
        bool touch = false;
        bool readOnly = false;      // reading mode: the prompt says nothing was written
        QString todayWord = QStringLiteral("Today");
        QString yesterdayWord = QStringLiteral("Yesterday");
        QString openAsPage = QStringLiteral("Open as page");
        QString promptToday = QStringLiteral("What's on today? Click to start writing.");
        QString promptDay = QStringLiteral("Nothing written on this day. Click to start writing.");
        QString readOnlyToday = QStringLiteral("Nothing written today yet.");
        QString readOnlyDay = QStringLiteral("Nothing written on this day.");
    };

    JournalView(atrament::Context<JournalView> &cx, QAbstractItemModel *days, PageMetrics metrics = {},
                PagePalette palette = {});
    ~JournalView();
    JournalView(const JournalView &) = delete;
    JournalView &operator=(const JournalView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<JournalView> &cx);

    // --- The host's parts (each set before the first frame).
    // A day's controller: its host, path, read-only state… (null: the day shows its heading only).
    std::function<std::unique_ptr<PageController>(const DayInfo &day)> makeController;
    // A day made: its page's looks and callbacks (as a page's: AtramentView's). Called outside any
    // update of the page; the stream's own hooks (the embedding, ↑/↓ past its edges) are set after.
    std::function<void(int day, const atrament::Entity<PageView> &page, PageController *controller)> setUpPage;
    // A day about to go (scrolled far away, removed from the model): its controller still exists.
    std::function<void(int day, PageController *controller)> onDayReleased;
    // The long date of a day ("Wednesday, October 14, 2026"); default: the ISO date.
    std::function<QString(const QString &date)> formatDate;
    // The date or "Open as page" clicked (`source`: Ctrl+E in the day, its Markdown source).
    std::function<void(const QString &date, bool source)> onOpenAsPage;
    // The prompt of a day without blocks clicked: true when the host took it (it refused in reading
    // mode, or began the day its way); else beginDay().
    std::function<bool(int day)> onBeginRequested;
    // The day written in changed (−1: none).
    std::function<void(int day)> onActiveDayChanged;
    // A selection over several days is shown: a day becoming the active one keeps the others'.
    std::function<bool()> spanActive;

    void setLook(const Look &look, atrament::Context<JournalView> &cx);
    const Look &look() const { return m_look; }
    // The days' geometry and colours: `metrics` an outline day's; a day in document mode starts its
    // text `documentGutter` px into the column (the outline days' gutter, or 0 when the journal is a
    // document by default — BlockEditor.documentInset).
    void setPageStyle(const PageMetrics &metrics, const PagePalette &palette, float documentGutter,
                      atrament::Context<JournalView> &cx);
    // The geometry of day `day`'s page (its note mode's).
    PageMetrics dayMetrics(int day) const;
    const PagePalette &palette() const { return m_palette; }
    // The journal template a day without a note starts from (beginDay).
    void setJournalTemplate(const QString &path) { m_template = path; }
    // The stream's selection over several days and its find: the days' controllers are attached
    // while they are made.
    void setStreamSelection(StreamSelection *selection);
    void setStreamFind(StreamFind *find);
    StreamSelection *streamSelection() const { return m_stream.data(); }

    // --- The days.
    int dayCount() const { return int(m_slots.size()); }
    DayInfo dayInfo(int day) const;
    // The day's controller and page (null: not made — far from the view).
    PageController *controller(int day) const;
    const atrament::Entity<PageView> *page(int day) const;
    int dayOfController(const PageController *controller) const;
    int dayOfPage(atrament::EntityId page) const;
    std::vector<int> madeDays() const;
    // Makes day `day` now (scrolled to, a find's next match there); whether it exists.
    bool makeDay(int day, atrament::Context<JournalView> &cx);
    // The day written in (−1: none). activateDay() makes it first; the day active before stops
    // editing and loses its selection unless spanActive().
    int activeDay() const { return m_active; }
    void activateDay(int day, atrament::Context<JournalView> &cx);
    // Starts writing in a day: its last block edited, or — no blocks yet — the journal template's
    // first empty block (the file is made by the first change: PageController::startNewPage).
    bool beginDay(int day, atrament::Context<JournalView> &cx);

    // --- Scrolling.
    atrament::ListHandle &handle() { return m_handle; }
    // The next frame shows day `day` there.
    void scrollToDay(int day, atrament::ListPosition position, atrament::Context<JournalView> &cx);
    // The day of `date`, else the nearest earlier listed one (−1: none) — JournalListModel's rule.
    int rowOfDate(const QString &date) const;
    int exactRowOfDate(const QString &date) const;
    bool scrollToDate(const QString &date, atrament::Context<JournalView> &cx);
    // The day at the view's top and how far into it the view starts (px; the date empty: the top
    // of the stream, its header too).
    struct ViewState {
        QString date;
        float offset = 0;
    };
    ViewState viewState() const;
    void restoreViewState(const ViewState &state, atrament::Context<JournalView> &cx);
    // The room an overlay covers at the bottom (the docked touch toolbar): reveals keep above it.
    void setBottomInset(float px) { m_bottomInset = std::max(0.0f, px); }
    // Air kept around a block revealed (the caret's row) at the view's edges.
    void setRevealMargin(float px) { m_revealMargin = std::max(0.0f, px); }

    // --- The stream's header and footer (QML items placed over them: the missed reminders, "Go
    // to day…", the empty journal's hint) and where the last frame put them.
    bool setHeaderHeight(float height);
    bool setFooterHeight(float height);
    const std::vector<ForeignFrame> &edgeFrames() const { return m_edgeFrames; }

    // --- The last frame (tests, the host).
    // Where day `day`'s section was drawn (window px).
    std::optional<atrament::Rect> dayBounds(int day) const;
    // Row `row` of day `day`'s page (its rowBounds), and the day under window point `p` (−1: none).
    std::optional<atrament::Rect> rowBounds(int day, int row) const;
    int dayAtY(float y) const;
    // Where the date of day `day`, its "Open as page" and its prompt were drawn (their middles).
    std::optional<atrament::Point> datePoint(int day) const;
    std::optional<atrament::Point> openAsPagePoint(int day) const;
    std::optional<atrament::Point> promptPoint(int day) const;
    // The find bar's matches tinted in every day (pageview.h setFind).
    void setFind(const QString &query, bool matchCase, bool matchDiacritics, bool wholeWord, atrament::Color color,
                 atrament::Context<JournalView> &cx);

private:
    struct Day;
    struct Slot {
        DayInfo info;
        std::unique_ptr<Day> day;
        std::uint64_t shownFrame = 0; // the last frame that drew its section
    };
    struct Parts { // where a frame drew a day's parts
        atrament::Rect section, date, open, prompt;
        std::uint64_t frame = 0;
    };

    DayInfo readDay(int row) const;
    float estimateFor(const DayInfo &info) const;
    void rebuildDays();
    void daysInserted(int first, int last);
    void daysRemoved(int first, int last);
    void daysChanged(int first, int last);
    void releaseDay(int day);
    bool keeps(int day) const;
    // The days the next frame needs (near the last frame's view), made; the far ones scheduled to go.
    void makeDaysNear(atrament::Window &window, atrament::Context<JournalView> &cx);
    void releaseFarDays();
    // A day's page asked to be revealed: the list shows that part of the day's row.
    void takeReveals();
    // A day zoomed into a block: its heading, the zoom's breadcrumb and the block in view.
    void revealZoom(int day);
    void dayActivity(PageController *controller, atrament::Context<JournalView> &cx);
    bool pastEdge(PageController *from, bool up, float x, atrament::Context<JournalView> &cx);
    // Today's date by the days' model (its isToday row).
    QString todayDate() const;
    // --- Drags across days (journaldrag.cpp).
    // The stream's view (window px, by the last frame) and how far it scrolled since.
    std::optional<atrament::Rect> viewport() const;
    float scrolledSinceFrame() const;
    // The pointer's y held into the view, where the days are now (the last frame's coordinates).
    float pointerY(atrament::Point p) const;
    // The day drawn at `y` (else the nearest one drawn; −1: none) and the block of its page at `y`
    // (above its blocks the first one, below them the last one; −1: none).
    int dayUnder(float y) const;
    int blockUnder(int day, float y) const;
    // A drag selecting from `fromRow` of `from` at window point `p`: over another day the selection
    // goes on there (whether it changed); nothing over `from`'s own day.
    std::optional<bool> selectAcross(PageController *from, int fromRow, atrament::Point p);
    // A mouse's press on a block dragged straight into another day (before its own drag began).
    void dragAcross(atrament::Point p);
    // A move or a release of a day's drag (PageView::followDrag): whether it was the drag's.
    bool followDrag(const atrament::PlatformInput &input);
    atrament::AnyElement renderDay(std::size_t row);
    atrament::AnyElement heading(int day, const DayInfo &info);
    atrament::AnyElement prompt(int day, const DayInfo &info);
    atrament::AnyElement edgeElement(ForeignFrame::Kind kind);
    bool dayEmpty(int day) const;

    QPointer<QAbstractItemModel> m_days;
    int m_dateRole = -1;
    int m_pathRole = -1;
    int m_existsRole = -1;
    int m_todayRole = -1;
    std::vector<Slot> m_slots;
    atrament::RowHeights m_heights;
    // Measured heights by date: kept across the model's resets.
    std::unordered_map<std::u16string, float> m_known;
    atrament::ListHandle m_handle;
    PageMetrics m_metrics;
    PagePalette m_palette;
    float m_documentGutter = 0;
    Look m_look;
    QString m_template;
    QPointer<StreamSelection> m_stream;
    std::vector<QMetaObject::Connection> m_streamConnections;
    QPointer<StreamFind> m_find;
    struct Find {
        QString query;
        bool matchCase = false;
        bool matchDiacritics = false;
        bool wholeWord = false;
        atrament::Color color;
    } m_findState;
    int m_active = -1;
    atrament::EntityId m_activeDayPage = 0; // the active day's page (its row may move)
    float m_bottomInset = 0;
    float m_revealMargin = 0;
    float m_headerHeight = 0;
    float m_footerHeight = 0;
    std::vector<ForeignFrame> m_edgeFrames;
    std::unordered_map<std::u16string, Parts> m_parts; // by date
    std::vector<QMetaObject::Connection> m_connections;
    atrament::App *m_app = nullptr;
    atrament::EntityId m_selfId = 0;
    std::uint64_t m_frame = 0;
    bool m_releaseScheduled = false;
    bool m_applyingActive = false;
};

} // namespace atr
} // namespace brulion::editor
