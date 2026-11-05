// The C++ behind the views (qml/views/*.qml), which keeps their QML compiled to C++: the route
// parameters read from a QVariantMap (AppController::routeText / routeInt / routeFlag /
// routeWith), SearchView's recent searches (AppSettings) and CleanupView's selection
// (CleanupSelection).
#include "appcontroller.h"
#include "appsettings.h"
#include "cleanupselection.h"

#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::shell::AppController;
using brulion::shell::AppSettings;
using brulion::shell::CleanupSelection;

namespace {

/// A row of Workspace::unreferencedFiles().
QVariant file(const QString &path, bool attachment, int openTasks = 0, int reminders = 0)
{
    return QVariantMap{ { u"path"_s, path },           { u"title"_s, path },          { u"folder"_s, QString() },
                        { u"attachment"_s, attachment }, { u"picture"_s, false },     { u"pdf"_s, false },
                        { u"size"_s, 10 },             { u"sizeText"_s, u"10 B"_s }, { u"modified"_s, u"dziś"_s },
                        { u"openTasks"_s, openTasks }, { u"reminders"_s, reminders } };
}

QStringList pathsOf(const QVariantList &rows)
{
    QStringList out;
    for (const QVariant &row : rows)
        out.append(row.toMap().value(u"path"_s).toString());
    return out;
}

} // namespace

class TestViews : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<AppController> makeApp()
    {
        AppController::Options o;
        o.settingsFile = m_tmp.filePath(u"settings-%1.ini"_s.arg(++m_n));
        o.dataDir = m_tmp.filePath(u"data-%1"_s.arg(m_n));
        return std::make_unique<AppController>(o);
    }

