// History: device-local version snapshots (dedupe, throttle, retention, prune, rename,
// several instances on one directory, clocks set back). Ported from Brulion v1.
#include "vault/history.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <functional>
#include <memory>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kMinute = 60'000;
constexpr qint64 kDay = 24 * 60 * kMinute;

struct Clock {
    QDateTime now{QDate(2026, 10, 14), QTime(9, 0)};
    std::function<QDateTime()> fn()
    {
        return [this] { return now; };
    }
    void advance(qint64 ms) { now = now.addMSecs(ms); }
};

int countFiles(const QString &dir)
{
    return static_cast<int>(QDir(dir).entryList(QDir::Files).size());
}

} // namespace

class TestHistory : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    Clock m_clock;
    std::unique_ptr<History> m_history;

    QString dir() const { return m_tmp->path() + u"/history"_s; }
    History &history() { return *m_history; }
    std::unique_ptr<History> another()
    {
        auto h = std::make_unique<History>(dir());
        h->setClock(m_clock.fn());
        return h;
    }
    QStringList contents(const VaultPath &path)
    {
        QStringList out;
        for (const HistoryEntry &e : history().list(path))
            out.append(history().read(e.id));
        return out;
    }
    QStringList reasons(const VaultPath &path)
    {
        QStringList out;
        for (const HistoryEntry &e : history().list(path))
            out.append(e.reason);
        return out;
    }
    void setPolicy(int throttleSeconds, int maxPerPath = 50, int keepAtLeast = 5)
    {
        HistoryPolicy policy;
        policy.throttleSeconds = throttleSeconds;
        policy.maxPerPath = maxPerPath;
        policy.keepAtLeast = keepAtLeast;
        history().setPolicy(policy);
    }

