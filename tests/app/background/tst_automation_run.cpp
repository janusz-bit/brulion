// Android's background run of automation requests (src/app/background/automationrun.h): a write
// with the notebook's key is carried out in the notebook the app published (Polish text, a task's
// reminder for the provisional alarm, the note's absolute path for the app to read again); without
// the key, with a wrong one, under policy Off, for another notebook, for something that needs the
// window or without a target nothing is written and the answer says why — and says what was
// written only when the key was valid.
#include "automationrun.h"

#include "automation/policy.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestAutomationRun : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_root;
    QString m_data;
    QString m_key;

    QJsonObject target(const QString &policy = u"ask"_s) const
    {
        return {{u"root"_s, m_root},
                {u"stateDir"_s, m_tmp.filePath(u"state"_s)},
                {u"name"_s, u"Notatki"_s},
                {u"policy"_s, policy},
                {u"language"_s, u"pl"_s},
                {u"remindersOn"_s, true}};
    }
    QJsonObject job(const QString &uri, const QJsonObject &t) const
    {
        return {{u"uri"_s, uri}, {u"id"_s, u"r1"_s}, {u"target"_s, t}};
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_root = m_tmp.filePath(u"Notatki"_s);
        m_data = m_tmp.filePath(u"data"_s);
        QVERIFY(writeFile(m_root + u"/Zakupy.md"_s, "# Zakupy\n\n## Dziś\n- chleb\n"));
        QVERIFY(writeFile(m_root + u"/templates/journal.md"_s, "## Plan\n- \n"));
        m_key = automation::KeyStore(automation::KeyStore::fileIn(m_data)).ensureKey(Vault::idForPath(m_root));
        QVERIFY(!m_key.isEmpty());
    }

    void writesWithTheKey()
    {
        background::AutomationRunner runner(m_data, [] { return QDateTime(QDate(2026, 11, 20), QTime(10, 0)); });
        QJsonObject r = runner.run(job(u"brulion://append?page=Zakupy&heading=Dzi%C5%9B&text=%C5%BC%C3%B3%C5%82ty+ser&key="_s + m_key, target()));
        QVERIFY2(r.value(u"ok"_s).toBool(), qPrintable(r.value(u"error"_s).toString()));
        QVERIFY(r.value(u"keyed"_s).toBool());
        QCOMPARE(r.value(u"path"_s).toString(), u"Zakupy.md"_s);
        QCOMPARE(r.value(u"line"_s).toInt(), 5);
        QCOMPARE(r.value(u"file"_s).toString(), QDir(m_root).filePath(u"Zakupy.md"_s));
        QCOMPARE(r.value(u"id"_s).toString(), u"r1"_s);
        QCOMPARE(readRaw(m_root + u"/Zakupy.md"_s), "# Zakupy\n\n## Dziś\n- chleb\n- żółty ser\n"_ba);
        // A task with a reminder: the time for the provisional alarm, the notebook for its body.
        r = runner.run(job(u"brulion://task?text=Zadzwoni%C4%87+do+mamy&remind=jutro+o+18&vault=notatki&key="_s + m_key, target()));
        QVERIFY2(r.value(u"ok"_s).toBool(), qPrintable(r.value(u"error"_s).toString()));
        QCOMPARE(r.value(u"reminder"_s).toString(), u"2026-11-21 18:00"_s);
        QCOMPARE(r.value(u"kind"_s).toString(), u"reminder"_s);
        QCOMPARE(r.value(u"vault"_s).toString(), Vault::idForPath(m_root));
        QCOMPARE(r.value(u"name"_s).toString(), u"Notatki"_s);
        QCOMPARE(readRaw(m_root + u"/journals/2026-11-20.md"_s), "## Plan\n- [ ] Zadzwonić do mamy ⏰ 2026-11-21 18:00\n"_ba);
        // An error of a keyed request: said, nothing written.
        r = runner.run(job(u"brulion://append?page=Brak&text=x&key="_s + m_key, target()));
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"error"_s).toString(), u"page_not_found"_s);
    }

    void refuses_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("policy");
        QTest::addColumn<QString>("error");
        QTest::newRow("no key") << u"brulion://append?page=Zakupy&text=x"_s << u"ask"_s << u"key_required"_s;
        QTest::newRow("wrong key") << u"brulion://append?page=Zakupy&text=x&key=zly"_s << u"ask"_s << u"wrong_key"_s;
        QTest::newRow("off") << u"brulion://append?page=Zakupy&text=x&key=KEY"_s << u"off"_s << u"links_off"_s;
        QTest::newRow("opening") << u"brulion://open?page=Zakupy&key=KEY"_s << u"ask"_s << u"needs_app"_s;
        QTest::newRow("a command") << u"brulion://run?extension=a&command=b&key=KEY"_s << u"ask"_s << u"needs_app"_s;
        QTest::newRow("another notebook") << u"brulion://append?vault=Praca&page=Zakupy&text=x&key=KEY"_s << u"ask"_s
                                          << u"unknown_notebook"_s;
        QTest::newRow("malformed") << u"brulion://append?text=%ZZ"_s << u"ask"_s << u"bad_uri"_s;
    }

    void refuses()
    {
        QFETCH(QString, uri);
        QFETCH(QString, policy);
        QFETCH(QString, error);
        const QByteArray before = readRaw(m_root + u"/Zakupy.md"_s);
        background::AutomationRunner runner(m_data);
        const QJsonObject r = runner.run(job(QString(uri).replace(u"KEY"_s, m_key), target(policy)));
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"error"_s).toString(), error);
        QVERIFY(!r.value(u"keyed"_s).toBool());
        QVERIFY(!r.contains(u"path"_s));
        QCOMPARE(readRaw(m_root + u"/Zakupy.md"_s), before);
    }

    void noTarget()
    {
        background::AutomationRunner runner(m_data);
        QCOMPARE(runner.run(job(u"brulion://append?page=Zakupy&text=x&key="_s + m_key, {})).value(u"error"_s).toString(),
                 u"no_notebook"_s);
        QJsonObject gone = target();
        gone.insert(u"root"_s, m_tmp.filePath(u"usunięty"_s));
        QCOMPARE(runner.run(job(u"brulion://append?page=Zakupy&text=x&key="_s + m_key, gone)).value(u"error"_s).toString(),
                 u"wrong_key"_s); // another folder is another notebook: it has no key
    }
};

QTEST_GUILESS_MAIN(TestAutomationRun)
#include "tst_automation_run.moc"
