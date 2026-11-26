// Settings → Application → "Browser extension (Web Clipper)" (WebClipperSetup, docs/CLIPPER.md): the
// browsers' rows in a private home (one found, one not), connecting writes the manifests naming the
// program, a manifest of the system's or a broken one shows, disconnecting removes only the user's own.
// The system's folders are private too (Environment::systemRoot): a machine whose browsers have
// Brulion's host — the NixOS module writes /etc/chromium/native-messaging-hosts/… — showed Chromium
// connected before anything was.
#include "webclippersetup.h"

#include "clipper/hostinstall.h"
#include "clipper/nativehost.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

QVariantMap rowOf(const QVariantList &rows, const QString &id)
{
    for (const QVariant &row : rows) {
        if (row.toMap().value(u"browserId"_s).toString() == id)
            return row.toMap();
    }
    return {};
}

/// Brulion's manifest for a Chromium browser at `path`, naming `program`.
bool writeManifest(const QString &path, const QString &program)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    // (No raw string: moc reads "//" in one as a comment.)
    f.write(QJsonDocument(QJsonObject{{u"name"_s, u"io.github.januszbit.brulion"_s},
                                      {u"path"_s, program},
                                      {u"type"_s, u"stdio"_s},
                                      {u"allowed_origins"_s,
                                       QJsonArray{clipper::chromeOrigin(clipper::chromeExtensionId())}}})
                    .toJson());
    return true;
}

} // namespace

class TestWebClipperSetup : public QObject {
    Q_OBJECT
    QTemporaryDir m_home;

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        if (clipper::currentOs() != clipper::Os::Linux)
            QSKIP("the private home moves the manifests' folders of Linux only");
        qputenv("HOME", m_home.path().toLocal8Bit());
        qunsetenv("XDG_CONFIG_HOME");
        qunsetenv("APPIMAGE");
        QVERIFY(QDir().mkpath(m_home.path() + u"/.config/chromium"_s));
    }

    /// This process's folders (the private home), with the system's in the home's "system".
    clipper::Environment environment() const
    {
        clipper::Environment env = clipper::currentEnvironment();
        env.systemRoot = m_home.path() + u"/system"_s;
        return env;
    }

    void connectAndDisconnect()
    {
        shell::WebClipperSetup setup(environment());
        QVERIFY(setup.available());
        QCOMPARE(setup.program(), QCoreApplication::applicationFilePath()); // no brulion-cli beside it
        QCOMPARE(setup.browsers().size(), 5);
        QVariantMap chromium = rowOf(setup.browsers(), u"chromium"_s);
        QCOMPARE(chromium.value(u"browserName"_s).toString(), u"Chromium"_s);
        QVERIFY(chromium.value(u"found"_s).toBool());
        QVERIFY(!chromium.value(u"connected"_s).toBool());
        QVERIFY(!rowOf(setup.browsers(), u"firefox"_s).value(u"found"_s).toBool());
        QCOMPARE(setup.connectedCount(), 0);

        QSignalSpy changed(&setup, &shell::WebClipperSetup::changed);
        setup.connectBrowsers();
        QVERIFY(!changed.isEmpty());
        QVERIFY(!setup.messageIsError());
        QVERIFY(setup.message().contains(u"Chromium"_s));
        QCOMPARE(setup.connectedCount(), 1); // only the browser this user has
        chromium = rowOf(setup.browsers(), u"chromium"_s);
        QVERIFY(chromium.value(u"connected"_s).toBool());
        QVERIFY(!chromium.value(u"broken"_s).toBool());
        const QString manifest =
                m_home.path() + u"/.config/chromium/NativeMessagingHosts/io.github.januszbit.brulion.json"_s;
        QFile f(manifest);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(f.readAll()).object().value(u"path"_s).toString(),
                 QDir::toNativeSeparators(setup.program()));
        f.close();
        QVERIFY(!QFileInfo::exists(m_home.path() + u"/.mozilla"_s));

        // A manifest naming a program that is gone shows as broken.
        QVERIFY(writeManifest(manifest, u"/nonexistent/brulion-cli"_s));
        setup.refresh();
        chromium = rowOf(setup.browsers(), u"chromium"_s);
        QVERIFY(chromium.value(u"broken"_s).toBool());
        QVERIFY(chromium.value(u"statusText"_s).toString().contains(u"/nonexistent/brulion-cli"_s));
        QCOMPARE(setup.connectedCount(), 0);

        setup.disconnectBrowsers();
        QVERIFY(!setup.messageIsError());
        QVERIFY(!QFileInfo::exists(manifest));
        QCOMPARE(setup.connectedCount(), 0);
    }

    void systemManifest()
    {
        // The system's manifest (the NixOS module, `clip install --system`): connected, for every
        // user, and never removed here.
        const clipper::Environment env = environment();
        const QString system =
                clipper::hostLocation(clipper::Browser::Chromium, clipper::Os::Linux, clipper::Scope::System, env)
                        .manifestPath;
        QCOMPARE(system, env.systemRoot + u"/etc/chromium/native-messaging-hosts/io.github.januszbit.brulion.json"_s);
        QVERIFY(writeManifest(system, QCoreApplication::applicationFilePath()));
        shell::WebClipperSetup setup(env);
        QVariantMap chromium = rowOf(setup.browsers(), u"chromium"_s);
        QVERIFY(chromium.value(u"connected"_s).toBool());
        QVERIFY(!chromium.value(u"broken"_s).toBool());
        QCOMPARE(chromium.value(u"statusText"_s).toString(),
                 u"Its clips come to Brulion (set up for every user of the computer)."_s);
        QCOMPARE(setup.connectedCount(), 1);
        QVERIFY(!rowOf(setup.browsers(), u"chrome"_s).value(u"connected"_s).toBool());

        setup.disconnectBrowsers();
        QVERIFY(!setup.messageIsError());
        QVERIFY(QFileInfo::exists(system));
        QVERIFY(rowOf(setup.browsers(), u"chromium"_s).value(u"connected"_s).toBool());
        QCOMPARE(setup.connectedCount(), 1);
        QVERIFY(QFile::remove(system));
    }
};

QTEST_GUILESS_MAIN(TestWebClipperSetup)
#include "tst_webclippersetup.moc"
