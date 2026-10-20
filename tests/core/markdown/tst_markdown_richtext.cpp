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

/// The fragment whose text is exactly `text` ("a" would otherwise find "oraz").
const Fragment *findExact(const QList<Fragment> &frags, const QString &text)
{
    for (const Fragment &f : frags) {
        if (f.text == text)
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

    void formulas()
    {
        // A formula as Qt's rich-text engine sees it: Unicode symbols, italic variables (lowercase
        // Greek too, as in TeX), raised scripts (verticalAlignment), an overline for a radical;
        // no delimiters and no LaTeX source left.
        const auto frags = fragments(renderer().render(u"Pole: $\\pi r^2$ i $\\sqrt{x}$ oraz $\\frac{a}{b}$"));
        const Fragment *pi = find(frags, u"π"_s);
        QVERIFY(pi);
        QVERIFY(pi->format.fontItalic());
        const Fragment *r = find(frags, u"r"_s);
        QVERIFY(r->format.fontItalic());
        const Fragment *square = find(frags, u"2"_s);
        QCOMPARE(square->format.verticalAlignment(), QTextCharFormat::AlignSuperScript);
        const Fragment *root = find(frags, u"√"_s);
        QVERIFY(root);
        const Fragment *radicand = find(frags, u"x"_s);
        QVERIFY(radicand->format.fontOverline());
        const Fragment *numerator = findExact(frags, u"a"_s);
        QVERIFY(numerator);
        QCOMPARE(numerator->format.verticalAlignment(), QTextCharFormat::AlignSuperScript);
        const Fragment *denominator = findExact(frags, u"b"_s);
        QVERIFY(denominator);
        QCOMPARE(denominator->format.verticalAlignment(), QTextCharFormat::AlignSubScript);
        QCOMPARE(plainText(renderer().render(u"$\\alpha \\le \\beta$")), u"α ≤ β"_s);
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
