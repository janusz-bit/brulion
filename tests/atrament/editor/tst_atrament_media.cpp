// Pictures, files, embedded blocks and query results in the block editor drawn by Atrament
// (src/app/editor/atrament/mediarows.cpp, queryrows.cpp, picturecache.h; docs/ATRAMENT.md "The
// block editor in Atrament (M2)", step 5): a page of a PageController over FakeEditorHost — its
// attachments written into the host's files — drawn headless on Atrament's test platform with the
// CPU renderer. A picture's row as tall before its picture is decoded as after, its pixels drawn, its
// alt text for screen readers, a click opening it and a click beside it editing the block, the
// memory held while shown, chips of other files, a PDF's thumbnail, the kinds Atrament does not draw
// staying the QML delegate's; embedded blocks in their frame with links and a working task box;
// a query's results with their task boxes, links and items opening.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "picturecache.h"
#include "queryresultmodel.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QBuffer>
#include <QImage>
#include <QImageWriter>
#include <QTest>

#include <functional>
#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Media.md"_s;

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf", "BrulionEmoji.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Emoji", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

QByteArray png(int w, int h, QColor colour)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter(&buffer, "png").write(image);
    return bytes;
}

constexpr unsigned AllKinds = PageView::Pictures | PageView::Files | PageView::Transclusions | PageView::Queries;

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;
    std::shared_ptr<PictureCache> cache = std::make_shared<PictureCache>();

    // `setup` puts the attachments and other pages into the host before the page loads.
    Harness(const QString &content, const std::function<void(FakeEditorHost &)> &setup, at::Size size = {600, 700},
            unsigned kinds = AllKinds)
        : tw(platform->newWindow(size, 1))
    {
        setup(host);
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000); // saved by the tests only
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this, kinds](PageView &v, at::Context<PageView> &cx) {
            v.setController(&controller, cx);
            v.setPictureCache(cache, cx);
            v.setDrawsMedia(kinds, PageView::MediaLook{}, cx);
        });
        app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    at::Window &window() { return *app.windows().front(); }
    // The picture frames of row `row` in the last frame.
    std::vector<PageView::PictureFrame> frames(int row)
    {
        std::vector<PageView::PictureFrame> out;
        for (const PageView::PictureFrame &f : view().pictureFrames())
            if (f.row == row)
                out.push_back(f);
        return out;
    }
    // Spins the event loop (the decodes come back through it) and draws what they asked for.
    bool waitDecoded(int row, int count = 1)
    {
        for (int i = 0; i < 500; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            tw.settle();
            const auto f = frames(row);
            if (int(f.size()) >= count
                && std::all_of(f.begin(), f.end(), [](const PageView::PictureFrame &p) { return p.decoded; }))
                return true;
            QTest::qWait(5);
        }
        return false;
    }
    const at::AccessibilityNode *node(at::Role role, std::u16string_view name)
    {
        for (const at::AccessibilityNode *n : window().accessibilityTree().nodes())
            if (n->role == role && n->name.find(name) != std::u16string_view::npos)
                return n;
        return nullptr;
    }
};

void noSetup(FakeEditorHost &) { }

} // namespace

class TestAtramentMedia : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void pictureKeepsItsSizeAndIsDrawn();
    void pictureNamedForScreenReaders();
    void pictureClickOpensBesideEdits();
    void widePictureFitsTheColumn();
    void heldWhileShown();
    void undecodablePictureNamesItsFormat();
    void foreignKindsStayTheDelegates();
    void fileChipsAndPdfThumbnails();
    void editedRowKeepsItsPictures();
    void transclusionDrawnWithLinksAndTasks();
    void queryResultsDrawn();
    void queryEditedIsTheDelegates();
};

