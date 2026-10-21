// AppSettings: defaults, validation, persistence in an INI file, recent vaults, the Nextcloud
// app password kept outside QSettings in an owner-only file, and the Nextcloud switch and server
// folder of each notebook: keyed by the notebook folder's id, the folder proposed for a notebook,
// folders another notebook syncs with, and the settings of 0.3.0 and older going to the notebook
// they belong to.
#include "appsettings.h"
#include "workspacesync.h"

#include "text/text.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::AppSettings;

namespace {

constexpr auto kServer = "https://chmura.example.pl";

/// What the sync bookkeeping of a notebook synced with this folder of jan's account records.
QString remoteOf(const QString &folder)
{
    return u"jan@https://chmura.example.pl/remote.php/dav/files/jan/"_s + folder;
}

} // namespace

class TestSettings : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString nextFile() { return m_tmp.filePath(u"s%1.ini"_s.arg(++m_n)); }

    /// Settings as 0.3.0 and older wrote them: one switch and one folder for every notebook.
    QString oldSettings(const QString &current, const QVariant &enabled = true, const QVariant &folder = u"Notatki"_s)
    {
        const QString file = nextFile();
        QSettings raw(file, QSettings::IniFormat);
        if (enabled.isValid())
            raw.setValue(u"sync/nextcloud/enabled"_s, enabled);
        if (folder.isValid())
            raw.setValue(u"sync/nextcloud/folder"_s, folder);
        raw.setValue(u"sync/nextcloud/url"_s, QString::fromLatin1(kServer));
        raw.setValue(u"sync/nextcloud/user"_s, u"jan"_s);
        if (!current.isEmpty())
            raw.setValue(u"vaults/current"_s, current);
        return file;
    }

    /// A notebook folder that exists.
    QString notebook(const QString &name)
    {
        const QString path = QDir::cleanPath(m_tmp.filePath(name));
        QDir().mkpath(path);
        return path;
    }

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
            s.setCurrentVault(u"/home/łucja/Notatki"_s);
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

    void nextcloudNeedsANotebook()
    {
        // With no notebook open (the start screen) nothing syncs and nothing can be switched on;
        // the settings of 0.3.0 still waiting for their notebook are not read either.
        const QString file = oldSettings(QString());
        AppSettings s(file, m_tmp.path());
        QVERIFY(s.currentVault().isEmpty());
        QVERIFY(!s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Brulion"_s);
        QSignalSpy changed(&s, &AppSettings::nextcloudChanged);
        s.setNextcloudEnabled(true);
        s.setNextcloudFolder(u"Gdzie indziej"_s);
        QVERIFY(changed.isEmpty());
        QVERIFY(!s.nextcloudEnabled());
        s.sync();
        QSettings raw(file, QSettings::IniFormat);
        QCOMPARE(raw.value(u"sync/nextcloud/enabled"_s).toBool(), true);
        QCOMPARE(raw.value(u"sync/nextcloud/folder"_s).toString(), u"Notatki"_s);
        QVERIFY(raw.childGroups().contains(u"sync"_s));
        raw.beginGroup(u"sync/nextcloud/notebooks"_s);
        QVERIFY(raw.childGroups().isEmpty());
    }

    void sharedNextcloudWaitsForItsNotebook()
    {
        // 0.3.0 synced notebook A. At the first start A is missing (an unmounted card), and the
        // user opens C: C must not take A's server folder (their notes would mix there). A gets
        // it back when it returns.
        const QString a = u"/media/karta/Notatnik A"_s;
        AppSettings s(oldSettings(a), m_tmp.path());
        QVERIFY(s.hasSharedNextcloud());
        s.recordSharedNextcloudOwner(s.currentVault()); // what AppController does at start
        QVERIFY(!s.nextcloudEnabled()); // nobody owns it yet: nothing syncs by accident

        const QString c = notebook(u"C-nowy"_s);
        s.setCurrentVault(c);
        s.notebookOpened(c, QString());
        QVERIFY(!s.nextcloudEnabled());
        QVERIFY(s.hasSharedNextcloud());
        QVERIFY(!s.hasOwnNextcloudFolder());
        // Started again later: the owner stays the one recorded first.
        s.recordSharedNextcloudOwner(c);
        s.notebookOpened(c, QString());
        QVERIFY(!s.nextcloudEnabled());

        s.setCurrentVault(a);
        QSignalSpy changed(&s, &AppSettings::nextcloudChanged);
        s.notebookOpened(a, QString());
        QCOMPARE(changed.size(), 1);
        QVERIFY(s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
        QVERIFY(!s.hasSharedNextcloud());

        // Taken over once: C, and any notebook after it, stay without sync.
        s.setCurrentVault(c);
        s.notebookOpened(c, remoteOf(u"Notatki"_s));
        QVERIFY(!s.nextcloudEnabled());
    }

    void sharedNextcloudGoesToTheSyncedNotebook_data()
    {
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("remote");
        QTest::addColumn<bool>("claims");
        QTest::newRow("its bookkeeping names the folder") << u"Notatki"_s << remoteOf(u"Notatki"_s) << true;
        QTest::newRow("diacritics") << u"Zażółć gęślą"_s
                                    << remoteOf(u"Za%C5%BC%C3%B3%C5%82%C4%87%20g%C4%99%C5%9Bl%C4%85"_s) << true;
        QTest::newRow("no folder: Brulion") << QString() << remoteOf(u"Brulion"_s) << true;
        QTest::newRow("another folder") << u"Notatki"_s << remoteOf(u"Notatki/Praca"_s) << false;
        QTest::newRow("another user") << u"Notatki"_s
                                      << u"ola@https://chmura.example.pl/remote.php/dav/files/ola/Notatki"_s << false;
        QTest::newRow("another server") << u"Notatki"_s
                                        << u"jan@https://inna.example.pl/remote.php/dav/files/jan/Notatki"_s << false;
        QTest::newRow("never synced") << u"Notatki"_s << QString() << false;
    }
    void sharedNextcloudGoesToTheSyncedNotebook()
    {
        // 0.3.0 users who quit in the documentation have no notebook to record: the notebook
        // whose sync bookkeeping names exactly the old account and folder is the synced one.
        QFETCH(QString, folder);
        QFETCH(QString, remote);
        QFETCH(bool, claims);
        AppSettings s(oldSettings(QString(), true, folder.isEmpty() ? QVariant() : QVariant(folder)), m_tmp.path());
        s.recordSharedNextcloudOwner(QString());
        const QString n = notebook(u"zsynchronizowany-%1"_s.arg(m_n));
        s.setCurrentVault(n);
        s.notebookOpened(n, remote);
        QCOMPARE(s.nextcloudEnabled(), claims);
        QCOMPARE(s.hasSharedNextcloud(), !claims);
        if (claims) {
            // Its folder is written, also the default one: it is never offered another folder.
            QVERIFY(s.hasOwnNextcloudFolder());
            QCOMPARE(s.nextcloudFolder(), folder.isEmpty() ? u"Brulion"_s : folder);
            QCOMPARE(s.proposedNextcloudFolder(), s.nextcloudFolder());
        }
    }

    void sampleNotebookTakesItsSyncBack()
    {
        // Whoever used the sample notebook as their notebook (it was current, its bookkeeping
        // names the folder) keeps syncing it; the documentation is never offered the settings
        // (AppController does not call notebookOpened() for it).
        const QString sample = notebook(u"data/Przykładowy brulion"_s);
        AppSettings s(oldSettings(sample), m_tmp.path());
        s.recordSharedNextcloudOwner(sample);
        s.setCurrentVault(sample);
        s.notebookOpened(sample, QString());
        QVERIFY(s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
    }

    void sharedFolderAlreadyTaken()
    {
        // Meanwhile another notebook here syncs with the old folder: the owner gets its folder,
        // but sync stays off — two notebooks never sync with one folder.
        const QString a = u"/media/karta/A"_s;
        AppSettings s(oldSettings(a), m_tmp.path());
        s.recordSharedNextcloudOwner(a);
        const QString c = notebook(u"C-zajęty"_s);
        s.setCurrentVault(c);
        s.setNextcloudFolder(u"notatki/"_s);
        s.setNextcloudEnabled(true);
        s.setCurrentVault(a);
        s.notebookOpened(a, QString());
        QVERIFY(!s.hasSharedNextcloud());
        QVERIFY(!s.nextcloudEnabled());
        QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
    }

    void notebookKeepsTheFolderItSyncedWith_data()
    {
        QTest::addColumn<QString>("remote");
        QTest::addColumn<QString>("folder");
        QTest::newRow("this account") << remoteOf(u"Notatki"_s) << u"Notatki"_s;
        QTest::newRow("nested, diacritics") << remoteOf(u"Dom/Pi%C4%99tro"_s) << u"Dom/Piętro"_s;
        QTest::newRow("another account") << u"ola@https://chmura.example.pl/remote.php/dav/files/ola/Notatki"_s
                                         << QString();
        QTest::newRow("malformed") << u"jan@"_s << QString();
        QTest::newRow("never synced") << QString() << QString();
    }
    void notebookKeepsTheFolderItSyncedWith()
    {
        // The notebook synced in 0.3.0 whose settings 0.3.1–0.3.5 gave to another notebook: when
        // the user turns sync on again, the form offers its old folder, not a new one.
        QFETCH(QString, remote);
        QFETCH(QString, folder);
        AppSettings s(nextFile(), m_tmp.path());
        s.setNextcloudUrl(QString::fromLatin1(kServer));
        s.setNextcloudUser(u"jan"_s);
        const QString n = notebook(u"dawny-%1"_s.arg(m_n));
        s.setCurrentVault(n);
        s.notebookOpened(n, remote);
        QVERIFY(!s.nextcloudEnabled());
        QCOMPARE(s.hasOwnNextcloudFolder(), !folder.isEmpty());
        QCOMPARE(s.proposedNextcloudFolder(), folder.isEmpty() ? QFileInfo(n).fileName() : folder);
        // A folder of its own is never replaced.
        s.setNextcloudFolder(u"Wybrany"_s);
        s.notebookOpened(n, remoteOf(u"Notatki"_s));
        QCOMPARE(s.nextcloudFolder(), u"Wybrany"_s);
    }

    void nextcloudRemote_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("user");
        QTest::addColumn<QString>("folder");
        QTest::addColumn<QString>("remote");
        QTest::newRow("plain") << u"https://chmura.example.pl"_s << u"jan"_s << u"Notatki"_s << remoteOf(u"Notatki"_s);
        QTest::newRow("no folder") << u"chmura.example.pl/"_s << u" jan "_s << QString() << remoteOf(u"Brulion"_s);
        QTest::newRow("slashes") << u"https://chmura.example.pl"_s << u"jan"_s << u"/Notatki/Dom/"_s
                                 << remoteOf(u"Notatki/Dom"_s);
        QTest::newRow("diacritics")
                << u"https://example.pl/nextcloud/"_s << u"Łucja"_s << u"Zażółć"_s
                << u"Łucja@https://example.pl/nextcloud/remote.php/dav/files/%C5%81ucja/Za%C5%BC%C3%B3%C5%82%C4%87"_s;
        QTest::newRow("no server") << QString() << u"jan"_s << u"Notatki"_s << QString();
    }
    void nextcloudRemote()
    {
        QFETCH(QString, url);
        QFETCH(QString, user);
        QFETCH(QString, folder);
        QFETCH(QString, remote);
        QCOMPARE(brulion::shell::nextcloudRemote(url, user, folder), remote);
        if (!remote.isEmpty()) {
            const QString back = brulion::shell::nextcloudRemoteFolder(url, user, remote);
            QCOMPARE(back,
                     folder.isEmpty() ? u"Brulion"_s : QDir::cleanPath(folder).mid(folder.startsWith(u'/') ? 1 : 0));
        }
    }

    void notebookSettingsFollowTheFolder()
    {
        // One folder reached through a symlink or a relative path is one notebook: one switch
        // and folder (as it has one sync bookkeeping). Settings 0.3.1–0.3.5 wrote under the path
        // as written move to the folder's id.
        const QString real = notebook(u"Dropbox/Notatnik źródłowy"_s);
        const QString link = QDir::cleanPath(m_tmp.filePath(u"Notatnik-skrót"_s));
        QVERIFY(QFile::link(real, link));
        const QString file = nextFile();
        const QString legacy = u"sync/nextcloud/notebooks/"_s + brulion::text::fnv1a64Hex(link.toUtf8());
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(legacy + u"/enabled"_s, true);
            raw.setValue(legacy + u"/folder"_s, u"Notatki"_s);
            raw.setValue(u"vaults/current"_s, link);
        }
        {
            AppSettings s(file, m_tmp.path());
            QVERIFY(s.nextcloudEnabled());
            QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
            s.setCurrentVault(real);
            QVERIFY(s.nextcloudEnabled());
            QCOMPARE(s.nextcloudFolder(), u"Notatki"_s);
            const QString cwd = QDir::currentPath();
            QVERIFY(QDir::setCurrent(m_tmp.path()));
            s.setCurrentVault(u"Dropbox/Notatnik źródłowy"_s);
            const bool relativeEnabled = s.nextcloudEnabled();
            QDir::setCurrent(cwd);
            QVERIFY(relativeEnabled);
            // Changed through one path, seen through the other.
            s.setCurrentVault(link);
            s.setNextcloudFolder(u"Notatki/Nowe"_s);
            s.setCurrentVault(real);
            QCOMPARE(s.nextcloudFolder(), u"Notatki/Nowe"_s);
        }
        QSettings raw(file, QSettings::IniFormat);
        QVERIFY(!raw.contains(legacy + u"/enabled"_s)); // moved
        raw.beginGroup(u"sync/nextcloud/notebooks"_s);
        const QStringList groups = raw.childGroups();
        QCOMPARE(groups.size(), 1);
        QCOMPARE(raw.value(groups.first() + u"/path"_s).toString(), real); // the last path written
    }

    void proposedFolder_data()
    {
        QTest::addColumn<bool>("account");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QStringList>("others");
        QTest::addColumn<QString>("shared");
        QTest::addColumn<QString>("expected");
        const QStringList none;
        QTest::newRow("new account") << false << u"Praca"_s << none << QString() << u"Notatki"_s;
        QTest::newRow("new account, Notatki taken")
                << false << u"Praca"_s << QStringList{u"Notatki"_s} << QString() << u"Notatki 2"_s;
        QTest::newRow("the notebook's name")
                << true << u"Praca"_s << QStringList{u"Notatki"_s} << QString() << u"Praca"_s;
        QTest::newRow("diacritics") << true << u"Zażółć gęślą jaźń"_s << none << QString() << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("characters a folder cannot have")
                << true << u"Plan: 2026*27?"_s << none << QString() << u"Plan 2026 27"_s;
        QTest::newRow("dots and spaces around") << true << u" .notatki. "_s << none << QString() << u"notatki"_s;
        QTest::newRow("nothing left") << true << u"..."_s << none << QString() << u"Notatki"_s;
        QTest::newRow("taken") << true << u"Praca"_s << QStringList{u"Praca"_s} << QString() << u"Praca 2"_s;
        QTest::newRow("taken, other case and slashes")
                << true << u"Praca"_s << QStringList{u"/praca/"_s} << QString() << u"Praca 2"_s;
        QTest::newRow("taken twice") << true << u"Praca"_s << QStringList{u"Praca"_s, u"Praca 2"_s} << QString()
                                     << u"Praca 3"_s;
        QTest::newRow("holds another notebook's folder")
                << true << u"Praca"_s << QStringList{u"Praca/Archiwum"_s} << QString() << u"Praca 2"_s;
        QTest::newRow("another notebook's default folder")
                << true << u"Brulion"_s << QStringList{QString()} << QString() << u"Brulion 2"_s;
        QTest::newRow("the old folder waiting for its notebook")
                << true << u"Notatki"_s << none << u"Notatki"_s << u"Notatki 2"_s;
    }
    void proposedFolder()
    {
        // A further notebook must not be offered a server folder another notebook has: the
        // user would accept it and both notebooks would mix there.
        QFETCH(bool, account);
        QFETCH(QString, name);
        QFETCH(QStringList, others);
        QFETCH(QString, shared);
        QFETCH(QString, expected);
        const QString file = shared.isEmpty() ? nextFile() : oldSettings(u"/media/karta/A"_s, true, shared);
        AppSettings s(file, m_tmp.path());
        if (account)
            s.setNextcloudUrl(QString::fromLatin1(kServer));
        else
            s.setNextcloudUrl(QString());
        for (qsizetype i = 0; i < others.size(); ++i) {
            s.setCurrentVault(m_tmp.filePath(u"inne-%1-%2"_s.arg(m_n).arg(i)));
            s.setNextcloudFolder(others.at(i)); // synced or not: the folder is that notebook's
        }
        s.setCurrentVault(m_tmp.filePath(u"propozycja-%1/%2"_s.arg(m_n).arg(name)));
        QVERIFY(!s.hasOwnNextcloudFolder());
        QCOMPARE(s.proposedNextcloudFolder(), expected);
        // Its own folder once it has one.
        s.setNextcloudFolder(u"Mój"_s);
        QCOMPARE(s.proposedNextcloudFolder(), u"Mój"_s);
    }

    void folderUsedByAnotherNotebook_data()
    {
        QTest::addColumn<QString>("theirs");
        QTest::addColumn<bool>("theirsOn");
        QTest::addColumn<bool>("theirsExists");
        QTest::addColumn<QString>("mine");
        QTest::addColumn<bool>("conflict");
        QTest::newRow("same folder") << u"Notatki"_s << true << true << u"Notatki"_s << true;
        QTest::newRow("other case, slashes") << u"Notatki"_s << true << true << u"/notatki/"_s << true;
        QTest::newRow("diacritics") << u"Zażółć"_s << true << true << u"ZAŻÓŁĆ"_s << true;
        QTest::newRow("inside theirs") << u"Notatki"_s << true << true << u"Notatki/Praca"_s << true;
        QTest::newRow("around theirs") << u"Notatki/Dom"_s << true << true << u"Notatki"_s << true;
        QTest::newRow("their default") << QString() << true << true << u"Brulion"_s << true;
        QTest::newRow("a similar name") << u"Notatki"_s << true << true << u"Notatki 2"_s << false;
        QTest::newRow("theirs is off") << u"Notatki"_s << false << true << u"Notatki"_s << false;
        QTest::newRow("their folder is gone") << u"Notatki"_s << true << false << u"Notatki"_s << false;
    }
    void folderUsedByAnotherNotebook()
    {
        QFETCH(QString, theirs);
        QFETCH(bool, theirsOn);
        QFETCH(bool, theirsExists);
        QFETCH(QString, mine);
        QFETCH(bool, conflict);
        AppSettings s(nextFile(), m_tmp.path());
        const QString other = m_tmp.filePath(u"ich-%1"_s.arg(m_n));
        if (theirsExists)
            QDir().mkpath(other);
        s.setCurrentVault(other);
        if (!theirs.isEmpty())
            s.setNextcloudFolder(theirs);
        s.setNextcloudEnabled(theirsOn);
        const QString me = notebook(u"mój-%1"_s.arg(m_n));
        s.setCurrentVault(me);
        QCOMPARE(s.nextcloudFolderUser(mine), conflict ? QDir::cleanPath(other) : QString());
        // Never itself, also through another path.
        s.setCurrentVault(other);
        QCOMPARE(s.nextcloudFolderUser(mine), QString());
        QCOMPARE(s.nextcloudFolderUser(mine, me), conflict ? QDir::cleanPath(other) : QString());
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
