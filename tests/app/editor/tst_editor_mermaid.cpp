// MermaidView (src/app/editor/mermaidview.h): a ```mermaid block laid out in the app's font and
// painted in the theme's colours; status and translated messages; the shared layout cache;
// the size for the item's width. BRULION_MERMAID_SHOTS=<dir> saves the painted samples as PNG.
#include "mermaidview.h"

#include "diagram/diagram.h"

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::editor::MermaidView;

namespace {

QImage paintView(MermaidView &v, qreal width, bool dark)
{
    v.setWidth(width);
    v.setHeight(v.implicitHeight());
    v.setDark(dark);
    if (dark) {
        v.setTextColor(QColor(u"#e8e4da"_s));
        v.setMutedColor(QColor(u"#a8a395"_s));
        v.setBackgroundColor(QColor(u"#1c2130"_s));
        v.setBorderColor(QColor(u"#3a3f4d"_s));
        v.setAccentColor(QColor(u"#9fc3e6"_s));
    }
    QImage img(QSize(int(width), std::max(1, int(v.height()))), QImage::Format_ARGB32_Premultiplied);
    img.fill(dark ? QColor(u"#1c2130"_s) : QColor(u"#fbf7ee"_s));
    QPainter p(&img);
    v.paint(&p);
    p.end();
    return img;
}

const char *const Samples[][2] = {
        {"flow", "flowchart TD\n    A[Początek] --> B{Czy działa?}\n    B -->|Tak| C[Świetnie]\n    B -->|Nie| "
                 "D[Napraw]\n    D --> B\n    C --> E((Koniec))\n"},
        {"flowlr", "graph LR\n  a([Stadion]) --> b[[Podprogram]] --> c[(Baza)]\n  c -.-> d>Flaga] ==> "
                   "e{{Sześciokąt}}\n  e --o f[/Równoległobok/] --x g[\\Odwrotny\\]\n  g <--> h[/Trapez\\] --- "
                   "i[\\Odwr. trapez/]\n  i -- etykieta --> j(((Podwójny)))\n  j --> j\n"},
        {"subgraph", "flowchart TB\n    c1-->a2\n    subgraph one [Pierwszy]\n    a1-->a2\n    end\n    subgraph two\n "
                     "   b1-->b2\n    end\n    subgraph three\n    direction LR\n    c1-->c2\n    end\n    one --> "
                     "two\n    three --> two\n    two --> c2\n    classDef red "
                     "fill:#f96,stroke:#333,stroke-width:2px\n    class a1 red\n    style b2 fill:#bbf\n"},
        {"seq", "sequenceDiagram\n    autonumber\n    participant A as Alicja\n    actor B as Bożena\n    A->>+B: "
                "Cześć, jak się masz?\n    B-->>-A: Świetnie!\n    loop Co godzinę\n        A->>B: Ping\n    end\n    "
                "alt zajęta\n        B-xA: Nie teraz\n    else wolna\n        B-)A: Chętnie\n    end\n    Note right "
                "of B: Bożena myśli\n    Note over A,B: Wspólna notatka\n    A->>A: Do siebie\n"},
        {"pie", "pie showData title Zwierzęta domowe\n    \"Psy\" : 386\n    \"Koty\" : 85.5\n    \"Szczury\" : 15\n   "
                " \"Żółwie\" : 4\n"},
};

} // namespace

