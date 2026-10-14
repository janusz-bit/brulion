// LocalStore: device-local JSON key/value store (persistence, debounced atomic saves,
// concurrent writers, corrupt files).
#include "vault/localstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

QJsonObject fileObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

class TestLocalStore : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_tmp;
    int m_counter = 0;
    QString freshPath() { return m_tmp.path() + u"/store-%1/store.json"_s.arg(++m_counter); }

private Q_SLOTS:
    void getSetRemove()
    {
        LocalStore store(freshPath());
        QVERIFY(store.isPersistent());
        QVERIFY(store.get(u"missing"_s).isUndefined());
        QCOMPARE(store.get(u"missing"_s, 7).toInt(), 7);
        const QJsonObject value{{u"x"_s, 1}, {u"tags"_s, QJsonArray{u"zażółć"_s}}};
        store.set(u"a"_s, value);
        QCOMPARE(store.get(u"a"_s).toObject(), value);
        QVERIFY(store.contains(u"a"_s));
        store.set(u"a"_s, 2);
        QCOMPARE(store.get(u"a"_s).toInt(), 2);
        QVERIFY(store.remove(u"a"_s));
        QVERIFY(!store.contains(u"a"_s));
        QVERIFY(!store.remove(u"never-existed"_s));
        store.set(u"k"_s, 1);
        store.set(u"k"_s, QJsonValue(QJsonValue::Undefined)); // Undefined removes
        QVERIFY(!store.contains(u"k"_s));
        store.set(u"null"_s, QJsonValue::Null); // null is a value
        QVERIFY(store.contains(u"null"_s));
        QCOMPARE(store.size(), 1);
    }

    void keysAndValuesByPrefix()
    {
        LocalStore store(freshPath());
        store.set(u"sync/b"_s, 2);
        store.set(u"sync/a"_s, 1);
        store.set(u"history/x"_s, u"h"_s);
        store.set(u"synchro"_s, u"not sync/"_s);
        store.set(u"sync/ż"_s, 3);
        QCOMPARE(store.keys(u"sync/"_s), (QStringList{u"sync/a"_s, u"sync/b"_s, u"sync/ż"_s}));
        const QJsonObject values = store.values(u"sync/"_s);
        QCOMPARE(values.size(), 3);
        QCOMPARE(values.value(u"sync/ż"_s).toInt(), 3);
        QCOMPARE(store.keys(), (QStringList{u"history/x"_s, u"sync/a"_s, u"sync/b"_s, u"sync/ż"_s, u"synchro"_s}));
        QVERIFY(store.keys(u"nothing/"_s).isEmpty());
        QVERIFY(store.values(u"nothing/"_s).isEmpty());
    }

    void prefixEndingInHighestCodeUnit()
    {
        const QString high = u"a"_s + QChar(0xFFFF);
        LocalStore store({});
        store.set(high + u'1', 1);
        store.set(high, 0);
        store.set(u"b"_s, 2);
        QCOMPARE(store.keys(high), (QStringList{high, high + u'1'}));
    }

    void removePrefixAndClear()
    {
        LocalStore store(freshPath());
        store.set(u"trash/.trash/a.md"_s, 1);
        store.set(u"trash/.trash/F/b.md"_s, 2);
        store.set(u"trash2"_s, 3);
        store.set(u"other"_s, 4);
        QCOMPARE(store.removePrefix(u"trash/"_s), 2);
        QCOMPARE(store.keys(), (QStringList{u"other"_s, u"trash2"_s}));
        store.clear();
        QVERIFY(store.keys().isEmpty());
        QVERIFY(store.flush());
        QVERIFY(fileObject(store.filePath()).isEmpty());
    }

    void persistsAcrossInstances()
    {
        const QString path = freshPath();
        {
            LocalStore store(path);
            store.set(u"zażółć"_s, QJsonObject{{u"n"_s, 1}});
            store.set(u"gęś"_s, u"Łódź"_s);
            QVERIFY(store.hasPendingChanges());
        } // the destructor flushes
        LocalStore again(path);
        QCOMPARE(again.keys(), (QStringList{u"gęś"_s, u"zażółć"_s}));
        QCOMPARE(again.get(u"gęś"_s).toString(), u"Łódź"_s);
        QVERIFY(!again.hasPendingChanges());
    }

    void debouncedFlush()
    {
        const QString path = freshPath();
        LocalStore store(path);
        store.setFlushDelay(30);
        QCOMPARE(store.flushDelay(), 30);
        store.set(u"a"_s, 1);
        store.set(u"b"_s, 2);
        QVERIFY(!QFileInfo::exists(path)); // not written synchronously
        QTRY_VERIFY(QFileInfo::exists(path));
        QVERIFY(!store.hasPendingChanges());
        QCOMPARE(fileObject(path), (QJsonObject{{u"a"_s, 1}, {u"b"_s, 2}}));
    }

    void zeroDelayWritesImmediately()
    {
        const QString path = freshPath();
        LocalStore store(path);
        store.setFlushDelay(0);
        store.set(u"a"_s, 1);
        QVERIFY(!store.hasPendingChanges());
        QCOMPARE(fileObject(path).value("a"_L1).toInt(), 1);
    }

    void unchangedValueIsNotAChange()
    {
        const QString path = freshPath();
        LocalStore store(path);
        store.set(u"a"_s, 1);
        QVERIFY(store.flush());
        store.set(u"a"_s, 1);
        QVERIFY(!store.hasPendingChanges());
    }

    void atomicSaveLeavesNoTemporaryFiles()
    {
        const QString path = freshPath();
        LocalStore store(path);
        for (int i = 0; i < 20; ++i) {
            store.set(u"klucz %1"_s.arg(i), i);
            QVERIFY(store.flush());
        }
        QCOMPARE(QDir(QFileInfo(path).absolutePath()).entryList(QDir::Files | QDir::Hidden),
                 QStringList{u"store.json"_s});
    }

    void mergesConcurrentWriters()
    {
        // Two stores on one file (e.g. two app instances): neither loses the other's keys.
        const QString path = freshPath();
        LocalStore a(path);
        LocalStore b(path);
        a.set(u"from-a"_s, 1);
        a.set(u"shared"_s, u"a"_s);
        QVERIFY(a.flush());
        b.set(u"from-b"_s, 2);
        b.set(u"shared"_s, u"b"_s);
        QVERIFY(b.flush());
        QCOMPARE(fileObject(path), (QJsonObject{{u"from-a"_s, 1}, {u"from-b"_s, 2}, {u"shared"_s, u"b"_s}}));
        QCOMPARE(b.get(u"from-a"_s).toInt(), 1); // b sees a's key after merging
        // A removal is merged too.
        a.remove(u"from-a"_s);
        QVERIFY(a.flush());
        QCOMPARE(fileObject(path), (QJsonObject{{u"from-b"_s, 2}, {u"shared"_s, u"b"_s}}));
        QVERIFY(a.reload());
        QCOMPARE(a.keys(), (QStringList{u"from-b"_s, u"shared"_s}));
    }

    void clearDropsKeysOfTheFile()
    {
        const QString path = freshPath();
        LocalStore a(path);
        a.set(u"x"_s, 1);
        QVERIFY(a.flush());
        LocalStore b(path);
        b.clear();
        b.set(u"y"_s, 2);
        QVERIFY(b.flush());
        QCOMPARE(fileObject(path), (QJsonObject{{u"y"_s, 2}}));
    }

    void reloadKeepsPendingChanges()
    {
        const QString path = freshPath();
        LocalStore a(path);
        LocalStore b(path);
        a.set(u"a"_s, 1);
        QVERIFY(a.flush());
        b.set(u"b"_s, 2); // pending
        QVERIFY(b.reload());
        QCOMPARE(b.keys(), (QStringList{u"a"_s, u"b"_s}));
        QVERIFY(b.hasPendingChanges());
    }

    void missingFileOnFlushKeepsOurData()
    {
        const QString path = freshPath();
        LocalStore store(path);
        store.set(u"a"_s, 1);
        QVERIFY(store.flush());
        QVERIFY(QFile::remove(path));
        store.set(u"b"_s, 2);
        QVERIFY(store.flush());
        QCOMPARE(fileObject(path), (QJsonObject{{u"a"_s, 1}, {u"b"_s, 2}}));
    }

    void corruptFileIsKeptAside()
    {
        const QString path = freshPath();
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("{ to nie jest JSON");
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"is not a JSON object"_s));
        LocalStore store(path);
        QVERIFY(store.keys().isEmpty());
        QFile corrupt(path + u".corrupt"_s);
        QVERIFY(corrupt.open(QIODevice::ReadOnly));
        QCOMPARE(corrupt.readAll(), "{ to nie jest JSON"_ba);
        store.set(u"ćma"_s, u"nocna"_s);
        QVERIFY(store.flush());
        QCOMPARE(fileObject(path), (QJsonObject{{u"ćma"_s, u"nocna"_s}}));
    }

    void inMemoryStore()
    {
        LocalStore store({});
        QVERIFY(!store.isPersistent());
        QVERIFY(store.filePath().isEmpty());
        store.set(u"a"_s, 1);
        QVERIFY(store.flush());
        QVERIFY(!store.hasPendingChanges());
        QCOMPARE(store.get(u"a"_s).toInt(), 1);
        LocalStore other({});
        QVERIFY(!other.contains(u"a"_s)); // independent
    }

    void saveFailureKeepsChangesPending()
    {
        const QString dir = m_tmp.path() + u"/readonly"_s;
        QVERIFY(QDir().mkpath(dir));
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(dir).isWritable()) {
            QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            QSKIP("running with privileges that ignore file permissions");
        }
        auto store = std::make_unique<LocalStore>(dir + u"/store.json"_s);
        QSignalSpy failed(store.get(), &LocalStore::saveFailed);
        store->set(u"a"_s, 1);
        QVERIFY(!store->flush());
        QCOMPARE(failed.count(), 1);
        QVERIFY(store->hasPendingChanges());
        QVERIFY(!store->errorString().isEmpty());
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(store->flush()); // retried successfully once writable again
        QVERIFY(!store->hasPendingChanges());
    }
};

QTEST_GUILESS_MAIN(TestLocalStore)
#include "tst_vault_localstore.moc"
