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
};

QTEST_GUILESS_MAIN(TestExtensionsHostile)
#include "tst_extensions_hostile.moc"
