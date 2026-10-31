// RichTextLinks (src/app/editor/richtextlinks.h) on real InlineRenderer HTML in an offscreen
// QGuiApplication: one entry per anchor in document order (each path part of [[A/B/C]] too, a
// label with bold words in it one link, the same link twice two), the task checkbox and the
// separator left out, rectangles from the layout — one per line of a wrapped link, at the fixed
// line height —, the Text mode laying the HTML out as the TextEdit mode reads it, nothing laid
// out while disabled, and elided rows cut at maxHeight.
#include "markdown/inline.h"
#include "markdown/inlinerenderer.h"
#include "richtextlinks.h"

#include <QFont>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace brulion::editor;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

constexpr qreal LineHeight = 24;

/// Rendered with the path parts: "Firma" a page, anything else new.
QString render(const QString &markdown)
{
    RenderContext c;
    c.today = QDate(2026, 10, 14);
    c.resolveLinkPart = [](const QString &target, int part) {
        const QList<LinkPathPart> parts = linkPathParts(target);
        return part >= 1 && part < parts.size() && target.left(parts[part - 1].to) == u"Firma"
                       ? LinkPartTarget::Kind::Page
                       : LinkPartTarget::Kind::New;
    };
    return InlineRenderer(c).render(markdown);
}

QFont testFont()
{
    QFont f;
    f.setFamily(u"DejaVu Sans"_s);
    f.setPixelSize(16);
    return f;
}

/// A document as a rich-text TextEdit holds it: the HTML at a width, every block at the fixed
/// line height (RichTextPills does that for BlockText's TextEdit).
std::unique_ptr<QTextDocument> documentOf(const QString &html, qreal width, qreal lineHeight = LineHeight)
{
    auto doc = std::make_unique<QTextDocument>();
    doc->setDocumentMargin(0);
    doc->setDefaultFont(testFont());
    doc->setHtml(html);
    QTextCursor cursor(doc.get());
    cursor.select(QTextCursor::Document);
    QTextBlockFormat f;
    f.setLineHeight(lineHeight, QTextBlockFormat::FixedHeight);
    cursor.mergeBlockFormat(f);
    doc->setTextWidth(width);
    return doc;
}

QStringList texts(const QList<RichTextLinks::Link> &links)
{
    QStringList out;
    for (const auto &l : links)
        out.append(l.text);
    return out;
}

QStringList hrefs(const QList<RichTextLinks::Link> &links)
{
    QStringList out;
    for (const auto &l : links)
        out.append(l.href);
    return out;
}

} // namespace

