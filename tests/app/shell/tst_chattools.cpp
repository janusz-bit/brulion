// An AI agent's tool calls in words (src/app/shell/chattools.h; docs/UI.md "AI agents (ACP)"): the
// summary line of the chat's activity and the answer of a turn of tool calls without words — in
// English and in Polish through the app's own translations (brulion_en.qm holds the English plural
// forms, brulion_pl.qm the three Polish ones).
#include "chattools.h"

#include <QCoreApplication>
#include <QTest>
#include <QTranslator>
#include <QVariantMap>

using namespace Qt::StringLiterals;
using brulion::shell::ToolStep;

namespace {

/// "read:completed" → {read, completed}.
QList<ToolStep> stepsOf(const QStringList &steps)
{
    QList<ToolStep> out;
    for (const QString &step : steps) {
        const qsizetype colon = step.indexOf(u':');
        out.append({step.left(colon), step.mid(colon + 1)});
    }
    return out;
}

/// `n` calls of `kind` that went as `status`.
QStringList times(int n, const QString &kind, const QString &status = u"completed"_s)
{
    QStringList out;
    for (int i = 0; i < n; ++i)
        out << kind + u':' + status;
    return out;
}

} // namespace

class TestChatTools : public QObject {
    Q_OBJECT

    QTranslator m_translator;

    /// The app's translation `language` ("en", "pl") installed instead of the one before.
    bool speak(const QString &language)
    {
#ifdef BRULION_QM_DIR
        QCoreApplication::removeTranslator(&m_translator);
        if (!m_translator.load(QStringLiteral(BRULION_QM_DIR "/brulion_") + language + u".qm"_s))
            return false;
        return QCoreApplication::installTranslator(&m_translator);
#else
        Q_UNUSED(language);
        return false;
#endif
    }

private Q_SLOTS:
    void summary_data()
    {
        QTest::addColumn<QStringList>("steps");
        QTest::addColumn<QString>("english");
        QTest::addColumn<QString>("polish");
        QTest::newRow("none") << QStringList() << QString() << QString();
        QTest::newRow("one read") << times(1, u"read"_s) << u"Read 1 file"_s << u"Przeczytano 1 plik"_s;
        QTest::newRow("reads, commands, an edit")
                << times(3, u"read"_s) + times(2, u"execute"_s) + times(1, u"edit"_s)
                << u"Read 3 files · ran 2 commands · edited 1 file"_s
                << u"Przeczytano 3 pliki · uruchomiono 2 polecenia · zmieniono 1 plik"_s;
        QTest::newRow("5 and 22") << times(5, u"read"_s) + times(22, u"edit"_s) << u"Read 5 files · edited 22 files"_s
                                  << u"Przeczytano 5 plików · zmieniono 22 pliki"_s;
        QTest::newRow("12 and 14") << times(12, u"execute"_s) + times(14, u"delete"_s)
                                   << u"Ran 12 commands · deleted 14 files"_s
                                   << u"Uruchomiono 12 poleceń · usunięto 14 plików"_s;
        QTest::newRow("in the order done") << QStringList{u"edit:completed"_s, u"read:completed"_s, u"edit:completed"_s}
                                           << u"Edited 2 files · read 1 file"_s
                                           << u"Zmieniono 2 pliki · przeczytano 1 plik"_s;
        QTest::newRow("moves, searches")
                << times(1, u"move"_s) + times(2, u"search"_s) << u"Moved 1 file · searched 2 times"_s
                << u"Przeniesiono 1 plik · szukano 2 razy"_s;
        QTest::newRow("searched once, 5 times") << times(1, u"search"_s) + times(5, u"fetch"_s)
                                                << u"Searched 1 time · fetched 5 web pages"_s
                                                << u"Szukano 1 raz · pobrano 5 stron internetowych"_s;
        QTest::newRow("web pages 1, 2") << times(1, u"fetch"_s) << u"Fetched 1 web page"_s
                                        << u"Pobrano 1 stronę internetową"_s;
        QTest::newRow("web pages 23") << times(23, u"fetch"_s) << u"Fetched 23 web pages"_s
                                      << u"Pobrano 23 strony internetowe"_s;
        // Beyond three kinds, the rest are other steps; other steps come last.
        QTest::newRow("more than three kinds")
                << times(1, u"read"_s) + times(1, u"execute"_s) + times(1, u"edit"_s) + times(2, u"search"_s)
                           + times(1, u"fetch"_s)
                << u"Read 1 file · ran 1 command · edited 1 file · 3 other steps"_s
                << u"Przeczytano 1 plik · uruchomiono 1 polecenie · zmieniono 1 plik · 3 inne kroki"_s;
        QTest::newRow("other steps last") << QStringList{u"think:completed"_s, u"read:completed"_s}
                                          << u"Read 1 file · 1 other step"_s << u"Przeczytano 1 plik · 1 inny krok"_s;
        QTest::newRow("other steps, 5") << times(1, u"read"_s) + times(5, u"other"_s)
                                        << u"Read 1 file · 5 other steps"_s
                                        << u"Przeczytano 1 plik · 5 innych kroków"_s;
        QTest::newRow("only other steps")
                << QStringList{u"think:completed"_s, u"other:completed"_s, u":completed"_s, u"żółć:completed"_s}
                << u"Took 4 steps"_s << u"Wykonano 4 kroki"_s;
        QTest::newRow("one unknown step") << times(1, u"switch_mode"_s) << u"Took 1 step"_s << u"Wykonano 1 krok"_s;
        // Failed calls are counted apart, never as done.
        QTest::newRow("failed apart") << times(1, u"read"_s) + times(2, u"execute"_s, u"failed"_s)
                                      << u"Read 1 file · 2 steps failed"_s
                                      << u"Przeczytano 1 plik · 2 kroki się nie powiodły"_s;
        QTest::newRow("only failed, 1") << times(1, u"edit"_s, u"failed"_s) << u"1 step failed"_s
                                        << u"1 krok się nie powiódł"_s;
        QTest::newRow("only failed, 5") << times(5, u"execute"_s, u"failed"_s) << u"5 steps failed"_s
                                        << u"5 kroków się nie powiodło"_s;
        // A call running or waiting counts with its kind.
        QTest::newRow("running") << QStringList{u"read:in_progress"_s, u"execute:pending"_s}
                                 << u"Read 1 file · ran 1 command"_s << u"Przeczytano 1 plik · uruchomiono 1 polecenie"_s;
    }