class TstEditorMermaid : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void status_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("message");
        QTest::newRow("flowchart") << u"graph TD\nA-->B"_s << u"ok"_s << u"graph"_s << QString();
        QTest::newRow("sequence") << u"sequenceDiagram\nA->>B: x"_s << u"ok"_s << u"sequenceDiagram"_s << QString();
        QTest::newRow("pie") << u"pie\n\"A\" : 1"_s << u"ok"_s << u"pie"_s << QString();
        QTest::newRow("unsupported") << u"gantt\ntitle Plan"_s << u"unsupported"_s << u"gantt"_s
                                     << u"Brulion does not draw “gantt” diagrams yet: this is their source."_s;
        QTest::newRow("empty") << QString() << u"error"_s << QString() << u"The diagram is empty."_s;
        QTest::newRow("syntax") << u"graph TD\nA-->B\nA ?? B"_s << u"error"_s << u"graph"_s
                                << u"Line 3: cannot read “?? B”."_s;
        QTest::newRow("unknown") << u"flow TD"_s << u"error"_s << QString()
                                 << u"Line 1: the diagram does not start with a Mermaid type (graph, flowchart, "
                                    u"sequenceDiagram, pie…)."_s;
        QTest::newRow("too large") << u"pie\n"_s + QString(u"\"x\" : 1\n"_s).repeated(70) << u"error"_s << u"pie"_s
                                   << u"Line 66: the diagram is too large to draw (more than 64 slices)."_s;
    }
    void status()
    {
        QFETCH(QString, source);
        QFETCH(QString, status);
        QFETCH(QString, type);
        QFETCH(QString, message);
        MermaidView v;
        QSignalSpy changed(&v, &MermaidView::resultChanged);
        v.setSource(source);
        QCOMPARE(v.status(), status);
        QCOMPARE(v.typeName(), type);
        QCOMPARE(v.message(), message);
        QCOMPARE(v.diagramSize().isEmpty(), status != u"ok");
        QCOMPARE(changed.count(), source.isEmpty() ? 0 : 1);
        QVERIFY(!v.description().isEmpty());
    }

    void sizeFollowsWidth()
    {
        MermaidView v;
        v.setSource(u"graph LR\nA[Pierwszy węzeł] --> B[Drugi węzeł] --> C[Trzeci węzeł] --> D[Czwarty]"_s);
        const QSizeF natural = v.diagramSize();
        QVERIFY(natural.width() > 300);
        QCOMPARE(v.implicitWidth(), natural.width());
        // Wider than the diagram: its own size (never scaled up), centred.
        v.setWidth(natural.width() * 2);
        QCOMPARE(v.implicitHeight(), std::ceil(natural.height()));
        // Narrower: scaled down, the height too.
        v.setWidth(natural.width() / 2);
        QCOMPARE(v.implicitHeight(), std::ceil(natural.height() / 2));
        // The font changes the layout.
        v.setFontPixelSize(20);
        QVERIFY(v.diagramSize().width() > natural.width());
    }

    void sharedCache()
    {
        const QString src = u"graph TD\nA-->B-->C"_s;
        const auto a = MermaidView::layout(src, QString(), 14);
        QCOMPARE(MermaidView::layout(src, QString(), 14).get(), a.get());
        QVERIFY(MermaidView::layout(src, QString(), 15).get() != a.get());
        QVERIFY(MermaidView::layout(src + u'\n', QString(), 14).get() != a.get());
        // Bounded: old entries go.
        for (int i = 0; i < 100; ++i)
            MermaidView::layout(u"graph TD\nA-->n"_s + QString::number(i), QString(), 14);
        QVERIFY(MermaidView::layout(src, QString(), 14).get() != a.get());
    }

    void paintsInTheTheme()
    {
        MermaidView v;
        v.setSource(u"graph TD\nA[Węzeł] --> B[Drugi]\nstyle B fill:#ff0000"_s);
        const QImage light = paintView(v, 300, false);
        const QImage dark = paintView(v, 300, true);
        const auto count = [](const QImage &img, const QColor &c) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    n += QColor(img.pixel(x, y)).name() == c.name() ? 1 : 0;
            return n;
        };
        // The node fill of the theme (accent over the background) and the explicit colour.
        const auto fillOf = [&](bool isDark) {
            brulion::diagram::ThemeColors t;
            t.dark = isDark;
            if (isDark) {
                t.text = v.textColor().name();
                t.mutedText = v.mutedColor().name();
                t.background = v.backgroundColor().name();
                t.border = v.borderColor().name();
                t.accent = v.accentColor().name();
            } else {
                t.background = u"#fbf7ee"_s;
            }
            return QColor(brulion::diagram::themedPalette(t)[brulion::diagram::Role::NodeFill]);
        };
        QVERIFY(count(light, fillOf(false)) > 500);
        QVERIFY(count(light, QColor(u"#ff0000"_s)) > 500);
        QVERIFY(count(dark, fillOf(true)) > 500);
        QVERIFY(count(dark, QColor(u"#ff0000"_s)) > 500);
        QVERIFY(count(dark, fillOf(false)) < 10);
        // Nothing painted for an error.
        MermaidView e;
        e.setSource(u"graph TD\nA[nie zamknięty"_s);
        QCOMPARE(e.status(), u"error"_s);
        const QImage blank = paintView(e, 200, false);
        QCOMPARE(count(blank, QColor(u"#fbf7ee"_s)), blank.width() * blank.height());
    }

    void translatedMessages()
    {
        using brulion::diagram::Error;
        // Every code has a message with its line where it has one.
        for (int c = int(Error::Code::Empty); c <= int(Error::Code::TooLarge); ++c) {
            const QString m = MermaidView::errorMessage(Error{Error::Code(c), 7, u"x"_s}, u"gantt"_s);
            QVERIFY(!m.isEmpty());
            if (Error::Code(c) != Error::Code::Empty && Error::Code(c) != Error::Code::Unsupported
                && Error::Code(c) != Error::Code::NoData)
                QVERIFY2(m.contains(u"7"_s), qPrintable(m));
        }
        for (const QString &what : {u"nodes"_s, u"links"_s, u"subgraphs"_s, u"nesting"_s, u"participants"_s,
                                    u"messages"_s, u"slices"_s, u"source"_s, u"layout"_s})
            QVERIFY(!MermaidView::errorMessage(Error{Error::Code::TooLarge, 0, what}, QString()).contains(u"Line"_s));
    }

    void shots()
    {
        // BRULION_MERMAID_SHOTS=<dir>: the samples as PNG, light and dark, to look at.
        const QString dir = qEnvironmentVariable("BRULION_MERMAID_SHOTS");
        for (const auto &s : Samples) {
            MermaidView v;
            v.setSource(QString::fromUtf8(s[1]));
            QCOMPARE(v.status(), u"ok"_s);
            for (const bool dark : {false, true}) {
                const QImage img = paintView(v, 720, dark);
                QVERIFY(!img.isNull());
                if (!dir.isEmpty())
                    img.save(QDir(dir).filePath(QString::fromUtf8(s[0]) + (dark ? u"-dark.png"_s : u".png"_s)));
            }
        }
    }
};

QTEST_MAIN(TstEditorMermaid)
#include "tst_editor_mermaid.moc"