class TestRichTextLinks : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void pathPartsAreLinksOfTheirOwn()
    {
        const auto doc = documentOf(render(u"[ ] Zapytać się [[Firma/Adam cośtam]] o to"_s), 800);
        const auto links = RichTextLinks::linksOf(doc.get(), LineHeight);
        // The task checkbox (task:toggle) and the separator are no entries.
        QCOMPARE(texts(links), (QStringList{u"Firma"_s, u"Adam cośtam"_s}));
        QCOMPARE(hrefs(links),
                 (QStringList{u"part:1:Firma%2FAdam%20co%C5%9Btam"_s, u"page:Firma%2FAdam%20co%C5%9Btam"_s}));
        // On one line, the part left of the page.
        QCOMPARE(links[0].rect.y(), links[1].rect.y());
        QVERIFY(links[0].rect.right() < links[1].rect.left());
        QCOMPARE(links[0].rect.height(), LineHeight);
    }

    void aLabelWithMarkupIsOneLink()
    {
        const auto doc = documentOf(render(u"[**pogrubiony** link](https://e.pl)"_s), 800);
        const auto links = RichTextLinks::linksOf(doc.get(), LineHeight);
        QCOMPARE(texts(links), QStringList{u"pogrubiony link"_s});
        QCOMPARE(hrefs(links), QStringList{u"ext:https://e.pl"_s});
    }

    void theSameLinkTwiceIsTwoLinks()
    {
        const auto doc = documentOf(render(u"[[Strona]] i znowu [[Strona]]"_s), 800);
        const auto links = RichTextLinks::linksOf(doc.get(), LineHeight);
        QCOMPARE(texts(links), (QStringList{u"Strona"_s, u"Strona"_s}));
        QVERIFY(links[0].rect.right() < links[1].rect.left());
    }

    void aWrappedLinkHasALineEach()
    {
        // Narrow: the long last part wraps; its first segment is `rect`.
        const auto doc = documentOf(render(u"[[Firma/Zażółć gęślą jaźń zażółć gęślą jaźń]]"_s), 120);
        const auto links = RichTextLinks::linksOf(doc.get(), LineHeight);
        QCOMPARE(links.size(), 2);
        QCOMPARE(links[1].text, u"Zażółć gęślą jaźń zażółć gęślą jaźń"_s); // whole graphemes, Polish kept
        QVERIFY2(links[1].rects.size() >= 2, qPrintable(QString::number(links[1].rects.size())));
        QCOMPARE(links[1].rect, links[1].rects.constFirst());
        // The fixed line height: the next line starts one line lower.
        QCOMPARE(links[1].rects[1].y() - links[1].rects[0].y(), LineHeight);
        for (const QRectF &r : links[1].rects)
            QVERIFY(r.right() <= 120.5);
    }

    void textModeMatchesTheDocument()
    {
        // A Text has no layout API: the HTML laid out here gives the TextEdit's rectangles.
        const QString html = render(u"Zapytać się [[Firma/Adam cośtam]] o to, czego się pouczyć [[Nowa/Strona]] dziś"_s);
        const auto doc = documentOf(html, 260);
        const auto expected = RichTextLinks::linksOf(doc.get(), LineHeight);
        RichTextLinks links;
        links.setFont(testFont());
        links.setWidth(260);
        links.setLineHeight(LineHeight);
        links.setHtml(html);
        const auto got = links.linkList();
        QCOMPARE(got.size(), expected.size());
        QCOMPARE(got.size(), 4);
        for (qsizetype k = 0; k < got.size(); ++k) {
            QCOMPARE(got[k].href, expected[k].href);
            QVERIFY2(qAbs(got[k].rect.x() - expected[k].rect.x()) <= 0.5, qPrintable(got[k].text));
            QVERIFY2(qAbs(got[k].rect.y() - expected[k].rect.y()) <= 0.5, qPrintable(got[k].text));
            QVERIFY2(qAbs(got[k].rect.width() - expected[k].rect.width()) <= 0.5, qPrintable(got[k].text));
        }
        // The QML shape.
        const QVariantMap first = links.links().constFirst().toMap();
        QCOMPARE(first.value(u"href"_s).toString(), u"part:1:Firma%2FAdam%20co%C5%9Btam"_s);
        QCOMPARE(first.value(u"text"_s).toString(), u"Firma"_s);
        QCOMPARE(first.value(u"height"_s).toReal(), LineHeight);
        QVERIFY(!first.value(u"rects"_s).toList().isEmpty());
    }

    void attachedDocumentFollowsItsLayout()
    {
        auto doc = documentOf(render(u"[[Firma/Adam]]"_s), 800);
        RichTextLinks links;
        QSignalSpy changed(&links, &RichTextLinks::linksChanged);
        links.attach(doc.get());
        QCOMPARE(texts(links.linkList()), (QStringList{u"Firma"_s, u"Adam"_s}));
        doc->setHtml(render(u"[[Inna]]"_s));
        QCOMPARE(texts(links.linkList()), QStringList{u"Inna"_s});
        QVERIFY(changed.count() >= 2);
        doc.reset(); // destroyed: no links, no crash
        QVERIFY(links.linkList().isEmpty());
    }

    void disabledLaysNothingOut()
    {
        RichTextLinks links;
        links.setEnabled(false);
        links.setFont(testFont());
        links.setWidth(400);
        links.setHtml(render(u"[[Firma/Adam]]"_s));
        QVERIFY(links.linkList().isEmpty());
        QVERIFY(links.links().isEmpty());
        QSignalSpy changed(&links, &RichTextLinks::linksChanged);
        links.setEnabled(true);
        QCOMPARE(links.linkList().size(), 2);
        QCOMPARE(changed.count(), 1);
        links.setEnabled(false);
        QVERIFY(links.linkList().isEmpty());
    }

    void maxHeightDropsWhatIsBelow()
    {
        RichTextLinks links;
        links.setFont(testFont());
        links.setWidth(400);
        links.setLineHeight(LineHeight);
        links.setHtml(render(u"[[Pierwsza]]\n[[Druga]]\n[[Trzecia]]"_s));
        QCOMPARE(texts(links.linkList()), (QStringList{u"Pierwsza"_s, u"Druga"_s, u"Trzecia"_s}));
        links.setMaxHeight(2 * LineHeight);
        QCOMPARE(texts(links.linkList()), (QStringList{u"Pierwsza"_s, u"Druga"_s}));
        links.setMaxHeight(0);
        QCOMPARE(links.linkList().size(), 3);
    }

    void hostileAndEmptyInput()
    {
        RichTextLinks links;
        links.setWidth(300);
        links.setHtml(QString());
        QVERIFY(links.linkList().isEmpty());
        links.setHtml(render(u"[[<b>/x\"&</b>]] <a href=\"javascript:alert(1)\">x</a>"_s));
        // The note's raw HTML is text: only the renderer's anchors are links.
        for (const auto &l : links.linkList())
            QVERIFY2(!l.href.startsWith(u"javascript"), qPrintable(l.href));
        QCOMPARE(links.linkList().size(), 3);
        links.attach(nullptr);
        links.setHtml(u"plain text"_s);
        QVERIFY(links.linkList().isEmpty());
    }
};

QTEST_MAIN(TestRichTextLinks)
#include "tst_editor_richlinks.moc"