    void summary()
    {
        QFETCH(QStringList, steps);
        QFETCH(QString, english);
        QFETCH(QString, polish);
        if (!speak(u"en"_s))
            QSKIP("the app's translations are not built here");
        QCOMPARE(brulion::shell::toolSummary(stepsOf(steps)), english);
        // ChatTurn.tools' rows: the same.
        QVariantList rows;
        for (const ToolStep &step : stepsOf(steps))
            rows.append(QVariantMap{{u"title"_s, u"Krok"_s}, {u"kind"_s, step.kind}, {u"status"_s, step.status}});
        QCOMPARE(brulion::shell::toolSummary(rows), english);
        QVERIFY(speak(u"pl"_s));
        QCOMPARE(brulion::shell::toolSummary(stepsOf(steps)), polish);
    }

    void answer()
    {
        // A turn of tool calls without words: the summary, then the calls' titles.
        if (!speak(u"en"_s))
            QSKIP("the app's translations are not built here");
        const QList<ToolStep> steps = stepsOf(times(1, u"read"_s) + times(1, u"edit"_s));
        QCOMPARE(brulion::shell::toolsAnswer(steps, {u"Read Dom.md"_s, u" Edit Dom.md "_s}),
                 u"Read 1 file · edited 1 file\n\n- Read Dom.md\n- Edit Dom.md"_s);
        QCOMPARE(brulion::shell::toolsAnswer({}, {}), u"Done."_s);
        // Titles only (an agent on another computer keeps no kinds): other steps.
        QCOMPARE(brulion::shell::toolsAnswer(stepsOf(times(2, QString())), {u"Read Zakupy.md"_s, u""_s}),
                 u"Took 2 steps\n\n- Read Zakupy.md"_s);
        QVERIFY(speak(u"pl"_s));
        QCOMPARE(brulion::shell::toolsAnswer(steps, {u"Read Dom.md"_s, u"Edit Dom.md"_s}),
                 u"Przeczytano 1 plik · zmieniono 1 plik\n\n- Read Dom.md\n- Edit Dom.md"_s);
        QCOMPARE(brulion::shell::toolsAnswer({}, {}), u"Gotowe."_s);
        QCOMPARE(brulion::shell::toolsAnswer({}, {u"Zapisz „Dom”"_s}), u"Zrobione:\n\n- Zapisz „Dom”"_s);
    }

    void cleanupTestCase() { QCoreApplication::removeTranslator(&m_translator); }
};

QTEST_GUILESS_MAIN(TestChatTools)
#include "tst_chattools.moc"
