// What a brulion:// address may do (automation/policy.h): opening always, writing and running with
// the notebook's key or after the user's "Write" (never where nobody can be asked), a wrong key
// and policy Off refused; callbacks only with the key; keys random, compared in constant time and
// kept per notebook in an owner-only file that survives damage.
#include "automation/policy.h"

#include "filesystemtraits.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::automation;

class TestAutomationPolicy : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void access_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("access");
        QTest::newRow("open") << u"brulion://open?page=Dom"_s << u"open"_s;
        QTest::newRow("daily") << u"brulion://daily"_s << u"open"_s;
        QTest::newRow("search") << u"brulion://search?q=x"_s << u"open"_s;
        QTest::newRow("view") << u"brulion://view?name=calendar"_s << u"open"_s;
        QTest::newRow("capture dialog") << u"brulion://capture"_s << u"open"_s;
        QTest::newRow("capture dialog, blank text") << u"brulion://capture?text=%20%0A"_s << u"open"_s;
        QTest::newRow("capture text") << u"brulion://capture?text=Mleko"_s << u"write"_s;
        QTest::newRow("append") << u"brulion://append?text=a"_s << u"write"_s;
        QTest::newRow("prepend") << u"brulion://prepend?text=a"_s << u"write"_s;
        QTest::newRow("task") << u"brulion://task?text=a"_s << u"write"_s;
        QTest::newRow("new") << u"brulion://new?title=a"_s << u"write"_s;
        QTest::newRow("run") << u"brulion://run?extension=a&command=b"_s << u"run"_s;
    }

    void access()
    {
        QFETCH(QString, uri);
        QFETCH(QString, access);
        const ParsedUri parsed = parseUri(uri);
        QVERIFY(parsed.request);
        QCOMPARE(accessName(accessOf(*parsed.request)), access);
    }

    void decide_data()
    {
        QTest::addColumn<int>("access");
        QTest::addColumn<int>("policy");
        QTest::addColumn<int>("key");
        QTest::addColumn<bool>("canAsk");
        QTest::addColumn<int>("decision");
        QTest::addColumn<QString>("error");
        const int open = int(Access::Open), write = int(Access::Write), run = int(Access::Run);
        const int ask = int(LinkPolicy::Ask), keyOnly = int(LinkPolicy::Key), off = int(LinkPolicy::Off);
        const int none = int(KeyState::None), valid = int(KeyState::Valid), wrong = int(KeyState::Wrong);
        const int allow = int(Decision::Allow), confirm = int(Decision::Confirm), refuse = int(Decision::Refuse);
        QTest::newRow("open, no key") << open << ask << none << true << allow << QString();
        QTest::newRow("open, policy key") << open << keyOnly << none << false << allow << QString();
        QTest::newRow("open, wrong key") << open << ask << wrong << true << refuse << u"wrong_key"_s;
        QTest::newRow("open, off") << open << off << none << true << refuse << u"links_off"_s;
        QTest::newRow("write, no key: asks") << write << ask << none << true << confirm << QString();
        QTest::newRow("write, no key, nobody to ask") << write << ask << none << false << refuse << u"key_required"_s;
        QTest::newRow("write, key") << write << ask << valid << false << allow << QString();
        QTest::newRow("write, wrong key") << write << ask << wrong << true << refuse << u"wrong_key"_s;
        QTest::newRow("write, policy key") << write << keyOnly << none << true << refuse << u"key_required"_s;
        QTest::newRow("write, policy key, key") << write << keyOnly << valid << true << allow << QString();
        QTest::newRow("write, off, key") << write << off << valid << true << refuse << u"links_off"_s;
        QTest::newRow("run, no key: asks") << run << ask << none << true << confirm << QString();
        QTest::newRow("run, key") << run << ask << valid << true << allow << QString();
        QTest::newRow("run, policy key") << run << keyOnly << none << true << refuse << u"key_required"_s;
    }

    void decide()
    {
        QFETCH(int, access);
        QFETCH(int, policy);
        QFETCH(int, key);
        QFETCH(bool, canAsk);
        QFETCH(int, decision);
        QFETCH(QString, error);
        const Verdict v = brulion::automation::decide(Access(access), LinkPolicy(policy), KeyState(key), canAsk);
        QCOMPARE(int(v.decision), decision);
        QCOMPARE(v.error, error);
    }

    void callbacksOnlyWithTheKey()
    {
        QVERIFY(mayCallBack(KeyState::Valid));
        QVERIFY(!mayCallBack(KeyState::None));
        QVERIFY(!mayCallBack(KeyState::Wrong));
    }

    void policyNames()
    {
        for (const LinkPolicy p : {LinkPolicy::Ask, LinkPolicy::Key, LinkPolicy::Off})
            QCOMPARE(policyFromName(policyName(p)), p);
        QCOMPARE(policyFromName(u"OFF"_s), LinkPolicy::Off);
        QCOMPARE(policyFromName(u"whatever"_s), DefaultPolicy);
        QCOMPARE(policyFromName(QString()), LinkPolicy::Ask);
    }

    void keys()
    {
        QSet<QString> seen;
        for (int i = 0; i < 200; ++i) {
            const QString key = generateKey();
            QCOMPARE(key.size(), 32);
            for (const QChar c : key)
                QVERIFY((c >= u'a' && c <= u'z') || (c >= u'2' && c <= u'7'));
            QVERIFY(!seen.contains(key));
            seen.insert(key);
        }
    }

    void keyMatches_data()
    {
        QTest::addColumn<QString>("given");
        QTest::addColumn<QString>("stored");
        QTest::addColumn<bool>("matches");
        const QString key = u"abcdefghijklmnopqrstuvwxyz234567"_s;
        QTest::newRow("same") << key << key << true;
        QTest::newRow("capitals and spaces") << u"  "_s + key.toUpper() + u" "_s << key << true;
        QTest::newRow("shorter") << key.left(31) << key << false;
        QTest::newRow("longer") << key + u"a"_s << key << false;
        QTest::newRow("one character") << u"b"_s + key.mid(1) << key << false;
        QTest::newRow("empty given") << QString() << key << false;
        QTest::newRow("no stored key") << key << QString() << false;
        QTest::newRow("both empty") << QString() << QString() << false;
        QTest::newRow("Polish letters") << u"zażółć"_s << u"zażółć"_s << true;
    }

    void keyMatches()
    {
        QFETCH(QString, given);
        QFETCH(QString, stored);
        QFETCH(bool, matches);
        QCOMPARE(brulion::automation::keyMatches(given, stored), matches);
        QCOMPARE(keyState(given, stored),
                 given.trimmed().isEmpty() ? KeyState::None : matches ? KeyState::Valid : KeyState::Wrong);
    }

    void keyStore()
    {
        QTemporaryDir dir;
        const QString file = KeyStore::fileIn(dir.path());
        QCOMPARE(file, dir.path() + u"/secrets/automation-keys.json"_s);
        KeyStore store(file);
        QCOMPARE(store.key(u"vault1"_s), QString());
        const QString first = store.ensureKey(u"vault1"_s);
        QCOMPARE(first.size(), 32);
        QCOMPARE(store.ensureKey(u"vault1"_s), first);
        QCOMPARE(KeyStore(file).key(u"vault1"_s), first);
        const QString other = store.ensureKey(u"vault2"_s);
        QVERIFY(other != first);
        // Owner-only, like the other secrets (Unix modes; on Windows the user's profile keeps others out).
        if (brulion::test::hasUnixPermissions())
            QCOMPARE(QFileInfo(file).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
                     QFileDevice::Permissions());
        const QString renewed = store.renew(u"vault1"_s);
        QVERIFY(!renewed.isEmpty() && renewed != first);
        QCOMPARE(store.key(u"vault1"_s), renewed);
        QCOMPARE(store.key(u"vault2"_s), other);
        QVERIFY(store.remove(u"vault1"_s));
        QCOMPARE(store.key(u"vault1"_s), QString());
        QCOMPARE(store.key(u"vault2"_s), other);
        QVERIFY(store.remove(u"never-had-one"_s));
        QVERIFY(store.ensureKey(QString()).isEmpty());
        QVERIFY(!store.error().isEmpty());
    }

    void damagedKeyFile()
    {
        QTemporaryDir dir;
        const QString file = KeyStore::fileIn(dir.path());
        QDir().mkpath(QFileInfo(file).absolutePath());
        for (const QByteArray &bytes : {QByteArray("not json"), QByteArray("[1,2]"), QByteArray("{\"v\": 5}"),
                                        QByteArray("{\"v\": \"short\"}")}) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(bytes);
            f.close();
            KeyStore store(file);
            // A damaged entry is no key (a short one would be guessable); a new one replaces it.
            QCOMPARE(store.key(u"v"_s), QString());
            const QString key = store.ensureKey(u"v"_s);
            QCOMPARE(key.size(), 32);
            QCOMPARE(store.key(u"v"_s), key);
        }
    }
};

QTEST_GUILESS_MAIN(TestAutomationPolicy)
#include "tst_automation_policy.moc"
