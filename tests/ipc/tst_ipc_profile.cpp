// The app's profile (src/ipc/profile.h): valid names, `--profile` on the command line (before a
// lone "--open", in both spellings), BRULION_PROFILE, and a test run's own "test" profile when it
// has no home of its own — never for one with XDG folders of its own (ctest, the scripts) or a
// profile chosen; malformed names refused.
#include "profile.h"

#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ipc::chooseProfile;

class TestIpcProfile : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void validNames_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("valid");
        QTest::newRow("letters") << u"test"_s << true;
        QTest::newRow("digits, dash, underscore") << u"shell-desktop_2"_s << true;
        QTest::newRow("32") << QString(32, u'a') << true;
        QTest::newRow("33") << QString(33, u'a') << false;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("diacritics") << u"żółw"_s << false;
        QTest::newRow("slash") << u"../Brulion"_s << false;
        QTest::newRow("space") << u"mój profil"_s << false;
    }
    void validNames()
    {
        QFETCH(QString, name);
        QFETCH(bool, valid);
        QCOMPARE(brulion::ipc::isValidProfileName(name), valid);
    }

    void profileArgument_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<QString>("profile");
        QTest::newRow("none") << QStringList{u"--onboarding"_s} << QString();
        QTest::newRow("separate value") << QStringList{u"--profile"_s, u"zrzuty"_s, u"--demo"_s} << u"zrzuty"_s;
        QTest::newRow("with equals") << QStringList{u"--profile=zrzuty"_s} << u"zrzuty"_s;
        QTest::newRow("the last wins") << QStringList{u"--profile=a"_s, u"--profile"_s, u"b"_s} << u"b"_s;
        QTest::newRow("no value") << QStringList{u"--profile"_s} << QString();
        QTest::newRow("a file after --open") << QStringList{u"--open"_s, u"--profile"_s, u"x"_s} << QString();
    }
    void profileArgument()
    {
        QFETCH(QStringList, arguments);
        QFETCH(QString, profile);
        QCOMPARE(brulion::ipc::profileArgument(arguments), profile);
    }

    void choose_data()
    {
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<QString>("environment");
        QTest::addColumn<QString>("xdgConfigHome");
        QTest::addColumn<QString>("profile");
        QTest::addColumn<bool>("implied");
        QTest::addColumn<bool>("error");
        const QString home = u"/home/ala"_s;
        const QStringList selftest{u"--demo"_s, u"--selftest-shell"_s};
        QTest::newRow("a normal run") << QStringList{u"--vault"_s, u"/home/ala/Notatki"_s} << QString() << QString()
                                      << QString() << false << false;
        QTest::newRow("a self-test by hand") << selftest << QString() << QString() << u"test"_s << true << false;
        QTest::newRow("the journey by hand") << QStringList{u"--journey"_s, u"/tmp/j"_s} << QString() << QString()
                                             << u"test"_s << true << false;
        QTest::newRow("a screenshot by hand") << QStringList{u"--screenshot"_s, u"/tmp/a.png"_s} << QString()
                                              << QString() << u"test"_s << true << false;
        QTest::newRow("XDG_CONFIG_HOME is the user's own") << selftest << QString() << u"/home/ala/.config/"_s
                                                           << u"test"_s << true << false;
        QTest::newRow("ctest: a home of its own") << selftest << QString() << u"/build/tests/home/config"_s
                                                  << QString() << false << false;
        QTest::newRow("Windows ctest: BRULION_PROFILE") << selftest << u"test-shell-desktop"_s << QString()
                                                        << u"test-shell-desktop"_s << false << false;
        QTest::newRow("--profile wins") << QStringList{u"--profile"_s, u"zrzuty"_s, u"--screenshot"_s, u"a.png"_s}
                                        << u"inny"_s << QString() << u"zrzuty"_s << false << false;
        QTest::newRow("a bad --profile") << QStringList{u"--profile"_s, u"../Brulion"_s} << QString() << QString()
                                         << QString() << false << true;
        QTest::newRow("--profile without a name") << QStringList{u"--profile"_s} << QString() << QString()
                                                  << QString() << false << true;
        QTest::newRow("a bad variable is the default") << QStringList() << u"mój profil"_s << QString() << QString()
                                                       << false << false;
        QTest::newRow("a file called --profile") << QStringList{u"--open"_s, u"--profile"_s, u"--screenshot"_s}
                                                 << QString() << QString() << QString() << false << false;
        QTest::newRow("remote control is no test run") << QStringList{u"--remote-control"_s, u"--now"_s, u"x"_s}
                                                       << QString() << QString() << QString() << false << false;
    }
    void choose()
    {
        QFETCH(QStringList, arguments);
        QFETCH(QString, environment);
        QFETCH(QString, xdgConfigHome);
        QFETCH(QString, profile);
        QFETCH(bool, implied);
        QFETCH(bool, error);
        const brulion::ipc::ProfileChoice choice = chooseProfile(arguments, environment, xdgConfigHome, u"/home/ala"_s);
        QCOMPARE(choice.profile, profile);
        QCOMPARE(choice.implied, implied);
        QCOMPARE(!choice.error.isEmpty(), error);
    }

    void applyExportsTheChoice()
    {
        qunsetenv("BRULION_PROFILE");
        const brulion::ipc::ProfileChoice choice = brulion::ipc::applyProfile({u"--profile=zrzuty"_s});
        QCOMPARE(choice.profile, u"zrzuty"_s);
        QCOMPARE(qEnvironmentVariable("BRULION_PROFILE"), u"zrzuty"_s);
        QCOMPARE(brulion::ipc::organizationName(), u"Brulion-zrzuty"_s);
        qunsetenv("BRULION_PROFILE");
        QCOMPARE(brulion::ipc::organizationName(), u"Brulion"_s);
    }
};

QTEST_GUILESS_MAIN(TestIpcProfile)
#include "tst_ipc_profile.moc"
