// OS integration (src/platform) that runs everywhere: "Start when you sign in" — the Run value's
// command, which program it starts, Brulion's own entries, Task Manager's StartupApproved values,
// Autostart on INI files (on, off, switched off in Task Manager, a separate profile, an entry of a
// moved portable copy pointed to this program) — and the Windows AppUserModelID of the toasts and
// its registration.
#include "appidentity.h"
#include "autostart.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

namespace {

const QString Exe = u"C:/Users/Łucja Żółć/AppData/Local/Programs/Brulion/brulion.exe"_s;
const QString ExeCommand = u"\"C:\\Users\\Łucja Żółć\\AppData\\Local\\Programs\\Brulion\\brulion.exe\" --hidden"_s;

QByteArray approvedValue(char first)
{
    QByteArray value(12, '\0');
    value[0] = first;
    value[4] = '\x5a'; // a FILETIME follows when it was switched off
    return value;
}

} // namespace

class TestAutostart : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString nextFile() { return m_tmp.filePath(u"reg%1.ini"_s.arg(++m_n)); }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void command_data()
    {
        QTest::addColumn<QString>("exe");
        QTest::addColumn<QString>("expected");
        QTest::newRow("Qt's slashes, Polish user name") << Exe << ExeCommand;
        QTest::newRow("backslashes already")
                << u"D:\\Przenośne\\Brulion\\brulion.exe"_s << u"\"D:\\Przenośne\\Brulion\\brulion.exe\" --hidden"_s;
        QTest::newRow("no spaces") << u"C:/Brulion/brulion.exe"_s << u"\"C:\\Brulion\\brulion.exe\" --hidden"_s;
    }
    void command()
    {
        QFETCH(QString, exe);
        QFETCH(QString, expected);
        QCOMPARE(autostartCommand(exe), expected);
        QVERIFY(autostartTargets(autostartCommand(exe), exe));
        QVERIFY(isBrulionAutostartCommand(autostartCommand(exe)));
    }

    void targets_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("exe");
        QTest::addColumn<bool>("expected");
        QTest::addColumn<QString>("program");
        QTest::newRow("this program") << ExeCommand << Exe << true
                                      << u"C:\\Users\\Łucja Żółć\\AppData\\Local\\Programs\\Brulion\\brulion.exe"_s;
        QTest::newRow("other case (NTFS)")
                << u"\"c:\\USERS\\łucja żółć\\appdata\\local\\programs\\brulion\\BRULION.EXE\" --hidden"_s << Exe
                << true << u"c:\\USERS\\łucja żółć\\appdata\\local\\programs\\brulion\\BRULION.EXE"_s;
        QTest::newRow("unquoted") << u"C:\\Brulion\\brulion.exe --hidden"_s << u"C:/Brulion/brulion.exe"_s << true
                                  << u"C:\\Brulion\\brulion.exe"_s;
        QTest::newRow("unquoted, nothing more") << u"C:\\Brulion\\brulion.exe"_s << u"C:/Brulion/brulion.exe"_s << true
                                                << u"C:\\Brulion\\brulion.exe"_s;
        QTest::newRow("other arguments") << u"\"C:\\Brulion\\brulion.exe\" --today"_s << u"C:/Brulion/brulion.exe"_s
                                         << true << u"C:\\Brulion\\brulion.exe"_s;
        QTest::newRow("dot segments") << u"\"C:\\Brulion\\..\\Brulion\\.\\brulion.exe\" --hidden"_s
                                      << u"C:/Brulion/brulion.exe"_s << true
                                      << u"C:\\Brulion\\..\\Brulion\\.\\brulion.exe"_s;
        QTest::newRow("spaces around") << u"   \"C:\\Brulion\\brulion.exe\"   --hidden  "_s
                                       << u"C:/Brulion/brulion.exe"_s << true << u"C:\\Brulion\\brulion.exe"_s;
        QTest::newRow("another copy") << u"\"D:\\Przenośne\\Brulion\\brulion.exe\" --hidden"_s << Exe << false
                                      << u"D:\\Przenośne\\Brulion\\brulion.exe"_s;
        QTest::newRow("another program") << u"\"C:\\Windows\\notepad.exe\""_s << Exe << false
                                         << u"C:\\Windows\\notepad.exe"_s;
        QTest::newRow("a longer name") << u"\"C:\\Brulion\\brulion.exe.old\" --hidden"_s << u"C:/Brulion/brulion.exe"_s
                                       << false << u"C:\\Brulion\\brulion.exe.old"_s;
        QTest::newRow("empty") << QString() << Exe << false << QString();
        QTest::newRow("only spaces") << u"   "_s << Exe << false << QString();
        QTest::newRow("quote not closed")
                << u"\"C:\\Brulion\\brulion.exe --hidden"_s << u"C:/Brulion/brulion.exe"_s << false << QString();
        QTest::newRow("empty quotes") << u"\"\" --hidden"_s << Exe << false << QString();
        QTest::newRow("no program known") << ExeCommand << QString() << false
                                          << u"C:\\Users\\Łucja Żółć\\AppData\\Local\\Programs\\Brulion\\brulion.exe"_s;
    }
    void targets()
    {
        QFETCH(QString, command);
        QFETCH(QString, exe);
        QFETCH(bool, expected);
        QFETCH(QString, program);
        QCOMPARE(autostartTargets(command, exe), expected);
        QCOMPARE(autostartProgram(command), program);
    }

    void brulionEntries_data()
    {
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("expected");
        QTest::newRow("ours") << ExeCommand << true;
        QTest::newRow("upper case") << u"\"D:\\BRULION\\BRULION.EXE\" --hidden"_s << true;
        QTest::newRow("forward slashes") << u"\"D:/Programy/brulion.exe\" --hidden"_s << true;
        QTest::newRow("no space after the quote") << u"\"D:\\Brulion\\brulion.exe\"--hidden"_s << true;
        QTest::newRow("unquoted") << u"D:\\Brulion\\brulion.exe --hidden"_s << true;
        QTest::newRow("without --hidden") << u"\"D:\\Brulion\\brulion.exe\""_s << false;
        QTest::newRow("more arguments") << u"\"D:\\Brulion\\brulion.exe\" --hidden --capture"_s << false;
        QTest::newRow("another argument") << u"\"D:\\Brulion\\brulion.exe\" --today"_s << false;
        QTest::newRow("brulion-cli") << u"\"D:\\Brulion\\brulion-cli.exe\" --hidden"_s << false;
        QTest::newRow("another program") << u"\"C:\\Program Files\\Inny\\brulion.exe.lnk\" --hidden"_s << false;
        QTest::newRow("quote not closed") << u"\"D:\\Brulion\\brulion.exe --hidden"_s << false;
        QTest::newRow("empty") << QString() << false;
    }
    void brulionEntries()
    {
        QFETCH(QString, command);
        QFETCH(bool, expected);
        QCOMPARE(isBrulionAutostartCommand(command), expected);
    }

    void startupApproved_data()
    {
        QTest::addColumn<QByteArray>("value");
        QTest::addColumn<bool>("disabled");
        QTest::newRow("on (02)") << approvedValue('\x02') << false;
        QTest::newRow("off (03)") << approvedValue('\x03') << true;
        QTest::newRow("on (06)") << approvedValue('\x06') << false;
        QTest::newRow("off (07)") << approvedValue('\x07') << true;
        QTest::newRow("empty") << QByteArray() << false;
        QTest::newRow("one byte, off") << QByteArray("\x03", 1) << true;
        QTest::newRow("one byte, on") << QByteArray("\x02", 1) << false;
    }
    void startupApproved()
    {
        QFETCH(QByteArray, value);
        QFETCH(bool, disabled);
        QCOMPARE(startupApprovedDisabled(value), disabled);
    }

    void onAndOff()
    {
        QSettings run(nextFile(), QSettings::IniFormat);
        QSettings approved(nextFile(), QSettings::IniFormat);
        Autostart autostart(&run, &approved, Exe);
        QCOMPARE(autostart.availability(), Autostart::Availability::Available);
        QVERIFY(!autostart.enabled());
        QVERIFY(autostart.command().isEmpty());

        QString error;
        QVERIFY2(autostart.setEnabled(true, &error), qPrintable(error));
        QVERIFY(autostart.enabled());
        QCOMPARE(run.value(u"Brulion"_s).toString(), ExeCommand);

        // Task Manager → Startup apps → Disable: the value stays, StartupApproved says "off".
        approved.setValue(u"Brulion"_s, approvedValue('\x03'));
        QVERIFY(!autostart.enabled());
        QCOMPARE(autostart.command(), ExeCommand);
        approved.setValue(u"Brulion"_s, approvedValue('\x02'));
        QVERIFY(autostart.enabled());
        // The registry's REG_BINARY as QSettings reads it on Windows: UTF-16 text of the bytes.
        approved.setValue(u"Brulion"_s, QString(QString(QChar(0x0003)) + QChar(0) + QChar(0x5a00)));
        QVERIFY(!autostart.enabled());
        // Switched on again in Brulion: Task Manager's "off" goes too.
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(!approved.contains(u"Brulion"_s));
        QVERIFY(autostart.enabled());

        // Another copy's entry is not this program's switch.
        run.setValue(u"Brulion"_s, u"\"D:\\Przenośne\\Brulion\\brulion.exe\" --hidden"_s);
        QVERIFY(!autostart.enabled());

        QVERIFY(autostart.setEnabled(false));
        QVERIFY(!run.contains(u"Brulion"_s));
        QVERIFY(!autostart.enabled());
        // Off when off: still fine.
        QVERIFY(autostart.setEnabled(false));
        // Other programs' entries are never touched.
        run.setValue(u"Inny program"_s, u"\"C:\\Inny\\program.exe\""_s);
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.setEnabled(false));
        QCOMPARE(run.value(u"Inny program"_s).toString(), u"\"C:\\Inny\\program.exe\""_s);
    }

    void separateProfileAndUnsupported()
    {
        QSettings run(nextFile(), QSettings::IniFormat);
        QSettings approved(nextFile(), QSettings::IniFormat);
        run.setValue(u"Brulion"_s, ExeCommand);
        Autostart profile(&run, &approved, Exe, Autostart::Availability::SeparateProfile);
        QVERIFY(!profile.enabled()); // a Run entry starts the default profile, not this one
        QString error;
        QVERIFY(!profile.setEnabled(false, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(run.value(u"Brulion"_s).toString(), ExeCommand);
        QVERIFY(!profile.refresh([](const QString &) { return false; }));

        Autostart none(nullptr, nullptr, Exe);
        QCOMPARE(none.availability(), Autostart::Availability::Unsupported);
        QVERIFY(!none.enabled());
        QVERIFY(!none.setEnabled(true));
        QVERIFY(none.command().isEmpty());

#ifndef Q_OS_WIN
        // Only Windows has a Run key (Linux: the NixOS module).
        const auto process = Autostart::forThisProcess(false);
        QCOMPARE(process->availability(), Autostart::Availability::Unsupported);
        QVERIFY(!process->enabled());
#endif
    }

    void refreshMovedCopy_data()
    {
        QTest::addColumn<QString>("stored");
        QTest::addColumn<bool>("exists");
        QTest::addColumn<bool>("rewritten");
        QTest::newRow("moved portable copy") << u"\"D:\\Pobrane\\Brulion\\brulion.exe\" --hidden"_s << false << true;
        QTest::newRow("unquoted, gone") << u"D:\\Stary\\brulion.exe --hidden"_s << false << true;
        QTest::newRow("another copy that exists")
                << u"\"D:\\Pobrane\\Brulion\\brulion.exe\" --hidden"_s << true << false;
        QTest::newRow("already this one") << ExeCommand << false << false;
        QTest::newRow("not Brulion's") << u"\"D:\\Inny\\brulion.exe\" --tryb"_s << false << false;
        QTest::newRow("another program") << u"\"C:\\Windows\\notepad.exe\""_s << false << false;
        QTest::newRow("nothing") << QString() << false << false;
    }
    void refreshMovedCopy()
    {
        QFETCH(QString, stored);
        QFETCH(bool, exists);
        QFETCH(bool, rewritten);
        QSettings run(nextFile(), QSettings::IniFormat);
        QSettings approved(nextFile(), QSettings::IniFormat);
        if (!stored.isEmpty())
            run.setValue(u"Brulion"_s, stored);
        Autostart autostart(&run, &approved, Exe);
        QStringList asked;
        QCOMPARE(autostart.refresh([&](const QString &path) {
            asked.append(path);
            return exists;
        }),
                 rewritten);
        QCOMPARE(run.value(u"Brulion"_s).toString(), rewritten ? ExeCommand : stored);
        QCOMPARE(run.contains(u"Brulion"_s), !stored.isEmpty());
        if (rewritten)
            QCOMPARE(asked, QStringList{autostartProgram(stored)});
    }

    void appUserModelIds_data()
    {
        QTest::addColumn<QString>("organization");
        QTest::addColumn<QString>("expected");
        QTest::newRow("default profile") << u"Brulion"_s << u"JanuszBit.Brulion"_s;
        QTest::newRow("test profile") << u"Brulion-test-shell"_s << u"JanuszBit.Brulion.test-shell"_s;
        QTest::newRow("package check") << u"Brulion-package-check"_s << u"JanuszBit.Brulion.package-check"_s;
        QTest::newRow("empty profile name") << u"Brulion-"_s << u"JanuszBit.Brulion"_s;
        QTest::newRow("no names set") << QString() << u"JanuszBit.Brulion"_s;
        QTest::newRow("another organisation") << u"Żółw"_s << u"JanuszBit.Brulion"_s;
    }
    void appUserModelIds()
    {
        QFETCH(QString, organization);
        QFETCH(QString, expected);
        QCOMPARE(appUserModelId(organization), expected);
    }

    void registration()
    {
        const QString file = nextFile();
        {
            QSettings classes(file, QSettings::IniFormat);
            QVERIFY(registerAppUserModelId(classes, u"JanuszBit.Brulion"_s, u"Brulion"_s,
                                           u"C:\\Users\\Łucja Żółć\\Brulion\\brulion.ico"_s));
            QCOMPARE(classes.value(u"JanuszBit.Brulion/DisplayName"_s).toString(), u"Brulion"_s);
            QCOMPARE(classes.value(u"JanuszBit.Brulion/IconUri"_s).toString(),
                     u"C:\\Users\\Łucja Żółć\\Brulion\\brulion.ico"_s);
            // The portable copy moved: the new icon path; a build without the icon keeps the old one.
            QVERIFY(registerAppUserModelId(classes, u"JanuszBit.Brulion"_s, u"Brulion"_s,
                                           u"E:\\Brulion\\brulion.ico"_s));
            QVERIFY(registerAppUserModelId(classes, u"JanuszBit.Brulion"_s, u"Brulion"_s, QString()));
            // A test profile's id is another key.
            QVERIFY(registerAppUserModelId(classes, u"JanuszBit.Brulion.test-shell"_s, u"Brulion (test-shell)"_s,
                                           QString()));
        }
        QSettings reread(file, QSettings::IniFormat);
        QCOMPARE(reread.value(u"JanuszBit.Brulion/IconUri"_s).toString(), u"E:\\Brulion\\brulion.ico"_s);
        QCOMPARE(reread.value(u"JanuszBit.Brulion.test-shell/DisplayName"_s).toString(), u"Brulion (test-shell)"_s);
        QVERIFY(!reread.contains(u"JanuszBit.Brulion.test-shell/IconUri"_s));
    }
};

QTEST_GUILESS_MAIN(TestAutostart)
#include "tst_autostart.moc"
