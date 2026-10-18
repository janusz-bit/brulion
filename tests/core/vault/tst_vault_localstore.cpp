// LocalStore: device-local JSON key/value store (persistence, debounced atomic saves,
// concurrent writers, corrupt files).
#include "vault/localstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScopeGuard>
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

QByteArray fileBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFileBytes(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

/// The store's semantics written the straightforward way (every flush reads and parses the
/// file): the reference that LocalStore must match whatever shortcuts it takes.
class ReferenceStore {
public:
    explicit ReferenceStore(QString path) : m_path(std::move(path)) { reload(); }

    QMap<QString, QJsonValue> data;

    void set(const QString &key, const QJsonValue &value)
    {
        if (value.isUndefined()) {
            remove(key);
            return;
        }
        if (const auto it = data.constFind(key); it != data.cend() && *it == value)
            return;
        data.insert(key, value);
        m_pending.insert(key, value);
    }
    void remove(const QString &key)
    {
        if (data.remove(key))
            m_pending.insert(key, std::nullopt);
    }
    void clear()
    {
        if (data.isEmpty() && m_pending.isEmpty() && m_lastBytes.isEmpty())
            return;
        data.clear();
        m_pending.clear();
        m_cleared = true;
    }
    void flush()
    {
        if (!m_cleared && m_pending.isEmpty())
            return;
        QMap<QString, QJsonValue> disk;
        QByteArray raw;
        read(disk, raw);
        if (!raw.isEmpty() && raw != m_lastBytes)
            data = applied(disk);
        QJsonObject object;
        for (auto it = data.cbegin(); it != data.cend(); ++it)
            object.insert(it.key(), it.value());
        m_lastBytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QVERIFY(writeFileBytes(m_path, m_lastBytes));
        m_pending.clear();
        m_cleared = false;
    }
    void reload()
    {
        QMap<QString, QJsonValue> disk;
        QByteArray raw;
        read(disk, raw);
        m_lastBytes = raw;
        data = applied(disk);
    }

private:
    void read(QMap<QString, QJsonValue> &out, QByteArray &raw)
    {
        out.clear();
        raw = fileBytes(m_path);
        if (raw.isEmpty() && !QFileInfo::exists(m_path))
            return;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            QFile::remove(m_path + u".corrupt"_s);
            QFile::rename(m_path, m_path + u".corrupt"_s);
            raw.clear();
            return;
        }
        const QJsonObject object = doc.object();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            out.insert(it.key(), it.value());
    }
    QMap<QString, QJsonValue> applied(const QMap<QString, QJsonValue> &disk) const
    {
        QMap<QString, QJsonValue> out = m_cleared ? QMap<QString, QJsonValue>() : disk;
        for (auto it = m_pending.cbegin(); it != m_pending.cend(); ++it) {
            if (it.value())
                out.insert(it.key(), *it.value());
            else
                out.remove(it.key());
        }
        return out;
    }

    QString m_path;
    QHash<QString, std::optional<QJsonValue>> m_pending;
    bool m_cleared = false;
    QByteArray m_lastBytes;
};

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

    void unchangedFileIsNotReparsedButChangesAreSeen()
    {
        // A flush finds the file as this store left it (nothing to merge), or changed by someone
        // else — also to content of the same size, or back to bytes it wrote earlier.
        const QString path = freshPath();
        LocalStore a(path);
        a.set(u"liczba"_s, 1);
        a.set(u"tekst"_s, u"zażółć"_s);
        QVERIFY(a.flush());
        const QByteArray first = fileBytes(path);
        a.set(u"liczba"_s, 2);
        QVERIFY(a.flush());
        const QByteArray second = fileBytes(path);
        QCOMPARE(second.size(), first.size());
        // Same size, other content: an older version of the file put back by somebody else.
        QVERIFY(writeFileBytes(path, first));
        a.set(u"nowy"_s, true);
        QVERIFY(a.flush());
        QCOMPARE(fileObject(path), (QJsonObject{{u"liczba"_s, 1}, {u"tekst"_s, u"zażółć"_s}, {u"nowy"_s, true}}));
        QCOMPARE(a.get(u"liczba"_s).toInt(), 1);
        // Another writer's keys of the same size as ours.
        QVERIFY(writeFileBytes(
                path, QJsonDocument(QJsonObject{{u"liczba"_s, 7}, {u"tekst"_s, u"zażółć"_s}, {u"nowy"_s, true}})
                              .toJson(QJsonDocument::Compact)));
        a.remove(u"nowy"_s);
        QVERIFY(a.flush());
        QCOMPARE(fileObject(path), (QJsonObject{{u"liczba"_s, 7}, {u"tekst"_s, u"zażółć"_s}}));
        QCOMPARE(a.get(u"liczba"_s).toInt(), 7);
        // Garbage of the same size as the last write: still kept aside, our view written.
        const QByteArray last = fileBytes(path);
        QVERIFY(writeFileBytes(path, QByteArray(last.size(), '#')));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"is not a JSON object"_s));
        a.set(u"po"_s, 1);
        QVERIFY(a.flush());
        QCOMPARE(fileBytes(path + u".corrupt"_s), QByteArray(last.size(), '#'));
        QCOMPARE(fileObject(path), (QJsonObject{{u"liczba"_s, 7}, {u"tekst"_s, u"zażółć"_s}, {u"po"_s, 1}}));
    }

    void matchesTheReferenceStore_data()
    {
        QTest::addColumn<quint32>("seed");
        for (quint32 seed = 1; seed <= 12; ++seed)
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    void matchesTheReferenceStore()
    {
        // Random operations on two stores sharing a file (two app instances) and on another
        // program writing that file, mirrored on two reference stores sharing a second file:
        // the stores' contents and the files' bytes must be the same after every step.
        QFETCH(quint32, seed);
        static QtMessageHandler previous = nullptr;
        previous =
                qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &message) {
                    if (!message.contains(u"is not a JSON object"_s))
                        previous(type, context, message);
                });
        const auto restore = qScopeGuard([] { qInstallMessageHandler(previous); });
        const QString real = freshPath();
        const QString model = freshPath();
        QVERIFY(QDir().mkpath(QFileInfo(real).absolutePath()));
        QVERIFY(QDir().mkpath(QFileInfo(model).absolutePath()));
        std::unique_ptr<LocalStore> stores[2] = {std::make_unique<LocalStore>(real),
                                                 std::make_unique<LocalStore>(real)};
        ReferenceStore refs[2] = {ReferenceStore(model), ReferenceStore(model)};
        QRandomGenerator rng(seed);
        const QStringList keys{u"ui/folds/a.md"_s,     u"ui/folds/Łódź.md"_s, u"sync/base/x.md"_s, u"sync/state/x.md"_s,
                               u"trash/.trash/ż.md"_s, u"ui/recentPages"_s,   u"reminders/seen"_s, u"zażółć"_s};
        const auto randomValue = [&]() -> QJsonValue {
            switch (rng.bounded(5)) {
            case 0: return int(rng.bounded(20));
            case 1: return u"wartość %1 gęślą"_s.arg(rng.bounded(9));
            case 2: return QJsonObject{{u"etag"_s, QString::number(rng.bounded(4))}, {u"size"_s, int(rng.bounded(3))}};
            case 3: return QJsonValue::Null;
            default: return QJsonArray{int(rng.bounded(3)), u"ą"_s};
            }
        };
        for (int step = 0; step < 400; ++step) {
            const int who = int(rng.bounded(2));
            LocalStore &store = *stores[who];
            ReferenceStore &ref = refs[who];
            const quint32 op = rng.bounded(100);
            QString what;
            if (op < 35) {
                const QString key = keys.at(rng.bounded(keys.size()));
                const QJsonValue value = randomValue();
                store.set(key, value);
                ref.set(key, value);
                what = u"set"_s;
            } else if (op < 45) {
                const QString key = keys.at(rng.bounded(keys.size()));
                store.remove(key);
                ref.remove(key);
                what = u"remove"_s;
            } else if (op < 48) {
                store.clear();
                ref.clear();
                what = u"clear"_s;
            } else if (op < 75) {
                QVERIFY(store.flush());
                ref.flush();
                what = u"flush"_s;
            } else if (op < 82) {
                QVERIFY(store.reload());
                ref.reload();
                what = u"reload"_s;
            } else if (op < 90) {
                // Another program writes keys of its own.
                QJsonObject object;
                for (const QString &key : keys) {
                    if (rng.bounded(2))
                        object.insert(key, randomValue());
                }
                const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
                QVERIFY(writeFileBytes(real, bytes));
                QVERIFY(writeFileBytes(model, bytes));
                what = u"external write"_s;
            } else if (op < 97) {
                // The same bytes written again (a copy back from a backup, a sync echo).
                const QByteArray bytes = fileBytes(real);
                QVERIFY(writeFileBytes(real, bytes));
                QVERIFY(writeFileBytes(model, fileBytes(model)));
                what = u"rewrite"_s;
            } else {
                const QByteArray garbage(int(fileBytes(real).size()), '{');
                QVERIFY(writeFileBytes(real, garbage));
                QVERIFY(writeFileBytes(model, garbage));
                what = u"garbage"_s;
            }
            const QString where = u"step %1 (%2 by %3)"_s.arg(step).arg(what).arg(who);
            for (int i = 0; i < 2; ++i) {
                const QStringList refKeys = refs[i].data.keys();
                QCOMPARE(stores[i]->keys(), refKeys);
                for (const QString &key : std::as_const(refKeys))
                    QVERIFY2(stores[i]->get(key) == refs[i].data.value(key), qPrintable(where + u" key "_s + key));
            }
            QVERIFY2(fileBytes(real) == fileBytes(model), qPrintable(where));
            QVERIFY2(fileBytes(real + u".corrupt"_s) == fileBytes(model + u".corrupt"_s), qPrintable(where));
        }
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
