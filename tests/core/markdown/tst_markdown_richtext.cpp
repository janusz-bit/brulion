// The InlineRenderer output as Qt's rich-text engine sees it (QTextDocument is what QML
// `Text { textFormat: Text.RichText }` uses): anchors, colours (also #AARRGGBB), formatting and
// that escaped input stays plain text. Links QtGui, so it runs with a (offscreen) QGuiApplication.
#include "markdown/inlinerenderer.h"

#include <QFontInfo>
#include <QGuiApplication>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

struct Fragment {
    QString text;
    QTextCharFormat format;
};

QList<Fragment> fragments(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    QList<Fragment> out;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.isValid())
                out.append({f.text(), f.charFormat()});
        }
    }
    return out;
}

QString plainText(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    return doc.toPlainText().replace(QChar::LineSeparator, u'\n');
}

const Fragment *find(const QList<Fragment> &frags, const QString &text)
{
    for (const Fragment &f : frags) {
        if (f.text.contains(text))
            return &f;
    }
    return nullptr;
}

InlineRenderer renderer()
{
    RenderContext c;
    c.today = QDate(2026, 10, 14);
    c.resolvePage = [](const QString &target) -> std::optional<QString> {
        return target == u"Brak" ? std::nullopt : std::optional<QString>(target);
    };
    return InlineRenderer(c);
}

} // namespace

class TestRichText : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void links()
    {
        const Palette p;
        const auto frags =
                fragments(renderer().render(u"Zobacz [[Strona]] i [[Brak]] oraz https://example.com/a?b=c&d=e"));
        const Fragment *page = find(frags, u"Strona"_s);
        QVERIFY(page);
        QVERIFY(page->format.isAnchor());
        QCOMPARE(page->format.anchorHref(), u"page:Strona"_s);
        QCOMPARE(page->format.foreground().color(), QColor(p.link));
        QVERIFY(!page->format.fontUnderline());
        const Fragment *missing = find(frags, u"Brak"_s);
        QVERIFY(missing);
        QCOMPARE(missing->format.foreground().color(), QColor(p.unresolved));
        const Fragment *url = find(frags, u"example.com"_s);
        QVERIFY(url);
        QCOMPARE(url->format.anchorHref(), u"ext:https://example.com/a?b=c&d=e"_s);
    }

    void chipsAndTags()
    {
        const Palette p;
        const auto frags = fragments(renderer().render(u"#praca 📅 2026-10-15 ⏰! 2026-10-15 06:30"));
        const Fragment *tag = find(frags, u"#praca"_s);
        QVERIFY(tag);
        QCOMPARE(tag->format.anchorHref(), u"tag:praca"_s);
        QCOMPARE(tag->format.background().color(), QColor(p.tag.background));
        QCOMPARE(tag->format.background().color().alpha(), 0x52);
        const Fragment *due = find(frags, u"jutro"_s);
        QVERIFY(due);
        QVERIFY(due->text.startsWith(QChar(0x00a0)));
        QCOMPARE(due->format.background().color(), QColor(p.due.background));
        QCOMPARE(due->format.foreground().color(), QColor(p.due.foreground));
        const Fragment *alarm = find(frags, u"06:30"_s);
        QVERIFY(alarm);
        QVERIFY(alarm->format.isAnchor());
        QVERIFY(alarm->format.anchorHref().startsWith(u"reminder:"));
        QCOMPARE(alarm->format.background().color(), QColor(p.alarm.background));
        QCOMPARE(alarm->format.foreground().color(), QColor(p.alarm.foreground));
        QVERIFY(!alarm->format.fontUnderline());
    }

    void formatting()
    {
        const Palette p;
        const auto frags = fragments(renderer().render(u"**gruby** *pochyły* ~~skreślony~~ ==ważny== `kod`"));
        QCOMPARE(find(frags, u"gruby"_s)->format.fontWeight(), int(QFont::Bold));
        QVERIFY(find(frags, u"pochyły"_s)->format.fontItalic());
        QVERIFY(find(frags, u"skreślony"_s)->format.fontStrikeOut());
        QCOMPARE(find(frags, u"ważny"_s)->format.background().color(), QColor(p.highlightBackground));
        const Fragment *code = find(frags, u"kod"_s);
        QCOMPARE(code->format.background().color(), QColor(p.codeBackground));
        QVERIFY(code->format.fontFixedPitch()
                || code->format.fontFamilies().toStringList().join(u',').contains(u"mono"));
    }

    void escapedInputStaysText()
    {
        const QString hostile = u"<script>alert(1)</script> <img src=x onerror=alert(1)> <b>nie</b> &amp;"_s;
        const QString html = renderer().render(hostile);
        QCOMPARE(plainText(html), hostile);
        for (const Fragment &f : fragments(html)) {
            QVERIFY(!f.format.isAnchor());
            QVERIFY(f.format.fontWeight() != int(QFont::Bold));
        }
        QCOMPARE(plainText(renderer().render(u"[x](javascript:alert(1))")), u"[x](javascript:alert(1))"_s);
    }

    void lineBreaksAndPolishText()
    {
        QCOMPARE(plainText(renderer().render(u"Zażółć gęślą\njaźń")), u"Zażółć gęślą\njaźń"_s);
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    TestRichText test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_markdown_richtext.moc"
