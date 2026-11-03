// SpellChecker over word-list backends (spell/spellchecker.h): Polish and English at once, the
// personal list and ignored words with their letter-case rules, hyphenated words, "np." with its
// dot, batches, merged suggestions; the edit distance; the languages a setting means
// (spell/languages.h); the 8-bit encodings of Hunspell dictionaries (spell/dictencoding.h).
#include "spell/dictencoding.h"
#include "spell/languages.h"
#include "spell/spellchecker.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::spell;

namespace {

/// A backend that counts its requests and answers checkAll() in one go (as Android does).
class CountingBackend final : public SpellBackend {
public:
    CountingBackend(QString language, QStringList words) : m_inner(std::move(language), words) { }
    QString language() const override { return m_inner.language(); }
    bool check(const QString &word) override
    {
        ++checks;
        return m_inner.check(word);
    }
    QStringList suggest(const QString &word, int max) override { return m_inner.suggest(word, max); }
    QList<bool> checkAll(const QStringList &words) override
    {
        ++batches;
        batchWords += words;
        QList<bool> out;
        for (const QString &w : words)
            out.append(m_inner.check(w));
        return out;
    }
    int checks = 0;
    int batches = 0;
    QStringList batchWords;

private:
    WordListBackend m_inner;
};

const QStringList Polish{u"kot"_s, u"pies"_s,   u"zażółć"_s,    u"gęślą"_s, u"jaźń"_s,  u"biało"_s, u"czerwony"_s,
                         u"np."_s, u"Kraków"_s, u"spotkanie"_s, u"z"_s,     u"kotek"_s, u"kotka"_s, u"koty"_s};
const QStringList English{u"cat"_s, u"dog"_s, u"don't"_s, u"team"_s, u"meeting"_s, u"e.g."_s, u"flag"_s, u"ship"_s};

std::vector<std::unique_ptr<SpellBackend>> backends(bool polish = true, bool english = true)
{
    std::vector<std::unique_ptr<SpellBackend>> out;
    if (polish)
        out.push_back(std::make_unique<WordListBackend>(u"pl"_s, Polish));
    if (english)
        out.push_back(std::make_unique<WordListBackend>(u"en"_s, English));
    return out;
}

} // namespace

