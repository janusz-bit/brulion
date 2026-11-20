// Luau's type checker over extensions (src/core/extensions/typecheck.h): the mistakes it finds with
// the page's lines — a function of `b` that is not there, an unknown global, a type that does not
// fit in strict code, a lint —, none in code that works (every built-in, every sample, every example
// of the documentation), the declarations of `b` naming every function of it, and hostile code that
// leaves the checker in time.

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/host.h"
#include "extensions/pageextension.h"
#include "extensions/typecheck.h"
#include "markdown/parse.h"

#include "testtiming.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

QString readSource(const QString &relativePath)
{
    QFile file(QStringLiteral(BRULION_SOURCE_DIR) + u'/' + relativePath);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QString describe(const QList<TypeProblem> &problems)
{
    QStringList out;
    for (const TypeProblem &p : problems)
        out << u"%1:%2 [%3] %4"_s.arg(p.line).arg(p.column).arg(p.kind, p.message);
    return out.join(u'\n');
}

QList<TypeProblem> typeErrors(const QList<TypeProblem> &problems)
{
    QList<TypeProblem> out;
    for (const TypeProblem &p : problems)
        if (p.kind == u"type")
            out << p;
    return out;
}

/// Every ```luau block of a Markdown file, padded with empty lines: its lines are the file's.
QStringList examplesIn(const QString &relativePath)
{
    static const QRegularExpression opening(uR"(^\s*(`{3,}|~{3,})\s*(?:luau|lua)\s*$)"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    QStringList out;
    const QStringList lines = readSource(relativePath).split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QRegularExpressionMatch m = opening.match(lines[i]);
        if (!m.hasMatch())
            continue;
        const QString fence = m.captured(1);
        QStringList body;
        qsizetype j = i + 1;
        for (; j < lines.size(); ++j) {
            const QString t = lines[j].trimmed();
            if (t.size() >= fence.size() && t.count(fence[0]) == t.size())
                break;
            body << lines[j];
        }
        out << QString(i + 1, u'\n') + body.join(u'\n');
        i = j;
    }
    return out;
}

/// A host for an engine that only lists what `b` has.
class NoHost : public Host {
public:
    QStringList pages(const QString &) override { return {}; }
    std::optional<QString> read(const QString &) override { return std::nullopt; }
    QVariantMap page(const QString &) override { return {}; }
    QVariantList blocks(const BlockQuery &) override { return {}; }
    QVariantList search(const QString &, int) override { return {}; }
    QVariantMap query(const QString &, int, QString *) override { return {}; }
    QString write(const QString &, const QString &, const QString &) override { return {}; }
    QString appendToJournal(const QString &, const QString &, const QString &) override { return {}; }
    QString replaceBlock(const QString &, const QString &, const QString &, const std::optional<QString> &,
                         QString *) override
    {
        return {};
    }
    QString today() override { return u"2026-11-20"_s; }
    QString language() override { return u"pl"_s; }
    QString uiLanguage() override { return u"pl"_s; }
    void timer(const QString &, int, const QString &) override { }
    void log(const QString &, const QString &) override { }
};

} // namespace