void TestAtramentMedia::pictureKeepsItsSizeAndIsDrawn()
{
    QString path;
    Harness h(u"Łąka o świcie:\n\n![[łąka.png]]\n\nKoniec.\n"_s,
              [&path](FakeEditorHost &host) { path = host.saveAttachment(u"łąka.png"_s, png(400, 300, QColor(220, 20, 30))); });
    QCOMPARE(path, u"assets/łąka.png"_s);
    QVERIFY(h.view().drawsRow(1));
    QVERIFY(h.view().foreignRows().empty());
    // Before its pixels: the frame of the file's size (400 × 300, from its header).
    auto f = h.frames(1);
    QCOMPARE(int(f.size()), 1);
    QVERIFY(!f[0].decoded);
    QCOMPARE(f[0].bounds.width(), 400.0f);
    QCOMPARE(f[0].bounds.height(), 300.0f);
    const at::Rect before = f[0].bounds;
    const float rowBefore = h.view().rowBounds(1)->height();
    const float below = h.view().rowBounds(2)->top();
    // Decoded on a worker thread, then drawn: nothing moved.
    QVERIFY(h.waitDecoded(1));
    f = h.frames(1);
    QCOMPARE(f[0].bounds.left(), before.left());
    QCOMPARE(f[0].bounds.top(), before.top());
    QCOMPARE(f[0].bounds.height(), before.height());
    QCOMPARE(h.view().rowBounds(1)->height(), rowBefore);
    QCOMPARE(h.view().rowBounds(2)->top(), below);
    // Its pixels, in the frame (decoded at the frame's side: 448 px steps, never enlarged).
    const at::Pixel p = h.tw.pixel(int(before.center().x), int(before.center().y));
    QVERIFY2(p.r > 200 && p.g < 50 && p.b < 60, qPrintable(u"%1 %2 %3"_s.arg(p.r).arg(p.g).arg(p.b)));
    QCOMPARE(h.cache->count(), 1);
    QCOMPARE(h.cache->bytes(), qint64(400 * 300 * 4));
}

void TestAtramentMedia::pictureNamedForScreenReaders()
{
    Harness h(u"- Ogród ![Kwitnąca jabłoń w sadzie](assets/jabłoń.png)\n"_s,
              [](FakeEditorHost &host) { host.saveAttachment(u"jabłoń.png"_s, png(120, 90, Qt::darkGreen)); });
    QVERIFY(h.view().drawsRow(0));
    h.window().setAccessibilityEnabled(true);
    h.tw.settle();
    const at::AccessibilityNode *image = h.node(at::Role::Image, u"Kwitnąca jabłoń");
    QVERIFY2(image, qPrintable(str(h.window().accessibilityTree().dump())));
    // The row's text is read too (its picture below it).
    QVERIFY(str(h.window().accessibilityTree().dump()).contains(u"Ogród"_s));
}

void TestAtramentMedia::pictureClickOpensBesideEdits()
{
    Harness h(u"![[żaba.png]]\n\nDalej.\n"_s,
              [](FakeEditorHost &host) { host.saveAttachment(u"żaba.png"_s, png(200, 100, Qt::green)); });
    int clickedRow = -1;
    QVariantMap clicked;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onEmbedClick = [&](int row, const QVariantMap &embed) {
            clickedRow = row;
            clicked = embed;
        };
    });
    const auto f = h.frames(0);
    QCOMPARE(int(f.size()), 1);
    // On the picture: the embed, as EmbedView's clicked(target) (the editor opens the viewer).
    h.tw.simulateClick(f[0].bounds.center());
    QCOMPARE(clickedRow, 0);
    QCOMPARE(clicked.value(u"target"_s).toString(), u"żaba.png"_s);
    QCOMPARE(h.controller.editingRow(), -1);
    // Beside it: the block is edited (BlockDelegate.editOnTap(-1)), its text with the picture under it.
    h.tw.simulateClick({f[0].bounds.right() + 60, f[0].bounds.center().y});
    QCOMPARE(h.controller.editingRow(), 0);
    QVERIFY(h.view().input());
    QCOMPARE(str(h.view().input()->read(h.app).text()), u"![[żaba.png]]"_s);
    QCOMPARE(int(h.frames(0).size()), 1);
}