private Q_SLOTS:
    // ---- Route parameters ---------------------------------------------------------------------
    void routeText_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<QString>("text");
        QTest::newRow("text") << QVariant(u"Zażółć gęślą jaźń.md"_s) << u"Zażółć gęślą jaźń.md"_s;
        QTest::newRow("empty text") << QVariant(QString()) << QString();
        QTest::newRow("missing") << QVariant() << QString();
        QTest::newRow("null") << QVariant::fromValue(nullptr) << QString();
        QTest::newRow("int") << QVariant(12) << u"12"_s;
        QTest::newRow("whole double") << QVariant(2.0) << u"2"_s;
        QTest::newRow("negative double") << QVariant(-3.0) << u"-3"_s;
        QTest::newRow("fraction") << QVariant(0.5) << u"0.5"_s;
        QTest::newRow("true") << QVariant(true) << u"true"_s;
        QTest::newRow("false") << QVariant(false) << u"false"_s;
    }
    void routeText()
    {
        QFETCH(QVariant, value);
        QFETCH(QString, text);
        auto app = makeApp();
        QVariantMap route{ { u"view"_s, u"page"_s } };
        if (value.isValid())
            route.insert(u"key"_s, value);
        QCOMPARE(app->routeText(route, u"key"_s), text);
        QCOMPARE(app->routeText({}, u"key"_s), QString());
    }

    void routeInt_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<int>("result");
        QTest::newRow("int") << QVariant(7) << 7;
        QTest::newRow("zero") << QVariant(0) << 0;
        QTest::newRow("negative") << QVariant(-2) << -2;
        QTest::newRow("double") << QVariant(3.0) << 3;
        QTest::newRow("text number") << QVariant(u"42"_s) << 42;
        QTest::newRow("text") << QVariant(u"żółw"_s) << -1;
        QTest::newRow("empty text") << QVariant(QString()) << -1;
        QTest::newRow("missing") << QVariant() << -1;
        QTest::newRow("null") << QVariant::fromValue(nullptr) << -1;
    }
    void routeInt()
    {
        QFETCH(QVariant, value);
        QFETCH(int, result);
        auto app = makeApp();
        QVariantMap route;
        if (value.isValid())
            route.insert(u"line"_s, value);
        QCOMPARE(app->routeInt(route, u"line"_s, -1), result);
    }

    void routeFlag_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<bool>("result");
        QTest::newRow("true") << QVariant(true) << true;
        QTest::newRow("false") << QVariant(false) << false;
        QTest::newRow("text") << QVariant(u"tak"_s) << true;
        QTest::newRow("text false") << QVariant(u"false"_s) << true; // JavaScript: a non-empty string
        QTest::newRow("empty text") << QVariant(QString()) << false;
        QTest::newRow("one") << QVariant(1) << true;
        QTest::newRow("zero") << QVariant(0) << false;
        QTest::newRow("zero double") << QVariant(0.0) << false;
        QTest::newRow("NaN") << QVariant(std::numeric_limits<double>::quiet_NaN()) << false;
        QTest::newRow("list") << QVariant(QStringList()) << true;
        QTest::newRow("map") << QVariant(QVariantMap()) << true;
        QTest::newRow("missing") << QVariant() << false;
        QTest::newRow("null") << QVariant::fromValue(nullptr) << false;
    }
    void routeFlag()
    {
        QFETCH(QVariant, value);
        QFETCH(bool, result);
        auto app = makeApp();
        QVariantMap route;
        if (value.isValid())
            route.insert(u"source"_s, value);
        QCOMPARE(app->routeFlag(route, u"source"_s), result);
    }

    void routeWith()
    {
        auto app = makeApp();
        const QVariantMap journal{ { u"view"_s, u"journal"_s },   { u"date"_s, u"2026-10-14"_s },
                                   { u"line"_s, 0 },              { u"blockId"_s, u"żółw-1"_s },
                                   { u"source"_s, QVariant::fromValue(nullptr) }, { u"fromTag"_s, true } };
        const QVariantMap page = app->routeWith({ { u"view"_s, u"page"_s }, { u"path"_s, u"journals/2026-10-14.md"_s } },
                                                journal, { u"line"_s, u"blockId"_s, u"heading"_s, u"source"_s, u"open"_s });
        // The keys asked for that the route has (a null one too); `date`, `fromTag` not.
        QCOMPARE(page, (QVariantMap{ { u"view"_s, u"page"_s },
                                     { u"path"_s, u"journals/2026-10-14.md"_s },
                                     { u"line"_s, 0 },
                                     { u"blockId"_s, u"żółw-1"_s },
                                     { u"source"_s, QVariant::fromValue(nullptr) } }));
        // A key of the base the route also has: the route's.
        QCOMPARE(app->routeWith({ { u"view"_s, u"page"_s } }, { { u"view"_s, u"tag"_s } }, { u"view"_s }),
                 (QVariantMap{ { u"view"_s, u"tag"_s } }));
        QCOMPARE(app->routeWith({ { u"view"_s, u"page"_s } }, {}, { u"line"_s }), (QVariantMap{ { u"view"_s, u"page"_s } }));
    }

    // ---- SearchView's recent searches -----------------------------------------------------------
    void recentSearches()
    {
        const QString file = m_tmp.filePath(u"recent.ini"_s);
        {
            AppSettings s(file, m_tmp.path());
            QVERIFY(s.recentSearches().isEmpty());
            QCOMPARE(s.rememberSearch(u"  Żółw  "_s), QStringList{ u"Żółw"_s });
            QCOMPARE(s.rememberSearch(u"jutro"_s), (QStringList{ u"jutro"_s, u"Żółw"_s }));
            // The same after folding case and diacritics: moved to the front, written as now.
            QCOMPARE(s.rememberSearch(u"ZOLW"_s), (QStringList{ u"ZOLW"_s, u"jutro"_s }));
            // Nothing typed: nothing changes.
            QCOMPARE(s.rememberSearch(u"   "_s), (QStringList{ u"ZOLW"_s, u"jutro"_s }));
            for (int i = 1; i <= 10; ++i)
                s.rememberSearch(u"szukam %1"_s.arg(i));
            const QStringList eight = s.recentSearches();
            QCOMPARE(eight.size(), 8);
            QCOMPARE(eight.first(), u"szukam 10"_s);
            QCOMPARE(eight.last(), u"szukam 3"_s);
        }
        {
            // Kept in the settings file.
            AppSettings s(file, m_tmp.path());
            QCOMPARE(s.recentSearches().size(), 8);
            QCOMPARE(s.recentSearches().first(), u"szukam 10"_s);
            s.clearRecentSearches();
            QVERIFY(s.recentSearches().isEmpty());
        }
        {
            AppSettings s(file, m_tmp.path());
            QVERIFY(s.recentSearches().isEmpty());
        }
        {
            // An INI file keeps a list of one as a single text.
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(u"search/recent"_s, u"gęś"_s);
            raw.sync();
        }
        AppSettings s(file, m_tmp.path());
        QCOMPARE(s.recentSearches(), QStringList{ u"gęś"_s });
    }

    // ---- CleanupView's selection ----------------------------------------------------------------
    void cleanupSelection()
    {
        CleanupSelection selection;
        QSignalSpy changed(&selection, &CleanupSelection::changed);
        selection.setFiles({ file(u"Notatka.md"_s, false, 2, 0), file(u"obraz.png"_s, true),
                             file(u"Żółw.md"_s, false, 0, 1), file(u"plik.pdf"_s, true) });
        QCOMPARE(changed.size(), 1);
        QCOMPARE(selection.attachmentCount(), 2);
        QCOMPARE(pathsOf(selection.shown()), (QStringList{ u"Notatka.md"_s, u"obraz.png"_s, u"Żółw.md"_s, u"plik.pdf"_s }));
        QVERIFY(selection.shownTicked().isEmpty());

        // Ticks of shown rows, in the list's order; a tick of a missing file stays but is not shown.
        selection.setTicked({ u"plik.pdf"_s, u"Notatka.md"_s, u"zniknął.md"_s });
        QCOMPARE(selection.shownTicked(), (QStringList{ u"Notatka.md"_s, u"plik.pdf"_s }));
        QCOMPARE(selection.tickedWithWork(), 1);
        QCOMPARE(selection.existingTicks(), (QStringList{ u"plik.pdf"_s, u"Notatka.md"_s }));

        selection.setKind(u"notes"_s);
        QCOMPARE(pathsOf(selection.shown()), (QStringList{ u"Notatka.md"_s, u"Żółw.md"_s }));
        QCOMPARE(selection.shownTicked(), QStringList{ u"Notatka.md"_s });
        selection.setKind(u"attachments"_s);
        QCOMPARE(pathsOf(selection.shown()), (QStringList{ u"obraz.png"_s, u"plik.pdf"_s }));
        QCOMPARE(selection.shownTicked(), QStringList{ u"plik.pdf"_s });
        QCOMPARE(selection.tickedWithWork(), 0);
        // An unknown kind shows everything.
        selection.setKind(u"coś"_s);
        QCOMPARE(selection.shown().size(), 4);

        // Changed copies of the ticks; the selection itself changes only when they are set.
        selection.setKind(u"notes"_s);
        QCOMPARE(selection.withTicked(u"Żółw.md"_s, true),
                 (QStringList{ u"plik.pdf"_s, u"Notatka.md"_s, u"zniknął.md"_s, u"Żółw.md"_s }));
        QCOMPARE(selection.withTicked(u"Notatka.md"_s, true), selection.ticked()); // already ticked
        QCOMPARE(selection.withTicked(u"Notatka.md"_s, false), (QStringList{ u"plik.pdf"_s, u"zniknął.md"_s }));
        QCOMPARE(selection.withAllShown(true),
                 (QStringList{ u"plik.pdf"_s, u"Notatka.md"_s, u"zniknął.md"_s, u"Żółw.md"_s }));
        QCOMPARE(selection.withAllShown(false), (QStringList{ u"plik.pdf"_s, u"zniknął.md"_s }));
        QCOMPARE(selection.ticked(), (QStringList{ u"plik.pdf"_s, u"Notatka.md"_s, u"zniknął.md"_s }));

        // The ticks a route keeps for Back.
        QCOMPARE(selection.routeTicks({ { u"view"_s, u"cleanup"_s },
                                        { u"selected"_s, QVariantList{ u"a.md"_s, u"ż.png"_s } } }),
                 (QStringList{ u"a.md"_s, u"ż.png"_s }));
        QVERIFY(selection.routeTicks({ { u"view"_s, u"cleanup"_s } }).isEmpty());

        // Setting the same again changes nothing.
        const int before = int(changed.size());
        selection.setTicked(selection.ticked());
        selection.setKind(u"notes"_s);
        QCOMPARE(changed.size(), before);
        // No files: nothing shown, no counts.
        selection.setFiles({});
        QVERIFY(selection.shown().isEmpty());
        QVERIFY(selection.shownTicked().isEmpty());
        QCOMPARE(selection.attachmentCount(), 0);
        QVERIFY(selection.existingTicks().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestViews)
#include "tst_views.moc"
