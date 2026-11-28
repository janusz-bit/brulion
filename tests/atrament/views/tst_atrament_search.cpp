// The Search view's results drawn by Atrament (src/app/views/atrament/searchview.h; docs/ATRAMENT.md
// "The views in Atrament (M3)", Search): rows of SearchResultModel's roles (a QStandardItemModel
// with them here: the view reads nothing else) drawn headless on Atrament's test platform — a
// result's title and excerpt with their matches in the accent and bold, its folder, "Close in
// meaning", an attachment's place and "Recognized text", a daily note's date in words; a click
// asking to open it (Ctrl: its modifiers), a middle click a new tab; the current result
// highlighted and kept in view; the rows following the model.
#include "searchmodel.h"
#include "pageicons.h"
#include "searchview.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QStandardItemModel>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::views::atr;
using brulion::content::SearchResultModel;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

// A result's roles; `matches` the UTF-16 positions of matched characters.
QStandardItem *result(const QString &title, const QString &excerpt, const QVariantList &titleMatches,
                      const QVariantList &excerptMatches, const QVariantMap &more = {})
{
    auto *item = new QStandardItem;
    item->setData(title, SearchResultModel::TitleRole);
    item->setData(excerpt, SearchResultModel::ExcerptRole);
    item->setData(titleMatches, SearchResultModel::TitleMatchesRole);
    item->setData(excerptMatches, SearchResultModel::ExcerptMatchesRole);
    item->setData(more.value(u"kind"_s, u"note"_s), SearchResultModel::KindRole);
    item->setData(more.value(u"folder"_s), SearchResultModel::FolderRole);
    item->setData(more.value(u"journalDate"_s), SearchResultModel::JournalDateRole);
    for (const auto &[key, role] : {std::pair{u"page"_s, int(SearchResultModel::PageRole)},
                                    std::pair{u"pageCount"_s, int(SearchResultModel::PageCountRole)},
                                    std::pair{u"matchedPages"_s, int(SearchResultModel::MatchedPagesRole)}})
        item->setData(more.value(key, 0), role);
    item->setData(more.value(u"ocr"_s, false), SearchResultModel::OcrRole);
    item->setData(more.value(u"byMeaning"_s, false), SearchResultModel::ByMeaningRole);
    return item;
}

QVariantList span(int from, int to)
{
    QVariantList out;
    for (int i = from; i < to; ++i)
        out.append(i);
    return out;
}

struct Harness {
    QStandardItemModel model;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<SearchResultsView> view;
    QStringList actions;

    explicit Harness(float width = 600) : tw(platform->newWindow({width, 700}, 1))
    {
        model.appendRow(result(u"Remont mieszkania"_s, u"kafelki do łazienki i remont kuchni"_s, span(0, 6), span(22, 28),
                               {{u"folder"_s, u"Projekty"_s}}));
        model.appendRow(result(u"2026-10-13"_s, u"zadzwonić do fachowca od remontu"_s, {}, span(25, 31),
                               {{u"journalDate"_s, u"2026-10-13"_s}}));
        model.appendRow(result(u"Instrukcja.pdf"_s, u"przed remontem wyłącz prąd"_s, {}, span(6, 12),
                               {{u"kind"_s, u"pdf"_s}, {u"page"_s, 3}, {u"pageCount"_s, 12}, {u"matchedPages"_s, 3},
                                {u"ocr"_s, true}}));
        model.appendRow(result(u"Wakacje"_s, u"plaża i słońce"_s, {}, {}, {{u"byMeaning"_s, true}}));
        view = app.newEntity<SearchResultsView>(&model);
        view.update(app, [this](SearchResultsView &v, at::Context<SearchResultsView> &) {
            v.formatDate = [](const QString &date) { return u"wtorek, 13 października "_s + date.left(4); };
            v.onAction = [this](const QString &action, const QVariantMap &args) {
                QStringList parts{action};
                for (auto it = args.cbegin(); it != args.cend(); ++it)
                    parts.append(it.key() + u'=' + it.value().toString());
                actions.append(parts.join(u' '));
            };
        });
        app.openWindow(tw, view);
        window().setAccessibilityEnabled(true);
        tw.settle();
    }
    const SearchResultsView &results() { return view.read(app); }
    at::Window &window() { return *app.windows().front(); }
    at::Rect part(int row, SearchResultsView::Part p) { return results().partBounds(row, p).value_or(at::Rect{}); }
    QString dump() { return str(window().accessibilityTree().dump()); }
    int quads(at::Color c)
    {
        int n = 0;
        for (const at::Primitive &p : tw.lastScene()->primitives())
            if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == c)
                ++n;
        return n;
    }
};

} // namespace

