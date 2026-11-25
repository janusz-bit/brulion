// Spell checking in the editor (src/app/editor/spellcheck.h, markdownhighlighter.h) over word-list
// backends (spell::WordListBackend): verdicts that come from the worker thread, languages from the
// setting and the UI language, the personal dictionary and ignored words, suggestions, a slow
// backend that never blocks the UI thread; the highlighter's underlines on prose only, the word at
// the caret, misspelledAt(), source and code modes — source mode's exclusions and a large file
// checked in slices; the rendered blocks (wholePage): wrongWords() and its cache, the underlines
// put on InlineRenderer's HTML, the word under a click, a correction and its undo.
#include "fakeeditorhost.h"
#include "markdown/inlinerenderer.h"
#include "markdownhighlighter.h"
#include "pagecontroller.h"
#include "spellcheck.h"
#include "spellmarks.h"

#include "testtiming.h"

#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QThread>

#include <atomic>

using namespace Qt::StringLiterals;
using namespace brulion;
using brulion::editor::MarkdownHighlighter;
using brulion::editor::SpellCheck;
using namespace brulion::editor;

namespace {

constexpr int NameProperty = QTextFormat::UserProperty + 1;

const QStringList Polish{u"kot"_s, u"pies"_s, u"zażółć"_s, u"gęślą"_s, u"jaźń"_s, u"ma"_s, u"np."_s, u"ala"_s};
const QStringList English{u"cat"_s, u"dog"_s, u"meeting"_s, u"the"_s};

SpellCheck::BackendFactory wordLists(std::atomic<int> *made = nullptr, std::atomic<bool> *offMainThread = nullptr)
{
    QThread *main = QThread::currentThread();
    return [made, offMainThread, main](const QString &language) -> std::unique_ptr<spell::SpellBackend> {
        if (made)
            ++*made;
        if (offMainThread)
            *offMainThread = QThread::currentThread() != main;
        if (language == u"pl")
            return std::make_unique<spell::WordListBackend>(u"pl"_s, Polish);
        if (language == u"en")
            return std::make_unique<spell::WordListBackend>(u"en"_s, English);
        return nullptr;
    };
}

/// A backend that takes its time over every request (Android's checker over IPC, Hunspell's
/// suggestions for a long word).
class SlowBackend final : public spell::SpellBackend {
public:
    QString language() const override { return u"pl"_s; }
    bool check(const QString &word) override
    {
        QThread::msleep(50);
        return word == u"kot";
    }
    QStringList suggest(const QString &, int) override
    {
        QThread::msleep(300);
        return {u"kot"_s};
    }
    QList<bool> checkAll(const QStringList &words) override
    {
        QThread::msleep(300);
        QList<bool> out;
        for (const QString &w : words)
            out.append(w == u"kot");
        return out;
    }
};

std::unique_ptr<SpellCheck> makeSpell(const QString &setting = u"auto"_s)
{
    auto spell = std::make_unique<SpellCheck>(QStringList{u"pl_PL"_s, u"en_US"_s}, wordLists(), u"test"_s);
    spell->setSystemLanguage(u"pl_PL"_s);
    spell->setUiLanguage(u"pl"_s);
    spell->setLanguageSetting(setting);
    return spell;
}

SpellCheck::Verdict settled(SpellCheck &spell, const QString &word)
{
    if (spell.verdict(word) == SpellCheck::Verdict::Pending)
        spell.waitUntilIdle();
    return spell.verdict(word);
}

/// Format names over [from, to) of block `blockNumber`, one per character.
QStringList namesAt(const QTextDocument &doc, int blockNumber, int from, int to)
{
    const QTextBlock block = doc.findBlockByNumber(blockNumber);
    QStringList out;
    for (int pos = from; pos < to; ++pos) {
        QString name;
        for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
            if (pos >= r.start && pos < r.start + r.length)
                name = r.format.property(NameProperty).toString();
        }
        out.append(name);
    }
    return out;
}

/// The words of block `blockNumber` that carry the spelling underline.
QStringList underlined(const QTextDocument &doc, int blockNumber = 0)
{
    const QTextBlock block = doc.findBlockByNumber(blockNumber);
    QStringList out;
    for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
        if (r.format.property(NameProperty).toString() == u"spelling")
            out.append(block.text().mid(r.start, r.length));
    }
    return out;
}

/// Let the checker answer and the highlighter apply the answers.
void settle(SpellCheck &spell)
{
    QVERIFY(spell.waitUntilIdle());
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
}

/// Every underlined word of the document, in order.
QStringList allUnderlined(const QTextDocument &doc)
{
    QStringList out;
    for (int n = 0; n < doc.blockCount(); ++n)
        out += underlined(doc, n);
    return out;
}

/// Let the checker answer and the highlighter finish every slice; the longest turn of the event
/// loop (the GUI thread's CPU time) on the way, through `longestMs`.
bool settleSlices(SpellCheck &spell, const MarkdownHighlighter &h, qint64 *longestMs = nullptr)
{
    QElapsedTimer timer;
    timer.start();
    test::CpuTimer turn;
    while (!(spell.idle() && h.spellingSettled())) {
        if (timer.elapsed() > test::kRoomyMs)
            return false;
        turn.start();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (longestMs)
            *longestMs = std::max(*longestMs, turn.elapsed());
        QThread::usleep(200);
    }
    return true;
}

/// The words a span of `html` underlines (spellmarks.h's style) shows, read back by Qt's rich text.
QStringList markedInHtml(const QString &html)
{
    QTextDocument doc;
    doc.setHtml(html);
    QStringList out;
    QString current;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (f.charFormat().fontUnderline() && f.charFormat().underlineColor() == QColor(u"#c0392b"_s)) {
                current += f.text();
            } else if (!current.isEmpty()) {
                out.append(std::exchange(current, {}));
            }
        }
        if (!current.isEmpty())
            out.append(std::exchange(current, {}));
    }
    return out;
}

} // namespace

