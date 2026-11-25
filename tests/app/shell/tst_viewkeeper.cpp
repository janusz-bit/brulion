// ViewKeeper (src/app/shell/viewkeeper.h): which hidden views a pane holds on to — the most
// recently shown up to its limit, none once the views were not switched for a while, none when
// asked to let go; a view is let go by its Loader's `kept` set to false.
#include "viewkeeper.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::ViewKeeper;

namespace {

constexpr int Slowdown = BRULION_TEST_SLOWDOWN;

/// Stands for ShellRouteView's KeptLoader: a QObject with the property `kept`.
struct Loaders {
    explicit Loaders(ViewKeeper &keeper)
    {
        for (const QString &key : {u"journals"_s, u"allPages"_s, u"search"_s, u"tasks"_s, u"calendar"_s}) {
            auto *loader = new QObject(&parent);
            loader->setProperty("kept", false);
            byKey.insert(key, loader);
            keeper.add(key, loader);
        }
    }
    /// What ShellRouteView does when it shows a kept view: its Loader is kept from then on.
    void show(ViewKeeper &keeper, const QString &key)
    {
        if (QObject *loader = byKey.value(key))
            loader->setProperty("kept", true);
        keeper.show(key);
    }
    QStringList kept() const
    {
        QStringList out;
        for (auto [key, loader] : byKey.asKeyValueRange()) {
            if (loader->property("kept").toBool())
                out.append(key);
        }
        out.sort();
        return out;
    }
    QObject parent;
    QHash<QString, QObject *> byKey;
};

} // namespace