class TestAtramentSearch : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { editor::atr::setPageIconFolder(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons")); }
    void resultsDrawn();
    void clicksOpen();
    void currentResult();
    void rowsFollowTheModel();
    void pageInfo_data();
    void pageInfo();
    void lookFromItsMap();
    void kindIcons();
    void laidOutAsTheDelegate();
    void matchesWhereTheModelSays();
    void hoverAndCaptionIcons();
    void farResultRevealedAndRestyled();
};

// Each result a row — its title (a daily note's date in words), its folder (not a daily note's),
// its excerpt, "Close in meaning", an attachment's place and "Recognized text" — read by screen
// readers as its texts; the matched characters in the accent (on the current row: the text's
// colour, still bold).
void TestAtramentSearch::resultsDrawn()
{
    Harness h;
    QCOMPARE(h.results().drawnRows(), 4);
    const QString dump = h.dump();
    for (const QString &text : {u"Remont mieszkania"_s, u"Projekty"_s, u"kafelki do łazienki"_s, u"wtorek, 13 października 2026"_s,
                                u"zadzwonić do fachowca"_s, u"Close in meaning"_s, u"plaża i słońce"_s,
                                u"Page 3 of 12 · 2 more page(s) match  ·  Recognized text"_s})
        QVERIFY2(dump.contains(text), qPrintable(text + u" in "_s + dump));
    QVERIFY2(!dump.contains(u"\"2026-10-13\""_s), qPrintable(dump));
    for (int r = 1; r < 4; ++r)
        QVERIFY(h.part(r, SearchResultsView::Part::Row).top() >= h.part(r - 1, SearchResultsView::Part::Row).bottom() - 0.5f);
    // The matches of the rows not current: "remont" twice (the daily note's excerpt, the PDF's).
    int matched = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->color == h.results().look().match)
            ++matched;
    QCOMPARE(matched, 12);
    // A daily note's folder is not shown (its heading is its date).
    Harness journal;
    journal.model.item(1)->setData(u"journals"_s, SearchResultModel::FolderRole);
    journal.tw.settle();
    QVERIFY2(!journal.dump().contains(u"journals"_s), qPrintable(journal.dump()));
    QCOMPARE(journal.results().rowData(-1).title, QString());
}

// A click opens the result (with Ctrl: its modifiers, a new tab for OpenIn); a middle click: a tab.
void TestAtramentSearch::clicksOpen()
{
    Harness h;
    h.tw.simulateClick(h.part(1, SearchResultsView::Part::Row).center());
    QCOMPARE(h.actions.last(), u"open modifiers=0 row=1"_s);
    at::Modifiers ctrl;
    ctrl.control = true;
    h.tw.simulateClick(h.part(0, SearchResultsView::Part::Title).center(), at::MouseButton::Left, ctrl);
    QCOMPARE(h.actions.last(), u"open modifiers=%1 row=0"_s.arg(int(Qt::ControlModifier)));
    h.tw.simulateClick(h.part(2, SearchResultsView::Part::Row).center(), at::MouseButton::Middle);
    QCOMPARE(h.actions.last(), u"open row=2 target=tab"_s);
}

// The current result (the field's ↑ / ↓): highlighted alone, kept in view.
void TestAtramentSearch::currentResult()
{
    Harness h;
    QCOMPARE(h.results().currentRow(), 0);
    QCOMPARE(h.quads(h.results().look().selected), 1);
    h.view.update(h.app, [](SearchResultsView &v, at::Context<SearchResultsView> &cx) { v.revealRow(2, cx); });
    h.tw.settle();
    QCOMPARE(h.results().currentRow(), 2);
    QCOMPARE(h.quads(h.results().look().selected), 1);
    QVERIFY(h.part(2, SearchResultsView::Part::Row).height() > 0);
}

// A query typed again: the model's rows replaced, the view's with them.
void TestAtramentSearch::rowsFollowTheModel()
{
    Harness h;
    h.model.removeRows(1, 3);
    h.tw.settle();
    QCOMPARE(h.results().drawnRows(), 1);
    h.model.appendRow(result(u"Żółw"_s, u"karmić żółwia sałatą"_s, span(0, 4), span(7, 13)));
    h.tw.settle();
    QCOMPARE(h.results().drawnRows(), 2);
    QVERIFY(h.dump().contains(u"karmić żółwia"_s));
}

