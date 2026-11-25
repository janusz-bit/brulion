// Secrets handed to AI agents as files (secretdrop/secretdrop.h): the file holds the value byte for
// byte (Polish letters, no line end) and only its owner may read it (0600), in this run's folder
// (0700) under a private base (0700); its owner's discard deletes it, its time deletes it (expired),
// the drop's end deletes the run's folder and its lock; sweep() removes the folders runs that ended
// left (no lock, a free one), never one a live run holds; a base that is a link is refused; while a
// value lives — a file, or held without one — the event log's redaction masks it, and not after.
#include "secretdrop/secretdrop.h"

#include "log/redact.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <cstdlib>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::secretdrop;

namespace {

constexpr QFileDevice::Permissions Owner = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
constexpr QFileDevice::Permissions OwnerFolder = Owner | QFileDevice::ExeOwner;
constexpr QFileDevice::Permissions Others = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
                                            | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

QByteArray contents(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<none>");
}

} // namespace

class TestSecretDrop : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init() { logging::clearSecrets(); }

    void writesAnOwnerOnlyFile()
    {
        QTemporaryDir home;
        const QString base = home.filePath(u"agent-secrets"_s);
        Drop drop(base);
        QVERIFY(drop.folder().isEmpty());
        const QString value = u"ghp_Zażółć-gęślą-jaźń-1234"_s;
        QString error;
        const QString path = drop.write(u"AI/Rozmowa.md"_s, value, MaxLifetimeMs, &error);
        QVERIFY2(!path.isEmpty(), qPrintable(error));
        QVERIFY(error.isEmpty());
        QCOMPARE(contents(path), value.toUtf8()); // no line end: "$(cat file)" is the value
        QVERIFY(QFileInfo(path).isAbsolute());
        QVERIFY(path.startsWith(drop.folder() + u'/'));
        QVERIFY(drop.folder().startsWith(QDir::cleanPath(base) + u"/run-"_s));
#ifdef Q_OS_UNIX
        QCOMPARE(QFileInfo(path).permissions() & (Owner | Others | QFileDevice::ExeOwner), Owner);
        QCOMPARE(QFileInfo(drop.folder()).permissions() & (OwnerFolder | Others), OwnerFolder);
        QCOMPARE(QFileInfo(base).permissions() & (OwnerFolder | Others), OwnerFolder);
#endif
        QVERIFY(QFileInfo::exists(drop.folder() + u".lock"_s));
        QCOMPARE(drop.files(u"AI/Rozmowa.md"_s), QStringList{path});
        QVERIFY(drop.holds(u"AI/Rozmowa.md"_s));
        QCOMPARE(drop.owners(), QStringList{u"AI/Rozmowa.md"_s});
        // A second one is another file.
        const QString second = drop.write(u"AI/Rozmowa.md"_s, u"1234"_s);
        QVERIFY(!second.isEmpty() && second != path);
        QCOMPARE(drop.files(u"AI/Rozmowa.md"_s).size(), 2);
    }

    void discardedByItsOwner()
    {
        QTemporaryDir home;
        Drop drop(home.filePath(u"s"_s));
        const QString a = drop.write(u"AI/A.md"_s, u"sekret-a"_s);
        const QString b = drop.write(u"AI/B.md"_s, u"sekret-b"_s);
        QVERIFY(QFile::exists(a) && QFile::exists(b));
        QCOMPARE(drop.discard(u"AI/A.md"_s), 1);
        QVERIFY(!QFile::exists(a));
        QVERIFY(QFile::exists(b));
        QVERIFY(!drop.holds(u"AI/A.md"_s));
        QCOMPARE(drop.discard(u"AI/A.md"_s), 0);
        drop.discardAll();
        QVERIFY(!QFile::exists(b));
        QVERIFY(drop.owners().isEmpty());
    }

    void expiresInItsTime()
    {
        QTemporaryDir home;
        Drop drop(home.filePath(u"s"_s));
        QSignalSpy expired(&drop, &Drop::expired);
        const QString soon = drop.write(u"AI/A.md"_s, u"krótko"_s, 50);
        const QString later = drop.write(u"AI/A.md"_s, u"dłużej"_s, 60000);
        QTRY_COMPARE(expired.size(), 1);
        QCOMPARE(expired.at(0).at(0).toString(), u"AI/A.md"_s);
        QCOMPARE(expired.at(0).at(1).toString(), soon);
        QVERIFY(!QFile::exists(soon));
        QVERIFY(QFile::exists(later));
        // Never longer than the longest lifetime, whatever is asked.
        const QString capped = drop.write(u"AI/B.md"_s, u"x1234"_s, MaxLifetimeMs * 10);
        QVERIFY(QFile::exists(capped));
        drop.hold(u"AI/C.md"_s, u"trzymane"_s, 30);
        QTRY_COMPARE(expired.size(), 2);
        QCOMPARE(expired.at(1).at(1).toString(), QString());
        QVERIFY(!drop.holds(u"AI/C.md"_s));
    }

    void endsWithTheRun()
    {
        QTemporaryDir home;
        const QString base = home.filePath(u"s"_s);
        QString folder;
        QString path;
        {
            Drop drop(base);
            path = drop.write(u"AI/A.md"_s, u"wartość"_s);
            folder = drop.folder();
            QVERIFY(QFile::exists(path));
        }
        QVERIFY(!QFile::exists(path));
        QVERIFY(!QFileInfo::exists(folder));
        QVERIFY(!QFileInfo::exists(folder + u".lock"_s));
    }

    void sweepsLeftovers()
    {
        QTemporaryDir home;
        const QString base = home.filePath(u"s"_s);
        QVERIFY(QDir().mkpath(base + u"/run-dead1"_s));
        QFile left(base + u"/run-dead1/secret-1"_s);
        QVERIFY(left.open(QIODevice::WriteOnly));
        left.write("stary sekret");
        left.close();
        // A run that crashed: its folder, its file and its lock (of a process that is gone).
        QProcess crash;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(u"SECRETDROP_CRASH_BASE"_s, base);
        crash.setProcessEnvironment(env);
        crash.start(QCoreApplication::applicationFilePath(), {});
        QVERIFY(crash.waitForFinished(60000));
        const QString crashed = QString::fromUtf8(crash.readAllStandardOutput()).trimmed();
        QVERIFY2(QFile::exists(crashed), qPrintable(crashed));
        const QString crashedFolder = QFileInfo(crashed).absolutePath();
        QVERIFY(QFileInfo::exists(crashedFolder + u".lock"_s));
        // A live run (its lock held: here by this process) keeps its folder.
        QVERIFY(QDir().mkpath(base + u"/run-live"_s));
        QLockFile live(base + u"/run-live.lock"_s);
        live.setStaleLockTime(0);
        QVERIFY(live.tryLock(0));
        // Things that are no run folders stay.
        QVERIFY(QDir().mkpath(base + u"/inne"_s));
        // And this run's own.
        Drop drop(base);
        const QString mine = drop.write(u"AI/A.md"_s, u"mój"_s);

        QCOMPARE(Drop::sweep(base), 2);
        QVERIFY(!QFileInfo::exists(base + u"/run-dead1"_s));
        QVERIFY(!QFileInfo::exists(base + u"/run-dead1.lock"_s));
        QVERIFY(!QFileInfo::exists(crashed));
        QVERIFY(!QFileInfo::exists(crashedFolder));
        QVERIFY(!QFileInfo::exists(crashedFolder + u".lock"_s));
        QVERIFY(QFileInfo::exists(base + u"/run-live"_s));
        QVERIFY(QFileInfo::exists(base + u"/inne"_s));
        QVERIFY(QFile::exists(mine));
        live.unlock();
        QCOMPARE(Drop::sweep(base), 1);
        QVERIFY(!QFileInfo::exists(base + u"/run-live"_s));
        QVERIFY(QFile::exists(mine));
        // Nothing to sweep, or no such folder.
        QCOMPARE(Drop::sweep(home.filePath(u"brak"_s)), 0);
        QCOMPARE(Drop::sweep(QString()), 0);
    }

    void refusesWhatIsNotPrivate()
    {
        Drop none{QString()};
        QString error;
        QVERIFY(none.write(u"AI/A.md"_s, u"x1234"_s, 1000, &error).isEmpty());
        QVERIFY(!error.isEmpty());
#ifdef Q_OS_UNIX
        QTemporaryDir home;
        QVERIFY(QDir().mkpath(home.filePath(u"real"_s)));
        QVERIFY(QFile::link(home.filePath(u"real"_s), home.filePath(u"link"_s)));
        Drop linked(home.filePath(u"link"_s));
        QVERIFY(linked.write(u"AI/A.md"_s, u"x1234"_s, 1000, &error).isEmpty());
        QVERIFY(error.contains(u"link"_s));
        QVERIFY(QDir(home.filePath(u"real"_s)).isEmpty());
        QCOMPARE(Drop::sweep(home.filePath(u"link"_s)), 0);
#endif
    }

    void maskedWhileItLives()
    {
        QTemporaryDir home;
        Drop drop(home.filePath(u"s"_s));
        const QString value = u"Hasło-Żółw-7731"_s;
        const QString line = u"agent ran: deploy --token %1"_s.arg(value);
        QCOMPARE(logging::redact(line), line);
        drop.write(u"AI/A.md"_s, value);
        QVERIFY(!logging::redact(line).contains(value));
        drop.hold(u"AI/B.md"_s, u"Inny-Sekret-99"_s);
        QVERIFY(!logging::redact(u"x Inny-Sekret-99"_s).contains(u"Inny-Sekret-99"_s));
        drop.discard(u"AI/A.md"_s);
        QCOMPARE(logging::redact(line), line);
        drop.discardAll();
        QCOMPARE(logging::secretCount(), 0);
    }
};

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    // sweepsLeftovers()' crashed run: a drop that writes a file and ends without a word, as a crash
    // leaves it (its folder, its file and its lock stay).
    if (const QString base = qEnvironmentVariable("SECRETDROP_CRASH_BASE"); !base.isEmpty()) {
        auto *drop = new Drop(base);
        const QByteArray path = drop->write(u"AI/A.md"_s, u"zostawiony"_s).toUtf8() + '\n';
        std::fwrite(path.constData(), 1, size_t(path.size()), stdout);
        std::fflush(stdout);
        std::_Exit(0);
    }
    TestSecretDrop test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_secretdrop.moc"
