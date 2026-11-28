// Hostile extensions (src/core/extensions/selftest.h): every one ends as the limits say, in time,
// and the engine works afterwards — the checks the browser build runs too (?selftest=extensions,
// scripts/wasm-smoke.mjs --extensions), here on the desktop's engine.

#include "extensions/selftest.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::extensions;

class TestExtensionsHostile : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void everyOneEndsAsItMust()
    {
        // (A time limit of its own: the subject here. Generous enough for the sanitizer build.)
        const QVariantMap report = hostileSelfTest(300 * BRULION_TEST_SLOWDOWN);
        QStringList failures;
        for (const QVariant &v : report.value(u"checks"_s).toList()) {
            const QVariantMap c = v.toMap();
            if (!c.value(u"ok"_s).toBool())
                failures << c.value(u"name"_s).toString() + u": "_s + c.value(u"detail"_s).toString();
        }
        QVERIFY2(report.value(u"ok"_s).toBool(),
                 qPrintable(failures.join(u'\n') + report.value(u"error"_s).toString()));
        QVERIFY(report.value(u"checks"_s).toList().size() >= 20);
        QCOMPARE(report.value(u"failed"_s).toInt(), 0);
        QVERIFY(report.value(u"engine"_s).toString().startsWith(u"Luau"_s));
    }

    void memoryEndsByTheMemoryLimit()
    {
        // The memory cases race no clock: with a time limit far too short for a growing table to
        // reach the 16 MB (5 ms here; the browser at a load of 60 needed more than 300 ms), they
        // still end out of memory — and the loops by the time limit.
        const QVariantMap report = hostileSelfTest(5 * BRULION_TEST_SLOWDOWN);
        int memoryCases = 0;
        for (const QVariant &v : report.value(u"checks"_s).toList()) {
            const QVariantMap c = v.toMap();
            const QString name = c.value(u"name"_s).toString();
            const QString got = c.value(u"got"_s).toString();
            if (name == u"a string that doubles" || name == u"a table that grows" || name == u"memory in pcall") {
                ++memoryCases;
                QVERIFY2(got == u"out_of_memory", qPrintable(name + u": "_s + got));
            }
            if (name == u"a loop for ever")
                QCOMPARE(got, u"timeout"_s);
        }
        QCOMPARE(memoryCases, 3);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsHostile)
#include "tst_extensions_hostile.moc"