// An attachment's place (SearchView's pageInfo).
void TestAtramentSearch::pageInfo_data()
{
    QTest::addColumn<QString>("kind");
    QTest::addColumn<int>("page");
    QTest::addColumn<int>("pages");
    QTest::addColumn<int>("matched");
    QTest::addColumn<QString>("info");
    QTest::newRow("a note") << u"note"_s << 0 << 0 << 0 << QString();
    QTest::newRow("its name") << u"pdf"_s << 1 << 5 << 0 << u"Found by its name"_s;
    QTest::newRow("a PDF's page") << u"pdf"_s << 2 << 5 << 1 << u"Page 2 of 5"_s;
    QTest::newRow("one more page") << u"pdf"_s << 2 << 5 << 2 << u"Page 2 of 5 · 1 more page(s) match"_s;
    QTest::newRow("and more pages") << u"pdf"_s << 2 << 5 << 3 << u"Page 2 of 5 · 2 more page(s) match"_s;
    QTest::newRow("a picture") << u"picture"_s << 0 << 0 << 1 << u"Picture"_s;
    QTest::newRow("a slide") << u"presentation"_s << 4 << 9 << 1 << u"Slide 4 of 9"_s;
    QTest::newRow("slides") << u"presentation"_s << 4 << 9 << 2 << u"Slide 4 of 9 · 1 more slide(s) match"_s;
    QTest::newRow("a document's part") << u"document"_s << 2 << 3 << 4 << u"Part 2 of 3 · 3 more part(s) match"_s;
}

void TestAtramentSearch::pageInfo()
{
    QFETCH(QString, kind);
    QFETCH(int, page);
    QFETCH(int, pages);
    QFETCH(int, matched);
    QFETCH(QString, info);
    SearchResultsView::RowData d;
    d.kind = kind;
    d.page = page;
    d.pageCount = pages;
    d.matchedPages = matched;
    QCOMPARE(SearchResultsView::pageInfo(d), info);
}

void TestAtramentSearch::lookFromItsMap()
{
    const SearchLook defaults;
    const SearchLook look = SearchLook::fromMap({{u"match"_s, QColor(u"#aa0000"_s)}, {u"textSize"_s, 17}, {u"uiFamily"_s, u"X"_s}});
    QCOMPARE(look.match, at::rgb(0xaa0000));
    QCOMPARE(look.textSize, 17.0f);
    QCOMPARE(look.faint, defaults.faint);
    QVERIFY(look.uiFamily == u"X");
}

// The kind's icon: a daily note's calendar, a PDF's file, a picture's image, a note's text.
void TestAtramentSearch::kindIcons()
{
    SearchResultsView::RowData d;
    d.kind = u"note"_s;
    QCOMPARE(SearchResultsView::kindIcon(d), u"file-text"_s);
    d.kind = u"pdf"_s;
    QCOMPARE(SearchResultsView::kindIcon(d), u"file"_s);
    d.kind = u"picture"_s;
    QCOMPARE(SearchResultsView::kindIcon(d), u"image"_s);
    d.kind = u"note"_s;
    d.journalDate = u"2026-10-13"_s;
    QCOMPARE(SearchResultsView::kindIcon(d), u"calendar-days"_s);
}

// A result far below made current is shown; a bigger text from the look measures the rows again,
// the row at the view's top staying there.
void TestAtramentSearch::farResultRevealedAndRestyled()
{
    Harness h;
    for (int k = 0; k < 40; ++k)
        h.model.appendRow(result(u"Wynik %1"_s.arg(k), u"remont numer %1"_s.arg(k), {}, span(0, 6)));
    h.tw.settle();
    const int last = h.model.rowCount() - 1;
    QVERIFY(!h.results().partBounds(last, SearchResultsView::Part::Row));
    h.view.update(h.app, [last](SearchResultsView &v, at::Context<SearchResultsView> &cx) { v.revealRow(last, cx); });
    h.tw.settle();
    QVERIFY(h.results().partBounds(last, SearchResultsView::Part::Row));
    QCOMPARE(h.results().currentRow(), last);
    const auto firstDrawn = [&h, last] {
        int r = 0;
        while (r < last && !h.results().partBounds(r, SearchResultsView::Part::Row))
            ++r;
        return r;
    };
    const int top = firstDrawn();
    QVERIFY(top > 0);
    const float before = h.part(top + 1, SearchResultsView::Part::Row).height();
    h.view.update(h.app, [](SearchResultsView &v, at::Context<SearchResultsView> &cx) {
        SearchLook look = v.look();
        look.textSize = 26;
        look.lineHeight = 36;
        v.setLook(look, cx);
    });
    h.tw.settle();
    QCOMPARE(firstDrawn(), top);
    QVERIFY2(h.part(top + 1, SearchResultsView::Part::Row).height() > before + 10,
             qPrintable(u"%1 → %2"_s.arg(before).arg(h.part(top + 1, SearchResultsView::Part::Row).height())));
}

