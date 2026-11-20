// The editor's AI assistant (writing/writing.h): what each action asks the model (its
// instruction, the context around the text, the length of the answer, effort "low"), the presets,
// what cannot be sent, the model's answers cleaned for the note (a fence, a preamble, quotes
// around them, a list marker or line breaks an inline text does not take, a continuation that
// repeats the text's end — also while the answer still comes), where a result goes as a pure
// edit (a rewrite in place with the selection's whitespace kept, a continuation after a space or
// a line break, a summary as a paragraph of its own) and the change word by word. Polish text,
// CRLF, malformed answers.
#include "writing/writing.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::writing;

Q_DECLARE_METATYPE(brulion::writing::Action)

namespace {

QString userText(const ai::Request &r)
{
    return r.messages.isEmpty() ? QString() : r.messages.first().content;
}

/// The diff as "=same -removed +added" parts.
QString shown(const QList<Change> &changes)
{
    QStringList out;
    for (const Change &c : changes)
        out << (c.kind == Change::Same ? u"="_s : c.kind == Change::Removed ? u"-"_s : u"+"_s) + c.text;
    return out.join(u'|');
}

} // namespace

class TestWriting : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void actionsAndPresets()
    {
        for (const Action a : {Action::Rewrite, Action::Continue, Action::Summarize})
            QCOMPARE(actionFrom(actionName(a)), a);
        QCOMPARE(actionFrom(u"nonsense"), Action::Rewrite);
        QCOMPARE(presets().size(), 7);
        for (const QString &preset : presets())
            QVERIFY2(!presetInstruction(preset).isEmpty(), qPrintable(preset));
        QVERIFY(presetInstruction(u"polish").contains(u"Polish"_s));
        QVERIFY(presetInstruction(u"żaden").isEmpty());
    }

    void requests()
    {
        Input in;
        in.action = Action::Rewrite;
        in.instruction = presetInstruction(u"shorter");
        in.text = u"Zażółć gęślą jaźń, bo jutro [[Dom]] i #remont 📅 2026-11-20."_s;
        in.before = u"Wstęp."_s;
        in.after = u"Dalej."_s;
        in.page = u"Remont"_s;
        QVERIFY(problem(in).isEmpty());
        const ai::Request r = request(in);
        QCOMPARE(r.messages.size(), 1);
        QCOMPARE(r.messages.first().role, u"user"_s);
        QCOMPARE(r.effort, u"low"_s);
        QVERIFY(!r.thoughts);
        QVERIFY(r.tools.isEmpty());
        QVERIFY(r.system.contains(u"alone"_s));
        QVERIFY(r.system.contains(u"[[…]]"_s));
        const QString text = userText(r);
        QVERIFY(text.startsWith(u"Rewrite the text below: Make it shorter"_s));
        QVERIFY(text.contains(u"<text>\n"_s + in.text + u"\n</text>"_s));
        QVERIFY(text.contains(u"<before>\nWstęp.\n</before>"_s));
        QVERIFY(text.contains(u"<after>\nDalej.\n</after>"_s));
        QVERIFY(text.contains(u"The note: Remont"_s));
        QVERIFY(!text.contains(u"one line"_s));
        QVERIFY(r.maxTokens >= 1000 && r.maxTokens <= 16000);
        // Inline: one line back.
        in.inline_ = true;
        QVERIFY(userText(request(in)).contains(u"give back one line"_s));
        in.text = u"dwa\nwiersze"_s;
        QVERIFY(userText(request(in)).contains(u"without a list marker"_s));
        // Continue: the text before the caret, nothing before it; Summarize: a short paragraph.
        in.action = Action::Continue;
        in.instruction.clear();
        in.text = u"Jutro jedziemy do"_s;
        const QString more = userText(request(in));
        QVERIFY(more.startsWith(u"Continue the text below"_s));
        QVERIFY(!more.contains(u"<before>"_s));
        QCOMPARE(request(in).maxTokens, 800);
        in.action = Action::Summarize;
        QVERIFY(userText(request(in)).startsWith(u"Sum the text below up"_s));
    }

    void longContextIsCut()
    {
        Input in;
        in.action = Action::Summarize;
        in.text = u"Środek."_s;
        in.before = QString(3000, u'ą') + u" koniec przed"_s;
        in.after = u"początek po "_s + QString(3000, u'ę');
        const QString text = userText(request(in));
        QVERIFY(text.contains(u"koniec przed\n</before>"_s));
        QVERIFY(text.contains(u"<after>\npoczątek po"_s));
        QVERIFY(text.size() < 2 * MaxContext + 1200);
        // A continuation of a long text: its end goes, never half a letter.
        in.action = Action::Continue;
        in.text = QString(MaxText + 500, u'x') + u" 🤖 ostatnie słowa"_s;
        const QString end = userText(request(in));
        QVERIFY(end.contains(u"ostatnie słowa\n</text>"_s));
        QVERIFY(problem(in).isEmpty()); // (a continuation sends the end of any text)
    }

    void problems_data()
    {
        QTest::addColumn<Action>("action");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("instruction");
        QTest::addColumn<QString>("problem"); // a word of it, "" none

        QTest::newRow("fine") << Action::Rewrite << u"Ala"_s << u"krócej"_s << QString();
        QTest::newRow("no text") << Action::Rewrite << u"  \n"_s << u"krócej"_s << u"no text"_s;
        QTest::newRow("nothing to continue") << Action::Continue << QString() << QString() << u"continue"_s;
        QTest::newRow("no instruction") << Action::Rewrite << u"Ala"_s << u" "_s << u"how"_s;
        QTest::newRow("too long") << Action::Summarize << QString(MaxText + 1, u'ż') << QString() << u"too long"_s;
        QTest::newRow("a summary needs no instruction") << Action::Summarize << u"Ala"_s << QString() << QString();
    }
    void problems()
    {
        QFETCH(Action, action);
        QFETCH(QString, text);
        QFETCH(QString, instruction);
        QFETCH(QString, problem);
        Input in;
        in.action = action;
        in.text = text;
        in.instruction = instruction;
        if (problem.isEmpty())
            QCOMPARE(writing::problem(in), QString());
        else
            QVERIFY2(writing::problem(in).contains(problem), qPrintable(writing::problem(in)));
    }

    void cleaning_data()
    {
        QTest::addColumn<Action>("action");
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("inlineText");
        QTest::addColumn<QString>("answer");
        QTest::addColumn<bool>("partial");
        QTest::addColumn<QString>("expected");

        const Action rw = Action::Rewrite;
        QTest::newRow("as it is") << rw << u"Ala ma kota."_s << false << u"  Ala ma psa.\n"_s << false << u"Ala ma psa."_s;
        QTest::newRow("a fence around it") << rw << u"tekst"_s << false << u"```markdown\n- [ ] zadanie\n- [x] inne\n```"_s
                                           << false << u"- [ ] zadanie\n- [x] inne"_s;
        QTest::newRow("a fence still open") << rw << u"tekst"_s << false << u"```\nżółw je"_s << true << u"żółw je"_s;
        QTest::newRow("an open fence at the end: kept") << rw << u"tekst"_s << false << u"```\nżółw je"_s << false
                                                        << u"```\nżółw je"_s;
        QTest::newRow("code the text had") << rw << u"```luau\nprint(1)\n```"_s << false
                                           << u"```luau\nprint(2)\n```"_s << false << u"```luau\nprint(2)\n```"_s;
        QTest::newRow("an English preamble") << rw << u"Ala"_s << false << u"Here is the rewritten text:\n\nAla ma psa."_s
                                             << false << u"Ala ma psa."_s;
        QTest::newRow("a Polish preamble") << rw << u"Ala"_s << false << u"Oto poprawiony tekst:\nAla ma psa."_s << false
                                           << u"Ala ma psa."_s;
        QTest::newRow("a colon in the text itself") << rw << u"Plan:\n- a"_s << false << u"Plan dnia:\n- a\n- b"_s << false
                                                    << u"Plan dnia:\n- a\n- b"_s;
        QTest::newRow("quotes around it") << rw << u"Ala ma kota"_s << false << u"„Ala ma psa”"_s << false << u"Ala ma psa"_s;
        QTest::newRow("English quotes") << rw << u"Ala"_s << false << u"\"Ala has a dog.\""_s << false << u"Ala has a dog."_s;
        QTest::newRow("quotes the text had") << rw << u"\"cytat\""_s << false << u"\"inny cytat\""_s << false
                                             << u"\"inny cytat\""_s;
        QTest::newRow("two quotes: not around it") << rw << u"a b"_s << false << u"\"a\" i \"b\""_s << false
                                                   << u"\"a\" i \"b\""_s;
        QTest::newRow("inline: no marker of its own") << rw << u"kup mleko"_s << true << u"- [ ] kup mleko i chleb"_s
                                                      << false << u"kup mleko i chleb"_s;
        QTest::newRow("inline: one line") << rw << u"kup mleko"_s << true << u"kup mleko\ni chleb"_s << false
                                          << u"kup mleko i chleb"_s;
        QTest::newRow("inline: lines it had") << rw << u"a\nb"_s << true << u"a\nc"_s << false << u"a\nc"_s;
        QTest::newRow("CRLF") << rw << u"x"_s << false << u"a\r\nb\r\n"_s << false << u"a\nb"_s;
        QTest::newRow("continue: the end repeated")
                << Action::Continue << u"Jutro jedziemy do Krakowa, a potem"_s << false
                << u"do Krakowa, a potem do Zakopanego."_s << false << u"do Zakopanego."_s;
        QTest::newRow("continue: a short overlap stays") << Action::Continue << u"Idę do"_s << false << u"do domu"_s
                                                         << false << u"do domu"_s;
        QTest::newRow("empty") << rw << u"x"_s << false << u"   "_s << false << QString();
    }
    void cleaning()
    {
        QFETCH(Action, action);
        QFETCH(QString, text);
        QFETCH(bool, inlineText);
        QFETCH(QString, answer);
        QFETCH(bool, partial);
        QFETCH(QString, expected);
        Input in;
        in.action = action;
        in.text = text;
        in.inline_ = inlineText;
        QCOMPARE(clean(answer, in, partial), expected);
    }

    void edits_data()
    {
        QTest::addColumn<Action>("action");
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<QString>("result");
        QTest::addColumn<QString>("expected");

        const Action rw = Action::Rewrite, more = Action::Continue, sum = Action::Summarize;
        QTest::newRow("rewrite in place") << rw << u"Ala ma kota i psa."_s << 4 << 11 << u"ma żółwia"_s
                                          << u"Ala ma żółwia i psa."_s;
        QTest::newRow("the selection's spaces kept") << rw << u"Ala ma kota i psa."_s << 3 << 12 << u" ma żółwia "_s
                                                     << u"Ala ma żółwia i psa."_s;
        QTest::newRow("all of it") << rw << u"stare\n"_s << 0 << 6 << u"nowe"_s << u"nowe\n"_s;
        QTest::newRow("backwards, out of range") << rw << u"abc"_s << 99 << -4 << u"x"_s << u"x"_s;
        QTest::newRow("only spaces selected") << rw << u"a   b"_s << 1 << 4 << u"-"_s << u"a-b"_s;
        QTest::newRow("continue after a word") << more << u"Jutro jedziemy"_s << 14 << 14 << u"do Krakowa."_s
                                               << u"Jutro jedziemy do Krakowa."_s;
        QTest::newRow("continue after a space") << more << u"Jutro "_s << 6 << 6 << u"pada."_s << u"Jutro pada."_s;
        QTest::newRow("continue with punctuation") << more << u"Jutro"_s << 5 << 5 << u", może."_s << u"Jutro, może."_s;
        QTest::newRow("continue a list") << more << u"- mleko"_s << 7 << 7 << u"- chleb"_s << u"- mleko\n- chleb"_s;
        QTest::newRow("continue in the middle") << more << u"Ala kot"_s << 3 << 3 << u"ma"_s << u"Ala ma kot"_s;
        QTest::newRow("continue at a word's start") << more << u"Alakot"_s << 3 << 3 << u"ma"_s << u"Ala ma kot"_s;
        QTest::newRow("continue nothing") << more << u"Ala"_s << 3 << 3 << u"  "_s << u"Ala"_s;
        QTest::newRow("summary after the paragraph") << sum << u"Pierwszy akapit.\n\nDrugi."_s << 0 << 9 << u"Krótko."_s
                                                     << u"Pierwszy akapit.\n\nKrótko.\n\nDrugi."_s;
        QTest::newRow("summary at the end") << sum << u"Tekst."_s << 0 << 6 << u"Krótko."_s << u"Tekst.\n\nKrótko."_s;
        QTest::newRow("summary at the end, a line break") << sum << u"Tekst.\n"_s << 0 << 7 << u"Krótko."_s
                                                          << u"Tekst.\n\nKrótko.\n"_s;
        QTest::newRow("summary before a blank line") << sum << u"A.\n\nB."_s << 0 << 2 << u"S."_s << u"A.\n\nS.\n\nB."_s;
        QTest::newRow("summary, CRLF") << sum << u"A.\r\n\r\nB."_s << 0 << 1 << u"S."_s << u"A.\n\nS.\r\n\r\nB."_s;
        QTest::newRow("summary of a selection ending at a line break") << sum << u"A.\nB.\n"_s << 0 << 3 << u"S."_s
                                                                      << u"A.\n\nS.\n\nB.\n"_s;
    }
    void edits()
    {
        QFETCH(Action, action);
        QFETCH(QString, text);
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(QString, result);
        QFETCH(QString, expected);
        const Edit e = edit(text, from, to, result, action);
        QVERIFY(e.from >= 0 && e.from <= e.to && e.to <= text.size());
        QCOMPARE(apply(text, e), expected);
    }

    void applyClamps()
    {
        QCOMPARE(apply(u"abc", Edit{2, 1, u"X"_s}), u"abXc"_s);
        QCOMPARE(apply(u"abc", Edit{-5, 99, u"X"_s}), u"X"_s);
        QCOMPARE(apply(QString(), Edit{0, 0, u"Ż"_s}), u"Ż"_s);
    }

    void diffs_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QString>("changes");

        QTest::newRow("the same") << u"Ala ma kota."_s << u"Ala ma kota."_s << u"=Ala ma kota."_s;
        QTest::newRow("a word") << u"Ala ma kota."_s << u"Ala ma psa."_s << u"=Ala ma |-kota|+psa|=."_s;
        QTest::newRow("words with a space between: one change")
                << u"Zażółć gęślą jaźń dziś"_s << u"Zażółć żółtą gęś dziś"_s << u"=Zażółć |-gęślą jaźń|+żółtą gęś|= dziś"_s;
        QTest::newRow("added at the end") << u"Ala"_s << u"Ala ma kota"_s << u"=Ala|+ ma kota"_s;
        QTest::newRow("removed") << u"Ala ma kota"_s << u"Ala"_s << u"=Ala|- ma kota"_s;
        QTest::newRow("from nothing") << QString() << u"nowe"_s << u"+nowe"_s;
        QTest::newRow("to nothing") << u"stare"_s << QString() << u"-stare"_s;
        QTest::newRow("Markdown") << u"- [ ] kup mleko"_s << u"- [x] kup mleko"_s << u"=- [|- |+x|=] kup mleko"_s;
    }
    void diffs()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QFETCH(QString, changes);
        const QList<Change> d = diff(before, after);
        QCOMPARE(shown(d), changes);
        // Each side read back from its parts.
        QString a, b;
        for (const Change &c : d) {
            if (c.kind != Change::Added)
                a += c.text;
            if (c.kind != Change::Removed)
                b += c.text;
        }
        QCOMPARE(a, before);
        QCOMPARE(b, after);
    }

    void longDiffs()
    {
        // Too long to compare word for word: all of it removed, all added (and quickly).
        QString before, after;
        for (int i = 0; i < 2500; ++i) {
            before += u"słowo%1 "_s.arg(i);
            after += u"inne%1 "_s.arg(i);
        }
        const QList<Change> d = diff(before, after);
        QCOMPARE(d.size(), 3); // (the last space is the same)
        QCOMPARE(d.at(0).kind, Change::Removed);
        QCOMPARE(d.at(1).kind, Change::Added);
    }
};

QTEST_GUILESS_MAIN(TestWriting)
#include "tst_writing.moc"
