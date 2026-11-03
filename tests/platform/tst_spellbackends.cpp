// This build's spell checking backend (src/platform/spellbackends.h). With Hunspell: a tiny Polish
// dictionary in ISO8859-2 (as the real pl_PL) and an English one in UTF-8, written here and found
// through DICPATH — diacritics, an affix rule, capitals at a sentence start, apostrophes, words the
// encoding cannot hold, suggestions back with their Polish letters, a Polish folder name, then
// SpellChecker over both. Without a backend (BRULION_SPELL_BACKEND=none): no languages, nothing made.
#include "spellbackends.h"

#include "spell/spellchecker.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

void write(const QString &path, const QByteArray &bytes)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

} // namespace

class TestSpellBackends : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_tmp;
    QString m_dir;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_dir = m_tmp.filePath(u"słowniki"_s);
        QVERIFY(QDir().mkpath(m_dir));
        // Polish, ISO8859-2: "kot" with the plural "koty" (flag A), "zażółć", "gęślą", "jaźń".
        write(m_dir + u"/pl_PL.aff"_s,
              "SET ISO8859-2\n"
              "TRY a\xb1\x62\x63\xe6\x64\x65\xea\x66ghijkl\xb3mn\xf1o\xf3prs\xb6tuwyz\xbc\xbf\n"
              "SFX A Y 1\n"
              "SFX A 0 y .\n");
        write(m_dir + u"/pl_PL.dic"_s, "4\nkot/A\nza\xbf\xf3\xb3\xe6\ng\xea\xb6l\xb1\nja\xbc\xf1\n");
        // English, UTF-8.
        write(m_dir + u"/en_US.aff"_s, "SET UTF-8\nTRY esianrtolcdugmphbyfvkwz'\nWORDCHARS '\n");
        write(m_dir + u"/en_US.dic"_s, "3\ncat\ndon't\nmeeting\n");
        // Found first; nothing else of this machine (the system's dictionaries) gets in the way.
        qputenv("DICPATH", QFile::encodeName(m_dir));
    }

    void engine()
    {
        QCOMPARE(platform::spellEngine(), QString::fromUtf8(BRULION_TEST_SPELL_BACKEND) == u"none"
                                                  ? QString()
                                                  : QString::fromUtf8(BRULION_TEST_SPELL_BACKEND));
    }

    void languages()
    {
        if (platform::spellEngine().isEmpty()) {
            QCOMPARE(platform::spellLanguages(), QStringList{});
            QVERIFY(!platform::createSpellBackend(u"pl"_s));
            return;
        }
        const QStringList languages = platform::spellLanguages();
        QVERIFY2(languages.contains(u"pl"_s) && languages.contains(u"en"_s), qPrintable(languages.join(u',')));
        QVERIFY(!platform::createSpellBackend(u"xx"_s));
    }

    void polish_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<bool>("right");
        QTest::newRow("diacritics") << u"zażółć"_s << true;
        QTest::newRow("another") << u"gęślą"_s << true;
        QTest::newRow("affix") << u"koty"_s << true;
        QTest::newRow("sentence start") << u"Zażółć"_s << true;
        QTest::newRow("capitals") << u"JAŹŃ"_s << true;
        QTest::newRow("typo") << u"zażułć"_s << false;
        QTest::newRow("not in the encoding") << u"voilà"_s << false;
        QTest::newRow("emoji") << u"kot😀"_s << false;
    }

    void polish()
    {
        if (platform::spellEngine() != u"hunspell")
            QSKIP("no Hunspell in this build");
        QFETCH(QString, word);
        QFETCH(bool, right);
        auto pl = platform::createSpellBackend(u"pl"_s);
        QVERIFY(pl);
        QCOMPARE(pl->language(), u"pl"_s);
        QCOMPARE(pl->check(word), right);
    }

    void suggestions()
    {
        if (platform::spellEngine() != u"hunspell")
            QSKIP("no Hunspell in this build");
        auto pl = platform::createSpellBackend(u"pl"_s);
        QVERIFY(pl);
        QVERIFY2(pl->suggest(u"zażułć"_s, 5).contains(u"zażółć"_s), qPrintable(pl->suggest(u"zażułć"_s, 5).join(u',')));
        QCOMPARE(pl->suggest(u"voilà"_s, 5), QStringList{});
        QCOMPARE(pl->suggest(u"kot"_s, 0), QStringList{});
        auto en = platform::createSpellBackend(u"en"_s);
        QVERIFY(en);
        QVERIFY(en->check(u"don't"_s));
        QVERIFY(!en->check(u"dont't"_s));
        QVERIFY(en->suggest(u"meetnig"_s, 3).contains(u"meeting"_s));
        // A "word" too long to bother Hunspell with counts as right.
        QVERIFY(en->check(QString(500, u'x')));
    }

    void checker()
    {
        if (platform::spellEngine() != u"hunspell")
            QSKIP("no Hunspell in this build");
        std::vector<std::unique_ptr<spell::SpellBackend>> backends;
        backends.push_back(platform::createSpellBackend(u"pl"_s));
        backends.push_back(platform::createSpellBackend(u"en"_s));
        spell::SpellChecker checker;
        checker.setBackends(std::move(backends));
        QCOMPARE(checker.checkAll({u"kot"_s, u"cat"_s, u"kot-cat"_s, u"don’t"_s, u"zażułć"_s, u"Kot."_s}),
                 (QList<bool>{true, true, true, true, false, true}));
        QCOMPARE(checker.suggestions(u"zażułć"_s, 1), QStringList{u"zażółć"_s});
    }
};

QTEST_GUILESS_MAIN(TestSpellBackends)
#include "tst_spellbackends.moc"