void TestAtramentMedia::widePictureFitsTheColumn()
{
    // 1600 × 400 in a 600 px window: as wide as the column (from the text's edge), its shape kept.
    Harness h(u"![[panorama.png]]\n"_s,
              [](FakeEditorHost &host) { host.saveAttachment(u"panorama.png"_s, png(1600, 400, Qt::blue)); });
    auto f = h.frames(0);
    QCOMPARE(int(f.size()), 1);
    const float column = 600 - f[0].bounds.left();
    QVERIFY2(std::abs(f[0].bounds.width() - column) < 1, qPrintable(QString::number(f[0].bounds.width())));
    QVERIFY2(std::abs(f[0].bounds.height() - column / 4) < 1.5f, qPrintable(QString::number(f[0].bounds.height())));
    // A tall one: at most 420 px high.
    Harness tall(u"![[wieża.png]]\n"_s,
                 [](FakeEditorHost &host) { host.saveAttachment(u"wieża.png"_s, png(300, 900, Qt::gray)); });
    f = tall.frames(0);
    QCOMPARE(f[0].bounds.height(), 420.0f);
    QCOMPARE(f[0].bounds.width(), 140.0f);
    // Decoded for the frame, not the file: 140 × 420 shown, 2048 not needed.
    QVERIFY(tall.waitDecoded(0));
    QVERIFY(tall.cache->bytes() <= qint64(160 * 448 * 4));
}

void TestAtramentMedia::heldWhileShown()
{
    // A budget smaller than a screenful: the pictures shown stay (no decode in every frame), those
    // scrolled away go.
    QString content;
    for (int k = 0; k < 6; ++k)
        content += u"![[zdjęcie%1.png]]\n\n"_s.arg(k);
    Harness h(content, [](FakeEditorHost &host) {
        for (int k = 0; k < 6; ++k)
            host.saveAttachment(u"zdjęcie%1.png"_s.arg(k), png(200, 200, QColor(10 * k, 100, 200)));
    });
    h.cache->setBudget(200 * 200 * 4);
    // Every picture in view decoded (a few of them: more than the budget holds).
    std::vector<int> shownRows;
    for (const PageView::PictureFrame &f : h.view().pictureFrames())
        shownRows.push_back(f.row);
    QVERIFY(shownRows.size() >= 2);
    for (const int row : shownRows)
        QVERIFY(h.waitDecoded(row));
    const qint64 shown = h.cache->bytes();
    QCOMPARE(shown, qint64(shownRows.size()) * 200 * 200 * 4); // over the budget: held
    // Settled: nothing decoded again.
    h.tw.settle();
    QCoreApplication::processEvents();
    h.tw.settle();
    QCOMPARE(h.cache->pending(), 0);
    QCOMPARE(h.cache->bytes(), shown);
    // Scrolled to the end: the first pictures are let go of and trimmed as the next ones come.
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &c) {
        v.handle().scrollToRow(5, at::ListPosition::End);
        c.notify();
    });
    h.tw.settle();
    QVERIFY(h.waitDecoded(5));
    h.tw.settle();
    QVERIFY(h.waitDecoded(5));
    QVERIFY(!h.cache->contains(u"file:assets/zdjęcie0.png"_s));
}

void TestAtramentMedia::undecodablePictureNamesItsFormat()
{
    Harness h(u"![[mapa.webp]]\n"_s, [](FakeEditorHost &host) {
        host.setUndecodablePictureFormats({u"WEBP"_s});
        host.saveAttachment(u"mapa.webp"_s, QByteArray("RIFF....WEBP"));
    });
    // Drawn: its frame (320 × 180, no size known) says what it is.
    QVERIFY(h.view().drawsRow(0));
    const auto f = h.frames(0);
    QCOMPARE(int(f.size()), 1);
    QCOMPARE(f[0].bounds.width(), 320.0f);
    QCOMPARE(f[0].bounds.height(), 180.0f);
    QCOMPARE(h.cache->pending(), 0); // (nothing to decode)
    h.window().setAccessibilityEnabled(true);
    h.tw.settle();
    QVERIFY(h.node(at::Role::Image, u"mapa.webp"));
}