class TestExtensionsTypes : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        if (!typeCheckerAvailable())
            QSKIP("this build has no type checker (BRULION_LUAU_CHECK off)");
    }

    void findsMistakes_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("message");
        QTest::newRow("a function of b that is not there")
                << u"\n\nb.command(\"x\", function(ctx)\n    return b.raed(\"Dom\")\nend)"_s << 4 << u"raed"_s;
        QTest::newRow("a node of b.ui that is not there")
                << u"b.view(\"v\", function() return b.ui.colum({}) end)"_s << 1 << u"colum"_s;
        QTest::newRow("an unknown global") << u"local x = 1\nprnt(\"żółw\")"_s << 2 << u"prnt"_s;
        QTest::newRow("too few arguments") << u"local ok = b.write(\"Dom\")"_s << 1 << u"rgument"_s;
        QTest::newRow("a colon for a dot") << u"local d = b:today()"_s << 1 << u"dot instead of a colon"_s;
        QTest::newRow("strict: a text where a number is")
                << u"--!strict\nlocal n: number = b.today()"_s << 2 << u"number"_s;
        QTest::newRow("strict: a nil that may be")
                << u"--!strict\nlocal p = b.page(\"Dom\")\nlocal x = p.name"_s << 3 << u"nil"_s;
        QTest::newRow("unreachable code")
                << u"local function f(x)\n    if x then return 1 else return 2 end\n    print(\"nigdy\")\nend"_s << 3
                << u"nreachable"_s;
        QTest::newRow("a syntax error") << u"local x = = 1"_s << 1 << u"xpected"_s;
    }
    void findsMistakes()
    {
        QFETCH(QString, code);
        QFETCH(int, line);
        QFETCH(QString, message);
        const QList<TypeProblem> problems = typeCheck(code);
        bool found = false;
        for (const TypeProblem &p : problems)
            found = found || (p.line == line && p.message.contains(message));
        QVERIFY2(found, qPrintable(describe(problems)));
    }

    void tooManyArgumentsOfACallAreFine()
    {
        // The extra values of a call passed on (Lua drops them): no warning, also in a function's
        // own annotated code.
        QVERIFY(typeErrors(typeCheck(u"local function trim(s: string): string return (s:match(\"^%s*(.-)%s*$\")) end\n"
                                     "local t = trim((\" a \"):gsub(\"a\", \"b\"))"_s))
                        .isEmpty());
    }

    void strictAsksMore()
    {
        // The same code: nothing nonstrict, the optional value with --!strict.
        const QString code = u"local p = b.page(\"Dom\")\nlocal name = p.name"_s;
        QVERIFY(typeErrors(typeCheck(code)).isEmpty());
        QVERIFY(!typeErrors(typeCheck(u"--!strict\n"_s + code)).isEmpty());
        QVERIFY(typeCheck(u"--!nocheck\nb.raed()"_s).isEmpty());
    }

    void thePagesLines()
    {
        // An extension page's code keeps the page's lines (ExtensionCode::source): a warning names them.
        const QString page = u"---\ntags: [rozszerzenie]\n---\n# Licznik\n\nOpis.\n\n```luau\nlocal a = 1\n"
                             "b.comand(\"x\", function() end)\n```\n"_s;
        const PageExtension e = readPageExtension(u"Licznik.md"_s, markdown::parseNote(u"Licznik.md"_s, page), page);
        const QList<TypeProblem> problems = typeCheck(e.source);
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().line, 10);
        QVERIFY(problems.first().message.contains(u"comand"_s));
        QCOMPARE(describeTypeProblems(problems, u"Licznik"_s).section(u':', 0, 1), u"Licznik:10"_s);
    }

    void codeThatWorksHasNoTypeErrors_data()
    {
        QTest::addColumn<QString>("code");
        // (The built-ins are `--!strict` and annotate all they can; strict mode asks more of
        // annotations than working code gives: here they are read as a page without it is.)
        for (const BuiltinExtension &b : builtinExtensions())
            QTest::newRow(qPrintable(u"built-in "_s + b.extension.id))
                    << QString(b.extension.source).replace(u"--!strict"_s, u"--!nonstrict"_s);
        const QString samples = u"tests/core/extensions/samples/extensions"_s;
        QDirIterator it(QStringLiteral(BRULION_SOURCE_DIR) + u'/' + samples, {u"*.md"_s}, QDir::Files);
        while (it.hasNext()) {
            it.next();
            const QString content = readSource(samples + u'/' + it.fileName());
            const PageExtension e =
                    readPageExtension(it.fileName(), markdown::parseNote(it.fileName(), content), content);
            QTest::newRow(qPrintable(u"sample "_s + it.fileName())) << e.source;
        }
        QStringList docs{u"docs/EXTENSIONS.md"_s};
        for (const QString &lang : {u"en"_s, u"pl"_s}) {
            QDirIterator pages(QStringLiteral(BRULION_SOURCE_DIR) + u"/src/app/shell/guide/"_s + lang, {u"*.md"_s},
                                              QDir::Files);
            while (pages.hasNext()) {
                pages.next();
                docs << u"src/app/shell/guide/"_s + lang + u'/' + pages.fileName();
            }
        }
        for (const QString &doc : std::as_const(docs)) {
            int n = 0;
            for (const QString &code : examplesIn(doc))
                QTest::newRow(qPrintable(u"%1 #%2"_s.arg(doc).arg(++n))) << code;
        }
    }
    void codeThatWorksHasNoTypeErrors()
    {
        // A warning points at a mistake, never at working code: the declarations of `b` are loose
        // where the API is.
        QFETCH(QString, code);
        const QList<TypeProblem> errors = typeErrors(typeCheck(code, test::kRoomyMs));
        QVERIFY2(errors.isEmpty(), qPrintable(describe(errors)));
    }

    void declarationsNameEveryFunction()
    {
        // A function added to `b` fails here until builtin/b.d.luau declares it.
        NoHost host;
        Engine engine;
        QVERIFY(engine.start(&host, Limits{}));
        Extension x;
        x.id = u"t"_s;
        QVERIFY(engine.load(x).ok());
        const Result names = engine.evaluate(u"t"_s, uR"lua(
            local names = {}
            for name, value in b do
                if type(value) == "table" then
                    for inner in value do table.insert(names, name .. "." .. inner) end
                else
                    table.insert(names, name)
                end
            end
            return names
        )lua"_s,
                                             u"names"_s);
        QVERIFY2(names.ok(), qPrintable(names.error));
        const QString defs = apiDefinitions();
        QVERIFY(defs.contains(u"declare b"_s));
        QStringList missing;
        for (const QString &name : names.value.toStringList()) {
            const QString member = name.section(u'.', -1);
            if (!QRegularExpression(u"\\b"_s + QRegularExpression::escape(member) + u"\\s*:"_s).match(defs).hasMatch())
                missing << name;
        }
        QVERIFY2(missing.isEmpty(), qPrintable(missing.join(u", "_s)));
    }

    void hostileCodeLeavesInTime()
    {
        // Nested a thousand times, fifty thousand lines, a table of a hundred thousand fields:
        // warnings or none, but the checker returns, within its time limit and some.
        const QStringList hostile{
                QString(2000, u'(') + u"1"_s + QString(2000, u')'),
                u"local t = "_s + QString(500, u'{') + QString(500, u'}'),
                QString(u"local a = b.today()\n"_s).repeated(50000),
                u"local t = {"_s + QString(u"x = 1, "_s).repeated(100000) + u"}"_s,
                u"type T<A> = { a: T<{A}> }\nlocal x: T<number> = nil :: any"_s,
        };
        for (const QString &code : hostile) {
            test::CpuTimer t;
            t.start();
            const QList<TypeProblem> problems = typeCheck(code, 2000);
            QVERIFY2(t.elapsed() < 20000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(t.elapsed())));
            QVERIFY(problems.size() <= kMaxTypeProblems);
        }
        // And works afterwards.
        QVERIFY(typeCheck(u"b.raed()"_s).first().message.contains(u"raed"_s));
    }
};

QTEST_GUILESS_MAIN(TestExtensionsTypes)
#include "tst_extensions_types.moc"