// A row as SearchView's delegate: inset by the side margin less 8 px, padded 12 px across and 8 px
// down, its title's and excerpt's lines 2 px apart; in a narrow window the title stays on one line,
// the excerpt on two at most, the attachment's place on one.
void TestAtramentSearch::laidOutAsTheDelegate()
{
    Harness h;
    const at::Rect row = h.part(0, SearchResultsView::Part::Row);
    const at::Rect title = h.part(0, SearchResultsView::Part::Title);
    const SearchLook &look = h.results().look();
    QCOMPARE(row.left(), look.sideMargin - 8);
    QCOMPARE(row.width(), 600 - 2 * (look.sideMargin - 8));
    QCOMPARE(title.left() - row.left(), 12.0f);
    QCOMPARE(title.top() - row.top(), 8.0f);
    QCOMPARE(row.height(), 8 + look.lineHeight + 2 + look.lineHeight + 8);
    Harness narrow(240);
    const float pdf = narrow.part(2, SearchResultsView::Part::Row).height();
    QVERIFY2(pdf <= 8 + look.lineHeight + 2 + 2 * look.lineHeight + 2 + look.lineHeight + 8 + 0.5f, qPrintable(QString::number(pdf)));
    QCOMPARE(narrow.part(2, SearchResultsView::Part::Title).height(), look.lineHeight);
}

// The matched characters are where the model's positions say — the first one too, "pl" and "ż"
// apart, positions outside the text ignored —, on any row not current.
void TestAtramentSearch::matchesWhereTheModelSays()
{
    Harness h;
    h.model.item(3)->setData(QVariantList{0, 1, 3, -1, 14, 999}, SearchResultModel::ExcerptMatchesRole);
    h.tw.settle();
    const auto matched = [&h] {
        int n = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *g = std::get_if<at::MonochromeSprite>(&p); g && g->color == h.results().look().match)
                ++n;
        return n;
    };
    QCOMPARE(matched(), 12 + 3);
    // Row 0 not current: its title's "Remont" (from position 0) and its excerpt's "remont" too; row
    // 3's in the text's colour now.
    h.view.update(h.app, [](SearchResultsView &v, at::Context<SearchResultsView> &cx) { v.revealRow(3, cx); });
    h.tw.settle();
    QCOMPARE(matched(), 12 + 12);
}

// The pointer over a result not current shows the hover's background, over the current one its
// own; a caption's icon (OCR's, "Close in meaning"'s) drawn in the faint colour.
void TestAtramentSearch::hoverAndCaptionIcons()
{
    Harness h;
    const at::Color hover = h.results().look().hover;
    QCOMPARE(h.quads(hover), 0);
    h.tw.simulateMouseMove(h.part(1, SearchResultsView::Part::Row).center());
    QCOMPARE(h.quads(hover), 1);
    h.tw.simulateMouseMove(h.part(0, SearchResultsView::Part::Row).center());
    QCOMPARE(h.quads(hover), 0);
    QCOMPARE(h.quads(h.results().look().selected), 1);
    const auto faintIcons = [&h](int row) {
        const at::Rect r = h.part(row, SearchResultsView::Part::Row);
        int n = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *path = std::get_if<at::PathPrimitive>(&p);
                path && path->color == h.results().look().faint && r.contains(path->bounds.center()))
                ++n;
        return n;
    };
    QVERIFY(faintIcons(2) > 0); // OCR's
    QVERIFY(faintIcons(3) > 0); // "Close in meaning"'s
    QCOMPARE(faintIcons(0), 0);
}

QTEST_GUILESS_MAIN(TestAtramentSearch)
#include "tst_atrament_search.moc"
