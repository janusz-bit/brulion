// Mermaid diagrams (diagram/diagram.h): the type of a source, the parsers of flowcharts, sequence
// diagrams and pie charts (every form, Polish labels, errors with their lines), labels and
// colours, the limits on hostile input, the palettes and the SVG (well-formed, escaped, nothing
// active, the dark theme's style sheet).
#include "diagram/diagram.h"
#include "diagram/flowchart_p.h"
#include "diagram/pie_p.h"
#include "diagram/sequence_p.h"
#include "diagram/svg.h"

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QTest>
#include <QXmlStreamReader>

#include <numeric>

using namespace Qt::StringLiterals;
using namespace brulion::diagram;
using namespace brulion::diagram::detail;

namespace {

Flowchart flow(const QString &source)
{
    auto r = parseFlowchart(source);
    if (!r)
        qWarning() << "parse error" << int(r.error().code) << r.error().line << r.error().detail;
    return r ? *r : Flowchart{};
}

int nodeIndex(const Flowchart &f, const QString &id)
{
    for (qsizetype i = 0; i < f.nodes.size(); ++i)
        if (f.nodes[i].id == id)
            return int(i);
    return -1;
}

/// Is `svg` well-formed XML? Its element names in `elements`.
bool wellFormed(const QString &svg, QStringList *elements = nullptr, QStringList *attributes = nullptr)
{
    QXmlStreamReader xml(svg);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (elements)
                elements->append(xml.name().toString());
            if (attributes)
                for (const QXmlStreamAttribute &a : xml.attributes())
                    attributes->append(a.qualifiedName().toString());
        }
    }
    if (xml.hasError())
        qWarning() << xml.errorString() << xml.lineNumber() << xml.columnNumber();
    return !xml.hasError();
}

} // namespace