class TestSpellChecker : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void correct_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QStringList>("personal");
        QTest::addColumn<QStringList>("ignored");
        QTest::addColumn<bool>("correct");

        const QStringList none;
        QTest::newRow("polish") << u"zażółć"_s << none << none << true;
        QTest::newRow("english") << u"meeting"_s << none << none << true;
        QTest::newRow("unknown") << u"zażułć"_s << none << none << false;
        QTest::newRow("capitalised at a sentence start") << u"Kot"_s << none << none << true;
        QTest::newRow("in capitals") << u"KOT"_s << none << none << true;
        QTest::newRow("proper noun needs its capital") << u"kraków"_s << none << none << false;
        QTest::newRow("proper noun in capitals") << u"KRAKÓW"_s << none << none << true;
        QTest::newRow("decomposed letters") << u"zażółć"_s << none << none << true;
        QTest::newRow("typographic apostrophe") << u"don’t"_s << none << none << true;
        QTest::newRow("hyphenated by parts") << u"biało-czerwony"_s << none << none << true;
        QTest::newRow("hyphenated across languages") << u"flag-kot"_s << none << none << true;
        QTest::newRow("hyphenated with an unknown part") << u"biało-czrwony"_s << none << none << false;
        QTest::newRow("hyphenated acronym") << u"PDF-y"_s << none << QStringList{u"y"_s} << true;
        QTest::newRow("abbreviation with its dot") << u"np."_s << none << none << true;
        QTest::newRow("english abbreviation") << u"e.g."_s << none << none << true;
        QTest::newRow("word at a sentence end") << u"kot."_s << none << none << true;
        QTest::newRow("abbreviation needs its dot") << u"np"_s << none << none << false;
        QTest::newRow("personal word") << u"Brulion"_s << QStringList{u"Brulion"_s} << none << true;
        QTest::newRow("personal word in capitals") << u"BRULION"_s << QStringList{u"Brulion"_s} << none << true;
        QTest::newRow("personal capital is kept") << u"brulion"_s << QStringList{u"Brulion"_s} << none << false;
        QTest::newRow("personal small word capitalised")
                << u"Smartfonik"_s << QStringList{u"smartfonik"_s} << none << true;
        QTest::newRow("personal word decomposed") << u"żuk"_s << QStringList{u"żuk"_s} << none << true;
        QTest::newRow("personal word with spaces around") << u"żuk"_s << QStringList{u"  żuk "_s} << none << true;
        QTest::newRow("ignored") << u"blablabla"_s << none << QStringList{u"blablabla"_s} << true;
        QTest::newRow("ignored part") << u"kot-blabla"_s << none << QStringList{u"blabla"_s} << true;
        QTest::newRow("empty") << QString() << none << none << true;
        QTest::newRow("only a dot") << u"."_s << none << none << true;
    }

    void correct()
    {
        QFETCH(QString, word);
        QFETCH(QStringList, personal);
        QFETCH(QStringList, ignored);
        QFETCH(bool, correct);
        SpellChecker checker;
        checker.setBackends(backends());
        checker.setPersonalWords(personal);
        checker.setIgnoredWords(ignored);
        QCOMPARE(checker.isCorrect(word), correct);
        // checkAll() decides the same.
        QCOMPARE(checker.checkAll({word}), QList<bool>{correct});
    }

    void oneLanguage()
    {
        SpellChecker checker;
        checker.setBackends(backends(true, false));
        QCOMPARE(checker.languages(), QStringList{u"pl"_s});
        QVERIFY(checker.isCorrect(u"kot"_s));
        QVERIFY(!checker.isCorrect(u"cat"_s));
        checker.setBackends(backends(false, true));
        QVERIFY(checker.isCorrect(u"cat"_s));
        QVERIFY(!checker.isCorrect(u"kot"_s));
    }

    void noBackendsNothingIsWrong()
    {
        SpellChecker checker;
        QVERIFY(!checker.hasBackends());
        QVERIFY(checker.isCorrect(u"asdfgh"_s));
        QCOMPARE(checker.checkAll({u"asdfgh"_s, u"kot"_s}), (QList<bool>{true, true}));
        QCOMPARE(checker.suggestions(u"asdfgh"_s), QStringList{});
    }

    void personalListReplaced()
    {
        SpellChecker checker;
        checker.setBackends(backends());
        checker.setPersonalWords({u"Brulion"_s});
        QVERIFY(checker.isCorrect(u"Brulion"_s));
        checker.setPersonalWords({});
        QVERIFY(!checker.isCorrect(u"Brulion"_s));
    }

    void batches()
    {
        auto pl = std::make_unique<CountingBackend>(u"pl"_s, Polish);
        auto en = std::make_unique<CountingBackend>(u"en"_s, English);
        CountingBackend *plRaw = pl.get();
        CountingBackend *enRaw = en.get();
        std::vector<std::unique_ptr<SpellBackend>> list;
        list.push_back(std::move(pl));
        list.push_back(std::move(en));
        SpellChecker checker;
        checker.setBackends(std::move(list));
        checker.setPersonalWords({u"Brulion"_s});
        const QList<bool> result =
                checker.checkAll({u"kot"_s, u"cat"_s, u"Brulion"_s, u"xyzzy"_s, u"biało-czerwony"_s});
        QCOMPARE(result, (QList<bool>{true, true, true, false, true}));
        // One request per backend; the personal word asks neither, the second backend gets only
        // what the first did not know.
        QCOMPARE(plRaw->batches, 1);
        QCOMPARE(enRaw->batches, 1);
        QCOMPARE(plRaw->batchWords, (QStringList{u"kot"_s, u"cat"_s, u"xyzzy"_s, u"biało-czerwony"_s}));
        QCOMPARE(enRaw->batchWords, (QStringList{u"cat"_s, u"xyzzy"_s, u"biało-czerwony"_s}));
    }

    void suggestions_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<int>("max");
        QTest::addColumn<QStringList>("suggestions");

        QTest::newRow("polish typo") << u"zażułć"_s << 6 << QStringList{u"zażółć"_s};
        QTest::newRow("both languages in turn") << u"kat"_s << 6 << QStringList{u"kot"_s, u"cat"_s, u"koty"_s};
        QTest::newRow("limited") << u"kat"_s << 2 << QStringList{u"kot"_s, u"cat"_s};
        QTest::newRow("transposition") << u"pise"_s << 6 << QStringList{u"pies"_s};
        QTest::newRow("capitalised") << u"Kat"_s << 2 << QStringList{u"Kot"_s, u"Cat"_s};
        QTest::newRow("nothing near") << u"qqqqqqqq"_s << 6 << QStringList{};
        QTest::newRow("dot dropped") << u"kat."_s << 1 << QStringList{u"kot"_s};
        QTest::newRow("zero") << u"kat"_s << 0 << QStringList{};
        QTest::newRow("empty") << QString() << 6 << QStringList{};
    }

    void suggestions()
    {
        QFETCH(QString, word);
        QFETCH(int, max);
        QFETCH(QStringList, suggestions);
        SpellChecker checker;
        checker.setBackends(backends());
        QCOMPARE(checker.suggestions(word, max), suggestions);
    }

    void editDistance_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("distance");
        QTest::newRow("same") << u"kot"_s << u"kot"_s << 0;
        QTest::newRow("substitution") << u"kot"_s << u"kat"_s << 1;
        QTest::newRow("insertion") << u"kot"_s << u"kotek"_s << 2;
        QTest::newRow("transposition") << u"pies"_s << u"pise"_s << 1;
        QTest::newRow("diacritic") << u"zazolc"_s << u"zażółć"_s << 3; // over the limit of 2: 3
        QTest::newRow("empty") << QString() << u"ab"_s << 2;
        QTest::newRow("far") << u"a"_s << u"abcdef"_s << 3;
    }

    void editDistance()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, distance);
        QCOMPARE(brulion::spell::editDistance(a, b, 2), distance);
    }

    void languages_data()
    {
        QTest::addColumn<QString>("setting");
        QTest::addColumn<QString>("ui");
        QTest::addColumn<QString>("system");
        QTest::addColumn<QStringList>("available");
        QTest::addColumn<QStringList>("languages");

        const QStringList both{u"pl_PL"_s, u"en_US"_s};
        QTest::newRow("auto, polish ui") << u"auto"_s << u"pl"_s << u"pl_PL"_s << both << QStringList{u"pl"_s, u"en"_s};
        QTest::newRow("auto, english ui") << u"auto"_s << u"en"_s << u"en_US"_s << both << QStringList{u"en"_s};
        QTest::newRow("auto, english ui, polish system")
                << u"auto"_s << u"en"_s << u"pl-PL"_s << both << QStringList{u"en"_s, u"pl"_s};
        QTest::newRow("auto, only english available")
                << u"auto"_s << u"pl"_s << u"pl_PL"_s << QStringList{u"en_GB"_s} << QStringList{u"en"_s};
        QTest::newRow("auto, nothing available") << u"auto"_s << u"pl"_s << u"pl"_s << QStringList{} << QStringList{};
        QTest::newRow("empty is auto") << QString() << u"pl"_s << u"C"_s << both << QStringList{u"pl"_s, u"en"_s};
        QTest::newRow("polish only") << u"pl"_s << u"en"_s << u"en"_s << both << QStringList{u"pl"_s};
        QTest::newRow("english only") << u"en"_s << u"pl"_s << u"pl"_s << both << QStringList{u"en"_s};
        QTest::newRow("both") << u"pl,en"_s << u"en"_s << u"en"_s << both << QStringList{u"pl"_s, u"en"_s};
        QTest::newRow("both, english first")
                << u" EN , pl "_s << u"pl"_s << u"pl"_s << both << QStringList{u"en"_s, u"pl"_s};
        QTest::newRow("unavailable left out") << u"pl,de"_s << u"pl"_s << u"pl"_s << both << QStringList{u"pl"_s};
        QTest::newRow("duplicates") << u"pl,pl_PL,pl"_s << u"pl"_s << u"pl"_s << both << QStringList{u"pl"_s};
        QTest::newRow("garbage is auto") << u",,,!!"_s << u"pl"_s << u"pl"_s << both << QStringList{u"pl"_s, u"en"_s};
    }

    void languages()
    {
        QFETCH(QString, setting);
        QFETCH(QString, ui);
        QFETCH(QString, system);
        QFETCH(QStringList, available);
        QFETCH(QStringList, languages);
        QCOMPARE(resolveLanguages(setting, ui, system, available), languages);
    }

    void primaryLanguage_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("language");
        QTest::newRow("pl_PL") << u"pl_PL"_s << u"pl"_s;
        QTest::newRow("en-US") << u"en-US"_s << u"en"_s;
        QTest::newRow("en_GB-ise") << u"en_GB-ise"_s << u"en"_s;
        QTest::newRow("upper") << u"PL"_s << u"pl"_s;
        QTest::newRow("three letters") << u"csb_PL"_s << u"csb"_s;
        QTest::newRow("posix") << u"pl_PL.UTF-8"_s << u"pl"_s;
        QTest::newRow("C") << u"C"_s << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("too long") << u"polski"_s << QString();
    }

    void primaryLanguage()
    {
        QFETCH(QString, name);
        QFETCH(QString, language);
        QCOMPARE(brulion::spell::primaryLanguage(name), language);
    }

    void encodings_data()
    {
        QTest::addColumn<QByteArray>("encoding");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QByteArray>("bytes");

        QTest::newRow("latin2 polish")
                << QByteArray("ISO8859-2") << u"zażółć gęślą jaźń ŻÓŁĆ ĘŚĄŹŃ"_s
                << QByteArray("za\xbf\xf3\xb3\xe6 g\xea\xb6l\xb1 ja\xbc\xf1 \xaf\xd3\xa3\xc6 \xca\xa6\xa1\xac\xd1");
        QTest::newRow("latin2 spelled otherwise") << QByteArray("iso-8859_2") << u"ą"_s << QByteArray("\xb1");
        QTest::newRow("latin1") << QByteArray("ISO8859-1") << u"café"_s << QByteArray("caf\xe9");
        QTest::newRow("latin9 euro") << QByteArray("ISO8859-15") << u"€Š"_s << QByteArray("\xa4\xa6");
        QTest::newRow("utf8") << QByteArray("UTF-8") << u"żółw"_s << QByteArray("\xc5\xbc\xc3\xb3\xc5\x82w");
        QTest::newRow("ascii") << QByteArray("ISO8859-2") << u"don't"_s << QByteArray("don't");
    }

    void encodings()
    {
        QFETCH(QByteArray, encoding);
        QFETCH(QString, text);
        QFETCH(QByteArray, bytes);
        QVERIFY(isKnownDictionaryEncoding(encoding));
        QCOMPARE(encodeDictionaryText(text, encoding), std::optional<QByteArray>(bytes));
        QCOMPARE(decodeDictionaryText(bytes, encoding), text);
    }

    void unencodable()
    {
        QVERIFY(!encodeDictionaryText(u"voilà"_s, "ISO8859-2"));
        QVERIFY(!encodeDictionaryText(u"ż"_s, "ISO8859-1"));
        QVERIFY(!encodeDictionaryText(u"¤"_s, "ISO8859-15")); // € took its byte
        QVERIFY(!encodeDictionaryText(u"😀"_s, "ISO8859-2"));
        QVERIFY(!encodeDictionaryText(u"kot"_s, "KOI8-R"));
        QVERIFY(!isKnownDictionaryEncoding("KOI8-R"));
        QVERIFY(!isKnownDictionaryEncoding(""));
        // Unknown encodings and broken UTF-8 are read as UTF-8.
        QCOMPARE(decodeDictionaryText("kot", "KOI8-R"), u"kot"_s);
        QCOMPARE(decodeDictionaryText("\xff", "UTF-8"), u"\uFFFD"_s);
    }

    void latin2RoundTrip()
    {
        QByteArray all;
        for (int b = 0x20; b < 0x100; ++b) {
            if (b != 0x7f && (b < 0x80 || b >= 0xa0))
                all.append(char(b));
        }
        const QString text = decodeDictionaryText(all, "ISO8859-2");
        QCOMPARE(text.size(), all.size());
        QCOMPARE(encodeDictionaryText(text, "ISO8859-2"), std::optional<QByteArray>(all));
    }
};

QTEST_GUILESS_MAIN(TestSpellChecker)
#include "tst_spell_checker.moc"