private Q_SLOTS:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        m_clock = Clock();
        m_history = another();
    }

    void storesAndListsNewestFirst()
    {
        QVERIFY(history().snapshot(u"Notatka.md"_s, u"pierwsza wersja"_s, u"user"_s));
        m_clock.advance(6 * kMinute);
        QVERIFY(history().snapshot(u"Notatka.md"_s, u"druga wersja — zażółć"_s, u"sync"_s));
        const QList<HistoryEntry> list = history().list(u"Notatka.md"_s);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list.at(0).reason, u"sync"_s);
        QCOMPARE(list.at(0).size, u"druga wersja — zażółć"_s.toUtf8().size());
        QCOMPARE(list.at(1).reason, u"user"_s);
        QCOMPARE(list.at(1).size, 15);
        QCOMPARE(list.at(1).time, QDateTime(QDate(2026, 10, 14), QTime(9, 0)));
        QCOMPARE(list.at(0).time, QDateTime(QDate(2026, 10, 14), QTime(9, 6)));
        QVERIFY(list.at(0).id != list.at(1).id);
        QCOMPARE(contents(u"Notatka.md"_s), (QStringList{u"druga wersja — zażółć"_s, u"pierwsza wersja"_s}));
        QCOMPARE(history().paths(), QStringList{u"Notatka.md"_s});
    }

    void unknownPathsAndIds()
    {
        QVERIFY(history().list(u"nic.md"_s).isEmpty());
        bool ok = true;
        QVERIFY(history().read(u"nope"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(history().read(u"../../etc/passwd"_s, &ok).isEmpty()); // never leaves the folder
        QVERIFY(!ok);
        QVERIFY(history().read(u"lq9x2k-0000abcd"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(!history().snapshot(QString(), u"x"_s, u"user"_s));
    }

    void normalisesPaths()
    {
        QVERIFY(history().snapshot(u"./a//b.md"_s, u"x"_s, u"user"_s));
        QCOMPARE(history().list(u"a/b.md"_s).size(), 1);
        const QString nfd = u"Żółw.md"_s.normalized(QString::NormalizationForm_D);
        QVERIFY(nfd != u"Żółw.md"_s);
        QVERIFY(history().snapshot(nfd, u"nfd"_s, u"user"_s));
        QCOMPARE(history().list(u"Żółw.md"_s).size(), 1);
    }

    void preservesContentByteForByte()
    {
        const QString content = u"- [ ] zadanie\r\n- ćma\r\n\tkod\n\xFEFF"_s;
        QVERIFY(history().snapshot(u"a.md"_s, content, u"user"_s));
        QCOMPARE(contents(u"a.md"_s), QStringList{content});
    }

    void dedupesIdenticalConsecutiveContent()
    {
        QVERIFY(history().snapshot(u"a.md"_s, u"same"_s, u"user"_s));
        m_clock.advance(10 * kMinute);
        QVERIFY(history().snapshot(u"a.md"_s, u"same"_s, u"user"_s));
        QVERIFY(history().snapshot(u"a.md"_s, u"same"_s, u"sync"_s));
        QCOMPARE(history().list(u"a.md"_s).size(), 1);
    }

    void throttlesOrdinarySnapshots()
    {
        history().snapshot(u"a.md"_s, u"v1"_s, u"user"_s);
        m_clock.advance(1 * kMinute);
        history().snapshot(u"a.md"_s, u"v2"_s, u"user"_s);
        m_clock.advance(3 * kMinute);
        history().snapshot(u"a.md"_s, u"v3"_s, u"system"_s);
        history().snapshot(u"b.md"_s, u"other path is independent"_s, u"user"_s);
        m_clock.advance(1 * kMinute + 1);
        history().snapshot(u"a.md"_s, u"v4"_s, u"user"_s);
        QCOMPARE(reasons(u"a.md"_s), (QStringList{u"user"_s, u"user"_s}));
        QCOMPARE(contents(u"a.md"_s), (QStringList{u"v4"_s, u"v1"_s}));
        QCOMPARE(history().list(u"b.md"_s).size(), 1);
    }

    void alwaysKeepsSyncImportExternal()
    {
        const QStringList steps{u"user"_s, u"sync"_s, u"user"_s, u"external"_s, u"import"_s, u"user"_s, u"user"_s};
        for (qsizetype i = 0; i < steps.size(); ++i) {
            history().snapshot(u"a.md"_s, u"v%1"_s.arg(i + 1), steps.at(i));
            m_clock.advance(1000);
        }
        QCOMPARE(reasons(u"a.md"_s),
                 (QStringList{u"user"_s, u"import"_s, u"external"_s, u"user"_s, u"sync"_s, u"user"_s}));
    }

    void capsSnapshotsPerPathOnWrite()
    {
        setPolicy(0, 7, 2);
        for (int i = 0; i < 12; ++i) {
            m_clock.advance(kMinute);
            QVERIFY(history().snapshot(u"a.md"_s, u"v%1"_s.arg(i), u"user"_s));
        }
        const QStringList all = contents(u"a.md"_s);
        QCOMPARE(all.size(), 7);
        QCOMPARE(all.first(), u"v11"_s);
        QCOMPARE(all.last(), u"v5"_s);
        QCOMPARE(countFiles(dir() + u"/objects"_s), 7);
    }

    void recoversFromCorruptedIndex()
    {
        const auto indexFile = [this](const QString &path) {
            return dir() + u"/index/"_s
                   + QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex())
                   + u".json"_s;
        };
        QVERIFY(QDir().mkpath(dir() + u"/index"_s));
        {
            QFile garbage(indexFile(u"a.md"_s));
            QVERIFY(garbage.open(QIODevice::WriteOnly));
            garbage.write("garbage");
            QFile partial(indexFile(u"b.md"_s));
            QVERIFY(partial.open(QIODevice::WriteOnly));
            partial.write("{\"path\":\"b.md\",\"entries\":[{\"id\":1},null,"
                          "{\"id\":\"x-0000abcd\",\"time\":1,\"size\":1,\"reason\":\"user\",\"hash\":\"h\"}]}");
        }
        QVERIFY(history().list(u"a.md"_s).isEmpty());
        const QList<HistoryEntry> b = history().list(u"b.md"_s);
        QCOMPARE(b.size(), 1);
        QCOMPARE(b.first().id, u"x-0000abcd"_s);
        QCOMPARE(b.first().time, QDateTime::fromMSecsSinceEpoch(1));
        QVERIFY(history().snapshot(u"a.md"_s, u"fresh"_s, u"user"_s));
        QCOMPARE(history().list(u"a.md"_s).size(), 1);
    }

    void pruneDropsOldSnapshotsButKeepsNewest()
    {
        setPolicy(0, 1000, 5);
        for (int i = 0; i < 8; ++i) {
            history().snapshot(u"old.md"_s, u"stara %1"_s.arg(i), u"user"_s);
            m_clock.advance(kDay);
        }
        history().snapshot(u"fresh.md"_s, u"nowa"_s, u"user"_s);
        m_clock.advance(200 * kDay);
        QCOMPARE(history().prune(), 3);
        QCOMPARE(history().list(u"old.md"_s).size(), 5);
        QCOMPARE(contents(u"old.md"_s).first(), u"stara 7"_s);
        QCOMPARE(history().list(u"fresh.md"_s).size(), 1);
        QCOMPARE(countFiles(dir() + u"/objects"_s), 6);
    }

    void pruneKeepsAtMost50PerPath()
    {
        setPolicy(0, 1000);
        for (int i = 0; i < 60; ++i) {
            m_clock.advance(kMinute);
            history().snapshot(u"a.md"_s, u"v%1"_s.arg(i), u"user"_s);
        }
        QCOMPARE(history().list(u"a.md"_s).size(), 60);
        const auto pruner = another();
        QCOMPARE(pruner->prune(), 10);
        const QList<HistoryEntry> list = pruner->list(u"a.md"_s);
        QCOMPARE(list.size(), 50);
        QCOMPARE(pruner->read(list.last().id), u"v10"_s);
    }

    void pruneRemovesOrphansButNotRecentOnes()
    {
        const qint64 now = m_clock.now.toMSecsSinceEpoch();
        const QString oldId = QString::number(now - 60 * kMinute, 36) + u"-0000abcd"_s;
        const QString newId = QString::number(now, 36) + u"-0000efgh"_s;
        const QString objects = dir() + u"/objects/"_s;
        QVERIFY(QDir().mkpath(objects));
        for (const QString &name : {oldId, newId, u"not-a-time-id"_s}) {
            QFile file(objects + name + u".txt"_s);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("orphan");
        }
        QVERIFY(history().snapshot(u"a.md"_s, u"kept"_s, u"user"_s));
        QCOMPARE(history().prune(), 0);
        const QStringList left = QDir(objects).entryList(QDir::Files);
        QCOMPARE(left.size(), 2);
        QVERIFY(left.contains(newId + u".txt"_s));
        QVERIFY(!left.contains(oldId + u".txt"_s));
        QCOMPARE(contents(u"a.md"_s), QStringList{u"kept"_s});
    }

    void pruneOnEmptyDirectory()
    {
        QCOMPARE(history().prune(), 0);
        QVERIFY(history().paths().isEmpty());
    }

    void renameMovesAndMergesHistory()
    {
        history().snapshot(u"stara.md"_s, u"a"_s, u"user"_s);
        m_clock.advance(kMinute);
        history().snapshot(u"nowa.md"_s, u"b"_s, u"user"_s);
        m_clock.advance(kMinute);
        QVERIFY(history().rename(u"stara.md"_s, u"nowa.md"_s));
        QVERIFY(history().list(u"stara.md"_s).isEmpty());
        QCOMPARE(contents(u"nowa.md"_s), (QStringList{u"b"_s, u"a"_s}));
        QCOMPARE(history().paths(), QStringList{u"nowa.md"_s});
        QVERIFY(history().rename(u"nic.md"_s, u"x.md"_s));
        QVERIFY(history().rename(u"nowa.md"_s, u"nowa.md"_s));
        QCOMPARE(history().paths(), QStringList{u"nowa.md"_s});
    }

    void removeDeletesIndexAndContent()
    {
        history().snapshot(u"ź.md"_s, u"treść"_s, u"user"_s);
        QVERIFY(history().remove(u"ź.md"_s));
        QVERIFY(history().list(u"ź.md"_s).isEmpty());
        QCOMPARE(countFiles(dir() + u"/objects"_s), 0);
        QCOMPARE(countFiles(dir() + u"/index"_s), 0);
    }

    void instancesShareTheDirectory()
    {
        const auto otherTab = another();
        QVERIFY(history().list(u"a.md"_s).isEmpty());
        otherTab->snapshot(u"a.md"_s, u"z drugiej karty"_s, u"user"_s);
        QCOMPARE(history().list(u"a.md"_s).size(), 1);
        // Dedupe compares with the newest snapshot on disk, not a stale view.
        m_clock.advance(10 * kMinute);
        otherTab->snapshot(u"a.md"_s, u"B"_s, u"user"_s);
        m_clock.advance(10 * kMinute);
        history().snapshot(u"a.md"_s, u"z drugiej karty"_s, u"user"_s);
        QCOMPARE(history().list(u"a.md"_s).size(), 3);
        // A rename keeps what the other instance added.
        QVERIFY(history().rename(u"a.md"_s, u"b.md"_s));
        QCOMPARE(otherTab->list(u"b.md"_s).size(), 3);
    }

    void clockSetBackKeepsOrder()
    {
        history().snapshot(u"Dziennik/środa.md"_s, u"wersja 1"_s, u"user"_s);
        m_clock.advance(-kDay); // NTP correction, manual change
        history().snapshot(u"Dziennik/środa.md"_s, u"wersja 2"_s, u"user"_s);
        m_clock.advance(10 * kMinute);
        history().snapshot(u"Dziennik/środa.md"_s, u"wersja 3"_s, u"user"_s);
        QCOMPARE(contents(u"Dziennik/środa.md"_s), (QStringList{u"wersja 3"_s, u"wersja 2"_s, u"wersja 1"_s}));
    }

    void pruneKeepsNewestDespiteEarlierTimestamps()
    {
        for (int i = 1; i <= 3; ++i) {
            history().snapshot(u"a.md"_s, u"stara %1"_s.arg(i), u"sync"_s);
            m_clock.advance(kMinute);
        }
        m_clock.advance(-200 * kDay); // a device that booted without a real-time clock
        history().snapshot(u"a.md"_s, u"nowa"_s, u"sync"_s);
        m_clock.advance(200 * kDay);
        history().prune(50, 90, 2);
        const QStringList all = contents(u"a.md"_s);
        QCOMPARE(all.first(), u"nowa"_s);
        QCOMPARE(all.size(), 4);
    }

    void failsWhenTheDirectoryIsNotWritable()
    {
        QVERIFY(QDir().mkpath(dir()));
        QFile::setPermissions(dir(), QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(dir()).isWritable()) {
            QFile::setPermissions(dir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            QSKIP("running with privileges that ignore file permissions");
        }
        const bool ok = history().snapshot(u"a.md"_s, u"x"_s, u"user"_s);
        QFile::setPermissions(dir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(!ok);
        QVERIFY(!history().errorString().isEmpty());
        QVERIFY(history().snapshot(u"a.md"_s, u"x"_s, u"user"_s)); // works again afterwards
        QCOMPARE(history().list(u"a.md"_s).size(), 1);
    }

    void keepsBytesExactly()
    {
        // Byte order mark, Windows-1250 letters, CRLF and a NUL: kept as they were.
        const QByteArray bytes("\xEF\xBB\xBFZa\xBF\xF3\xB3\xE6\r\n\x00x", 13);
        QVERIFY(history().snapshotBytes(u"stary.txt"_s, bytes, u"sync"_s));
        const QList<HistoryEntry> list = history().list(u"stary.txt"_s);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().size, bytes.size());
        bool ok = false;
        QCOMPARE(history().readBytes(list.first().id, &ok), bytes);
        QVERIFY(ok);
        QVERIFY(!history().read(list.first().id).startsWith(QChar(0xFEFF))); // read() drops the BOM
        QVERIFY(history().readBytes(u"../index"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(!history().snapshotBytes(QString(), bytes, u"sync"_s));
    }

    void textAndBytesOfTheSameContentAreOneVersion()
    {
        QVERIFY(history().snapshot(u"ćma.md"_s, u"- ćma\r\n"_s, u"user"_s));
        m_clock.advance(10 * kMinute);
        QVERIFY(history().snapshotBytes(u"ćma.md"_s, u"- ćma\r\n"_s.toUtf8(), u"sync"_s));
        QCOMPARE(history().list(u"ćma.md"_s).size(), 1);
        QCOMPARE(history().readBytes(history().list(u"ćma.md"_s).first().id), u"- ćma\r\n"_s.toUtf8());
    }

    void anEmptyClockFallsBackToTheSystemClock()
    {
        history().setClock({});
        QVERIFY(history().snapshot(u"a.md"_s, u"x"_s, u"user"_s));
        const QList<HistoryEntry> list = history().list(u"a.md"_s);
        QCOMPARE(list.size(), 1);
        QVERIFY(list.first().time.isValid());
        QCOMPARE(history().prune(), 0);
    }
};

QTEST_GUILESS_MAIN(TestHistory)
#include "tst_vault_history.moc"
