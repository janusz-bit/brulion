#include "searchview.h"

#include "lookmap.h"

#include "pageicons.h"
#include "searchmodel.h" // (its roles)

#include <QCoreApplication>

#include <algorithm>

namespace brulion::views::atr {

namespace at = brulion::atrament;
using content::SearchResultModel;
using editor::atr::u16View;
using namespace Qt::StringLiterals;

namespace {

// SearchView's words (its translations; not lupdate's: no context of its own).
QString viewWords(const char *text, int n = -1)
{
    return QCoreApplication::translate("SearchView", text, nullptr, n);
}

} // namespace

QString SearchResultsView::kindIcon(const RowData &d)
{
    if (!d.journalDate.isEmpty())
        return u"calendar-days"_s;
    if (d.kind == u"pdf")
        return u"file"_s;
    return d.kind == u"picture" ? u"image"_s : u"file-text"_s;
}

SearchResultsView::SearchResultsView(at::Context<SearchResultsView> &cx, QAbstractItemModel *model)
    : m_model(model)
    , m_id(cx.entityId())
    , m_rows(m_look.lineHeight * 2 + 24)
{
    m_rows.followFor(model, cx, [](SearchResultsView &) {});
}

SearchResultsView::~SearchResultsView() = default;

namespace {

constexpr ColorField<SearchLook> kColors[] = {
        {"background", &SearchLook::background}, {"text", &SearchLook::text},   {"muted", &SearchLook::muted},
        {"faint", &SearchLook::faint},           {"match", &SearchLook::match}, {"hover", &SearchLook::hover},
        {"selected", &SearchLook::selected}};
constexpr SizeField<SearchLook> kSizes[] = {{"textSize", &SearchLook::textSize},
                                            {"smallSize", &SearchLook::smallSize},
                                            {"lineHeight", &SearchLook::lineHeight},
                                            {"sideMargin", &SearchLook::sideMargin},
                                            {"radius", &SearchLook::radius}};

} // namespace

SearchLook SearchLook::fromMap(const QVariantMap &look)
{
    return lookFrom(look, kColors, kSizes);
}

void SearchResultsView::setLook(SearchLook look, at::Context<SearchResultsView> &cx)
{
    m_rows.restyle(m_look, std::move(look), cx);
}

SearchResultsView::RowData SearchResultsView::rowData(int row) const
{
    RowData d;
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return d;
    const QModelIndex i = m_model->index(row, 0);
    d.title = i.data(SearchResultModel::TitleRole).toString();
    d.folder = i.data(SearchResultModel::FolderRole).toString();
    d.excerpt = i.data(SearchResultModel::ExcerptRole).toString();
    d.journalDate = i.data(SearchResultModel::JournalDateRole).toString();
    d.kind = i.data(SearchResultModel::KindRole).toString();
    d.titleMatches = i.data(SearchResultModel::TitleMatchesRole).toList();
    d.excerptMatches = i.data(SearchResultModel::ExcerptMatchesRole).toList();
    d.page = i.data(SearchResultModel::PageRole).toInt();
    d.pageCount = i.data(SearchResultModel::PageCountRole).toInt();
    d.matchedPages = i.data(SearchResultModel::MatchedPagesRole).toInt();
    d.ocr = i.data(SearchResultModel::OcrRole).toBool();
    d.byMeaning = i.data(SearchResultModel::ByMeaningRole).toBool();
    return d;
}

QString SearchResultsView::pageInfo(const RowData &d)
{
    if (!d.attachment())
        return {};
    if (d.matchedPages == 0)
        return viewWords("Found by its name");
    if (d.kind == u"document" || d.kind == u"presentation")
        return partInfo(d);
    const int more = d.matchedPages - 1;
    const QString where = d.kind == u"pdf" ? viewWords("Page %1 of %2").arg(d.page).arg(d.pageCount) : viewWords("Picture");
    return more > 0 ? where + u" · "_s + viewWords("%n more page(s) match", more) : where;
}

QString SearchResultsView::partInfo(const RowData &d)
{
    const bool slides = d.kind == u"presentation";
    const QString place = (slides ? viewWords("Slide %1 of %2") : viewWords("Part %1 of %2")).arg(d.page).arg(d.pageCount);
    const int more = d.matchedPages - 1;
    if (more <= 0)
        return place;
    return place + u" · "_s + (slides ? viewWords("%n more slide(s) match", more) : viewWords("%n more part(s) match", more));
}

at::AnyElement SearchResultsView::render(at::Window &, at::Context<SearchResultsView> &)
{
    m_parts.clear();
    SearchResultsView *self = this;
    const std::u16string family = m_look.uiFamily;
    return at::div()
            .id(u"results")
            .sizeFull()
            .flexCol()
            .bg(m_look.background)
            .when(!family.empty(), [family](at::Div &&d) { return std::move(d).fontFamily(family); })
            .child(at::list(u"results", m_rows.heights(),
                            [self](std::size_t row, at::Window &, at::App &) -> at::AnyElement { return self->renderRow(row); })
                           .trackScroll(m_rows.list())
                           .ariaLabel(u"Search results")
                           .flex1());
}

// A result (SearchView's delegate): its heading, its excerpt, "Close in meaning", an attachment's
// place; a click opens it (Ctrl: a new tab), a middle click in a new tab.
at::AnyElement SearchResultsView::renderRow(std::size_t row)
{
    const RowData d = rowData(int(row));
    const bool current = int(row) == m_current;
    // (On the current row the accent is too light: the text's colour, the matches staying bold.)
    const at::Color match = current ? m_look.text : m_look.match;
    const SearchResultsView *self = this;
    const int r = int(row);
    return at::div()
            .id(at::ElementId::number(row))
            .relative()
            .mx(m_look.sideMargin - 8)
            .px(12)
            .py(8)
            .rounded(m_look.radius)
            .bg(current ? m_look.selected : at::transparent())
            .hover([look = m_look.hover, current](at::Style &s) {
                if (!current)
                    s.background = at::Background::solid(look);
            })
            .textSize(m_look.textSize)
            .lineHeight(m_look.lineHeight)
            .textColor(m_look.text)
            .cursorPointer()
            .onClick([self, r](const at::ClickEvent &click, at::Window &, at::App &) {
                self->act(u"open"_s, {{u"row"_s, r}, {u"modifiers"_s, click.up.modifiers.control ? int(Qt::ControlModifier) : 0}});
            })
            .onMouseUp(at::MouseButton::Middle,
                       [self, r](const at::MouseUpEvent &, at::Window &, at::App &) {
                           self->act(u"open"_s, {{u"row"_s, r}, {u"target"_s, u"tab"_s}});
                       })
            .child(rowColumn(r, d, match))
            .child(tracker(r, Part::Row));
}

// A result's lines: its heading, its excerpt (two lines at most), "Close in meaning", an
// attachment's place (and "Recognized text").
at::AnyElement SearchResultsView::rowColumn(int row, const RowData &d, at::Color match)
{
    at::Div column = at::div().relative().flexCol().gap(2).child(heading(row, d, match));
    if (!d.excerpt.isEmpty())
        std::move(column).child(at::div().textColor(m_look.muted).child(highlighted(d.excerpt, d.excerptMatches, match, 2)));
    if (d.byMeaning)
        std::move(column).child(caption(u"sparkles"_s, viewWords("Close in meaning")));
    if (d.attachment())
        std::move(column).child(caption(d.ocr ? u"scan-text"_s : QString(),
                                        pageInfo(d) + (d.ocr ? u"  ·  "_s + viewWords("Recognized text") : QString())));
    return column;
}

// A result's heading: its title, a daily note's date in words.
QString SearchResultsView::headingOf(const RowData &d) const
{
    return !d.journalDate.isEmpty() && formatDate ? formatDate(d.journalDate) : d.title;
}

// The kind's icon, the title (a daily note's date in words) with its matches, the folder.
at::AnyElement SearchResultsView::heading(int row, const RowData &d, at::Color match)
{
    const bool journal = !d.journalDate.isEmpty();
    const QString title = headingOf(d);
    at::Div line = at::div()
                           .relative()
                           .flexRow()
                           .itemsCenter()
                           .gap(8)
                           .child(editor::atr::iconElement(kindIcon(d), 16, m_look.muted))
                           .child(at::div().flex1().minW(0).fontSemibold().child(
                                   highlighted(title, journal ? QVariantList() : d.titleMatches, match, 1)));
    if (!d.folder.isEmpty() && !journal)
        std::move(line).child(at::div()
                                      .flexShrink0()
                                      .maxW(220)
                                      .whitespaceNowrap()
                                      .overflowHidden()
                                      .textSize(m_look.smallSize)
                                      .textColor(m_look.faint)
                                      .child(at::text(u16View(d.folder))));
    return std::move(line).child(tracker(row, Part::Title));
}

// A caption line: an icon (none: "") and its words, faint.
at::AnyElement SearchResultsView::caption(const QString &iconName, const QString &text)
{
    at::Div line = at::div().flexRow().itemsCenter().gap(4).textSize(m_look.smallSize).textColor(m_look.faint);
    if (!iconName.isEmpty())
        std::move(line).child(editor::atr::iconElement(iconName, 14, m_look.faint));
    return std::move(line).child(at::text(u16View(text)).maxLines(1));
}

at::AnyElement SearchResultsView::highlighted(const QString &text, const QVariantList &positions, at::Color match,
                                              int maxLines)
{
    std::vector<at::TextSpan> spans;
    at::TextStyleRefinement marked;
    marked.color = match;
    marked.fontWeight = at::FontWeight{at::FontWeight::Bold};
    for (const QVariant &p : positions) {
        const int at = p.toInt();
        if (at < 0 || at >= text.size())
            continue;
        // (Adjacent positions grow the last span: one span per matched word.)
        if (!spans.empty() && spans.back().range.end == at)
            spans.back().range.end = at + 1;
        else
            spans.push_back({at::Range{at, at + 1}, marked});
    }
    return at::text(u16View(text)).spans(spans).maxLines(maxLines);
}

void SearchResultsView::act(const QString &action, const QVariantMap &args) const
{
    if (onAction)
        onAction(action, args);
}

void SearchResultsView::revealRow(int row, at::Context<SearchResultsView> &cx)
{
    m_current = row;
    m_rows.reveal(row);
    cx.notify();
}

} // namespace brulion::views::atr
