// Spell checking in the editor (src/app/editor/spellcheck.h, markdownhighlighter.h) over word-list
// backends (spell::WordListBackend): verdicts that come from the worker thread, languages from the
// setting and the UI language, the personal dictionary and ignored words, suggestions, a slow
// backend that never blocks the UI thread; the highlighter's underlines on prose only, the word at
// the caret, misspelledAt(), source and code modes.
#include "markdownhighlighter.h"
#include "spellcheck.h"

#include <QElapsedTimer>
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
        // Nothing is made before it is needed.
        QCOMPARE(made.load(), 0);
        QCOMPARE(settled(spell, u"kot"_s), SpellCheck::Verdict::Correct);
        QCOMPARE(made.load(), 1);
        QVERIFY(offMain.load());
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
        SpellCheck spell({u"pl"_s}, [](const QString &) -> std::unique_ptr<spell::SpellBackend> {
            QThread::msleep(300); // loading a big dictionary
            return std::make_unique<SlowBackend>();
        });
        spell.setUiLanguage(u"pl"_s);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 2000; ++i)
            spell.verdict(u"słowo"_s + QString::number(i));
        spell.verdict(u"kot"_s);
        spell.requestSuggestions(u"kott"_s);
        // The UI thread goes on: events are processed while the worker loads and checks.
        for (int i = 0; i < 5; ++i)
            QCoreApplication::processEvents();
        QVERIFY2(timer.elapsed() < 250, qPrintable(QString::number(timer.elapsed())));
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
};

QTEST_MAIN(TestEditorSpellCheck)
#include "tst_editor_spellcheck.moc"
