// VaultIndex: the page-property keys and values in use (propertyKeys(), propertyValues()) — the
// suggestions of the "Właściwości" panel.
#include "index/vaultindex.h"

#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

const PropertyKeyInfo *keyInfo(const QList<PropertyKeyInfo> &keys, const QString &key)
{
    for (const PropertyKeyInfo &k : keys) {
        if (k.key == key)
            return &k;
    }
    return nullptr;
}

QStringList keysOf(const QList<PropertyKeyInfo> &keys)
{
    QStringList out;
    for (const PropertyKeyInfo &k : keys)
        out.append(k.key);
    return out;
}

QStringList valuesOf(const QList<PropertyValueCount> &values)
{
    QStringList out;
    for (const PropertyValueCount &v : values)
        out.append(v.value + u'=' + QString::number(v.count));
    return out;
}

void build(VaultIndex &index)
{
    VaultIndex::Batch batch(index);
    index.update(u"Dom.md"_s, u"---\nstatus: w toku\ntermin: 2026-10-28\ntags: [dom, Żółw]\nbrulion: outline\n"
                              u"rel:\n  - \"[[Ogród]]\"\n  - Łódź\n---\n# Dom\n- koszt:: 1200\n"_s);
    index.update(u"Ogród.md"_s, u"---\nStatus: gotowe\ntermin: 2026-11-02\ntags:\n  - żółw\n  - ogród\n---\n"_s);
    index.update(u"Notatka.md"_s, u"---\nstatus: w toku\ntermin: jutro\npusta:\nliczba:\n---\nTreść\n"_s);
    index.update(u"Logseq.md"_s, u"status:: w toku\nrel:: [[Dom]], [[Łódź]]\nocena:: 4\n\n- a\n"_s);
    index.update(u"Liczby.md"_s, u"---\nliczba: 3\n---\n> [!note] Uwaga\n> treść\n"_s);
    index.update(u"zdjęcie.png"_s, QString());
}

} // namespace

class TestIndexProperties : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void keys()
    {
        VaultIndex index;
        build(index);
        const QList<PropertyKeyInfo> keys = index.propertyKeys();
        // Most used first (ties by name); `brulion` and block properties (koszt::, callouts) left out.
        QCOMPARE(keysOf(keys),
                 QStringList({u"status"_s, u"termin"_s, u"liczba"_s, u"rel"_s, u"tags"_s, u"ocena"_s, u"pusta"_s}));
        const PropertyKeyInfo *status = keyInfo(keys, u"status"_s);
        QVERIFY(status);
        QCOMPARE(status->count, 4); // Status / status merged, the most used spelling
        QCOMPARE(status->type, u"text"_s);
        QCOMPARE(keyInfo(keys, u"termin"_s)->type, u"date"_s); // two dates, one text
        QCOMPARE(keyInfo(keys, u"tags"_s)->type, u"tags"_s);
        QCOMPARE(keyInfo(keys, u"rel"_s)->type, u"list"_s);
        QCOMPARE(keyInfo(keys, u"rel"_s)->count, 2);
        QCOMPARE(keyInfo(keys, u"ocena"_s)->type, u"number"_s);  // Logseq
        QCOMPARE(keyInfo(keys, u"liczba"_s)->type, u"number"_s); // one empty, one number
        QCOMPARE(keyInfo(keys, u"pusta"_s)->type, QString());    // only empty values
        QVERIFY(!keyInfo(keys, u"brulion"_s));
        QVERIFY(!keyInfo(keys, u"koszt"_s));
        QVERIFY(!keyInfo(keys, u"callouttype"_s));
    }

    void values()
    {
        VaultIndex index;
        build(index);
        QCOMPARE(valuesOf(index.propertyValues(u"STATUS"_s)), QStringList({u"w toku=3"_s, u"gotowe=1"_s}));
        // List items one by one; Logseq links as names.
        QStringList rel = valuesOf(index.propertyValues(u"rel"_s));
        QCOMPARE(rel.size(), 3);
        QCOMPARE(rel.takeFirst(), u"Łódź=2"_s);
        rel.sort();
        QCOMPARE(rel, QStringList({u"Dom=1"_s, u"[[Ogród]]=1"_s}));
        // One entry per value, case-insensitively: the most used spelling (ties: the first seen).
        QCOMPARE(valuesOf(index.propertyValues(u"tags"_s)), QStringList({u"Żółw=2"_s, u"dom=1"_s, u"ogród=1"_s}));
        // A prefix without diacritics: starts with first, then contains; the limit.
        QCOMPARE(valuesOf(index.propertyValues(u"tags"_s, u"zol"_s)), QStringList({u"Żółw=2"_s}));
        QCOMPARE(valuesOf(index.propertyValues(u"tags"_s, u"o"_s)),
                 QStringList({u"ogród=1"_s, u"Żółw=2"_s, u"dom=1"_s}));
        QCOMPARE(valuesOf(index.propertyValues(u"tags"_s, u"o"_s, 1)), QStringList({u"ogród=1"_s}));
        QCOMPARE(valuesOf(index.propertyValues(u"termin"_s)),
                 QStringList({u"2026-10-28=1"_s, u"2026-11-02=1"_s, u"jutro=1"_s}));
        QCOMPARE(valuesOf(index.propertyValues(u"ocena"_s)), QStringList({u"4=1"_s}));
        QVERIFY(index.propertyValues(u"pusta"_s).isEmpty());
        QVERIFY(index.propertyValues(u"brak"_s).isEmpty());
        QVERIFY(index.propertyValues(QString()).isEmpty());
        QVERIFY(index.propertyValues(u"status"_s, QString(), 0).isEmpty());
    }

    void cacheFollowsChanges()
    {
        VaultIndex index;
        build(index);
        QCOMPARE(keyInfo(index.propertyKeys(), u"status"_s)->count, 4);
        QCOMPARE(valuesOf(index.propertyValues(u"status"_s)), QStringList({u"w toku=3"_s, u"gotowe=1"_s}));
        index.update(u"Nowa.md"_s, u"---\nstatus: gotowe\nźródło: Wikipedia\n---\n"_s);
        QCOMPARE(keyInfo(index.propertyKeys(), u"status"_s)->count, 5);
        QVERIFY(keyInfo(index.propertyKeys(), u"źródło"_s));
        QCOMPARE(valuesOf(index.propertyValues(u"status"_s)), QStringList({u"w toku=3"_s, u"gotowe=2"_s}));
        index.remove(u"Notatka.md"_s);
        QCOMPARE(keyInfo(index.propertyKeys(), u"status"_s)->count, 4);
        QVERIFY(!keyInfo(index.propertyKeys(), u"pusta"_s));
        QCOMPARE(valuesOf(index.propertyValues(u"status"_s)), QStringList({u"gotowe=2"_s, u"w toku=2"_s}));
        index.rename(u"Ogród.md"_s, u"Sad.md"_s, u"---\nStatus: zrobione\n---\n"_s);
        QCOMPARE(valuesOf(index.propertyValues(u"status"_s)),
                 QStringList({u"w toku=2"_s, u"gotowe=1"_s, u"zrobione=1"_s}));
        QCOMPARE(keyInfo(index.propertyKeys(), u"tags"_s)->count, 1);
    }

    void emptyIndex()
    {
        VaultIndex index;
        QVERIFY(index.propertyKeys().isEmpty());
        QVERIFY(index.propertyValues(u"a"_s).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestIndexProperties)
#include "tst_index_properties.moc"
