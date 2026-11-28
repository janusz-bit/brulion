// SearchResultsView — the Search view's results drawn by Atrament (docs/ATRAMENT.md "The views in
// Atrament (M3)", Search, step 2): the rows of a SearchResultModel (src/app/content/searchmodel.h:
// read through its roles, so any model with them is one — the tests' QStandardItemModel) in a
// measured list in place of SearchView.qml's ListView of its delegates. A result shows its
// kind's icon, its title (a daily note's date in words: `formatDate`) with the matched characters in
// the accent and bold, its folder, its excerpt in two lines at most with its matches, "Close in
// meaning" for a note found by meaning alone, and an attachment's page ("Page 3 of 12 · 2 more
// pages match", a slide, a part, "Found by its name") with "Recognized text" for OCR.
//
// The view does nothing itself: a click on a result asks `onAction("open", {row, modifiers})`
// (Qt::KeyboardModifiers: Ctrl a new tab, OpenIn.clickTarget), a middle click
// `onAction("open", {row, target: "tab"})`. The search field keeps the keyboard: its ↑ / ↓ move the
// current result (setCurrentRow, shown highlighted and in view), Enter opens it.
#pragma once

#include "modelrows.h"
#include "partrecorder.h"
#include "renderedspans.h"
#include "ui/atrament.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace brulion::views::atr {

// SearchView.qml's look (Theme).
struct SearchLook {
    atrament::Color background = atrament::rgb(0xffffff);
    atrament::Color text = atrament::rgb(0x1f2328);
    atrament::Color muted = atrament::rgb(0x6e7781);
    atrament::Color faint = atrament::rgb(0x8c959f);
    atrament::Color match = atrament::rgb(0x3b5bdb);       // Theme.accentText
    atrament::Color hover = atrament::rgba(0x8c959f1a);    // Theme.bgHover
    atrament::Color selected = atrament::rgba(0x3b5bdb1f); // Theme.bgSelected
    float textSize = 15;  // Theme.textBase
    float smallSize = 12; // captions
    float lineHeight = 22;
    float sideMargin = 16;
    float radius = 10;
    std::u16string uiFamily;

    // From AtramentView.viewLook: its fields by name (text, muted, faint, match, hover, selected,
    // background; textSize, smallSize, lineHeight, sideMargin, radius; uiFamily).
    static SearchLook fromMap(const QVariantMap &look);
};

class SearchResultsView {
public:
    using Look = SearchLook;
    enum class Part { Row, Title };
    // A part by its name ("title"; anything else: the row).
    static Part partNamed(const QString &name) { return name == u"title" ? Part::Title : Part::Row; }

    SearchResultsView(atrament::Context<SearchResultsView> &cx, QAbstractItemModel *model);
    ~SearchResultsView();
    SearchResultsView(const SearchResultsView &) = delete;
    SearchResultsView &operator=(const SearchResultsView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<SearchResultsView> &cx);
    void setLook(SearchLook look, atrament::Context<SearchResultsView> &cx);
    const SearchLook &look() const { return m_look; }

    std::function<void(const QString &action, const QVariantMap &args)> onAction;
    // A daily note's date ("YYYY-MM-DD") in words (DatesHelper.formatLong); unset: as it is.
    std::function<QString(const QString &date)> formatDate;

    // The current result (highlighted; the field's ↑ / ↓), shown in the next frame.
    void revealRow(int row, atrament::Context<SearchResultsView> &cx);
    int currentRow() const { return m_current; }
    std::optional<atrament::Rect> partBounds(int row, Part part) const { return m_parts.bounds(row, part); }
    int drawnRows() const { return m_parts.rows(); }

    // A result as the model's roles give it.
    struct RowData {
        QString title, folder, excerpt, journalDate, kind;
        QVariantList titleMatches, excerptMatches;
        int page = 0, pageCount = 0, matchedPages = 0;
        bool ocr = false, byMeaning = false;
        bool attachment() const { return kind != u"note"; }
    };
    RowData rowData(int row) const;
    // An attachment's place ("Page 3 of 12 · 2 more pages match"; "" for a note) — SearchView's
    // pageInfo.
    static QString pageInfo(const RowData &d);
    // A document's part or a presentation's slide, and how many more match.
    static QString partInfo(const RowData &d);
    // The kind's icon (SearchView's): a daily note's, a PDF's, a picture's, any other's.
    static QString kindIcon(const RowData &d);
    QString headingOf(const RowData &d) const;

private:
    atrament::AnyElement renderRow(std::size_t row);
    atrament::AnyElement rowColumn(int row, const RowData &d, atrament::Color match);
    atrament::AnyElement heading(int row, const RowData &d, atrament::Color match);
    atrament::AnyElement caption(const QString &icon, const QString &text);
    atrament::AnyElement tracker(int row, Part part) { return m_parts.tracker(row, part, part == Part::Row); }
    // `text` with the characters at `positions` (UTF-16) in the match colour and bold.
    atrament::AnyElement highlighted(const QString &text, const QVariantList &positions, atrament::Color match,
                                     int maxLines);
    void act(const QString &action, const QVariantMap &args) const;

    QPointer<QAbstractItemModel> m_model;
    atrament::EntityId m_id;
    SearchLook m_look;
    ModelRows m_rows;
    int m_current = 0;
    PartRecorder<Part> m_parts;
};

} // namespace brulion::views::atr
