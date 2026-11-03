// StreamFind (docs/UI.md "Find and replace"): the find bar over the days of the journal stream —
// matches counted in every day (attached days by their controllers' text, the others from their
// files, a file changed elsewhere counted again), the current match over all days, next / previous
// going on to the nearest day with a match (dayRequested, the day's first or last match once it is
// the one written in) and round the stream, a new query with nothing after the caret, Replace going
// on after a day's last match, Replace all over every day (controllers: their undo; files:
// compare-and-swap) and its undo, the limit of days searched, Polish letters without diacritics.
#include "fakeeditorhost.h"
#include "notefind.h"
#include "pagecontroller.h"
#include "streamfind.h"

#include <QGuiApplication>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

constexpr int PathRole = Qt::UserRole + 1;

// Top to bottom, as the stream lists them.
const QStringList Paths{u"journals/2026-11-03.md"_s, u"journals/2026-11-02.md"_s, u"journals/2026-10-30.md"_s,
                        u"journals/2026-10-29.md"_s};

struct Stream {
    FakeEditorHost host;
    QStandardItemModel model;
    NoteFind find;
    StreamFind stream;
    /// Pages of the stream that "exist" (on screen or in the list's cache), by row.
    std::unique_ptr<PageController> pages[4];

    explicit Stream(const QStringList &contents)
    {
        model.setItemRoleNames({{PathRole, "path"}});
        for (int i = 0; i < Paths.size(); ++i) {
            if (i < contents.size() && !contents.at(i).isNull())
                host.setFile(Paths.at(i), contents.at(i));
            auto *item = new QStandardItem;
            item->setData(Paths.at(i), PathRole);
            model.appendRow(item);
        }
        stream.setHost(&host);
        stream.setModel(&model);
        stream.setFind(&find);
        // The view: the day asked for becomes the one written in (JournalsView.activateDay()).
        QObject::connect(&stream, &StreamFind::dayRequested, &stream, [this](int row, const QString &) {
            if (!pages[row])
                open(row);
            find.setController(pages[row].get());
        });
    }
    void open(int i)
    {
        pages[i] = std::make_unique<PageController>();
        pages[i]->setNow(u"2026-11-03 10:00"_s);
        pages[i]->setSaveDelay(0);
        pages[i]->setHost(&host);
        pages[i]->setPath(Paths.at(i));
        stream.attach(pages[i].get());
    }
    void close(int i)
    {
        if (find.controller() == pages[i].get())
            find.setController(nullptr);
        stream.detach(pages[i].get());
        pages[i].reset();
    }
    /// The bar opened in day `row` with `query`, counted.
    void search(int row, const QString &query)
    {
        if (!pages[row])
            open(row);
        find.setController(pages[row].get());
        find.setActive(true);
        find.setOriginHere(-1);
        find.setQuery(query);
        find.refresh();
        stream.refresh();
    }
    int activeRow() const
    {
        for (int i = 0; i < 4; ++i) {
            if (pages[i] && find.controller() == pages[i].get())
                return i;
        }
        return -1;
    }
    /// Wait for a day switched to by the stream to show its match (queued).
    void settle() { QCoreApplication::processEvents(); }
};

} // namespace