class TestViewKeeper : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void keepsAllByDefault()
    {
        ViewKeeper keeper;
        QCOMPARE(keeper.limit(), -1);
        QCOMPARE(keeper.idleMs(), 0);
        Loaders loaders(keeper);
        for (const QString &key : {u"journals"_s, u"tasks"_s, u"calendar"_s, u"allPages"_s, u"search"_s})
            loaders.show(keeper, key);
        QCOMPARE(keeper.hidden(), (QStringList{u"allPages"_s, u"calendar"_s, u"tasks"_s, u"journals"_s}));
        QCOMPARE(loaders.kept().size(), 5);
    }

    void theMostRecentlyShownStay()
    {
        ViewKeeper keeper;
        keeper.setLimit(2);
        Loaders loaders(keeper);
        QSignalSpy gone(&keeper, &ViewKeeper::letGo);
        loaders.show(keeper, u"journals"_s);
        QCOMPARE(keeper.hidden(), QStringList{});
        loaders.show(keeper, u"tasks"_s);
        loaders.show(keeper, u"calendar"_s);
        QCOMPARE(keeper.hidden(), (QStringList{u"tasks"_s, u"journals"_s}));
        QCOMPARE(gone.size(), 0);
        // A third hidden one: the one shown longest ago goes.
        loaders.show(keeper, u"search"_s);
        QCOMPARE(keeper.hidden(), (QStringList{u"calendar"_s, u"tasks"_s}));
        QCOMPARE(gone.size(), 1);
        QCOMPARE(gone.at(0).at(0).toString(), u"journals"_s);
        QCOMPARE(loaders.kept(), (QStringList{u"calendar"_s, u"search"_s, u"tasks"_s}));
        // Going back to a hidden one takes it out of the hidden ones; the one left joins them.
        loaders.show(keeper, u"tasks"_s);
        QCOMPARE(keeper.hidden(), (QStringList{u"search"_s, u"calendar"_s}));
        QCOMPARE(gone.size(), 1);
        // Back and forth between two views never lets go of either.
        for (int i = 0; i < 6; ++i)
            loaders.show(keeper, i % 2 ? u"tasks"_s : u"search"_s);
        QCOMPARE(gone.size(), 1);
        QCOMPARE(loaders.kept(), (QStringList{u"calendar"_s, u"search"_s, u"tasks"_s}));
    }

    void aViewThatIsNotKeptHidesTheShownOne()
    {
        // A page, Settings…: "" — the kept view shown before is hidden, and counts.
        ViewKeeper keeper;
        keeper.setLimit(1);
        Loaders loaders(keeper);
        loaders.show(keeper, u"journals"_s);
        loaders.show(keeper, QString());
        QCOMPARE(keeper.hidden(), QStringList{u"journals"_s});
        loaders.show(keeper, u"tasks"_s);
        loaders.show(keeper, u"page"_s); // (an unknown key is none of the kept views)
        QCOMPARE(keeper.hidden(), QStringList{u"tasks"_s});
        QCOMPARE(loaders.kept(), QStringList{u"tasks"_s});
        // The same view again changes nothing.
        loaders.show(keeper, u"page"_s);
        QCOMPARE(keeper.hidden(), QStringList{u"tasks"_s});
    }

    void limits_data()
    {
        QTest::addColumn<int>("limit");
        QTest::addColumn<QStringList>("hidden");
        QTest::newRow("none") << 0 << QStringList{};
        QTest::newRow("one") << 1 << QStringList{u"calendar"_s};
        QTest::newRow("three") << 3 << QStringList{u"calendar"_s, u"tasks"_s, u"journals"_s};
        QTest::newRow("all") << -1 << QStringList{u"calendar"_s, u"tasks"_s, u"journals"_s};
    }
    void limits()
    {
        QFETCH(int, limit);
        QFETCH(QStringList, hidden);
        ViewKeeper keeper;
        keeper.setLimit(limit);
        Loaders loaders(keeper);
        for (const QString &key : {u"journals"_s, u"tasks"_s, u"calendar"_s, u"search"_s})
            loaders.show(keeper, key);
        QCOMPARE(keeper.hidden(), hidden);
        QCOMPARE(loaders.kept().size(), hidden.size() + 1);
    }

    void aLowerLimitLetsGoAtOnce()
    {
        ViewKeeper keeper;
        Loaders loaders(keeper);
        for (const QString &key : {u"journals"_s, u"tasks"_s, u"calendar"_s, u"search"_s})
            loaders.show(keeper, key);
        QSignalSpy hidden(&keeper, &ViewKeeper::hiddenChanged);
        keeper.setLimit(1);
        QCOMPARE(keeper.hidden(), QStringList{u"calendar"_s});
        QCOMPARE(loaders.kept(), (QStringList{u"calendar"_s, u"search"_s}));
        QCOMPARE(hidden.size(), 1);
    }

    void builtAhead()
    {
        // A view built ahead, hidden: held like one shown just before the current one.
        ViewKeeper keeper;
        keeper.setLimit(2);
        Loaders loaders(keeper);
        loaders.show(keeper, u"journals"_s);
        loaders.byKey.value(u"tasks"_s)->setProperty("kept", true);
        keeper.built(u"tasks"_s);
        QCOMPARE(keeper.hidden(), QStringList{u"tasks"_s});
        keeper.built(u"tasks"_s);    // (again: nothing)
        keeper.built(u"journals"_s); // (the one shown: nothing)
        keeper.built(u"nothing"_s);
        QCOMPARE(keeper.hidden(), QStringList{u"tasks"_s});
        // Shown, it is the view built ahead (never let go in between).
        QSignalSpy gone(&keeper, &ViewKeeper::letGo);
        loaders.show(keeper, u"tasks"_s);
        QCOMPARE(gone.size(), 0);
        QCOMPARE(keeper.hidden(), QStringList{u"journals"_s});
    }

    void idle()
    {
        ViewKeeper keeper;
        keeper.setIdleMs(120 * Slowdown);
        Loaders loaders(keeper);
        QSignalSpy gone(&keeper, &ViewKeeper::letGo);
        loaders.show(keeper, u"journals"_s);
        loaders.show(keeper, u"tasks"_s);
        loaders.show(keeper, u"calendar"_s);
        // Switching views keeps them: the time starts again with every switch.
        QTest::qWait(60 * Slowdown);
        loaders.show(keeper, u"tasks"_s);
        QTest::qWait(60 * Slowdown);
        loaders.show(keeper, u"calendar"_s);
        QTest::qWait(60 * Slowdown);
        QCOMPARE(gone.size(), 0);
        // Left alone, the hidden ones go; the one shown stays.
        QTRY_COMPARE_WITH_TIMEOUT(gone.size(), 2, 3000 * Slowdown);
        QCOMPARE(keeper.hidden(), QStringList{});
        QCOMPARE(loaders.kept(), QStringList{u"calendar"_s});
        // Shown again later, a view is kept again (ShellRouteView makes it anew).
        loaders.show(keeper, u"tasks"_s);
        QCOMPARE(keeper.hidden(), QStringList{u"calendar"_s});
        QTRY_COMPARE_WITH_TIMEOUT(gone.size(), 3, 3000 * Slowdown);
        QCOMPARE(loaders.kept(), QStringList{u"tasks"_s});
    }

    void letGoHidden()
    {
        ViewKeeper keeper;
        Loaders loaders(keeper);
        for (const QString &key : {u"journals"_s, u"tasks"_s, u"calendar"_s})
            loaders.show(keeper, key);
        QSignalSpy gone(&keeper, &ViewKeeper::letGo);
        QSignalSpy hidden(&keeper, &ViewKeeper::hiddenChanged);
        keeper.letGoHidden();
        QCOMPARE(gone.size(), 2);
        QCOMPARE(hidden.size(), 1);
        QCOMPARE(keeper.hidden(), QStringList{});
        QCOMPARE(loaders.kept(), QStringList{u"calendar"_s});
        keeper.letGoHidden(); // (nothing hidden: nothing)
        QCOMPARE(gone.size(), 2);
        QCOMPARE(hidden.size(), 1);
    }

    void clear()
    {
        // The pane shows no route: its Loaders were emptied by ShellRouteView itself.
        ViewKeeper keeper;
        keeper.setLimit(2);
        Loaders loaders(keeper);
        for (const QString &key : {u"journals"_s, u"tasks"_s, u"calendar"_s})
            loaders.show(keeper, key);
        QSignalSpy gone(&keeper, &ViewKeeper::letGo);
        keeper.clear();
        QCOMPARE(keeper.hidden(), QStringList{});
        QCOMPARE(gone.size(), 0); // (not its doing)
        // The view shown before the clear is not "the one shown before" any more.
        loaders.show(keeper, u"search"_s);
        QCOMPARE(keeper.hidden(), QStringList{});
    }

    void aLoaderThatIsGone()
    {
        ViewKeeper keeper;
        keeper.setLimit(0);
        auto first = std::make_unique<QObject>();
        auto second = std::make_unique<QObject>();
        keeper.add(u"tasks"_s, first.get());
        keeper.add(u"calendar"_s, second.get());
        keeper.add(QString(), second.get());
        keeper.add(u"none"_s, nullptr);
        keeper.show(u"tasks"_s);
        first.reset();
        keeper.show(u"calendar"_s); // lets go of "tasks", whose Loader is gone: nothing to set
        QCOMPARE(keeper.hidden(), QStringList{});
    }
};

QTEST_GUILESS_MAIN(TestViewKeeper)
#include "tst_viewkeeper.moc"