class TestEditorSpellCheck : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void verdicts()
    {
        auto spell = makeSpell();
        QVERIFY(spell->available());
        QCOMPARE(spell->availableLanguages(), (QStringList{u"pl"_s, u"en"_s}));
        QCOMPARE(spell->languages(), (QStringList{u"pl"_s, u"en"_s}));
        QVERIFY(spell->active());
        QSignalSpy checked(spell.get(), &SpellCheck::checked);
        QCOMPARE(spell->verdict(u"zażółć"_s), SpellCheck::Verdict::Pending);
        QCOMPARE(spell->verdict(u"zażułć"_s), SpellCheck::Verdict::Pending);
        QCOMPARE(spell->verdict(u"meeting"_s), SpellCheck::Verdict::Pending);
        // Asked once, however often it is looked at.
        QCOMPARE(spell->verdict(u"zażółć"_s), SpellCheck::Verdict::Pending);
        QCOMPARE(spell->pendingCount(), 3);
        QVERIFY(spell->waitUntilIdle());
        QVERIFY(checked.count() >= 1);
        QCOMPARE(spell->verdict(u"zażółć"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(spell->verdict(u"zażułć"_s), SpellCheck::Verdict::Wrong);
        QCOMPARE(spell->verdict(u"meeting"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(spell->verdict(u"np."_s), SpellCheck::Verdict::Pending);
        QCOMPARE(settled(*spell, u"np."_s), SpellCheck::Verdict::Correct);
        QCOMPARE(spell->verdict(QString()), SpellCheck::Verdict::Correct);
    }

    void backendsMadeOnTheWorker()
    {
        std::atomic<int> made = 0;
        std::atomic<bool> offMain = false;
        SpellCheck spell({u"pl"_s}, wordLists(&made, &offMain));
        spell.setUiLanguage(u"pl"_s);
        // Made once, on the worker, for the first word asked about.
        QCOMPARE(settled(spell, u"kot"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(made.load(), 1);
        QVERIFY(offMain.load());
    }

    void workerOnlyWhenAWordIsAsked()
    {
        // Settings alone make no worker and read no dictionary: a notebook nobody types in, spell
        // checking of the edited block only, never pays for them.
        std::atomic<int> made = 0;
        SpellCheck spell({u"pl"_s, u"en"_s}, wordLists(&made));
        spell.setSystemLanguage(u"pl_PL"_s);
        spell.setUiLanguage(u"pl"_s);
        spell.setLanguageSetting(u"pl,en"_s);
        spell.setPersonalWords({u"brulion"_s});
        spell.setEnabled(false);
        spell.setEnabled(true);
        QCoreApplication::processEvents();
        QVERIFY(!spell.loaded());
        QCOMPARE(made.load(), 0);
        // The first word brings it, with the settings of now.
        QCOMPARE(settled(spell, u"brulion"_s), SpellCheck::Verdict::Correct);
        QVERIFY(spell.loaded());
        QCOMPARE(made.load(), 2);
        QCOMPARE(settled(spell, u"meeting"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(settled(spell, u"zażułć"_s), SpellCheck::Verdict::Wrong);
        QCOMPARE(made.load(), 2);
    }

    void dictionariesGoWhenIdle()
    {
        std::atomic<int> made = 0;
        SpellCheck spell({u"pl"_s}, wordLists(&made));
        QCOMPARE(spell.idleMs(), SpellCheck::DefaultIdleMs);
        spell.setIdleMs(150 * BRULION_TEST_SLOWDOWN);
        spell.setUiLanguage(u"pl"_s);
        spell.setPersonalWords({u"brulion"_s});
        spell.ignore(u"qwerty"_s);
        QSignalSpy unloaded(&spell, &SpellCheck::unloaded);
        QSignalSpy invalidated(&spell, &SpellCheck::invalidated);
        QCOMPARE(settled(spell, u"kot"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(settled(spell, u"zażułć"_s), SpellCheck::Verdict::Wrong);
        QVERIFY(spell.loaded());
        const int revision = spell.revision();
        // Nothing asked for a while: the worker and its dictionaries go.
        QVERIFY(unloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QVERIFY(!spell.loaded());
        // What is known stays known — nothing is underlined anew, nothing asked again.
        QCOMPARE(spell.knownVerdict(u"kot"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(spell.knownVerdict(u"zażułć"_s), SpellCheck::Verdict::Wrong);
        QCOMPARE(spell.verdict(u"kot"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(spell.pendingCount(), 0);
        QCOMPARE(spell.revision(), revision);
        QCOMPARE(invalidated.size(), 0);
        QVERIFY(!spell.loaded());
        QCOMPARE(made.load(), 1);
        // A word not known yet brings the worker back, with the personal and the ignored words.
        QCOMPARE(spell.verdict(u"pies"_s), SpellCheck::Verdict::Pending);
        QCOMPARE(settled(spell, u"pies"_s), SpellCheck::Verdict::Correct);
        QVERIFY(spell.loaded());
        QCOMPARE(made.load(), 2);
        QCOMPARE(settled(spell, u"brulion"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(settled(spell, u"qwerty"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(settled(spell, u"gęślom"_s), SpellCheck::Verdict::Wrong);
        // So do suggestions.
        QVERIFY(unloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QSignalSpy ready(&spell, &SpellCheck::suggestionsReady);
        spell.requestSuggestions(u"kott"_s);
        QVERIFY(spell.loaded());
        QVERIFY(ready.wait(5000 * BRULION_TEST_SLOWDOWN));
        QCOMPARE(made.load(), 3);
    }

    void typingKeepsTheDictionaries()
    {
        std::atomic<int> made = 0;
        SpellCheck spell({u"pl"_s}, wordLists(&made));
        spell.setIdleMs(200 * BRULION_TEST_SLOWDOWN);
        spell.setUiLanguage(u"pl"_s);
        QSignalSpy unloaded(&spell, &SpellCheck::unloaded);
        // A new word every now and then, sooner than the idle time: never unloaded in between.
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(settled(spell, u"słowo"_s + QString::number(i)), SpellCheck::Verdict::Wrong);
            QTest::qWait(60 * BRULION_TEST_SLOWDOWN);
        }
        QCOMPARE(unloaded.size(), 0);
        QCOMPARE(made.load(), 1);
        // Never: idleMs 0.
        spell.setIdleMs(0);
        QTest::qWait(400 * BRULION_TEST_SLOWDOWN);
        QVERIFY(spell.loaded());
        QCOMPARE(unloaded.size(), 0);
    }

    void renderedTextLetsThemGoSooner()
    {
        // A page that was opened asks about its words once, and is read then: the dictionaries go
        // sooner than after a typed word.
        QVERIFY(SpellCheck::DefaultReadIdleMs < SpellCheck::DefaultIdleMs);
        std::atomic<int> made = 0;
        SpellCheck spell({u"pl"_s}, wordLists(&made));
        QCOMPARE(spell.readIdleMs(), SpellCheck::DefaultReadIdleMs);
        spell.setIdleMs(60 * 1000);
        spell.setReadIdleMs(150 * BRULION_TEST_SLOWDOWN);
        spell.setUiLanguage(u"pl"_s);
        QSignalSpy unloaded(&spell, &SpellCheck::unloaded);
        bool pending = false;
        QVERIFY(spell.wrongWords(u"kot zażułć"_s, &pending).isEmpty());
        QVERIFY(pending);
        QVERIFY(spell.waitUntilIdle());
        QVERIFY(spell.loaded());
        QCOMPARE(spell.wrongWords(u"kot zażułć"_s).size(), 1);
        QVERIFY(unloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QVERIFY(!spell.loaded());
        QCOMPARE(spell.wrongWords(u"kot zażułć"_s).size(), 1); // (known: nothing asked again)
        QVERIFY(!spell.loaded());
        // A typed word keeps them for the long time, whatever is read meanwhile.
        QCOMPARE(settled(spell, u"pies"_s), SpellCheck::Verdict::Correct);
        QVERIFY(spell.loaded());
        (void)spell.wrongWords(u"gęślom jaźń"_s, &pending);
        QVERIFY(pending);
        QVERIFY(spell.waitUntilIdle());
        QTest::qWait(600 * BRULION_TEST_SLOWDOWN);
        QVERIFY(spell.loaded());
        QCOMPARE(unloaded.size(), 1);
        QCOMPARE(made.load(), 2);
        // readIdleMs 0: rendered text waits as long as typing.
        QVERIFY(spell.unload());
        spell.setReadIdleMs(0);
        (void)spell.wrongWords(u"ala ma kota"_s, &pending);
        QVERIFY(pending);
        QVERIFY(spell.waitUntilIdle());
        QTest::qWait(600 * BRULION_TEST_SLOWDOWN);
        QVERIFY(spell.loaded());
        // Never longer than the time after typing.
        spell.setReadIdleMs(120 * 1000);
        spell.setIdleMs(150 * BRULION_TEST_SLOWDOWN);
        QVERIFY(unloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        (void)spell.wrongWords(u"pies ma kotta"_s, &pending);
        QVERIFY(pending);
        QVERIFY(unloaded.wait(5000 * BRULION_TEST_SLOWDOWN));
        QVERIFY(!spell.loaded());
    }

    void unloadOnRequest()
    {
        // The app is away (main.cpp: MemoryPolicy's letGo()): now, unless words are on their way.
        QSemaphore gate;
        SpellCheck spell({u"pl"_s}, [&gate](const QString &) -> std::unique_ptr<spell::SpellBackend> {
            (void)gate.tryAcquire(1, brulion::test::kRoomyMs);
            return std::make_unique<spell::WordListBackend>(u"pl"_s, Polish);
        });
        const auto open = qScopeGuard([&gate] { gate.release(2); }); // also when a check fails
        spell.setIdleMs(0);
        spell.setUiLanguage(u"pl"_s);
        QSignalSpy unloaded(&spell, &SpellCheck::unloaded);
        QVERIFY(!spell.unload()); // (nothing loaded: nothing to do)
        QCOMPARE(spell.verdict(u"kot"_s), SpellCheck::Verdict::Pending);
        QVERIFY(!spell.unload()); // queued
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        QVERIFY(spell.loaded());
        QVERIFY(!spell.unload()); // on its way: the worker is still reading its dictionary
        QCOMPARE(unloaded.size(), 0);
        gate.release();
        QVERIFY(spell.waitUntilIdle());
        QVERIFY(spell.unload());
        QVERIFY(!spell.loaded());
        QCOMPARE(unloaded.size(), 1);
        QCOMPARE(spell.verdict(u"kot"_s), SpellCheck::Verdict::Correct);
        gate.release();
        QCOMPARE(settled(spell, u"pies"_s), SpellCheck::Verdict::Correct);
    }

    void manyWordsInBatches()
    {
        auto spell = makeSpell();
        QSignalSpy checked(spell.get(), &SpellCheck::checked);
        for (int i = 0; i < 3 * SpellCheck::BatchSize + 7; ++i)
            QCOMPARE(spell->verdict(u"słowo%1"_s.arg(QChar(u'a' + i % 26)) + QString::number(i)),
                     SpellCheck::Verdict::Pending);
        QVERIFY(spell->waitUntilIdle());
        QCOMPARE(checked.count(), 4);
    }

    void inactive()
    {
        auto spell = makeSpell();
        QSignalSpy active(spell.get(), &SpellCheck::activeChanged);
        QSignalSpy invalidated(spell.get(), &SpellCheck::invalidated);
        spell->setEnabled(false);
        QVERIFY(!spell->active());
        QCOMPARE(active.count(), 1);
        QCOMPARE(invalidated.count(), 1);
        QCOMPARE(spell->verdict(u"zażułć"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(spell->pendingCount(), 0);
        spell->setEnabled(true);
        QCOMPARE(spell->verdict(u"zażułć"_s), SpellCheck::Verdict::Pending);

        // No languages on this device: not available, never active.
        SpellCheck none({}, wordLists());
        QVERIFY(!none.available());
        QVERIFY(!none.active());
        QCOMPARE(none.verdict(u"zażułć"_s), SpellCheck::Verdict::Correct);
        QSignalSpy suggestions(&none, &SpellCheck::suggestionsReady);
        none.requestSuggestions(u"zażułć"_s);
        QCOMPARE(suggestions.count(), 1);
        QCOMPARE(suggestions.at(0).at(1).toStringList(), QStringList{});
    }

    void languages_data()
    {
        QTest::addColumn<QString>("setting");
        QTest::addColumn<QString>("ui");
        QTest::addColumn<QString>("system");
        QTest::addColumn<QStringList>("languages");
        QTest::newRow("auto, polish") << u"auto"_s << u"pl"_s << u"pl_PL"_s << QStringList{u"pl"_s, u"en"_s};
        QTest::newRow("auto, english") << u"auto"_s << u"en"_s << u"en_GB"_s << QStringList{u"en"_s};
        QTest::newRow("auto, english ui on a polish system")
                << u"auto"_s << u"en"_s << u"pl_PL"_s << QStringList{u"en"_s, u"pl"_s};
        QTest::newRow("polish") << u"pl"_s << u"en"_s << u"en_US"_s << QStringList{u"pl"_s};
        QTest::newRow("english") << u"en"_s << u"pl"_s << u"pl_PL"_s << QStringList{u"en"_s};
        QTest::newRow("both") << u"pl,en"_s << u"en"_s << u"en_US"_s << QStringList{u"pl"_s, u"en"_s};
        QTest::newRow("unknown") << u"de"_s << u"pl"_s << u"de_DE"_s << QStringList{};
    }

    void languages()
    {
        QFETCH(QString, setting);
        QFETCH(QString, ui);
        QFETCH(QString, system);
        QFETCH(QStringList, languages);
        SpellCheck spell({u"pl"_s, u"en"_s}, wordLists());
        spell.setSystemLanguage(system);
        spell.setUiLanguage(ui);
        spell.setLanguageSetting(setting);
        QCOMPARE(spell.languages(), languages);
        QCOMPARE(spell.active(), !languages.isEmpty());
    }

    void languageChangeForgets()
    {
        auto spell = makeSpell();
        QCOMPARE(settled(*spell, u"kot"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(settled(*spell, u"cat"_s), SpellCheck::Verdict::Correct);
        QSignalSpy invalidated(spell.get(), &SpellCheck::invalidated);
        spell->setLanguageSetting(u"en"_s);
        QCOMPARE(invalidated.count(), 1);
        QCOMPARE(spell->verdict(u"kot"_s), SpellCheck::Verdict::Pending);
        QCOMPARE(settled(*spell, u"kot"_s), SpellCheck::Verdict::Wrong);
        QCOMPARE(settled(*spell, u"cat"_s), SpellCheck::Verdict::Correct);
        // The UI language only matters to "auto".
        spell->setUiLanguage(u"en"_s);
        QCOMPARE(spell->languages(), QStringList{u"en"_s});
    }

    void personalDictionary()
    {
        auto spell = makeSpell();
        QCOMPARE(settled(*spell, u"Brulion"_s), SpellCheck::Verdict::Wrong);
        QSignalSpy edited(spell.get(), &SpellCheck::personalWordsEdited);
        QSignalSpy invalidated(spell.get(), &SpellCheck::invalidated);
        // "Brulion." from the menu (the word with its dot) is kept as "Brulion".
        spell->addToDictionary(u"Brulion."_s);
        QCOMPARE(spell->personalWords(), QStringList{u"Brulion"_s});
        QCOMPARE(edited.count(), 1);
        // Adding forgets nothing: the word stays marked until the answer (no flicker).
        QCOMPARE(invalidated.count(), 0);
        QCOMPARE(spell->verdict(u"Brulion"_s), SpellCheck::Verdict::Wrong);
        QVERIFY(spell->waitUntilIdle());
        QCOMPARE(spell->verdict(u"Brulion"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(settled(*spell, u"BRULION"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(settled(*spell, u"brulion"_s), SpellCheck::Verdict::Wrong);

        // Twice, empty: nothing.
        spell->addToDictionary(u"Brulion"_s);
        spell->addToDictionary(u"  "_s);
        QCOMPARE(edited.count(), 1);

        // Sorted, without duplicates, typographic apostrophes as ASCII.
        spell->addToDictionary(u"żuk"_s);
        spell->addToDictionary(u"O’Neill"_s);
        QCOMPARE(spell->personalWords(), (QStringList{u"Brulion"_s, u"O'Neill"_s, u"żuk"_s}));

        // Taking a word out asks about everything again.
        spell->removeFromDictionary(u"Brulion"_s);
        QCOMPARE(invalidated.count(), 1);
        QCOMPARE(edited.count(), 4);
        QCOMPARE(settled(*spell, u"Brulion"_s), SpellCheck::Verdict::Wrong);
        spell->removeFromDictionary(u"nie ma"_s);
        QCOMPARE(edited.count(), 4);

        // Set from the settings (main.cpp): no personalWordsEdited.
        spell->setPersonalWords({u"zz"_s, u"aa"_s, u"aa"_s, QString()});
        QCOMPARE(spell->personalWords(), (QStringList{u"aa"_s, u"zz"_s}));
        QCOMPARE(edited.count(), 4);
    }

    void ignore()
    {
        auto spell = makeSpell();
        QCOMPARE(settled(*spell, u"blabla"_s), SpellCheck::Verdict::Wrong);
        QSignalSpy ignored(spell.get(), &SpellCheck::ignoredWordsChanged);
        spell->ignore(u"blabla"_s);
        QCOMPARE(ignored.count(), 1);
        QCOMPARE(spell->ignoredWords(), QStringList{u"blabla"_s});
        QCOMPARE(spell->verdict(u"blabla"_s), SpellCheck::Verdict::Wrong);
        QVERIFY(spell->waitUntilIdle());
        QCOMPARE(spell->verdict(u"blabla"_s), SpellCheck::Verdict::Correct);
        // Not stored: the personal dictionary stays as it was.
        QCOMPARE(spell->personalWords(), QStringList{});
    }

    void suggestions()
    {
        auto spell = makeSpell();
        QSignalSpy ready(spell.get(), &SpellCheck::suggestionsReady);
        spell->requestSuggestions(u"kat"_s);
        QVERIFY(ready.wait());
        QCOMPARE(ready.at(0).at(0).toString(), u"kat"_s);
        // Polish first, then English, in turn.
        QCOMPARE(ready.at(0).at(1).toStringList(), (QStringList{u"kot"_s, u"cat"_s, u"ma"_s}));
        QCOMPARE(spell->suggestionsFor(u"kat"_s), (QStringList{u"kot"_s, u"cat"_s, u"ma"_s}));
        spell->requestSuggestions(u"qqqqqqqq"_s);
        QVERIFY(ready.wait());
        QCOMPARE(ready.at(1).at(1).toStringList(), QStringList{});
    }

    void slowBackendNeverBlocks()
    {
        // Loading a big dictionary takes until the test opens the gate (not a fixed time a slow
        // runner could outlast): a UI thread that waited for the load would find it done.
        QSemaphore gate;
        std::atomic<bool> loaded{false};
        SpellCheck spell({u"pl"_s}, [&gate, &loaded](const QString &) -> std::unique_ptr<spell::SpellBackend> {
            (void)gate.tryAcquire(1, brulion::test::kRoomyMs);
            loaded = true;
            return std::make_unique<SlowBackend>();
        });
        const auto open = qScopeGuard([&gate] { gate.release(); }); // also when a check fails
        spell.setUiLanguage(u"pl"_s);
        int pending = 0;
        for (int i = 0; i < 2000; ++i)
            pending += spell.verdict(u"słowo"_s + QString::number(i)) == SpellCheck::Verdict::Pending ? 1 : 0;
        pending += spell.verdict(u"kot"_s) == SpellCheck::Verdict::Pending ? 1 : 0;
        spell.requestSuggestions(u"kott"_s);
        // The UI thread goes on: events are processed while the worker loads and checks.
        for (int i = 0; i < 5; ++i)
            QCoreApplication::processEvents();
        QVERIFY(!loaded);
        QCOMPARE(pending, 2001); // asked, not waited for
        gate.release();
        QVERIFY(spell.waitUntilIdle(30000));
        QCOMPARE(spell.verdict(u"kot"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(spell.verdict(u"słowo7"_s), SpellCheck::Verdict::Wrong);
    }

    void dictionaryForm()
    {
        auto spell = makeSpell();
        QCOMPARE(spell->dictionaryForm(u" np. "_s), u"np"_s);
        QCOMPARE(spell->dictionaryForm(u"don’t"_s), u"don't"_s);
        QCOMPARE(spell->dictionaryForm(u"żuk"_s), u"żuk"_s);
    }

    // ---- MarkdownHighlighter ---------------------------------------------------------------

    void underlinesProseOnly()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(
                u"Ala ma kott `kott` [[kott]] #kott 📅 2026-10-12 https://kott.pl/kott [kott](kott.md) ^kott"_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        QCOMPARE(underlined(doc), QStringList{}); // pending
        settle(*spell);
        // The Markdown link's text is prose; its target, code, the wiki link, the tag, the date,
        // the URL and the block id are not.
        QCOMPARE(underlined(doc), (QStringList{u"kott"_s, u"kott"_s}));
        const QString text = doc.begin().text();
        QCOMPARE(namesAt(doc, 0, int(text.indexOf(u"kott"_s)), int(text.indexOf(u"kott"_s)) + 4),
                 QStringList(4, u"spelling"_s));
        // The underline's colour; plain text gets the palette's text colour with it (Qt Quick
        // draws a format only when it has a colour).
        h.setSpellColor(QColor(u"#123456"_s));
        h.setPalette({{u"text"_s, QColor(u"#010203"_s)}});
        const QTextBlock block = doc.begin();
        bool found = false;
        for (const QTextLayout::FormatRange &r : block.layout()->formats()) {
            if (r.format.property(NameProperty).toString() == u"spelling") {
                QVERIFY(r.format.fontUnderline());
                QCOMPARE(r.format.underlineColor(), QColor(u"#123456"_s));
                // (The link's text keeps the link's colour.)
                if (r.start == text.indexOf(u"kott"_s))
                    QCOMPARE(r.format.foreground().color(), QColor(u"#010203"_s));
                found = true;
            }
        }
        QVERIFY(found);
    }

    void polishDiacritics()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(u"Zażółć gęślą jaźń, zażułć gęśla!"_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        settle(*spell);
        QCOMPARE(underlined(doc), (QStringList{u"zażułć"_s, u"gęśla"_s}));
    }

    void onlyPendingLinesAgain()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(u"kot pies\nkott\n```\nkott w kodzie\n```"_s);
        MarkdownHighlighter h;
        h.setMode(u"source"_s);
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        settle(*spell);
        QCOMPARE(underlined(doc, 0), QStringList{});
        QCOMPARE(underlined(doc, 1), QStringList{u"kott"_s});
        // Fenced code is no prose.
        QCOMPARE(underlined(doc, 3), QStringList{});
        // A word typed later: only its line waits.
        QTextCursor cursor(doc.findBlockByNumber(0));
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertText(u" piess"_s);
        QCOMPARE(underlined(doc, 0), QStringList{});
        settle(*spell);
        QCOMPARE(underlined(doc, 0), QStringList{u"piess"_s});
        QCOMPARE(underlined(doc, 1), QStringList{u"kott"_s});
    }

    void frontmatterIsNoProse()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(u"---\ntytul: kott\n---\nkott"_s);
        MarkdownHighlighter h;
        h.setMode(u"source"_s);
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        settle(*spell);
        QCOMPARE(underlined(doc, 1), QStringList{});
        QCOMPARE(underlined(doc, 3), QStringList{u"kott"_s});
    }

    void codeModeChecksNothing()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(u"kott = 3 # kott"_s);
        MarkdownHighlighter h;
        h.setMode(u"code"_s);
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        settle(*spell);
        QCOMPARE(underlined(doc), QStringList{});
        QCOMPARE(spell->pendingCount(), 0);
    }

    void caretWord()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(u"kott pies kott"_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        h.setSpellCaret(14); // typing the last word
        h.setSpellCheck(spell.get());
        settle(*spell);
        QCOMPARE(underlined(doc), QStringList{u"kott"_s});
        QCOMPARE(namesAt(doc, 0, 0, 1), QStringList{u"spelling"_s});
        // The caret moves on: the word is marked.
        h.setSpellCaret(4);
        QCOMPARE(namesAt(doc, 0, 10, 11), QStringList{u"spelling"_s});
        QCOMPARE(namesAt(doc, 0, 0, 1), QStringList{QString()});
        h.setSpellCaret(-1);
        QCOMPARE(underlined(doc), (QStringList{u"kott"_s, u"kott"_s}));
    }

    void misspelledAt()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(u"pies\nAla kott np. `kott`"_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        settle(*spell);
        const int line = doc.findBlockByNumber(1).position();
        const QVariantMap at = h.misspelledAt(line + 5);
        QCOMPARE(at.value(u"from"_s).toInt(), line + 4);
        QCOMPARE(at.value(u"to"_s).toInt(), line + 8);
        QCOMPARE(at.value(u"word"_s).toString(), u"kott"_s);
        QCOMPARE(at.value(u"text"_s).toString(), u"kott"_s);
        // Right after the word too (the caret of a right-click there).
        QCOMPARE(h.misspelledAt(line + 8).value(u"from"_s).toInt(), line + 4);
        QVERIFY(h.misspelledAt(line + 1).isEmpty());  // right
        QVERIFY(h.misspelledAt(line + 11).isEmpty()); // np. with its dot
        QVERIFY(h.misspelledAt(line + 16).isEmpty()); // code
        QVERIFY(h.misspelledAt(-1).isEmpty());
        QVERIFY(h.misspelledAt(9999).isEmpty());
        // Without spell checking: nothing.
        h.setSpellCheck(nullptr);
        QVERIFY(h.misspelledAt(line + 5).isEmpty());
        QCOMPARE(underlined(doc, 1), QStringList{});
    }

    void personalWordTakesTheUnderlineAway()
    {
        auto spell = makeSpell();
        QTextDocument doc;
        doc.setPlainText(u"Brulion i kott"_s);
        MarkdownHighlighter h;
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        settle(*spell);
        QCOMPARE(underlined(doc), (QStringList{u"Brulion"_s, u"kott"_s}));
        spell->addToDictionary(u"Brulion"_s);
        // Still marked while it is asked again (no flicker), then gone; "kott" stays marked.
        QCOMPARE(underlined(doc), (QStringList{u"Brulion"_s, u"kott"_s}));
        settle(*spell);
        QCOMPARE(underlined(doc), QStringList{u"kott"_s});
        // Spell checking switched off: every underline goes.
        spell->setEnabled(false);
        QCOMPARE(underlined(doc), QStringList{});
        spell->setEnabled(true);
        settle(*spell);
        QCOMPARE(underlined(doc), QStringList{u"kott"_s});
    }

    // ---- Source mode ----------------------------------------------------------------------------

    void sourceModeExclusions_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("marked");
        const auto row = [](const char *name, const QString &text, const QStringList &marked) {
            QTest::newRow(name) << text << marked;
        };
        row("prose", u"Zażółć gęśla jaźń, zażułć!"_s, {u"gęśla"_s, u"zażułć"_s});
        row("code span", u"Zażółć `zażułć` gęśla"_s, {u"gęśla"_s});
        row("fenced code", u"```\nzażułć gęśla\n```\njaźni"_s, {u"jaźni"_s});
        row("tilde fence, language", u"~~~python\nzażułć\n~~~\nkot"_s, {});
        row("link target", u"[jaźń](zażułć.md) [gęśla](https://x.pl/kott)"_s, {u"gęśla"_s});
        row("wiki link and embed", u"[[zażułć]] ![[gęśla.png]] jaźni"_s, {u"jaźni"_s});
        row("url and e-mail", u"https://zażułć.pl/kott zażułć@poczta.pl gęśla"_s, {u"gęśla"_s});
        row("tags", u"#zażułć #gęśla/kott pies"_s, {});
        row("inline math", u"$zażułć$ i $$gęśla$$ jaźni"_s, {u"jaźni"_s});
        row("display math", u"$$\n\\text{zażułć}\n$$\ngęśla"_s, {u"gęśla"_s});
        row("display math in a list", u"- $$\n  zażułć\n  $$\n- gęśla"_s, {u"gęśla"_s});
        row("frontmatter", u"---\ntytuł: zażułć\ntags: [gęśla]\n---\nkott"_s, {u"kott"_s});
        row("property name", u"statuss:: gęśla"_s, {u"gęśla"_s});
        row("comment over lines", u"kott %%zażułć\ngęśla%% jaźni"_s, {u"kott"_s, u"jaźni"_s});
        row("comment in a line", u"%%zażułć%% gęśla <!--jaźni-->"_s, {u"gęśla"_s});
        row("task and date", u"- [ ] zażułć 📅 2026-11-23 #praca"_s, {u"zażułć"_s});
        row("heading and quote", u"# Gęśla\n> zażułć"_s, {u"Gęśla"_s, u"zażułć"_s});
        row("unknown fence stays code", u"````\n```\nzażułć\n````\ngęśla"_s, {u"gęśla"_s});
    }
    void sourceModeExclusions()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, marked);
        auto spell = makeSpell(u"pl"_s);
        QTextDocument doc;
        MarkdownHighlighter h;
        h.setMode(u"source"_s);
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        doc.setPlainText(text);
        QVERIFY(settleSlices(*spell, h));
        QCOMPARE(allUnderlined(doc), marked);
        // misspelledAt() finds exactly what is marked: nothing inside the syntax.
        int found = 0;
        for (int pos = 0; pos < doc.characterCount(); ++pos) {
            const QVariantMap at = h.misspelledAt(pos);
            if (!at.isEmpty() && at.value(u"from"_s).toInt() == pos)
                ++found;
        }
        QCOMPARE(found, marked.size());
    }

    void largeSourceInSlices()
    {
        auto spell = makeSpell(u"pl"_s);
        // ~80 KB (BRULION_SPELL_SOURCE_KB: another size; docs/PERFORMANCE.md measured 500): every
        // 50th line has a word of its own, every line a misspelled one.
        const int kb = qEnvironmentVariableIsSet("BRULION_SPELL_SOURCE_KB")
                               ? qEnvironmentVariableIntValue("BRULION_SPELL_SOURCE_KB")
                               : 80;
        QStringList lines;
        qsizetype size = 0;
        for (int i = 0; size < kb * 1000; ++i) {
            lines.append(u"- Ala ma kot i pies, zażółć gęślą jaźń — zażułć "_s
                         + (i % 50 == 0 ? u"słowo%1"_s.arg(QChar(u'a' + (i / 50) % 26)) : u"pies"_s));
            size += lines.last().size() + 1;
        }
        const QString text = lines.join(u'\n');
        QTextDocument doc;
        MarkdownHighlighter h;
        h.setMode(u"source"_s);
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        test::CpuTimer setting;
        setting.start();
        doc.setTextWidth(800);
        doc.setPlainText(text);
        (void)doc.size(); // laid out whole, as a TextEdit's document is
        const qint64 setMs = setting.elapsed();
        // The text is highlighted at once, its spelling comes after it in slices.
        QVERIFY(!h.spellingSettled());
        QCOMPARE(underlined(doc, 0), QStringList{});
        qint64 longest = 0;
        test::CpuTimer all;
        all.start();
        QVERIFY(settleSlices(*spell, h, &longest));
        qInfo("%d KB, %lld lines in source mode: text set %lld ms, spelling %lld ms of the GUI thread, longest turn "
              "%lld ms",
              kb, qlonglong(lines.size()), setMs, all.elapsed(), longest);
        // No turn of the event loop near a frame's worth of stall (a line or a few past the slice).
        QVERIFY2(longest < 100 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(longest)));
        const int last = doc.blockCount() - 1;
        QCOMPARE(underlined(doc, 0), (QStringList{u"zażułć"_s, u"słowoa"_s}));
        QCOMPARE(underlined(doc, last), QStringList{u"zażułć"_s});
        // Typing: only the edited line asks and is marked again.
        QTextCursor cursor(doc.findBlockByNumber(last / 2));
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertText(u" pieseł"_s);
        QVERIFY(spell->pendingCount() + int(!spell->idle()) <= 2);
        QVERIFY(settleSlices(*spell, h));
        QVERIFY(underlined(doc, last / 2).contains(u"pieseł"_s));
        // Switched off: the marks go (in slices); on again: back.
        spell->setEnabled(false);
        QVERIFY(settleSlices(*spell, h));
        QCOMPARE(underlined(doc, last), QStringList{});
        spell->setEnabled(true);
        QVERIFY(settleSlices(*spell, h));
        QCOMPARE(underlined(doc, last), QStringList{u"zażułć"_s});
    }

    void sourceViewOnly()
    {
        auto spell = makeSpell(u"pl"_s);
        // ~0.5 MB (BRULION_SPELL_SOURCE_KB: another size), a misspelled word on every line.
        const int kb = qEnvironmentVariableIsSet("BRULION_SPELL_SOURCE_KB")
                               ? qEnvironmentVariableIntValue("BRULION_SPELL_SOURCE_KB")
                               : 500;
        QStringList lines;
        qsizetype size = 0;
        for (int i = 0; size < kb * 1000; ++i) {
            lines.append(u"- Ala ma kot i pies, zażółć gęślą jaźń — zażułć pies %1"_s.arg(i));
            size += lines.last().size() + 1;
        }
        QTextDocument doc;
        doc.setTextWidth(800);
        MarkdownHighlighter h;
        h.setMode(u"source"_s);
        h.attach(&doc);
        h.setSpellCheck(spell.get());
        // The view: the first 30 lines (as SourceEditor gives it before the text is set).
        h.setSpellView(0, 0);
        test::CpuTimer cpu;
        cpu.start();
        doc.setPlainText(lines.join(u'\n'));
        (void)doc.size();
        const qint64 setMs = cpu.restart();
        const int end30 = doc.findBlockByNumber(30).position();
        h.setSpellView(0, end30);
        qint64 longest = 0;
        QVERIFY(settleSlices(*spell, h, &longest));
        qInfo("%d KB, %lld lines, the view at the top: text set %lld ms, spelling %lld ms of the GUI thread, "
              "longest turn %lld ms",
              kb, qlonglong(lines.size()), setMs, cpu.elapsed(), longest);
        QVERIFY2(longest < 100 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(longest)));
        const int margin = MarkdownHighlighter::SpellViewMargin;
        QCOMPARE(underlined(doc, 0), QStringList{u"zażułć"_s});
        QCOMPARE(underlined(doc, 30 + margin), QStringList{u"zażułć"_s});
        QCOMPARE(underlined(doc, 31 + margin), QStringList{}); // past the margin: not checked
        QCOMPARE(underlined(doc, doc.blockCount() - 1), QStringList{});
        QVERIFY(h.misspelledAt(doc.findBlockByNumber(doc.blockCount() - 1).position() + 50).isEmpty());
        // Scrolled to the end: its lines are checked.
        const int last = doc.blockCount() - 1;
        h.setSpellView(doc.findBlockByNumber(last - 30).position(), doc.characterCount() - 1);
        QVERIFY(settleSlices(*spell, h));
        QCOMPARE(underlined(doc, last), QStringList{u"zażułć"_s});
        QCOMPARE(underlined(doc, last - 30 - margin), QStringList{u"zażułć"_s});
        QCOMPARE(underlined(doc, last - 31 - margin), QStringList{});
        // (No view: every line — largeSourceInSlices.)
    }

    // ---- Rendered blocks ------------------------------------------------------------------------

    void wrongWordsCache()
    {
        auto spell = makeSpell(u"pl"_s);
        QSignalSpy revisions(spell.get(), &SpellCheck::revisionChanged);
        const QString text = u"Ala ma kotta\n`zażułć` i gęśla"_s;
        bool pending = false;
        QCOMPARE(spell->wrongWords(text, &pending), QList<spell::WordSpan>{});
        QVERIFY(pending);
        settle(*spell);
        QVERIFY(!revisions.isEmpty());
        const QList<spell::WordSpan> wrong = spell->wrongWords(text, &pending);
        QVERIFY(!pending);
        // (Code is no prose, single letters are not checked.)
        QCOMPARE(wrong.size(), 2);
        QCOMPARE(text.mid(wrong[0].from, wrong[0].length()), u"kotta"_s);
        QCOMPARE(text.mid(wrong[1].from, wrong[1].length()), u"gęśla"_s);
        // The same text again: the answer kept.
        QCOMPARE(spell->wrongWords(text), wrong);
        // A word added to the dictionary: still marked while it is asked again, then the cached
        // answer is stale.
        spell->addToDictionary(u"gęśla"_s);
        QCOMPARE(spell->wrongWords(text).size(), 2);
        settle(*spell);
        QCOMPARE(spell->wrongWords(text).size(), 1);
        // Another language: every verdict is asked again.
        spell->setLanguageSetting(u"en"_s);
        QCOMPARE(spell->wrongWords(text, &pending), QList<spell::WordSpan>{});
        QVERIFY(pending);
        settle(*spell);
        QCOMPARE(spell->wrongWords(text).size(), 3); // "Ala", "ma", "kotta": Polish in English
        // Off: nothing.
        spell->setEnabled(false);
        QCOMPARE(spell->wrongWords(text), QList<spell::WordSpan>{});
    }

    void marksOnRenderedHtml_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QStringList>("marked");
        const int paragraph = int(BlockKind::Paragraph);
        QTest::newRow("plain") << u"Ala ma kotta i psa"_s << paragraph << QStringList{u"kotta"_s};
        QTest::newRow("emphasis") << u"**Zażułć** i *gęśla*, ~~jaźni~~"_s << paragraph
                                  << QStringList{u"Zażułć"_s, u"gęśla"_s, u"jaźni"_s};
        QTest::newRow("escapes before") << u"A & B < \"kotta\""_s << paragraph << QStringList{u"kotta"_s};
        QTest::newRow("no-break space") << u"w kotta z psa"_s << paragraph << QStringList{u"kotta"_s};
        QTest::newRow("link text is a whole")
                << u"[kotta](https://x.pl) kotta"_s << paragraph << QStringList{u"kotta"_s};
        QTest::newRow("code and tag") << u"`kotta` #kotta [[kotta]]"_s << paragraph << QStringList{};
        QTest::newRow("lines") << u"pierwsza kotta\ndruga gęśla"_s << paragraph << QStringList{u"kotta"_s, u"gęśla"_s};
        QTest::newRow("heading") << u"## Nagłówek kotta"_s << int(BlockKind::Heading) << QStringList{u"kotta"_s};
        QTest::newRow("quote") << u"> cytat kotta"_s << int(BlockKind::Quote) << QStringList{u"kotta"_s};
        QTest::newRow("callout") << u"> [!note] Uwaga kotta\n> treść gęśla"_s << int(BlockKind::Callout)
                                 << QStringList{u"kotta"_s, u"gęśla"_s};
    }
    void marksOnRenderedHtml()
    {
        QFETCH(QString, body);
        QFETCH(int, kind);
        QFETCH(QStringList, marked);
        auto spell = makeSpell(u"pl"_s);
        spell->addToDictionary(u"pierwsza"_s);
        for (const QString &w : {u"druga"_s, u"nagłówek"_s, u"cytat"_s, u"uwaga"_s, u"treść"_s, u"psa"_s, u"i"_s,
                                 u"w"_s, u"z"_s, u"a"_s, u"b"_s})
            spell->addToDictionary(w);
        (void)spell->wrongWords(body);
        settle(*spell);
        const QList<spell::WordSpan> wrong = spell->wrongWords(body);
        const markdown::InlineRenderer renderer;
        const markdown::TracedHtml traced = renderer.renderBlockTraced(body, BlockKind(kind));
        const QString html = markMisspelledHtml(traced.html, traced.anchors, body, wrong, QColor(u"#c0392b"_s));
        QCOMPARE(markedInHtml(html), marked);
        // The text Qt shows is the same with the marks.
        QTextDocument plain;
        plain.setHtml(traced.html);
        QTextDocument withMarks;
        withMarks.setHtml(html);
        QCOMPARE(withMarks.toPlainText(), plain.toPlainText());
        // Nothing to mark: the HTML itself.
        QCOMPARE(markMisspelledHtml(traced.html, traced.anchors, body, {}, QColor(u"#c0392b"_s)), traced.html);
    }

    void renderedBlockMenuAndCorrection()
    {
        auto spell = makeSpell(u"pl"_s);
        spell->addToDictionary(u"zapisz"_s);
        spell->addToDictionary(u"psa"_s);
        FakeEditorHost host;
        host.setFile(u"Pisownia.md"_s, u"- Ala ma kotta i psa\n- **Zapisz** gęśla `kotta`\n"_s);
        PageController page;
        page.setSpellCheck(spell.get());
        page.setNow(u"2026-10-15 10:00"_s);
        page.setHost(&host);
        page.setPath(u"Pisownia.md"_s);
        BlockListModel *model = page.blocks();
        QCOMPARE(model->rowCount(), 2);
        const auto html = [&](int row) { return model->data(model->index(row), BlockListModel::HtmlRole).toString(); };
        const QColor red(u"#c0392b"_s);
        (void)page.spellMarkedHtml(0, html(0), 0, red);
        (void)page.spellMarkedHtml(1, html(1), 0, red);
        settle(*spell);
        QCOMPARE(markedInHtml(page.spellMarkedHtml(0, html(0), spell->revision(), red)), QStringList{u"kotta"_s});
        QCOMPARE(markedInHtml(page.spellMarkedHtml(1, html(1), spell->revision(), red)), QStringList{u"gęśla"_s});
        // Kept: the same text again.
        const QString kept = page.spellMarkedHtml(1, html(1), spell->revision(), red);
        QVERIFY(kept.isSharedWith(page.spellMarkedHtml(1, html(1), spell->revision(), red)));
        // A stale html (the binding's old text): as it is.
        QCOMPARE(page.spellMarkedHtml(1, u"<b>stary</b>"_s, spell->revision(), red), u"<b>stary</b>"_s);

        // The word under a click: positions in Qt's rich text of the rendering.
        QTextDocument shown;
        shown.setHtml(html(1));
        const int at = int(shown.toPlainText().indexOf(u"gęśla"_s));
        QVERIFY(at > 0);
        const QVariantMap word = page.misspelledInRendered(1, at + 2);
        QCOMPARE(word.value(u"text"_s).toString(), u"gęśla"_s);
        QCOMPARE(word.value(u"word"_s).toString(), u"gęśla"_s);
        const QString body = model->document().at(1).body;
        QCOMPARE(body.mid(word.value(u"from"_s).toInt(), word.value(u"to"_s).toInt() - word.value(u"from"_s).toInt()),
                 u"gęśla"_s);
        QCOMPARE(page.misspelledInRendered(1, at + 5).value(u"text"_s).toString(), u"gęśla"_s);            // its end
        QVERIFY(page.misspelledInRendered(1, 1).isEmpty());                                                // "Zapisz"
        QVERIFY(page.misspelledInRendered(1, int(shown.toPlainText().indexOf(u"kotta"_s)) + 1).isEmpty()); // code

        // A correction: the block's text, one undo step.
        QVERIFY(!page.replaceInBody(1, word.value(u"from"_s).toInt(), word.value(u"to"_s).toInt(), u"inne"_s,
                                    u"gęślą"_s)); // no longer the word there: nothing
        QVERIFY(page.replaceInBody(1, word.value(u"from"_s).toInt(), word.value(u"to"_s).toInt(), u"gęśla"_s,
                                   u"gęślą"_s));
        QCOMPARE(page.text(), u"- Ala ma kotta i psa\n- **Zapisz** gęślą `kotta`\n"_s);
        settle(*spell);
        QCOMPARE(markedInHtml(page.spellMarkedHtml(1, html(1), spell->revision(), red)), QStringList{});
        page.undo();
        QCOMPARE(page.text(), u"- Ala ma kotta i psa\n- **Zapisz** gęśla `kotta`\n"_s);
        // Other views are never marked: code.
        host.setFile(u"Kod.md"_s, u"```\nkotta\n```\n"_s);
        page.setPath(u"Kod.md"_s);
        (void)page.spellMarkedHtml(0, html(0), 0, red);
        settle(*spell);
        QCOMPARE(page.spellMarkedHtml(0, html(0), spell->revision(), red), html(0));
    }
};

QTEST_MAIN(TestEditorSpellCheck)
#include "tst_editor_spellcheck.moc"
