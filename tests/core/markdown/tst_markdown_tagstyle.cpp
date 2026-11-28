// markdown::tagStyleOf: the style a block gets from its tags (an extension's b.app.tagStyle,
// docs/EXTENSIONS.md "Styles of blocks by tag") — the first tag that has one, itself or a parent
// (`#projekt/dom` by `projekt`), in any case; never for code, the frontmatter, HTML or a rule. One
// decision for the editor's rows and the exports.
#include "markdown/tagstyle.h"

#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

class TestTagStyle : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void styleOf_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("tint"); // "" none
        const int item = int(BlockKind::ListItem);
        QTest::newRow("its tag") << item << u"Zapłacić czynsz #pilne"_s << u"danger"_s;
        QTest::newRow("any case") << item << u"Zapłacić #PILNE"_s << u"danger"_s;
        QTest::newRow("the first tag that has one") << item << u"#dom #projekt"_s << u"#12ab34"_s;
        QTest::newRow("a parent tag") << item << u"Remont #projekt/kuchnia"_s << u"#12ab34"_s;
        QTest::newRow("its own over the parent's") << item << u"#ważne/dziś"_s << u"info"_s;
        QTest::newRow("a Polish parent") << item << u"#ważne/jutro"_s << u"warning"_s;
        QTest::newRow("no styled tag") << item << u"Zwykły blok #dom"_s << QString();
        QTest::newRow("no tag") << item << u"Bez tagów"_s << QString();
        QTest::newRow("in a code span: no tag") << item << u"`#pilne` w kodzie"_s << QString();
        QTest::newRow("a heading") << int(BlockKind::Heading) << u"Plan #pilne"_s << u"danger"_s;
        QTest::newRow("a paragraph") << int(BlockKind::Paragraph) << u"Akapit #pilne"_s << u"danger"_s;
        QTest::newRow("code") << int(BlockKind::Code) << u"#pilne"_s << QString();
        QTest::newRow("the frontmatter") << int(BlockKind::Frontmatter) << u"tags: #pilne"_s << QString();
        QTest::newRow("HTML") << int(BlockKind::Html) << u"<b>#pilne</b>"_s << QString();
        QTest::newRow("a rule") << int(BlockKind::Rule) << u"---"_s << QString();
    }
    void styleOf()
    {
        QFETCH(int, kind);
        QFETCH(QString, text);
        QFETCH(QString, tint);
        const TagStyles styles{{u"pilne"_s, {u"danger"_s, true, true}},
                               {u"projekt"_s, {u"#12ab34"_s, false, true}},
                               {u"ważne"_s, {u"warning"_s, true, false}},
                               {u"ważne/dziś"_s, {u"info"_s, true, true}}};
        const std::optional<TagStyle> style = tagStyleOf(BlockKind(kind), text, styles);
        QCOMPARE(style ? style->tint : QString(), tint);
    }

    void theStyleAsDeclared()
    {
        const TagStyles styles{{u"projekt"_s, {u"#12ab34"_s, false, true}}};
        QCOMPARE(tagStyleOf(BlockKind::ListItem, u"#projekt"_s, styles), (TagStyle{u"#12ab34"_s, false, true}));
        QCOMPARE(tagStyleOf(BlockKind::ListItem, u"#projekt"_s, {}), std::nullopt);
    }
};

QTEST_GUILESS_MAIN(TestTagStyle)
#include "tst_markdown_tagstyle.moc"