void TestAtramentMedia::foreignKindsStayTheDelegates()
{
    // A recording (a player), a drawing (its preview from an image provider), a picture from the
    // internet not shown yet: the QML delegate's rows, text included — and not Atrament's to edit.
    Harness h(u"- Nagranie ![[Nagranie łąki.m4a]]\n- ![](https://example.com/kot.jpg)\n- Zwykły punkt\n"_s,
              [](FakeEditorHost &host) { host.saveAttachment(u"Nagranie łąki.m4a"_s, QByteArray("ftyp")); });
    QVERIFY(!h.view().drawsRow(0));
    QVERIFY(!h.view().drawsRow(1));
    QVERIFY(h.view().drawsRow(2));
    QVERIFY(!h.view().editsRow(0));
    QVERIFY(!h.view().editsRow(1));
    QVERIFY(h.view().editsRow(2));
    const std::vector<std::size_t> foreign = h.view().foreignRows();
    QCOMPARE(foreign, (std::vector<std::size_t>{0, 1}));
    // Switched off: a picture's row is the delegate's as before.
    Harness off(u"![[łąka.png]]\n"_s,
                [](FakeEditorHost &host) { host.saveAttachment(u"łąka.png"_s, png(40, 40, Qt::red)); }, {600, 400},
                PageView::Files | PageView::Transclusions | PageView::Queries);
    QVERIFY(!off.view().drawsRow(0));
    QVERIFY(!off.view().editsRow(0));
}

void TestAtramentMedia::fileChipsAndPdfThumbnails()
{
    QString thumb;
    Harness h(u"- Umowa ![[umowa najmu.docx]]\n- Wykład ![[wykład.pdf#page=3]]\n"_s, [&thumb](FakeEditorHost &host) {
        host.saveAttachment(u"umowa najmu.docx"_s, QByteArray("PK"));
        host.saveAttachment(u"wykład.pdf"_s, QByteArray("%PDF-1.7"));
        const QString picture = host.saveAttachment(u"strona.png"_s, png(212, 300, Qt::white));
        thumb = host.assetUrl(picture).toString();
        host.setPdfViewable(true);
        host.setPdfThumbnail(u"assets/wykład.pdf"_s,
                             QVariantMap{{u"state"_s, u"ready"_s},
                                         {u"url"_s, QUrl(thumb)},
                                         {u"width"_s, 212},
                                         {u"height"_s, 300},
                                         {u"pages"_s, 12},
                                         {u"pageWidth"_s, 595.0},
                                         {u"pageHeight"_s, 842.0}});
    });
    QVERIFY(h.view().drawsRow(0));
    QVERIFY(h.view().drawsRow(1));
    QVariantMap clicked;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onEmbedClick = [&](int, const QVariantMap &embed) { clicked = embed; };
    });
    h.window().setAccessibilityEnabled(true);
    h.tw.settle();
    // The file's chip: a button named by the file.
    const at::AccessibilityNode *chip = h.node(at::Role::Button, u"umowa najmu.docx");
    QVERIFY2(chip, qPrintable(str(h.window().accessibilityTree().dump())));
    h.tw.simulateClick(chip->bounds.center());
    QCOMPARE(clicked.value(u"target"_s).toString(), u"umowa najmu.docx"_s);
    // The PDF's page: A4's shape at 297 px wide, its thumbnail drawn, a click opens page 3.
    auto f = h.frames(1);
    QCOMPARE(int(f.size()), 1);
    QCOMPARE(f[0].bounds.width(), 297.0f);
    QVERIFY2(std::abs(f[0].bounds.height() - 297 * 842.0f / 595) < 1.5f, qPrintable(QString::number(f[0].bounds.height())));
    QVERIFY(h.waitDecoded(1));
    const at::AccessibilityNode *pdf = h.node(at::Role::Button, u"wykład.pdf");
    QVERIFY(pdf);
    h.tw.simulateClick(h.frames(1)[0].bounds.center());
    QVERIFY(clicked.value(u"pdf"_s).toBool());
    QCOMPARE(clicked.value(u"page"_s).toInt(), 3);
}

