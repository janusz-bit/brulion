// AppSettings: defaults, validation, persistence in an INI file, recent vaults and the Nextcloud
// app password kept outside QSettings in an owner-only file.
#include "appsettings.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::AppSettings;

class TestSettings : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString nextFile() { return m_tmp.filePath(u"s%1.ini"_s.arg(++m_n)); }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void defaults()
    {
        AppSettings s(nextFile(), m_tmp.path());
        QCOMPARE(s.themeMode(), u"system"_s);
        QVERIFY(s.language() == u"pl" || s.language() == u"en");
        QCOMPARE(s.fontScale(), 1.0);
        QCOMPARE(s.editorFont(), u"serif"_s);
        QCOMPARE(s.editorFontSize(), 16);
        QCOMPARE(s.editorWidth(), 760);
        QVERIFY(!s.editorSourceMode());
        QVERIFY(s.currentVault().isEmpty());
        QVERIFY(s.recentVaults().isEmpty());
        QVERIFY(s.closeToTray());
        QVERIFY(s.remindersEnabled());
        QCOMPARE(s.snoozeMinutes(), 10);
        QCOMPARE(s.alarmRingMinutes(), 10);
        QVERIFY(!s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Brulion"_s);
        QCOMPARE(s.syncIntervalMinutes(), 15);
        QVERIFY(!s.hasNextcloudPassword());
    }

    void persistence()
    {
        const QString file = nextFile();
        {
            AppSettings s(file, m_tmp.path());
            s.setThemeMode(u"dark"_s);
            s.setLanguage(u"en"_s);
            s.setFontScale(1.15);
            s.setReducedMotion(true);
            s.setEditorFont(u"sans"_s);
            s.setEditorFontSize(19);
            s.setEditorWidth(0);
            s.setEditorSourceMode(true);
            s.setCloseToTray(false);
            s.setRemindersEnabled(false);
            s.setSnoozeMinutes(15);
            s.setAlarmRingMinutes(5);
            s.setNotifyMissed(false);
            s.setNextcloudEnabled(true);
            s.setNextcloudUrl(u"https://chmura.example.pl/"_s);
            s.setNextcloudUser(u" Łucja "_s);
            s.setNextcloudFolder(u"/Notatki/Brulion/"_s);
            s.setSyncIntervalMinutes(30);
            s.setValue(u"views/tasks/showDone"_s, true);
        }
        AppSettings s(file, m_tmp.path());
        QCOMPARE(s.themeMode(), u"dark"_s);
        QCOMPARE(s.language(), u"en"_s);
        QCOMPARE(s.fontScale(), 1.15);
        QVERIFY(s.reducedMotion());
        QCOMPARE(s.editorFont(), u"sans"_s);
        QCOMPARE(s.editorFontSize(), 19);
        QCOMPARE(s.editorWidth(), 0); // full width
        QVERIFY(s.editorSourceMode());
        QVERIFY(!s.closeToTray());
        QVERIFY(!s.remindersEnabled());
        QCOMPARE(s.snoozeMinutes(), 15);
        QCOMPARE(s.alarmRingMinutes(), 5);
        QVERIFY(!s.notifyMissed());
        QVERIFY(s.nextcloudEnabled());
        QCOMPARE(s.nextcloudUrl(), u"https://chmura.example.pl"_s);
        QCOMPARE(s.nextcloudUser(), u"Łucja"_s);
        QCOMPARE(s.nextcloudFolder(), u"Notatki/Brulion"_s);
        QCOMPARE(s.syncIntervalMinutes(), 30);
        QCOMPARE(s.value(u"views/tasks/showDone"_s).toBool(), true);
        QCOMPARE(s.value(u"views/missing"_s, 7).toInt(), 7);
    }

    void nextcloudBelongsToNotebook()
    {
        // Syncing and the server folder are the notebook's: a second notebook (or the
        // documentation) must not inherit them, or its notes would flow into the first one.
        AppSettings s(nextFile(), m_tmp.path());
        s.setCurrentVault(u"/notes/a"_s);
        s.setNextcloudUrl(u"https://chmura.example.pl"_s);
        s.setNextcloudEnabled(true);
        s.setNextcloudFolder(u"Notatki"_s);
        QVERIFY(s.nextcloudEnabled());
        QSignalSpy changed(&s, &AppSettings::nextcloudChanged);
        s.setCurrentVault(u"/notes/b"_s);
        QVERIFY(!changed.isEmpty());
        QVERIFY(!s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Brulion"_s);
        QCOMPARE(s.nextcloudUrl(), u"https://chmura.example.pl"_s); // the account is shared
        s.setNextcloudFolder(u"Zażółć"_s);
        s.setCurrentVault(u"/notes/a"_s);
        QVERIFY(s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
        s.setCurrentVault(u"/notes/b"_s);
        QCOMPARE(s.nextcloudFolder(), u"Zażółć"_s);
        QVERIFY(!s.nextcloudEnabled());
    }

    void sharedNextcloudGoesToTheFirstNotebook()
    {
        // Settings written by 0.3.0 and older (one switch and folder for all notebooks).
        const QString file = nextFile();
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(u"sync/nextcloud/enabled"_s, true);
            raw.setValue(u"sync/nextcloud/folder"_s, u"Notatki"_s);
            raw.setValue(u"sync/nextcloud/url"_s, u"https://chmura.example.pl"_s);
            raw.setValue(u"vaults/current"_s, u"/notes/a"_s);
        }
        AppSettings s(file, m_tmp.path());
        QVERIFY(!s.nextcloudEnabled()); // nobody owns it yet: nothing syncs by accident
        s.claimSharedNextcloud(u"/notes/a"_s);
        QVERIFY(s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
        s.claimSharedNextcloud(u"/notes/b"_s); // once only
        s.setCurrentVault(u"/notes/b"_s);
        QVERIFY(!s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Brulion"_s);
        s.setCurrentVault(u"/notes/a"_s);
        QVERIFY(s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
    }

    void validation()
    {
        AppSettings s(nextFile(), m_tmp.path());
        s.setThemeMode(u"purple"_s);
        QCOMPARE(s.themeMode(), u"system"_s);
        s.setLanguage(u"de"_s);
        QCOMPARE(s.language(), u"pl"_s);
        s.setFontScale(5);
        QCOMPARE(s.fontScale(), 1.4);
        s.setFontScale(0.1);
        QCOMPARE(s.fontScale(), 0.85);
        s.setEditorFont(u"comic"_s);
        QCOMPARE(s.editorFont(), u"serif"_s);
        s.setEditorFontSize(100);
        QCOMPARE(s.editorFontSize(), 28);
        s.setEditorWidth(100);
        QCOMPARE(s.editorWidth(), 480);
        s.setSnoozeMinutes(0);
        QCOMPARE(s.snoozeMinutes(), 1);
        s.setAlarmRingMinutes(600);
        QCOMPARE(s.alarmRingMinutes(), 60);
        s.setSyncIntervalMinutes(-5);
        QCOMPARE(s.syncIntervalMinutes(), 0);
    }

    void genericValuesKeepTheirType()
    {
        // INI files store everything as text: after a restart other areas' booleans and numbers
        // come back with the type of their default (QML compares them with === and does
        // arithmetic with them). The file below is what QSettings wrote in an earlier run.
        const QString file = nextFile();
        {
            QFile ini(file);
            QVERIFY(ini.open(QIODevice::WriteOnly));
            ini.write("[ui]\nsidebar\\visible=false\n\n"
                      "[views]\ntasks\\limit=25\ntasks\\zoom=1.5\ntasks\\name=Za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87\n"
                      "tasks\\bad=du\xc5\xbco\n");
        }
        AppSettings s(file, m_tmp.path());
        const QVariant visible = s.value(u"ui/sidebar/visible"_s, true);
        QCOMPARE(visible.typeId(), QMetaType::Bool);
        QCOMPARE(visible.toBool(), false);
        const QVariant limit = s.value(u"views/tasks/limit"_s, 10);
        QCOMPARE(limit.typeId(), QMetaType::Int);
        QCOMPARE(limit.toInt(), 25);
        const QVariant zoom = s.value(u"views/tasks/zoom"_s, 1.0);
        QCOMPARE(zoom.typeId(), QMetaType::Double);
        QCOMPARE(zoom.toDouble(), 1.5);
        QCOMPARE(s.value(u"views/tasks/name"_s, QString()).toString(), u"Zażółć"_s);
        // A value that does not convert falls back to the default instead of a wrong one.
        QCOMPARE(s.value(u"views/tasks/bad"_s, 10), QVariant(10));
        // Without a default: as stored.
        QCOMPARE(s.value(u"views/tasks/name"_s).toString(), u"Zażółć"_s);
        QCOMPARE(s.value(u"views/tasks/limit"_s).toString(), u"25"_s);
    }

    void signalsOnlyOnChange()
    {
        AppSettings s(nextFile(), m_tmp.path());
        QSignalSpy theme(&s, &AppSettings::themeModeChanged);
        s.setThemeMode(u"system"_s); // the default: stored, but no change
        QCOMPARE(theme.size(), 0);
        s.setThemeMode(u"light"_s);
        s.setThemeMode(u"light"_s);
        QCOMPARE(theme.size(), 1);
        QSignalSpy nc(&s, &AppSettings::nextcloudChanged);
        s.setNextcloudUrl(u"https://a.example"_s);
        s.setNextcloudUrl(u"https://a.example/"_s); // same after normalisation
        QCOMPARE(nc.size(), 1);
        QSignalSpy value(&s, &AppSettings::valueChanged);
        s.setValue(u"x/y"_s, 1);
        s.setValue(u"x/y"_s, 1);
        QCOMPARE(value.size(), 1);
    }

    void recentVaults()
    {
        AppSettings s(nextFile(), m_tmp.path());
        QSignalSpy recent(&s, &AppSettings::recentVaultsChanged);
        QSignalSpy current(&s, &AppSettings::currentVaultChanged);
        s.addRecentVault(u"/home/ala/Notatki"_s);
        s.addRecentVault(u"/home/ala/Żółta teczka/"_s);
        s.addRecentVault(u"/home/ala/Notatki"_s); // moves to the front, no duplicate
        QCOMPARE(recent.size(), 3);
        QCOMPARE(current.size(), 3);
        QVariantList list = s.recentVaults();
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).toMap().value(u"path"_s).toString(), u"/home/ala/Notatki"_s);
        QCOMPARE(list.at(1).toMap().value(u"name"_s).toString(), u"Żółta teczka"_s);
        QVERIFY(!list.at(0).toMap().value(u"lastOpened"_s).toString().isEmpty());
        QCOMPARE(s.currentVault(), u"/home/ala/Notatki"_s);

        for (int i = 0; i < 12; ++i)
            s.addRecentVault(u"/v/%1"_s.arg(i));
        QCOMPARE(s.recentVaults().size(), AppSettings::MaxRecentVaults);
        QCOMPARE(s.recentVaults().first().toMap().value(u"path"_s).toString(), u"/v/11"_s);

        s.forgetRecentVault(u"/v/11"_s);
        QCOMPARE(s.recentVaults().first().toMap().value(u"path"_s).toString(), u"/v/10"_s);
        const auto before = recent.size();
        s.forgetRecentVault(u"/not/there"_s);
        QCOMPARE(recent.size(), before);
    }

    void passwordFile()
    {
        QTemporaryDir data;
        const QString file = nextFile();
        AppSettings s(file, data.path());
        s.setNextcloudUser(u"ala"_s); // the settings file exists and could hold it
        QSignalSpy changed(&s, &AppSettings::nextcloudPasswordChanged);
        QVERIFY(s.setNextcloudPassword(u"hasło-zażółć-123"_s));
        QVERIFY(s.hasNextcloudPassword());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(s.nextcloudPassword(), u"hasło-zażółć-123"_s);
        QCOMPARE(s.passwordFile(), data.path() + u"/secrets/nextcloud-password"_s);

        // Owner-only permissions, and never in the settings file.
        const QFileDevice::Permissions perms = QFile::permissions(s.passwordFile());
        QVERIFY(perms & QFileDevice::ReadOwner);
        QVERIFY(perms & QFileDevice::WriteOwner);
        QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup
                           | QFileDevice::WriteOther)));
        const QFileDevice::Permissions dirPerms = QFile::permissions(data.path() + u"/secrets"_s);
        QVERIFY(!(dirPerms & (QFileDevice::ReadOther | QFileDevice::ExeOther | QFileDevice::ReadGroup)));
        s.sync();
        QFile ini(file);
        QVERIFY(ini.open(QIODevice::ReadOnly));
        QVERIFY(!ini.readAll().contains("123"));

        // Read back by another instance; replaced; deleted.
        AppSettings other(file, data.path());
        QCOMPARE(other.nextcloudPassword(), u"hasło-zażółć-123"_s);
        QVERIFY(s.setNextcloudPassword(u"nowe"_s));
        QCOMPARE(other.nextcloudPassword(), u"nowe"_s);
        QVERIFY(s.setNextcloudPassword(QString()));
        QVERIFY(!s.hasNextcloudPassword());
        QVERIFY(!QFile::exists(s.passwordFile()));
        QVERIFY(s.setNextcloudPassword(QString())); // deleting nothing is fine
    }
};

QTEST_GUILESS_MAIN(TestSettings)
#include "tst_settings.moc"
