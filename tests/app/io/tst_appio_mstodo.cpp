// MsTodoImport: the client id and authority remembered in AppSettings, validation before anything
// is sent, the whole way against FakeMicrosoft (sign-in code shown → signed in → lists downloaded →
// the ImportController's preview → written into a real notebook), failures and cancel — and that
// the token never reaches the settings or the notebook.
#include "appsettings.h"
#include "importcontroller.h"
#include "mstodoimport.h"
#include "workspace.h"

#include "fakemicrosoft.h"

#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ioviews::ImportController;
using brulion::ioviews::MsTodoImport;
using brulion::shell::AppSettings;
using mstodotest::FakeMicrosoft;

namespace {

template<typename Object>
bool waitState(const Object &o, const QString &state, int ms = 10000)
{
    QElapsedTimer t;
    t.start();
    while (o.state() != state && t.elapsed() < ms)
        QTest::qWait(5);
    if (o.state() != state)
        qWarning() << "state" << o.state() << "error" << o.errorText();
    return o.state() == state;
}

QByteArray readAll(const QString &dir)
{
    QByteArray out;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QFile f(it.next());
        if (f.open(QIODevice::ReadOnly))
            out += f.readAll();
    }
    return out;
}

} // namespace

class TestAppIoMsTodo : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<AppSettings> settings()
    {
        ++m_n;
        return std::make_unique<AppSettings>(m_tmp.filePath(u"settings-%1.ini"_s.arg(m_n)),
                                             m_tmp.filePath(u"data-%1"_s.arg(m_n)));
    }

    std::unique_ptr<brulion::shell::Workspace> open(QString *rootOut)
    {
        const QString root = m_tmp.filePath(u"notatnik-%1"_s.arg(++m_n));
        QDir().mkpath(root);
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 10, 27), QTime(9, 0)); });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        *rootOut = root;
        return ws;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void rememberedOnTheDevice()
    {
        auto s = settings();
        {
            MsTodoImport m;
            m.setSettings(s.get());
            QCOMPARE(m.clientId(), QString());
            QCOMPARE(m.authority(), u"consumers"_s);
            QSignalSpy changed(&m, &MsTodoImport::clientIdChanged);
            m.setClientId(u" 3f2504e0-4f89-41d3-9a0c-0305e82c3301 "_s);
            QCOMPARE(changed.size(), 1);
            m.setAuthority(u"organizations"_s);
        }
        MsTodoImport again;
        again.setSettings(s.get());
        QCOMPARE(again.clientId(), u"3f2504e0-4f89-41d3-9a0c-0305e82c3301"_s);
        QCOMPARE(again.authority(), u"organizations"_s);
        QCOMPARE(s->value(u"import/mstodo/clientId"_s).toString(), u"3f2504e0-4f89-41d3-9a0c-0305e82c3301"_s);
    }

    void validation_data()
    {
        QTest::addColumn<QString>("clientId");
        QTest::addColumn<QString>("authority");
        QTest::addColumn<bool>("clientIdBad");
        QTest::addColumn<bool>("authorityBad");
        QTest::newRow("empty") << QString() << u"consumers"_s << true << false;
        QTest::newRow("not a guid") << u"moja-aplikacja"_s << u"consumers"_s << true << false;
        QTest::newRow("bad authority") << u"3f2504e0-4f89-41d3-9a0c-0305e82c3301"_s << u"https://zła/"_s << false << true;
    }

    void validation()
    {
        QFETCH(QString, clientId);
        QFETCH(QString, authority);
        QFETCH(bool, clientIdBad);
        QFETCH(bool, authorityBad);
        FakeMicrosoft ms;
        MsTodoImport m;
        m.setTransport(&ms);
        m.setClientId(clientId);
        m.setAuthority(authority);
        QCOMPARE(m.clientIdError(), QString()); // nothing shown before the first try
        m.signIn();
        QVERIFY(m.showErrors());
        QCOMPARE(!m.clientIdError().isEmpty(), clientIdBad);
        QCOMPARE(!m.authorityError().isEmpty(), authorityBad);
        QCOMPARE(m.state(), u"idle"_s);
        QTest::qWait(10);
        QCOMPARE(ms.requests.size(), 0);
    }

    void signInDownloadAndImport()
    {
        QString root;
        auto ws = open(&root);
        auto s = settings();
        FakeMicrosoft ms;
        ms.addSampleData();
        ImportController c;
        c.setWorkspace(ws.get());
        MsTodoImport m;
        m.setSettings(s.get());
        m.setTransport(&ms);
        m.setTimeUnitMs(1);
        m.setImporter(&c);
        m.setClientId(ms.clientId);
        QSignalSpy downloaded(&m, &MsTodoImport::downloaded);
        m.signIn();
        QCOMPARE(m.state(), u"starting"_s);
        QVERIFY(m.busy());
        QVERIFY(waitState(m, u"waiting"_s));
        QCOMPARE(m.userCode(), u"F7GH2K9QX"_s);
        QCOMPARE(m.verificationUrl(), u"https://microsoft.com/devicelogin"_s);
        QCOMPARE(m.verificationText(), u"microsoft.com/devicelogin"_s);
        QCOMPARE(m.secondsLeft(), 900);
        QVERIFY(m.copyCode());

        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(downloaded.size(), 1);
        QCOMPARE(m.state(), u"idle"_s);
        QCOMPARE(m.userCode(), QString());
        QCOMPARE(c.effectiveId(), u"mstodo"_s);
        QCOMPARE(c.importerId(), u"mstodo"_s);
        QCOMPARE(c.sourceName(), u"Microsoft To Do"_s);
        QCOMPARE(c.targetFolder(), u"Import/Microsoft To Do"_s);
        const QVariantMap p = c.preview();
        QCOMPARE(p.value(u"pages"_s).toInt(), 2);
        QCOMPARE(p.value(u"attachments"_s).toInt(), 1);
        QCOMPARE(p.value(u"warnings"_s).toStringList(), QStringList());

        c.startImport();
        QVERIFY(waitState(c, u"done"_s));
        QFile page(root + u"/Import/Microsoft To Do/Zadania.md"_s);
        QVERIFY(page.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(page.readAll());
        QVERIFY2(text.contains(u"- [ ] Zadzwonić do pani Józefy ⏫"_s), qPrintable(text));
        QVERIFY(QFile::exists(root + u"/Import/Microsoft To Do/Zakupy ąę.md"_s));
        QVERIFY(QFile::exists(root + u"/assets/Microsoft To Do/notatka.txt"_s));

        // The token is in no file: neither the notebook (with its .brulion state) nor the settings.
        const QByteArray token = ms.accessToken.toUtf8();
        QVERIFY(!readAll(root).contains(token));
        QVERIFY(!readAll(root + u"-state"_s).contains(token));
        QVERIFY(!readAll(m_tmp.path()).contains(token));
    }

    void failureShown()
    {
        FakeMicrosoft ms;
        ms.pollScript = {u"authorization_pending"_s, u"authorization_declined"_s};
        ImportController c;
        MsTodoImport m;
        m.setTransport(&ms);
        m.setTimeUnitMs(1);
        m.setImporter(&c);
        m.setClientId(ms.clientId);
        m.signIn();
        QVERIFY(waitState(m, u"error"_s));
        QCOMPARE(m.errorCode(), u"declined"_s);
        QVERIFY(!m.errorText().isEmpty());
        QVERIFY(!m.busy());
        QCOMPARE(c.state(), u"empty"_s);
        // Trying again works.
        ms.pollScript.clear();
        m.signIn();
        QVERIFY(waitState(m, u"idle"_s));
        QCOMPARE(m.errorText(), QString());
    }

    void downloadFailureShown()
    {
        FakeMicrosoft ms;
        ms.addSampleData();
        ms.graphStatus = 403;
        ImportController c;
        MsTodoImport m;
        m.setTransport(&ms);
        m.setTimeUnitMs(1);
        m.setImporter(&c);
        m.setClientId(ms.clientId);
        m.signIn();
        QVERIFY(waitState(m, u"error"_s));
        QCOMPARE(m.errorCode(), u"forbidden"_s);
        QVERIFY(m.errorText().contains(u"Tasks.Read"_s));
        QCOMPARE(c.state(), u"empty"_s);
    }

    void cancelWhileWaiting()
    {
        FakeMicrosoft ms;
        ms.pollScript = QStringList(100, u"authorization_pending"_s);
        ImportController c;
        MsTodoImport m;
        m.setTransport(&ms);
        m.setTimeUnitMs(1);
        m.setImporter(&c);
        m.setClientId(ms.clientId);
        m.signIn();
        QVERIFY(waitState(m, u"waiting"_s));
        m.cancel();
        QCOMPARE(m.state(), u"idle"_s);
        QCOMPARE(m.userCode(), QString());
        const auto sent = ms.requests.size();
        QTest::qWait(30);
        QCOMPARE(ms.requests.size(), sent);
        QCOMPARE(c.state(), u"empty"_s);
    }
};

QTEST_MAIN(TestAppIoMsTodo)
#include "tst_appio_mstodo.moc"