void TestAtramentMedia::editedRowKeepsItsPictures()
{
    Harness h(u"- Zdjęcie ![[pies.png]] z wakacji\n- drugi\n"_s,
              [](FakeEditorHost &host) { host.saveAttachment(u"pies.png"_s, png(160, 120, Qt::yellow)); });
    QVERIFY(h.view().editsRow(0));
    const float height = h.view().rowBounds(0)->height();
    h.controller.startEditing(0, 3);
    h.tw.settle();
    QCOMPARE(h.view().editedRow(), 0);
    QVERIFY(h.view().input());
    QCOMPARE(int(h.frames(0).size()), 1);
    // Typing reaches the controller; the picture stays under the field.
    h.tw.simulateTyping(u"ś");
    QCOMPARE(h.model()->editTextAt(0), u"Zdjśęcie ![[pies.png]] z wakacji"_s);
    QCOMPARE(int(h.frames(0).size()), 1);
    QVERIFY(std::abs(h.view().rowBounds(0)->height() - height) < 40);
}

void TestAtramentMedia::transclusionDrawnWithLinksAndTasks()
{
    Harness h(u"- Przepis na dziś:\n- ![[Przepisy#Zupa]]\n"_s, [](FakeEditorHost &host) {
        host.setFile(u"Przepisy.md"_s,
                     u"# Zupa\n- Pokrój marchew i [[Seler|seler]]\n- [ ] Ugotuj ziemniaki\n# Deser\n- Lody\n"_s);
    });
    QVERIFY(h.view().drawsRow(1));
    int linkRow = -1;
    QString link, linkPath;
    int linkLine = -1;
    QString opened;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onEmbeddedLink = [&](int row, const QString &l, const QString &p, int line, const QString &) {
            linkRow = row;
            link = l;
            linkPath = p;
            linkLine = line;
        };
        v.onLink = [&](int, const QString &href) { opened = href; };
    });
    h.window().setAccessibilityEnabled(true);
    h.tw.settle();
    const QString tree = str(h.window().accessibilityTree().dump());
    // The frame named as TransclusionView's, its header a link to the section, the section's blocks.
    QVERIFY2(h.node(at::Role::Group, u"Przepisy › Zupa"), qPrintable(tree));
    QVERIFY2(tree.contains(u"Pokrój marchew"_s), qPrintable(tree));
    QVERIFY2(!tree.contains(u"Lody"_s), qPrintable(tree)); // (the next section is not embedded)
    const at::AccessibilityNode *header = h.node(at::Role::Link, u"Przepisy › Zupa");
    QVERIFY(header);
    h.tw.simulateClick(header->bounds.center());
    QVERIFY2(opened.startsWith(u"page:"_s) && opened.contains(u"Przepisy"_s), qPrintable(opened));
    // A link of the embedded text: its block's file and line go with it.
    const at::AccessibilityNode *seler = h.node(at::Role::Link, u"seler");
    QVERIFY2(seler, qPrintable(tree));
    h.tw.simulateClick(seler->bounds.center());
    QCOMPARE(linkRow, 1);
    QVERIFY2(link.contains(u"Seler"_s), qPrintable(link));
    QCOMPARE(linkPath, u"Przepisy.md"_s);
    QCOMPARE(linkLine, 1);
    // Its task's box changes the task in its own file.
    const at::AccessibilityNode *box = h.node(at::Role::CheckBox, u"Ugotuj ziemniaki");
    QVERIFY2(box, qPrintable(tree));
    QVERIFY(!box->states.checked);
    h.tw.simulateClick(box->bounds.center());
    QTRY_VERIFY2(h.host.file(u"Przepisy.md"_s).contains(u"- [x] Ugotuj ziemniaki"_s), qPrintable(h.host.file(u"Przepisy.md"_s)));
}