class TstDiagram : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void detect_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("type");
        QTest::newRow("graph") << u"graph TD\nA-->B"_s << int(Kind::Flowchart) << u"graph"_s;
        QTest::newRow("flowchart") << u"flowchart LR\nA-->B"_s << int(Kind::Flowchart) << u"flowchart"_s;
        QTest::newRow("leading blank lines") << u"\n\n  flowchart\n"_s << int(Kind::Flowchart) << u"flowchart"_s;
        QTest::newRow("comment first") << u"%% opis\ngraph TD"_s << int(Kind::Flowchart) << u"graph"_s;
        QTest::newRow("init directive") << u"%%{init: {'theme':'dark'}}%%\nsequenceDiagram"_s << int(Kind::Sequence)
                                        << u"sequenceDiagram"_s;
        QTest::newRow("directive over lines")
                << u"%%{init: {\n 'theme': 'forest'\n}}%%\npie"_s << int(Kind::Pie) << u"pie"_s;
        QTest::newRow("front matter") << u"---\ntitle: Przepływ\nconfig:\n  look: handDrawn\n---\ngraph TD"_s
                                      << int(Kind::Flowchart) << u"graph"_s;
        QTest::newRow("pie showData") << u"pie showData"_s << int(Kind::Pie) << u"pie"_s;
        QTest::newRow("class diagram") << u"classDiagram\nA <|-- B"_s << int(Kind::Unsupported) << u"classDiagram"_s;
        QTest::newRow("state v2") << u"stateDiagram-v2\n[*] --> A"_s << int(Kind::Unsupported) << u"stateDiagram-v2"_s;
        QTest::newRow("gantt") << u"gantt\ntitle Plan"_s << int(Kind::Unsupported) << u"gantt"_s;
        QTest::newRow("mindmap") << u"mindmap\n  root"_s << int(Kind::Unsupported) << u"mindmap"_s;
        QTest::newRow("er") << u"erDiagram"_s << int(Kind::Unsupported) << u"erDiagram"_s;
        QTest::newRow("empty") << QString() << int(Kind::Unknown) << QString();
        QTest::newRow("only comments") << u"%% nic\n%% tu"_s << int(Kind::Unknown) << QString();
        QTest::newRow("prose") << u"Zażółć gęślą jaźń"_s << int(Kind::Unknown) << QString();
        QTest::newRow("graphs (not graph)") << u"graphs TD"_s << int(Kind::Unknown) << QString();
        QTest::newRow("case matters") << u"Graph TD"_s << int(Kind::Unknown) << QString();
    }
    void detect()
    {
        QFETCH(QString, source);
        QFETCH(int, kind);
        QFETCH(QString, type);
        QString name;
        QCOMPARE(int(detectKind(source, &name)), kind);
        QCOMPARE(name, type);
    }

    void renderErrors_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("code");
        QTest::addColumn<int>("line");
        using C = Error::Code;
        QTest::newRow("empty") << u"  \n"_s << int(C::Empty) << 0;
        QTest::newRow("unknown") << u"\nhello world"_s << int(C::UnknownType) << 2;
        QTest::newRow("unsupported") << u"classDiagram\nA <|-- B"_s << int(C::Unsupported) << 0;
        QTest::newRow("bad statement") << u"graph TD\nA-->B\nA ??? B"_s << int(C::Syntax) << 3;
        QTest::newRow("dangling link") << u"graph TD\nA-->"_s << int(C::Syntax) << 2;
        QTest::newRow("unclosed bracket") << u"graph TD\n\nA[Początek --> B"_s << int(C::UnterminatedText) << 3;
        QTest::newRow("unclosed quote") << u"graph TD\nA[\"cytat] --> B"_s << int(C::UnterminatedText) << 2;
        QTest::newRow("unclosed pipe") << u"graph TD\nA -->|tak B"_s << int(C::UnterminatedText) << 2;
        QTest::newRow("end without subgraph") << u"graph TD\nA-->B\nend"_s << int(C::UnexpectedEnd) << 3;
        QTest::newRow("subgraph without end") << u"graph TD\nA-->B\nsubgraph S\nC\n"_s << int(C::UnclosedBlock) << 3;
        QTest::newRow("loop without end")
                << u"sequenceDiagram\nA->>B: x\nloop zawsze\nA->>B: y"_s << int(C::UnclosedBlock) << 3;
        QTest::newRow("sequence end") << u"sequenceDiagram\nA->>B: x\nend"_s << int(C::UnexpectedEnd) << 3;
        QTest::newRow("sequence else outside") << u"sequenceDiagram\nelse nic"_s << int(C::Syntax) << 2;
        QTest::newRow("sequence nonsense") << u"sequenceDiagram\n\nA => B"_s << int(C::Syntax) << 3;
        QTest::newRow("note without colon") << u"sequenceDiagram\nNote right of A tekst"_s << int(C::Syntax) << 2;
        QTest::newRow("pie value") << u"pie\n\"Psy\" : dużo"_s << int(C::InvalidValue) << 2;
        QTest::newRow("accDescr unclosed")
                << u"graph TD\nA-->B\naccDescr {\n opis\nC-->D"_s << int(C::UnclosedBlock) << 3;
        QTest::newRow("sequence accDescr unclosed")
                << u"sequenceDiagram\naccDescr {\nA->>B: x"_s << int(C::UnclosedBlock) << 2;
        QTest::newRow("pie no quotes") << u"pie\nPsy : 3"_s << int(C::Syntax) << 2;
        QTest::newRow("pie no data") << u"pie title Nic"_s << int(C::NoData) << 1;
        QTest::newRow("pie only zeros") << u"pie\n\"A\" : 0\n\"B\" : -3"_s << int(C::NoData) << 0;
        QTest::newRow("pie header junk") << u"pie raz dwa"_s << int(C::Syntax) << 1;
        QTest::newRow("front matter lines count") << u"---\ntitle: T\n---\ngraph TD\nA -- B"_s << int(C::Syntax) << 5;
    }
    void renderErrors()
    {
        QFETCH(QString, source);
        QFETCH(int, code);
        QFETCH(int, line);
        const Result r = render(source);
        QVERIFY(!r.ok());
        QCOMPARE(int(r.error->code), code);
        QCOMPARE(r.error->line, line);
        QVERIFY(r.scene.items.isEmpty());
    }

    void flowHeader_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<int>("direction");
        QTest::newRow("default") << u"graph\nA"_s << int(Direction::TB);
        QTest::newRow("TD") << u"graph TD\nA"_s << int(Direction::TB);
        QTest::newRow("TB") << u"flowchart TB\nA"_s << int(Direction::TB);
        QTest::newRow("BT") << u"flowchart BT\nA"_s << int(Direction::BT);
        QTest::newRow("LR") << u"graph LR\nA"_s << int(Direction::LR);
        QTest::newRow("RL") << u"graph RL\nA"_s << int(Direction::RL);
        QTest::newRow("semicolons") << u"graph LR;A-->B;B-->C"_s << int(Direction::LR);
        QTest::newRow("direction statement") << u"flowchart\ndirection BT\nA"_s << int(Direction::BT);
    }
    void flowHeader()
    {
        QFETCH(QString, source);
        QFETCH(int, direction);
        const Flowchart f = flow(source);
        QCOMPARE(int(f.direction), direction);
        QVERIFY(!f.nodes.isEmpty());
    }

    void flowShapes_data()
    {
        QTest::addColumn<QString>("node");
        QTest::addColumn<int>("shape");
        QTest::addColumn<QString>("label");
        QTest::newRow("plain id") << u"zażółć"_s << int(NodeShape::Rect) << u"zażółć"_s;
        QTest::newRow("rect") << u"A[Prostokąt]"_s << int(NodeShape::Rect) << u"Prostokąt"_s;
        QTest::newRow("rounded") << u"A(Zaokrąglony)"_s << int(NodeShape::Rounded) << u"Zaokrąglony"_s;
        QTest::newRow("stadium") << u"A([Stadion])"_s << int(NodeShape::Stadium) << u"Stadion"_s;
        QTest::newRow("subroutine") << u"A[[Podprogram]]"_s << int(NodeShape::Subroutine) << u"Podprogram"_s;
        QTest::newRow("cylinder") << u"A[(Baza)]"_s << int(NodeShape::Cylinder) << u"Baza"_s;
        QTest::newRow("circle") << u"A((Koło))"_s << int(NodeShape::Circle) << u"Koło"_s;
        QTest::newRow("double circle") << u"A(((Dwa)))"_s << int(NodeShape::DoubleCircle) << u"Dwa"_s;
        QTest::newRow("asymmetric") << u"A>Flaga]"_s << int(NodeShape::Asymmetric) << u"Flaga"_s;
        QTest::newRow("rhombus") << u"A{Decyzja?}"_s << int(NodeShape::Rhombus) << u"Decyzja?"_s;
        QTest::newRow("hexagon") << u"A{{Sześć}}"_s << int(NodeShape::Hexagon) << u"Sześć"_s;
        QTest::newRow("parallelogram") << u"A[/Wejście/]"_s << int(NodeShape::Parallelogram) << u"Wejście"_s;
        QTest::newRow("parallelogram alt") << u"A[\\Wyjście\\]"_s << int(NodeShape::ParallelogramAlt) << u"Wyjście"_s;
        QTest::newRow("trapezoid") << u"A[/Trapez\\]"_s << int(NodeShape::Trapezoid) << u"Trapez"_s;
        QTest::newRow("trapezoid alt") << u"A[\\Odwrócony/]"_s << int(NodeShape::TrapezoidAlt) << u"Odwrócony"_s;
        QTest::newRow("quoted with brackets") << u"A[\"Tekst (z) [nawiasami] {i} klamrami\"]"_s << int(NodeShape::Rect)
                                              << u"Tekst (z) [nawiasami] {i} klamrami"_s;
        QTest::newRow("quoted rhombus") << u"A{\"a } b\"}"_s << int(NodeShape::Rhombus) << u"a } b"_s;
        QTest::newRow("br") << u"A[Pierwsza<br>druga<br/>trzecia<br />czwarta]"_s << int(NodeShape::Rect)
                            << u"Pierwsza\ndruga\ntrzecia\nczwarta"_s;
        QTest::newRow("entities") << u"A[\"#quot;cytat#quot; #35; #9829; &amp; &lt;b&gt;\"]"_s << int(NodeShape::Rect)
                                  << u"\"cytat\" # ♥ & <b>"_s;
        QTest::newRow("forbidden entity") << u"A[x#0;y#xD800;z]"_s << int(NodeShape::Rect) << u"x#0;y#xD800;z"_s;
        QTest::newRow("markdown string") << u"A[\"`**Gruby** i *kursywa*`\"]"_s << int(NodeShape::Rect)
                                         << u"Gruby i kursywa"_s;
        QTest::newRow("markdown newline") << u"A[\"`raz\\ndwa`\"]"_s << int(NodeShape::Rect) << u"raz\ndwa"_s;
        QTest::newRow("simple tags") << u"A[<b>gruby</b> <i>i</i> <span style='x'>s</span>]"_s << int(NodeShape::Rect)
                                     << u"gruby i s"_s;
        QTest::newRow("other tags kept as text")
                << u"A[\"<script>alert(1)</script>\"]"_s << int(NodeShape::Rect) << u"<script>alert(1)</script>"_s;
        QTest::newRow("space before bracket") << u"A [Spacja]"_s << int(NodeShape::Rect) << u"Spacja"_s;
        QTest::newRow("new syntax") << u"A@{ shape: diam, label: \"Nowa składnia\" }"_s << int(NodeShape::Rhombus)
                                    << u"Nowa składnia"_s;
        QTest::newRow("new syntax cyl") << u"A@{ shape: cyl }"_s << int(NodeShape::Cylinder) << u"A"_s;
        QTest::newRow("id with dash") << u"krok-1[Krok]"_s << int(NodeShape::Rect) << u"Krok"_s;
    }
    void flowShapes()
    {
        QFETCH(QString, node);
        QFETCH(int, shape);
        QFETCH(QString, label);
        const Flowchart f = flow(u"graph TD\n"_s + node);
        QCOMPARE(f.nodes.size(), 1);
        QCOMPARE(int(f.nodes[0].shape), shape);
        QCOMPARE(f.nodes[0].label, label);
    }

    void flowLinks_data()
    {
        QTest::addColumn<QString>("link");
        QTest::addColumn<int>("stroke");
        QTest::addColumn<int>("start");
        QTest::addColumn<int>("end");
        QTest::addColumn<int>("length");
        QTest::addColumn<QString>("label");
        const int S = int(LinkStroke::Solid), D = int(LinkStroke::Dotted), T = int(LinkStroke::Thick),
                  I = int(LinkStroke::Invisible);
        const int N = int(Head::None), Ar = int(Head::Arrow), Ci = int(Head::Circle), Cr = int(Head::Cross);
        QTest::newRow("-->") << u"A-->B"_s << S << N << Ar << 1 << QString();
        QTest::newRow("--- ") << u"A --- B"_s << S << N << N << 1 << QString();
        QTest::newRow("--->") << u"A--->B"_s << S << N << Ar << 2 << QString();
        QTest::newRow("----") << u"A----B"_s << S << N << N << 2 << QString();
        QTest::newRow("-.->") << u"A-.->B"_s << D << N << Ar << 1 << QString();
        QTest::newRow("-..->") << u"A-..->B"_s << D << N << Ar << 2 << QString();
        QTest::newRow("-.-") << u"A-.-B"_s << D << N << N << 1 << QString();
        QTest::newRow("==>") << u"A==>B"_s << T << N << Ar << 1 << QString();
        QTest::newRow("===") << u"A===B"_s << T << N << N << 1 << QString();
        QTest::newRow("====>") << u"A====>B"_s << T << N << Ar << 3 << QString();
        QTest::newRow("~~~") << u"A~~~B"_s << I << N << N << 1 << QString();
        QTest::newRow("--o") << u"A --o B"_s << S << N << Ci << 1 << QString();
        QTest::newRow("--x") << u"A --x B"_s << S << N << Cr << 1 << QString();
        QTest::newRow("<-->") << u"A <--> B"_s << S << Ar << Ar << 1 << QString();
        QTest::newRow("o--o") << u"A o--o B"_s << S << Ci << Ci << 1 << QString();
        QTest::newRow("x--x") << u"A x--x B"_s << S << Cr << Cr << 1 << QString();
        QTest::newRow("<-.->") << u"A <-.-> B"_s << D << Ar << Ar << 1 << QString();
        QTest::newRow("<==>") << u"A <==> B"_s << T << Ar << Ar << 1 << QString();
        QTest::newRow("pipe label") << u"A-->|Tak, zgoda|B"_s << S << N << Ar << 1 << u"Tak, zgoda"_s;
        QTest::newRow("pipe label spaced") << u"A --> |ąę| B"_s << S << N << Ar << 1 << u"ąę"_s;
        QTest::newRow("pipe quoted") << u"A-->|\"a|b\"|B"_s << S << N << Ar << 1 << u"a|b"_s;
        QTest::newRow("open pipe") << u"A---|tekst|B"_s << S << N << N << 1 << u"tekst"_s;
        QTest::newRow("text arrow") << u"A-- tekst ze spacjami -->B"_s << S << N << Ar << 1 << u"tekst ze spacjami"_s;
        QTest::newRow("text open") << u"A -- tekst --- B"_s << S << N << N << 1 << u"tekst"_s;
        QTest::newRow("text dotted") << u"A-. kropki .->B"_s << D << N << Ar << 1 << u"kropki"_s;
        QTest::newRow("text thick") << u"A == gruba ==> B"_s << T << N << Ar << 1 << u"gruba"_s;
        QTest::newRow("text both") << u"A <-- obie --> B"_s << S << Ar << Ar << 1 << u"obie"_s;
        QTest::newRow("text with dash") << u"A -- pre-test --> B"_s << S << N << Ar << 1 << u"pre-test"_s;
        QTest::newRow("text longer") << u"A -- dalej ---> B"_s << S << N << Ar << 2 << u"dalej"_s;
    }
    void flowLinks()
    {
        QFETCH(QString, link);
        QFETCH(int, stroke);
        QFETCH(int, start);
        QFETCH(int, end);
        QFETCH(int, length);
        QFETCH(QString, label);
        const Flowchart f = flow(u"graph LR\n"_s + link);
        QCOMPARE(f.nodes.size(), 2);
        QCOMPARE(f.nodes[0].id, u"A"_s);
        QCOMPARE(f.nodes[1].id, u"B"_s);
        QCOMPARE(f.edges.size(), 1);
        const FlowEdge &e = f.edges[0];
        QCOMPARE(e.from, (FlowEnd{0, false}));
        QCOMPARE(e.to, (FlowEnd{1, false}));
        QCOMPARE(int(e.stroke), stroke);
        QCOMPARE(int(e.startHead), start);
        QCOMPARE(int(e.endHead), end);
        QCOMPARE(e.length, length);
        QCOMPARE(e.label, label);
    }

    void flowChainsAndGroups()
    {
        const Flowchart f = flow(u"graph TD\nA[Jeden] --> B --> C[Trzy] & D --> E & F\nG & H -.- I"_s);
        QCOMPARE(f.nodes.size(), 9);
        // A→B, B→C, B→D, C→E, C→F, D→E, D→F, G→I, H→I
        QCOMPARE(f.edges.size(), 9);
        QCOMPARE(f.nodes[nodeIndex(f, u"C"_s)].label, u"Trzy"_s);
        const auto has = [&](const QString &a, const QString &b) {
            for (const FlowEdge &e : f.edges)
                if (e.from.index == nodeIndex(f, a) && e.to.index == nodeIndex(f, b))
                    return true;
            return false;
        };
        QVERIFY(has(u"B"_s, u"D"_s));
        QVERIFY(has(u"D"_s, u"F"_s));
        QVERIFY(has(u"H"_s, u"I"_s));
        QCOMPARE(f.edges.last().stroke, LinkStroke::Dotted);
        // A later definition gives the shape and label; a mention does not reset them.
        const Flowchart g = flow(u"graph TD\nA-->B\nB(Później)\nA[Wcześniej]-->B"_s);
        QCOMPARE(g.nodes[1].label, u"Później"_s);
        QCOMPARE(g.nodes[1].shape, NodeShape::Rounded);
        QCOMPARE(g.nodes[0].label, u"Wcześniej"_s);
        QCOMPARE(g.edges.size(), 2);
    }

    void flowSubgraphs()
    {
        const Flowchart f = flow(u"flowchart TB\n"
                                 u"  c1-->a2\n"
                                 u"  subgraph one [Pierwszy podgraf]\n"
                                 u"    a1-->a2\n"
                                 u"  end\n"
                                 u"  subgraph \"Cytowany tytuł\"\n"
                                 u"    b1\n"
                                 u"    subgraph inner\n"
                                 u"      direction LR\n"
                                 u"      b2 --> b3\n"
                                 u"    end\n"
                                 u"  end\n"
                                 u"  subgraph Wiele słów\n"
                                 u"    c1\n"
                                 u"  end\n"
                                 u"  one --> inner\n"
                                 u"  a1 --> one\n"_s);
        QCOMPARE(f.clusters.size(), 4);
        QCOMPARE(f.clusters[0].id, u"one"_s);
        QCOMPARE(f.clusters[0].title, u"Pierwszy podgraf"_s);
        QCOMPARE(f.clusters[1].title, u"Cytowany tytuł"_s);
        QCOMPARE(f.clusters[2].id, u"inner"_s);
        QCOMPARE(f.clusters[2].parent, 1);
        QCOMPARE(f.clusters[2].direction, std::optional(Direction::LR));
        QCOMPARE(f.clusters[3].title, u"Wiele słów"_s);
        // a2 was first named outside, then inside `one`: it is in `one`; c1 moved into the last.
        QCOMPARE(f.nodes[nodeIndex(f, u"a2"_s)].cluster, 0);
        QCOMPARE(f.nodes[nodeIndex(f, u"c1"_s)].cluster, 3);
        QCOMPARE(f.nodes[nodeIndex(f, u"b1"_s)].cluster, 1);
        QCOMPARE(f.nodes[nodeIndex(f, u"b3"_s)].cluster, 2);
        // Links to subgraphs: no node named `one` or `inner`.
        QCOMPARE(nodeIndex(f, u"one"_s), -1);
        QCOMPARE(nodeIndex(f, u"inner"_s), -1);
        const FlowEdge &e = f.edges[f.edges.size() - 2];
        QCOMPARE(e.from, (FlowEnd{0, true}));
        QCOMPARE(e.to, (FlowEnd{2, true}));
        // A subgraph linked to its own member is laid out, but that link is not drawn.
        const Result r = render(u"flowchart TB\n  subgraph one\n a1-->a2\n end\n a1 --> one\n"_s);
        QVERIFY(r.ok());
    }

    void flowStyles()
    {
        const Flowchart f = flow(u"graph TD\n"
                                 u"  A:::warm --> B --> C\n"
                                 u"  classDef warm fill:#f96,stroke:#333,stroke-width:4px,color:white\n"
                                 u"  classDef cold,frozen fill:rgb(10, 20, 30);\n"
                                 u"  classDef default stroke-dasharray: 5 5\n"
                                 u"  class B,C cold\n"
                                 u"  style C fill:#BBF,stroke:url(javascript:alert(1)),color:\"><script>\n"
                                 u"  linkStyle 0 stroke:#ff3,stroke-width:3px,color:red\n"
                                 u"  linkStyle default stroke:blue\n"
                                 u"  click A callback \"Tooltip\"\n"
                                 u"  click B href \"https://example.com\" _blank\n"_s);
        QCOMPARE(f.nodes.size(), 3);
        const Style &a = f.nodes[0].style;
        QCOMPARE(a.fill, u"#ff9966"_s);
        QCOMPARE(a.stroke, u"#333333"_s);
        QCOMPARE(a.strokeWidth, 4.0);
        QCOMPARE(a.color, u"#ffffff"_s);
        QCOMPARE(a.dash, std::optional(Dash::Dashed));
        QCOMPARE(f.nodes[1].style.fill, u"#0a141e"_s);
        // Invalid colours are dropped, never passed on.
        QCOMPARE(f.nodes[2].style.fill, u"#bbbbff"_s);
        QCOMPARE(f.nodes[2].style.stroke, QString());
        QCOMPARE(f.nodes[2].style.color, QString());
        QCOMPARE(f.edges[0].style.stroke, u"#ffff33"_s);
        QCOMPARE(f.edges[0].style.strokeWidth, 3.0);
        QCOMPARE(f.edges[0].style.color, u"#ff0000"_s);
        QCOMPARE(f.edges[1].style.stroke, u"#0000ff"_s);
        // `click` lines add nothing.
        const Scene s = render(u"graph TD\nA-->B\nclick A \"https://example.com\""_s).scene;
        for (const Item &it : s.items)
            for (const QString &l : it.lines)
                QVERIFY(!l.contains(u"example"_s));
    }

    void flowCommentsAndTitle()
    {
        const Result r = render(u"---\ntitle: Mój tytuł\n---\n%%{init: {\"flowchart\": {\"curve\": \"linear\"}}}%%\n"
                                u"graph TD\n%% komentarz A-->Z\nA --> B %% not a comment in Mermaid either\n"_s);
        QVERIFY(!r.ok()); // `%%` is only a comment on its own line
        const Result ok = render(u"---\ntitle: Mój tytuł\n---\ngraph TD\n%% komentarz A-->Z\nA --> B\n"_s);
        QVERIFY(ok.ok());
        QCOMPARE(ok.scene.title, u"Mój tytuł"_s);
        bool titleDrawn = false;
        for (const Item &it : ok.scene.items)
            titleDrawn = titleDrawn || (it.color.role == Role::Title && it.lines == QStringList{u"Mój tytuł"_s});
        QVERIFY(titleDrawn);
        const Flowchart f = flow(u"graph TD\n%% komentarz A-->Z\nA --> B"_s);
        QCOMPARE(f.nodes.size(), 2);
        QCOMPARE(flow(u"graph TD\naccTitle: Dostępny tytuł\naccDescr {\n wiele\n linii\n}\nA"_s).title,
                 u"Dostępny tytuł"_s);
        // Only the keywords: nodes named like them are nodes.
        const Flowchart acc = flow(u"graph TD\naccTitleNode --> accDescrNode"_s);
        QCOMPARE(acc.nodes.size(), 2);
        QCOMPARE(acc.edges.size(), 1);
        QVERIFY(acc.title.isEmpty());
    }

    void cssColors_data()
    {
        QTest::addColumn<QString>("css");
        QTest::addColumn<QString>("hex");
        QTest::newRow("#rgb") << u"#F9f"_s << u"#ff99ff"_s;
        QTest::newRow("#rrggbb") << u" #1A2b3C "_s << u"#1a2b3c"_s;
        QTest::newRow("#rgba") << u"#f008"_s << u"#ff000088"_s;
        QTest::newRow("#rrggbbaa") << u"#11223344"_s << u"#11223344"_s;
        QTest::newRow("rgb") << u"rgb(255, 0, 10)"_s << u"#ff000a"_s;
        QTest::newRow("rgb spaces") << u"rgb(1 2 3)"_s << u"#010203"_s;
        QTest::newRow("rgba") << u"rgba(0,0,0,0.5)"_s << u"#00000080"_s;
        QTest::newRow("rgba percent") << u"rgba(0 0 0 / 25%)"_s << u"#00000040"_s;
        QTest::newRow("name") << u"LightBlue"_s << u"#add8e6"_s;
        QTest::newRow("transparent") << u"transparent"_s << u"#00000000"_s;
        QTest::newRow("clamped") << u"rgb(999,0,0)"_s << u"#ff0000"_s;
        QTest::newRow("unknown name") << u"blurple"_s << QString();
        QTest::newRow("markup") << u"#fff\"><script>"_s << QString();
        QTest::newRow("url") << u"url(#x)"_s << QString();
        QTest::newRow("bad hex") << u"#ggg"_s << QString();
        QTest::newRow("five digits") << u"#12345"_s << QString();
        QTest::newRow("hsl") << u"hsl(0,100%,50%)"_s << QString();
        QTest::newRow("empty") << QString() << QString();
    }
    void cssColors()
    {
        QFETCH(QString, css);
        QFETCH(QString, hex);
        QCOMPARE(parseCssColor(css), hex);
    }

    void sequenceParse()
    {
        auto r = parseSequence(u"sequenceDiagram\n"
                               u"  title: Rozmowa\n"
                               u"  participant A as Alicja Żak\n"
                               u"  actor B\n"
                               u"  A->>+B: Cześć<br>Bożena\n"
                               u"  B-->>-A: Hej\n"
                               u"  A->B: linia\n"
                               u"  A-->B: kropki\n"
                               u"  A-xB: krzyżyk\n"
                               u"  A--xB: krzyżyk kropki\n"
                               u"  A-)B: asynchronicznie\n"
                               u"  A--)B: asynchronicznie kropki\n"
                               u"  A<<->>B: w obie\n"
                               u"  A<<-->>B: w obie kropki\n"
                               u"  C->>C: do siebie\n"
                               u"  activate C\n"
                               u"  deactivate C\n"
                               u"  Note left of A: lewa\n"
                               u"  note right of B: prawa\n"
                               u"  Note over A,C: nad wszystkimi\n"
                               u"  loop Co minutę\n"
                               u"    A->>B: ping\n"
                               u"  end\n"
                               u"  alt sukces\n"
                               u"    B->>A: ok\n"
                               u"  else porażka\n"
                               u"    B->>A: błąd\n"
                               u"  end\n"
                               u"  par równolegle\n    A->>B: a\n  and\n    A->>C: b\n  end\n"
                               u"  critical krytyczne\n    A->>B: x\n  option awaria\n    A->>B: y\n  end\n"
                               u"  opt opcja\n    A->>B: z\n  end\n"
                               u"  break przerwa\n    A->>B: q\n  end\n"
                               u"  rect rgb(200, 220, 255)\n    A->>B: w prostokącie\n  end\n"
                               u"  links A: {\"Strona\": \"https://example.com\"}\n"_s);
        QVERIFY(r);
        const SequenceDiagram &d = *r;
        QCOMPARE(d.title, u"Rozmowa"_s);
        QCOMPARE(d.participants.size(), 3);
        QCOMPARE(d.participants[0].label, u"Alicja Żak"_s);
        QVERIFY(d.participants[1].actor);
        QCOMPARE(d.participants[2].id, u"C"_s); // named by its first message
        const SeqEvent &first = d.events[0];
        QCOMPARE(first.type, SeqEvent::Type::Message);
        QCOMPARE(first.text, u"Cześć\nBożena"_s);
        QVERIFY(first.activateTarget);
        QCOMPARE(first.endHead, Head::Arrow);
        QVERIFY(!first.dotted);
        QVERIFY(d.events[1].deactivateSource);
        QVERIFY(d.events[1].dotted);
        const auto heads = [&](int i) {
            return std::tuple(d.events[i].dotted, d.events[i].startHead, d.events[i].endHead);
        };
        QCOMPARE(heads(2), std::tuple(false, Head::None, Head::None));
        QCOMPARE(heads(3), std::tuple(true, Head::None, Head::None));
        QCOMPARE(heads(4), std::tuple(false, Head::None, Head::Cross));
        QCOMPARE(heads(5), std::tuple(true, Head::None, Head::Cross));
        QCOMPARE(heads(6), std::tuple(false, Head::None, Head::Open));
        QCOMPARE(heads(7), std::tuple(true, Head::None, Head::Open));
        QCOMPARE(heads(8), std::tuple(false, Head::Arrow, Head::Arrow));
        QCOMPARE(heads(9), std::tuple(true, Head::Arrow, Head::Arrow));
        QCOMPARE(d.events[10].from, d.events[10].to);
        QCOMPARE(d.events[11].type, SeqEvent::Type::Activate);
        QCOMPARE(d.events[12].type, SeqEvent::Type::Deactivate);
        QCOMPARE(d.events[13].place, SeqEvent::NotePlace::LeftOf);
        QCOMPARE(d.events[14].place, SeqEvent::NotePlace::RightOf);
        QCOMPARE(d.events[15].place, SeqEvent::NotePlace::Over);
        QCOMPARE(d.events[15].to, 2);
        QStringList blocks;
        for (const SeqEvent &e : d.events)
            if (e.type == SeqEvent::Type::BlockStart || e.type == SeqEvent::Type::BlockSection)
                blocks.append(e.keyword + u':' + (e.keyword == u"rect" ? e.color : e.text));
        QCOMPARE(blocks, (QStringList{u"loop:Co minutę"_s, u"alt:sukces"_s, u"else:porażka"_s, u"par:równolegle"_s,
                                      u"and:"_s, u"critical:krytyczne"_s, u"option:awaria"_s, u"opt:opcja"_s,
                                      u"break:przerwa"_s, u"rect:#c8dcff"_s}));
        QVERIFY(render(u"sequenceDiagram\nA->>B: x"_s).ok());
        // Spaces anywhere before the colon; the text after it as written.
        auto spaced = parseSequence(u"sequenceDiagram\nAlicja   Kowalska  ->>+   Bob :  Tekst:  z dwukropkiem"_s);
        QVERIFY(spaced);
        QCOMPARE(spaced->participants[0].id, u"Alicja Kowalska"_s);
        QCOMPARE(spaced->participants[1].id, u"Bob"_s);
        QVERIFY(spaced->events[0].activateTarget);
        QCOMPARE(spaced->events[0].text, u"Tekst:  z dwukropkiem"_s);
        auto noText = parseSequence(u"sequenceDiagram\nA->>B"_s);
        QVERIFY(noText);
        QVERIFY(noText->events[0].text.isEmpty());
        auto note = parseSequence(u"sequenceDiagram\nnote   RIGHT OF   A :  uwaga: tak"_s);
        QVERIFY(note);
        QCOMPARE(note->events[0].place, SeqEvent::NotePlace::RightOf);
        QCOMPARE(note->events[0].text, u"uwaga: tak"_s);
        QVERIFY(parseSequence(u"sequenceDiagram\naccDescr {\nopis\n}\nA->>B: x"_s));
    }

    void sequenceExtras()
    {
        auto r = parseSequence(u"sequenceDiagram\n"
                               u"  autonumber 10 5\n"
                               u"  box Aqua Grupa\n    participant A\n    participant B\n  end\n"
                               u"  box rgb(1,2,3)\n    participant C\n  end\n"
                               u"  A->>B: a\n  B->>C: b\n  autonumber off\n  C->>A: c\n"
                               u"  create participant D\n  C->>D: nowy\n  destroy D\n"_s);
        QVERIFY(r);
        QCOMPARE(r->boxes.size(), 2);
        QCOMPARE(r->boxes[0].color, u"#00ffff"_s);
        QCOMPARE(r->boxes[0].title, u"Grupa"_s);
        QCOMPARE(r->boxes[1].color, u"#010203"_s);
        QCOMPARE(r->participants[0].box, 0);
        QCOMPARE(r->participants[2].box, 1);
        QCOMPARE(r->participants[3].box, -1);
        QCOMPARE(r->events[0].number, 10);
        QCOMPARE(r->events[1].number, 15);
        QCOMPARE(r->events[2].number, 0);
        QVERIFY(render(u"sequenceDiagram\nautonumber\nA->>B: x\nB->>A: y"_s).ok());
    }

    void sequenceScene()
    {
        const Result r =
                render(u"sequenceDiagram\nparticipant A as Alicja\nA->>B: Wiadomość\nNote left of A: lewa notatka"_s);
        QVERIFY(r.ok());
        // Every item inside the scene (a note left of the first participant moves everything right).
        for (const Item &it : r.scene.items) {
            if (it.type == Item::Type::Text)
                QVERIFY(it.box.left() >= -0.5 && it.box.right() <= r.scene.size.width() + 0.5);
            for (const PathOp &op : it.path)
                if (op.type != PathOp::Type::Close)
                    QVERIFY(op.p.x() >= -0.5 && op.p.x() <= r.scene.size.width() + 0.5);
        }
        // Both participants top and bottom.
        int names = 0;
        for (const Item &it : r.scene.items)
            names += it.lines == QStringList{u"Alicja"_s} ? 1 : 0;
        QCOMPARE(names, 2);
    }

    void pie()
    {
        auto r = parsePie(u"pie showData\n  title Zwierzęta w domu\n  \"Psy\" : 386\n  \"Koty\" : 85.5\n"
                          u"  \"Nic\" : 0\n  \"Ujemne\" : -4\n  \"Żółwie\": 15\n"_s);
        QVERIFY(r);
        QVERIFY(r->showData);
        QCOMPARE(r->title, u"Zwierzęta w domu"_s);
        QCOMPARE(r->slices.size(), 3);
        QCOMPARE(r->slices[2].label, u"Żółwie"_s);
        QCOMPARE(r->slices[1].value, 85.5);
        const QList<qreal> angles = pieAngles(*r);
        QCOMPARE(angles.size(), 3);
        QVERIFY(std::abs(std::accumulate(angles.cbegin(), angles.cend(), 0.0) - 360.0) < 1e-9);
        QVERIFY(std::abs(angles[0] - 386.0 / 486.5 * 360) < 1e-9);
        // A `;` after the value; values whose sum overflows.
        auto semi = parsePie(u"pie\n\"A\" : 1;\n\"B\" : 3 ;"_s);
        QVERIFY(semi);
        QCOMPARE(semi->slices[1].value, 3.0);
        auto huge = parsePie(u"pie\n\"a\" : 1e308\n\"b\" : 1e308"_s);
        QVERIFY(huge);
        QCOMPARE(pieAngles(*huge), (QList<qreal>{180, 180}));
        auto t = parsePie(u"pie title Tytuł w nagłówku\n\"A\" : 1"_s);
        QVERIFY(t);
        QCOMPARE(t->title, u"Tytuł w nagłówku"_s);
        // One slice: a whole circle; the legend shows the value with showData.
        const Result one = render(u"pie showData\n\"Jedyny\" : 42.5"_s);
        QVERIFY(one.ok());
        bool legend = false;
        for (const Item &it : one.scene.items)
            legend = legend || (it.lines.size() == 1 && it.lines[0].startsWith(u"Jedyny [42.5]"_s));
        QVERIFY(legend);
    }

    void limits_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<bool>("ok");
        QString loops = u"graph TD\n"_s;
        for (int i = 0; i < 10000; ++i)
            loops += u"A-->A\n"_s;
        QTest::newRow("10000 self-loops") << loops << false;
        QString nodes = u"graph TD\n"_s;
        for (int i = 0; i <= limits::MaxNodes; ++i)
            nodes += u"n"_s + QString::number(i) + u'\n';
        QTest::newRow("too many nodes") << nodes << false;
        QString nested = u"graph TD\n"_s;
        for (int i = 0; i < 40; ++i)
            nested += u"subgraph s"_s + QString::number(i) + u'\n';
        nested += u"A-->B\n"_s;
        for (int i = 0; i < 40; ++i)
            nested += u"end\n"_s;
        QTest::newRow("deep subgraphs") << nested << false;
        QString deepOk = u"graph TD\n"_s;
        for (int i = 0; i < limits::MaxNesting; ++i)
            deepOk += u"subgraph s"_s + QString::number(i) + u"\nn"_s + QString::number(i) + u"-->m"_s
                      + QString::number(i) + u'\n';
        for (int i = 0; i < limits::MaxNesting; ++i)
            deepOk += u"end\n"_s;
        QTest::newRow("nested to the limit") << deepOk << true;
        QTest::newRow("huge label") << u"graph TD\nA[\""_s + QString(99000, u'ż') + u"\"]-->B"_s << true;
        QTest::newRow("huge label words")
                << u"graph TD\nA[\""_s + QString(u"słowo "_s).repeated(16000) + u"\"]"_s << true;
        QTest::newRow("source too large") << u"graph TD\n%%"_s + QString(limits::MaxSourceBytes, u'x') << false;
        QString chain = u"graph LR\n"_s;
        for (int i = 0; i < 999; ++i)
            chain += u"n"_s + QString::number(i) + u"-->n"_s + QString::number(i + 1) + u'\n';
        chain += u"n999-->n0\n"_s;
        // (1000 nodes in a row: wider than limits::MaxSceneExtent.)
        QTest::newRow("long cycle") << chain << false;
        QString shorter = u"graph LR\n"_s;
        for (int i = 0; i < 299; ++i)
            shorter += u"n"_s + QString::number(i) + u"-->n"_s + QString::number(i + 1) + u'\n';
        shorter += u"n299-->n0\n"_s;
        QTest::newRow("cycle") << shorter << true;
        QString dense = u"graph TD\n"_s;
        for (int i = 0; i < 60; ++i)
            for (int j = 0; j < 50; ++j)
                if ((i * 7 + j * 13) % 11 < 1)
                    dense += u"a"_s + QString::number(i) + u"-->a"_s + QString::number(j + 3) + u'\n';
        QTest::newRow("dense") << dense << true;
        QString many = u"sequenceDiagram\n"_s;
        for (int i = 0; i <= limits::MaxMessages; ++i)
            many += u"A->>B: m\n"_s;
        QTest::newRow("too many messages") << many << false;
        QString parts = u"sequenceDiagram\n"_s;
        for (int i = 0; i <= limits::MaxParticipants; ++i)
            parts += u"participant p"_s + QString::number(i) + u'\n';
        QTest::newRow("too many participants") << parts << false;
        QString slices = u"pie\n"_s;
        for (int i = 0; i <= limits::MaxSlices; ++i)
            slices += u"\"s"_s + QString::number(i) + u"\" : 1\n"_s;
        QTest::newRow("too many slices") << slices << false;
        // Long runs of spaces where the regular expressions look for an arrow or a colon.
        QTest::newRow("spaces in a message") << u"sequenceDiagram\nA"_s + QString(90000, u' ') + u"B->>C: x"_s << true;
        QTest::newRow("spaces around an arrow")
                << u"sequenceDiagram\nA"_s + QString(45000, u' ') + u"->>"_s + QString(45000, u' ') + u"B: x"_s << true;
        QTest::newRow("spaces in a note")
                << u"sequenceDiagram\nNote over A"_s + QString(90000, u' ') + u": x"_s << true;
        // Drawings beyond any screen, within the other limits.
        QString tall = u"graph TD\n"_s;
        for (int i = 0; i < 999; ++i)
            tall += u"n"_s + QString::number(i) + u" ----------> n"_s + QString::number(i + 1) + u'\n';
        QTest::newRow("too tall") << tall << false;
        QString sections = u"sequenceDiagram\nalt a\n"_s + QString(u"else b\n"_s).repeated(3000) + u"end\n"_s;
        QTest::newRow("too many statements") << sections << false;
        QString deepSeq = u"sequenceDiagram\n"_s;
        for (int i = 0; i < 30; ++i)
            deepSeq += u"loop x\n"_s;
        QTest::newRow("deep blocks") << deepSeq << false;
    }
    void limits()
    {
        QFETCH(QString, source);
        QFETCH(bool, ok);
        QElapsedTimer t;
        t.start();
        const Result r = render(source);
        QCOMPARE(r.ok(), ok);
        if (!ok)
            QCOMPARE(int(r.error->code), int(Error::Code::TooLarge));
        // Fast: hostile notes never stall the editor (generous for sanitizers and slow CI).
        QVERIFY2(t.elapsed() < 6000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
        for (const Item &it : r.scene.items) {
            QVERIFY(it.lines.size() <= limits::MaxLabelLines + 1);
            for (const QString &l : it.lines)
                QVERIFY(l.size() <= limits::MaxLabelLength);
        }
    }

    void garbage_data()
    {
        QTest::addColumn<QString>("source");
        // Malformed input of every kind: an error or a drawing, never a crash.
        QTest::newRow("brackets") << u"graph TD\n[[[(((((]]]))))"_s;
        QTest::newRow("only arrows") << u"graph TD\n--> --> -->"_s;
        QTest::newRow("unicode ids") << u"graph TD\nżółw-->ŻÓŁW-->日本-->🙂"_s;
        QTest::newRow("nul") << u"graph TD\nA[\0]-->B"_s;
        QTest::newRow("controls") << u"graph TD\nA[\x01\x02\x1b]-->B"_s;
        QTest::newRow("lone surrogate") << u"graph TD\nA[x"_s + QChar(0xd800) + u"y]-->B"_s;
        QTest::newRow("CRLF") << u"graph TD\r\nA-->B\r\nB-->C\r\n"_s;
        QTest::newRow("semicolon in quotes") << u"graph TD;A[\"a;b\"]-->B;"_s;
        QTest::newRow("empty subgraph") << u"graph TD\nsubgraph pusty\nend\nA-->pusty"_s;
        QTest::newRow("subgraph to subgraph") << u"graph TD\nsubgraph a\nx\nend\nsubgraph b\ny\nend\na-->b\nb-->a"_s;
        QTest::newRow("link to own subgraph") << u"graph TD\nsubgraph a\nx\nend\nx-->a\na-->x"_s;
        QTest::newRow("only header") << u"flowchart LR"_s;
        QTest::newRow("sequence only header") << u"sequenceDiagram"_s;
        QTest::newRow("sequence garbage") << u"sequenceDiagram\n->>: :\n"_s;
        QTest::newRow("note over many") << u"sequenceDiagram\nNote over A,B,C: x"_s;
        QTest::newRow("pie huge") << u"pie\n\"a\" : 1e308\n\"b\" : 1e308"_s;
        QTest::newRow("pie tiny") << u"pie\n\"a\" : 1e-300\n\"b\" : 1"_s;
        QTest::newRow("pie nan") << u"pie\n\"a\" : nan"_s;
        QTest::newRow("style junk")
                << u"graph TD\nA-->B\nstyle A fill:#f9f;;;:,\nlinkStyle 99 stroke:red\nlinkStyle x y"_s;
        QTest::newRow("classDef junk") << u"graph TD\nclassDef\nclass\nA:::"_s;
        QTest::newRow("deactivate unknown") << u"sequenceDiagram\ndeactivate X\nA->>-B: x"_s;
        QTest::newRow("frontmatter unclosed") << u"---\ntitle: x\ngraph TD\nA"_s;
    }
    void garbage()
    {
        QFETCH(QString, source);
        const Result r = render(source);
        if (r.ok()) {
            QVERIFY(r.scene.size.width() > 0 && r.scene.size.height() > 0);
            const QString svg = toSvg(r.scene, lightPalette());
            QVERIFY(wellFormed(svg));
        } else {
            QVERIFY(r.scene.items.isEmpty());
        }
    }

    void deterministic()
    {
        const QString src = u"graph TD\nA-->B & C & D\nB-->E\nC-->E\nD-->F\nE-->F\nF-->A\nsubgraph s\nB\nC\nend"_s;
        const QString a = toSvg(render(src).scene, lightPalette(), nullptr);
        for (int i = 0; i < 3; ++i)
            QCOMPARE(toSvg(render(src).scene, lightPalette(), nullptr), a);
    }

    void palettes()
    {
        const QRegularExpression hex(u"^#[0-9a-f]{6}$"_s);
        for (const Palette &p : {lightPalette(), darkPalette(), themedPalette({}),
                                 themedPalette({u"garbage"_s, u"\"><x"_s, QString(), u"#12"_s, u"#abc"_s, true})}) {
            for (int r = int(Role::Background); r < RoleCount; ++r)
                QVERIFY2(hex.match(p.colors[r]).hasMatch(), qPrintable(QString::number(r) + u' ' + p.colors[r]));
        }
        // The dark palette is dark, the light one light; the text reads on the nodes.
        QVERIFY(lightPalette()[Role::Background] != darkPalette()[Role::Background]);
        QCOMPARE(lightPalette()[Role::NodeText], u"#1f2430"_s);
        QCOMPARE(darkPalette()[Role::NodeText], u"#e6e3dc"_s);
        QCOMPARE(colorOf(Paint{Role::NodeFill, u"#123456"_s}, lightPalette()), u"#123456"_s);
        QCOMPARE(colorOf(Paint{}, lightPalette()), QString());
    }

    void svg()
    {
        const Result r = render(u"---\ntitle: \"Tytuł & <coś>\"\n---\n"
                                u"graph TD\nA[\"<script>alert('x')</script> & ą\"] -->|\"onload=x\"| B{\"a#quot;b\"}\n"
                                u"style A fill:#ff000080\nclick A href \"javascript:alert(1)\"\n"_s);
        QVERIFY(r.ok());
        const Palette dark = darkPalette();
        const QString svg = toSvg(r.scene, lightPalette(), &dark);
        QStringList elements, attributes;
        QVERIFY(wellFormed(svg, &elements, &attributes));
        QCOMPARE(elements.front(), u"svg"_s);
        for (const QString &e : std::as_const(elements))
            QVERIFY2((QStringList{u"svg"_s, u"title"_s, u"style"_s, u"path"_s, u"rect"_s, u"text"_s}.contains(e)),
                     qPrintable(e));
        for (const QString &a : std::as_const(attributes)) {
            QVERIFY2(!a.startsWith(u"on"_s, Qt::CaseInsensitive), qPrintable(a));
            QVERIFY2(!a.contains(u"href"_s, Qt::CaseInsensitive), qPrintable(a));
        }
        QVERIFY(!svg.contains(u"<script"_s, Qt::CaseInsensitive));
        QVERIFY(!svg.contains(u"foreignObject"_s, Qt::CaseInsensitive));
        QVERIFY(!svg.contains(u"javascript"_s, Qt::CaseInsensitive));
        QVERIFY(!svg.contains(u"url("_s));
        QVERIFY(svg.contains(u"&lt;script&gt;alert(&apos;x&apos;)&lt;/script&gt;"_s));
        QVERIFY(svg.contains(u"&amp;"_s) && svg.contains(u"ą"_s));
        QVERIFY(svg.contains(u"<title>Tytuł &amp; &lt;coś&gt;</title>"_s));
        QVERIFY(svg.contains(u"role=\"img\""_s));
        QVERIFY(svg.contains(u"viewBox=\"0 0 "_s));
        // An explicit colour with alpha: the colour and its opacity.
        QVERIFY(svg.contains(u"fill=\"#ff0000\" fill-opacity=\"0.5\""_s));
        // The dark theme: one media block with the roles' colours.
        QVERIFY(svg.contains(u"<style>@media (prefers-color-scheme:dark){"_s));
        QVERIFY(svg.contains(u".bmd-f"_s + QString::number(int(Role::NodeFill)) + u"{fill:"_s + dark[Role::NodeFill]
                             + u'}'));
        QVERIFY(svg.contains(u"class=\"bmd-f"_s));
        QCOMPARE(svg.count(u"@media"_s), 1);
        // Without the dark palette: no style sheet, no classes.
        const QString light = toSvg(r.scene, lightPalette());
        QVERIFY(!light.contains(u"<style"_s));
        QVERIFY(!light.contains(u"class=\"bmd"_s));
        QVERIFY(wellFormed(light));
        // Characters XML forbids are left out.
        Scene s;
        s.size = QSizeF(10, 10);
        Item t;
        t.type = Item::Type::Text;
        t.lines = {u"a\x01"_s + QChar(0xdc00) + u"b\x7f"_s};
        t.color = Paint{Role::NodeText, {}};
        s.items.append(t);
        Item bad;
        bad.path = rectPath(QRectF(0, 0, 1, 1));
        bad.fill = Paint{Role::None, u"\"/><script>"_s};
        s.items.append(bad);
        const QString x = toSvg(s, lightPalette());
        QVERIFY(wellFormed(x));
        QVERIFY(!x.contains(u"script"_s));
        QVERIFY(x.contains(u"<title>Diagram</title>"_s));
    }

    void labels_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("clean");
        QTest::newRow("trim") << u"  ąę  "_s << u"ąę"_s;
        QTest::newRow("quotes") << u"\"cytat\""_s << u"cytat"_s;
        QTest::newRow("one quote") << u"\"pół"_s << u"\"pół"_s;
        QTest::newRow("br lines trimmed") << u"a <br> b"_s << u"a\nb"_s;
        QTest::newRow("BR upper") << u"a<BR/>b"_s << u"a\nb"_s;
        QTest::newRow("named entity") << u"#hellip;"_s << u"…"_s;
        QTest::newRow("hex entity") << u"#x41;"_s << u"A"_s;
        QTest::newRow("unknown entity") << u"#nieznany;"_s << u"#nieznany;"_s;
        QTest::newRow("hash text") << u"#1 na liście"_s << u"#1 na liście"_s;
        QTest::newRow("tab") << u"a\tb"_s << u"a b"_s;
    }
    void labels()
    {
        QFETCH(QString, raw);
        QFETCH(QString, clean);
        QCOMPARE(cleanLabel(raw), clean);
    }

    void wrapping()
    {
        const Metrics m = approximateMetrics(14);
        const TextBlock t =
                layoutText(m, u"Bardzo długa etykieta węzła, która musi się zawinąć w kilka linii"_s, 14, false, 120);
        QVERIFY(t.lines.size() > 2);
        for (const QString &l : t.lines)
            QVERIFY(m.measure(l, 14, false).width() <= 120 + 0.01);
        QCOMPARE(t.lines.join(u' '), u"Bardzo długa etykieta węzła, która musi się zawinąć w kilka linii"_s);
        const TextBlock word = layoutText(m, QString(300, u'w'), 14, false, 100);
        QVERIFY(word.lines.size() > 1);
        QVERIFY(word.size.width() <= 100 + 0.01);
        const TextBlock many = layoutText(m, QString(u"x\n"_s).repeated(100), 14, false, 0);
        QCOMPARE(many.lines.size(), limits::MaxLabelLines);
        QVERIFY(many.lines.last().endsWith(u'…'));
        // Explicit breaks are kept, empty lines too.
        QCOMPARE(layoutText(m, u"a\n\nb"_s, 14, false, 200).lines, (QStringList{u"a"_s, QString(), u"b"_s}));
        // Bold is wider, wide characters take an em.
        QVERIFY(m.measure(u"abc", 14, true).width() > m.measure(u"abc", 14, false).width());
        QCOMPARE(m.measure(u"日本", 10, false).width(), 20.0);
    }
};

QTEST_GUILESS_MAIN(TstDiagram)
#include "tst_diagram.moc"