class TestStreamFind : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void countsEveryDay()
    {
        Stream s({u"- kawa rano\n- herbata\n"_s, u"- Kawa z mlekiem\n- kawa bez cukru\n"_s, u"- nic\n"_s,
                  u"- ostatnia kawa\n"_s});
        s.open(0);
        s.open(1); // attached; days 2 and 3 only on disk
        s.search(0, u"kawa"_s);
        QCOMPARE(s.find.count(), 1);
        QCOMPARE(s.stream.count(), 4);
        QCOMPARE(s.stream.dayCount(), 3);
        QCOMPARE(s.stream.searchedDays(), 4);
        QVERIFY(!s.stream.limited());
        QCOMPARE(s.stream.countIn(Paths.at(1)), 2);
        QCOMPARE(s.stream.countIn(Paths.at(2)), 0);
        QCOMPARE(s.stream.countIn(Paths.at(3)), 1);
        // The first match on show: the first over all days.
        QCOMPARE(s.find.current(), 0);
        QCOMPARE(s.stream.current(), 0);
    }

    void diacriticsAndOptions()
    {
        Stream s({u"- dziś\n"_s, u"- Gęślą jaźń\n"_s, u"- GESLA\n"_s});
        s.search(0, u"gesla"_s);
        QCOMPARE(s.stream.count(), 2);
        s.find.setMatchDiacritics(true);
        s.find.refresh();
        s.stream.refresh();
        QCOMPARE(s.stream.count(), 1); // "GESLA" only
        s.find.setMatchCase(true);
        s.find.refresh();
        s.stream.refresh();
        QCOMPARE(s.stream.count(), 0);
    }

    void nextGoesOnToTheNextDay()
    {
        Stream s({u"- kawa\n"_s, u"- nic\n"_s, u"- kawa i kawa\n"_s, u"- kawa\n"_s});
        s.search(0, u"kawa"_s);
        QSignalSpy requested(&s.stream, &StreamFind::dayRequested);
        QSignalSpy targets(&s.find, &NoteFind::targetRequested);
        QCOMPARE(s.stream.current(), 0);
        s.stream.next(); // day 0 has no more: day 2 (day 1 has none)
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first().at(0).toInt(), 2);
        s.settle();
        QCOMPARE(s.activeRow(), 2);
        QCOMPARE(s.find.current(), 0);
        QCOMPARE(s.stream.current(), 1);
        QVERIFY(!targets.isEmpty());
        s.stream.next(); // within day 2
        QCOMPARE(s.activeRow(), 2);
        QCOMPARE(s.stream.current(), 2);
        s.stream.next(); // day 3
        s.settle();
        QCOMPARE(s.activeRow(), 3);
        QCOMPARE(s.stream.current(), 3);
        s.stream.next(); // round the stream: day 0 again
        s.settle();
        QCOMPARE(s.activeRow(), 0);
        QCOMPARE(s.stream.current(), 0);
    }

    void previousGoesToTheLastMatchOfTheDayBefore()
    {
        Stream s({u"- kawa\n"_s, u"- kawa, kawa, kawa\n"_s, u"- nic\n"_s});
        s.search(0, u"kawa"_s);
        s.stream.previous(); // round the stream backwards: the last day with matches is day 1
        s.settle();
        QCOMPARE(s.activeRow(), 1);
        QCOMPARE(s.find.current(), 2); // its last match
        QCOMPARE(s.stream.current(), 3);
        s.stream.previous();
        QCOMPARE(s.stream.current(), 2);
        s.stream.previous();
        s.stream.previous(); // day 0
        s.settle();
        QCOMPARE(s.activeRow(), 0);
        QCOMPARE(s.stream.current(), 0);
    }

    void onlyThisDayRoundsWithinIt()
    {
        Stream s({u"- kawa\n- kawa\n"_s, u"- nic\n"_s});
        s.search(0, u"kawa"_s);
        QSignalSpy requested(&s.stream, &StreamFind::dayRequested);
        s.stream.next();
        QCOMPARE(s.find.current(), 1);
        s.stream.next(); // no other day has one: the first match of this day
        QCOMPARE(requested.size(), 0);
        QCOMPARE(s.find.current(), 0);
        s.stream.previous();
        QCOMPARE(s.find.current(), 1);
    }

    void newQueryGoesToTheNearestDay()
    {
        // Nothing in the day written in: the search goes down to the first day that has a match.
        Stream s({u"- herbata\n"_s, u"- nic\n"_s, u"- żółw\n"_s});
        s.search(0, u"herbata"_s);
        QCOMPARE(s.activeRow(), 0);
        s.find.setQuery(u"żółw"_s);
        s.find.refresh();
        s.settle();
        QCOMPARE(s.activeRow(), 2);
        QCOMPARE(s.find.current(), 0);
        QCOMPARE(s.stream.current(), 0);
        // Nothing anywhere: it stays.
        s.find.setQuery(u"słoń"_s);
        s.find.refresh();
        s.settle();
        QCOMPARE(s.activeRow(), 2);
        QCOMPARE(s.stream.count(), 0);
    }

    void daysOnDiskFollowChanges()
    {
        Stream s({u"- kawa\n"_s, u"- nic\n"_s});
        s.search(0, u"kawa"_s);
        QCOMPARE(s.stream.count(), 1);
        // Synced in: a day's file changes on disk.
        s.host.externalWrite(Paths.at(1), u"- kawa i kawa\n"_s);
        s.stream.refresh();
        QCOMPARE(s.stream.count(), 3);
        // A day attached: its unsaved text counts.
        s.open(1);
        s.pages[1]->setEditText(0, u"kawa"_s);
        s.stream.refresh();
        QCOMPARE(s.stream.count(), 2);
        // A new day at the top of the stream.
        auto *item = new QStandardItem;
        item->setData(u"journals/2026-11-04.md"_s, PathRole);
        s.host.setFile(u"journals/2026-11-04.md"_s, u"- kawa\n"_s);
        s.model.insertRow(0, item);
        s.stream.refresh();
        QCOMPARE(s.stream.count(), 3);
        QCOMPARE(s.stream.current(), 1); // one match above the day written in now
        s.model.removeRow(0);
        s.stream.refresh();
        QCOMPARE(s.stream.count(), 2);
    }

    void dayLimit()
    {
        Stream s({u"- kawa\n"_s, u"- kawa\n"_s, u"- kawa\n"_s, u"- kawa\n"_s});
        s.stream.setDayLimit(2);
        s.search(0, u"kawa"_s);
        QCOMPARE(s.stream.searchedDays(), 2);
        QVERIFY(s.stream.limited());
        QCOMPARE(s.stream.count(), 2);
        s.stream.setDayLimit(0);
        s.stream.refresh();
        QVERIFY(!s.stream.limited());
        QCOMPARE(s.stream.count(), 4);
    }

    void replaceGoesOnAfterTheDaysLast()
    {
        Stream s({u"- kawa\n"_s, u"- kawa\n"_s});
        s.search(0, u"kawa"_s);
        QCOMPARE(s.find.current(), 0);
        QVERIFY(s.stream.replaceCurrent(u"herbata"_s));
        s.settle();
        // Day 0 has none left: day 1's match is on show, nothing replaced there yet.
        QCOMPARE(s.pages[0]->text(), u"- herbata\n"_s);
        QCOMPARE(s.activeRow(), 1);
        QCOMPARE(s.find.current(), 0);
        QCOMPARE(s.host.file(Paths.at(1)), u"- kawa\n"_s);
        QCOMPARE(s.stream.count(), 1);
    }

    void replaceAllAndUndo()
    {
        Stream s({u"- kawa\n"_s, u"- kawa, kawa\n"_s, u"- nic\n"_s, u"- zimna kawa\r\n"_s});
        s.open(1); // attached: its controller replaces (its own undo)
        s.search(0, u"kawa"_s);
        QCOMPARE(s.stream.count(), 4);
        const QVariantMap r = s.stream.replaceAll(u"herbata"_s);
        QCOMPARE(r.value(u"count"_s).toInt(), 4);
        QCOMPARE(r.value(u"days"_s).toInt(), 3);
        QVERIFY(r.value(u"token"_s).toInt() > 0);
        QCOMPARE(s.pages[0]->text(), u"- herbata\n"_s);
        QCOMPARE(s.pages[1]->text(), u"- herbata, herbata\n"_s);
        // Day 3 on disk only: written with its own line ending.
        QCOMPARE(s.host.file(Paths.at(3)), u"- zimna herbata\r\n"_s);
        QCOMPARE(s.stream.count(), 0);
        const QVariantMap u = s.stream.undo(r.value(u"token"_s).toInt());
        QCOMPARE(u.value(u"restored"_s).toInt(), 3);
        QVERIFY(u.value(u"left"_s).toStringList().isEmpty());
        QCOMPARE(s.pages[0]->text(), u"- kawa\n"_s);
        QCOMPARE(s.pages[1]->text(), u"- kawa, kawa\n"_s);
        QCOMPARE(s.host.file(Paths.at(3)), u"- zimna kawa\r\n"_s);
        s.stream.refresh(); // (the day written in recounts a moment after its undo)
        QCOMPARE(s.stream.count(), 4);
        // Once only.
        QCOMPARE(s.stream.undo(r.value(u"token"_s).toInt()).value(u"restored"_s).toInt(), 0);
    }

    void undoLeavesWhatChangedSince()
    {
        Stream s({u"- kawa\n"_s, u"- kawa\n"_s});
        s.search(0, u"kawa"_s);
        const QVariantMap r = s.stream.replaceAll(u"mleko"_s);
        QCOMPARE(r.value(u"count"_s).toInt(), 2);
        // Day 1 edited elsewhere after the replace: its undo would lose that.
        s.host.externalWrite(Paths.at(1), u"- mleko i miód\n"_s);
        const QVariantMap u = s.stream.undo(r.value(u"token"_s).toInt());
        QCOMPARE(u.value(u"restored"_s).toInt(), 1);
        QCOMPARE(u.value(u"left"_s).toStringList(), QStringList{Paths.at(1)});
        QCOMPARE(s.host.file(Paths.at(1)), u"- mleko i miód\n"_s);
        QCOMPARE(s.pages[0]->text(), u"- kawa\n"_s);
    }

    void closedBarCountsNothing()
    {
        Stream s({u"- kawa\n"_s, u"- kawa\n"_s});
        s.search(0, u"kawa"_s);
        QCOMPARE(s.stream.count(), 2);
        s.find.setActive(false);
        QCOMPARE(s.stream.count(), 0);
        QCOMPARE(s.stream.current(), -1);
        QCOMPARE(s.stream.searchedDays(), 0);
        // A day destroyed while scrolled away: counted from its file again.
        s.find.setActive(true);
        s.close(0);
        s.find.setController(nullptr);
        s.stream.refresh();
        QCOMPARE(s.stream.count(), 2);
    }
};

QTEST_MAIN(TestStreamFind)
#include "tst_editor_streamfind.moc"