void TestAtramentMedia::queryResultsDrawn()
{
    Harness h(u"Zakupy na weekend:\n\n```query\ntype:tasks tag:zakupy\n```\n"_s, [](FakeEditorHost &host) {
        host.setFile(u"Listy/Sklep.md"_s, u"- [ ] Kupić mleko #zakupy\n- [ ] Kupić chleb w [[Piekarnia]] #zakupy\n"
                                          u"- [ ] Zadzwonić do Łucji\n"_s);
    });
    QCOMPARE(h.model()->index(1).data(BlockListModel::ViewRole).toString(), u"query"_s);
    QVERIFY(h.view().drawsRow(1));
    QueryResultModel *results = h.view().queryResults(1);
    QVERIFY(results);
    QTRY_VERIFY(results->ready());
    h.tw.settle();
    QCOMPARE(results->itemCount(), 2);
    int answeredRow = -1;
    QVariantMap answer;
    int linkItem = -1;
    QString link;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onQueryAnswer = [&](int row, const QVariantMap &a) {
            answeredRow = row;
            answer = a;
        };
        v.onQueryLink = [&](int, int item, const QString &l, const QString &, int, const QString &) {
            linkItem = item;
            link = l;
        };
    });
    h.window().setAccessibilityEnabled(true);
    h.tw.settle();
    const QString tree = str(h.window().accessibilityTree().dump());
    QVERIFY2(h.node(at::Role::Group, u"Query"), qPrintable(tree));
    QVERIFY2(tree.contains(u"2 task(s)"_s), qPrintable(tree));
    QVERIFY2(!tree.contains(u"Zadzwonić"_s), qPrintable(tree));
    // A result's link: the item and the link go to the host.
    const at::AccessibilityNode *bakery = h.node(at::Role::Link, u"Piekarnia");
    QVERIFY2(bakery, qPrintable(tree));
    h.tw.simulateClick(bakery->bounds.center());
    QVERIFY2(link.contains(u"Piekarnia"_s), qPrintable(link));
    QVERIFY(linkItem >= 0);
    // A task box: the task changes in its own file.
    const at::AccessibilityNode *milk = h.node(at::Role::CheckBox, u"Kupić mleko");
    QVERIFY2(milk, qPrintable(tree));
    h.tw.simulateClick(milk->bounds.center());
    QTRY_VERIFY2(h.host.file(u"Listy/Sklep.md"_s).contains(u"- [x] Kupić mleko"_s), qPrintable(h.host.file(u"Listy/Sklep.md"_s)));
    // A click on an item opens it (another page: the host navigates there).
    const at::AccessibilityNode *item = h.node(at::Role::ListItem, u"Kupić chleb");
    QVERIFY2(item, qPrintable(tree));
    h.tw.simulateClick({item->bounds.right() - 4, item->bounds.top() + 6});
    QCOMPARE(answeredRow, 1);
    QCOMPARE(h.host.lastRoute().value(u"path"_s).toString(), u"Listy/Sklep.md"_s);
}

void TestAtramentMedia::queryEditedIsTheDelegates()
{
    Harness h(u"Akapit.\n\n```query\ntype:tasks\n```\n"_s, noSetup);
    QVERIFY(h.view().drawsRow(1));
    QVERIFY(!h.view().editsRow(1));
    // A click on its header edits the query: the QML delegate's InlineEditor takes the row.
    h.controller.startEditing(1, -1);
    h.tw.settle();
    QVERIFY(!h.view().drawsRow(1));
    const std::vector<std::size_t> foreign = h.view().foreignRows();
    QCOMPARE(foreign, (std::vector<std::size_t>{1}));
    h.controller.stopEditing();
    h.tw.settle();
    QVERIFY(h.view().drawsRow(1));
}

QTEST_MAIN(TestAtramentMedia)
#include "tst_atrament_media.moc"
