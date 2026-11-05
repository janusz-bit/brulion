// ShortcutMap: custom keyboard shortcuts — parsing key chords, what cannot be a shortcut
// (typing keys, the editor's own keys, AltGr letters), conflicts and taking a shortcut over,
// reset, persistence in the settings file, and stored values that are malformed.
#include "appsettings.h"
#include "shortcutmap.h"

#include <QFile>
#include <QKeySequence>
#include <QSet>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::AppSettings;
using brulion::shell::ShortcutMap;

namespace {

QList<ShortcutMap::Action> sampleActions()
{
    return {
            {u"palette"_s, u"Paleta poleceń"_s, u"Ctrl+K"_s},
            {u"journal.today"_s, u"Dzisiejszy dziennik"_s, u"Ctrl+J"_s},
            {u"journal.yesterday"_s, u"Wczorajszy dziennik"_s, {}},
            {u"go.settings"_s, u"Ustawienia"_s, u"Ctrl+,"_s},
            {u"panel.toggle"_s, u"Panel boczny"_s, u"Ctrl+\\"_s},
            {u"view.focus"_s, u"Tryb skupienia"_s, u"F8"_s},
            {u"nav.back"_s, u"Wstecz"_s, u"Alt+Left"_s},
    };
}

} // namespace

class TestShortcutMap : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString nextFile() { return m_tmp.filePath(u"s%1.ini"_s.arg(++m_n)); }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void normalize_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("normalized");

        QTest::newRow("as written") << u"Ctrl+Shift+F"_s << u"Ctrl+Shift+F"_s;
        QTest::newRow("lower case") << u"ctrl+shift+f"_s << u"Ctrl+Shift+F"_s;
        QTest::newRow("spaces around") << u"  Ctrl+J \t"_s << u"Ctrl+J"_s;
        QTest::newRow("modifier order") << u"Shift+Ctrl+F"_s << u"Ctrl+Shift+F"_s;
        QTest::newRow("comma key") << u"Ctrl+,"_s << u"Ctrl+,"_s;
        QTest::newRow("plus key") << u"Ctrl++"_s << u"Ctrl++"_s;
        QTest::newRow("backslash") << u"Ctrl+\\"_s << u"Ctrl+\\"_s;
        QTest::newRow("arrow") << u"Alt+Left"_s << u"Alt+Left"_s;
        QTest::newRow("function key") << u"F8"_s << u"F8"_s;
        QTest::newRow("letter alone") << u"a"_s << u"A"_s;
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("blank") << u"   "_s << QString();
        QTest::newRow("two chords") << u"Ctrl+K, Ctrl+S"_s << QString();
        QTest::newRow("unknown key") << u"Ctrl+Zażółć"_s << QString();
        QTest::newRow("modifier alone") << u"Ctrl"_s << QString();
        QTest::newRow("garbage") << u"Ctrl+Shift+Nonsense"_s << QString();
    }
    void normalize()
    {
        QFETCH(QString, text);
        QFETCH(QString, normalized);
        QCOMPARE(ShortcutMap::normalize(text), normalized);
    }

    void check_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("sequence");
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("conflictId");

        QTest::newRow("free") << u"journal.today"_s << u"Ctrl+Alt+J"_s << u"ok"_s << QString();
        QTest::newRow("free function key") << u"journal.yesterday"_s << u"Shift+F5"_s << u"ok"_s << QString();
        QTest::newRow("its own") << u"journal.today"_s << u"ctrl+j"_s << u"same"_s << QString();
        QTest::newRow("another's") << u"journal.today"_s << u"Ctrl+K"_s << u"conflict"_s << u"palette"_s;
        QTest::newRow("another's in other words")
                << u"journal.yesterday"_s << u"ctrl+\\"_s << u"conflict"_s << u"panel.toggle"_s;
        QTest::newRow("letter") << u"journal.today"_s << u"J"_s << u"typing"_s << QString();
        QTest::newRow("shift letter") << u"journal.today"_s << u"Shift+J"_s << u"typing"_s << QString();
        QTest::newRow("escape") << u"journal.today"_s << u"Esc"_s << u"typing"_s << QString();
        QTest::newRow("tab") << u"journal.today"_s << u"Tab"_s << u"typing"_s << QString();
        QTest::newRow("copy") << u"journal.today"_s << u"Ctrl+C"_s << u"reserved"_s << QString();
        QTest::newRow("undo") << u"journal.today"_s << u"Ctrl+Z"_s << u"reserved"_s << QString();
        QTest::newRow("fold") << u"journal.today"_s << u"Ctrl+."_s << u"reserved"_s << QString();
        QTest::newRow("move block") << u"journal.today"_s << u"Alt+Up"_s << u"reserved"_s << QString();
        QTest::newRow("drawing duplicate") << u"journal.today"_s << u"Ctrl+D"_s << u"reserved"_s << QString();
        QTest::newRow("pdf next match") << u"journal.today"_s << u"F3"_s << u"reserved"_s << QString();
        QTest::newRow("altgr ą") << u"journal.today"_s << u"Ctrl+Alt+A"_s << u"reserved"_s << QString();
        QTest::newRow("altgr ż") << u"journal.today"_s << u"Ctrl+Alt+Z"_s << u"reserved"_s << QString();
        QTest::newRow("altgr capital Ś") << u"journal.today"_s << u"Ctrl+Alt+Shift+S"_s << u"reserved"_s << QString();
        QTest::newRow("two chords") << u"journal.today"_s << u"Ctrl+K, Ctrl+J"_s << u"invalid"_s << QString();
        QTest::newRow("nonsense") << u"journal.today"_s << u"Hyper+Zażółć"_s << u"invalid"_s << QString();
        QTest::newRow("unknown command") << u"nie.ma"_s << u"Ctrl+Alt+J"_s << u"unknown"_s << QString();
    }
    void check()
    {
        QFETCH(QString, id);
        QFETCH(QString, sequence);
        QFETCH(QString, status);
        QFETCH(QString, conflictId);
        AppSettings settings(nextFile(), m_tmp.path());
        ShortcutMap map(&settings);
        map.setActions(sampleActions());
        const QVariantMap verdict = map.check(id, sequence);
        QCOMPARE(verdict.value(u"status"_s).toString(), status);
        QCOMPARE(verdict.value(u"conflictId"_s).toString(), conflictId);
        // Everything but a free one says why, and only a free one (or its own) is assigned.
        QCOMPARE(verdict.value(u"message"_s).toString().isEmpty(), status == u"ok" || status == u"same");
        if (status == u"conflict")
            QVERIFY(verdict.value(u"message"_s).toString().contains(map.title(conflictId)));
        QCOMPARE(map.assign(id, sequence), status == u"ok" || status == u"same");
    }

    void defaults()
    {
        AppSettings settings(nextFile(), m_tmp.path());
        ShortcutMap map(&settings);
        map.setActions(sampleActions());
        QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+J"_s);
        QCOMPARE(map.sequence(u"journal.yesterday"_s), QString());
        QCOMPARE(map.sequence(u"nie.ma"_s), QString());
        QCOMPARE(map.actionFor(u"ctrl+k"_s), u"palette"_s);
        QCOMPARE(map.customCount(), 0);
        QCOMPARE(map.sequences().size(), 6);
        QCOMPARE(map.bindings().size(), 6);
        QCOMPARE(map.bindings().first().toMap().value(u"id"_s).toString(), u"palette"_s);
        QVERIFY(!map.isCustom(u"journal.today"_s));
    }

    void assignAndPersist()
    {
        const QString file = nextFile();
        {
            AppSettings settings(file, m_tmp.path());
            ShortcutMap map(&settings);
            map.setActions(sampleActions());
            QSignalSpy changed(&map, &ShortcutMap::changed);
            QVERIFY(map.assign(u"journal.today"_s, u"ctrl+alt+j"_s));
            QCOMPARE(changed.size(), 1);
            QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+Alt+J"_s);
            QCOMPARE(map.actionFor(u"Ctrl+J"_s), QString()); // the old one is free
            QVERIFY(map.isCustom(u"journal.today"_s));
            // A command without a default gets one; another loses its shortcut.
            QVERIFY(map.assign(u"journal.yesterday"_s, u"Ctrl+J"_s));
            map.clear(u"panel.toggle"_s);
            QCOMPARE(map.sequence(u"panel.toggle"_s), QString());
            QCOMPARE(map.customCount(), 3);
            // The comma key is written so that it reads back as one string.
            QVERIFY(map.assign(u"go.settings"_s, u"Ctrl+Shift+,"_s));
            settings.sync();
        }
        AppSettings settings(file, m_tmp.path());
        ShortcutMap map(&settings);
        map.setActions(sampleActions());
        QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+Alt+J"_s);
        QCOMPARE(map.sequence(u"journal.yesterday"_s), u"Ctrl+J"_s);
        QCOMPARE(map.sequence(u"panel.toggle"_s), QString());
        QCOMPARE(map.sequence(u"go.settings"_s), u"Ctrl+Shift+,"_s);
        QCOMPARE(map.customCount(), 4);
        // Assigning its default again: no longer custom (nothing stored for it).
        QVERIFY(map.assign(u"journal.today"_s, u"Ctrl+Alt+J"_s));
        map.clear(u"journal.yesterday"_s); // its default is none: not custom either
        QVERIFY(map.assign(u"journal.today"_s, u"Ctrl+J"_s));
        QVERIFY(!map.isCustom(u"journal.today"_s));
        QVERIFY(!map.isCustom(u"journal.yesterday"_s));
        QVERIFY(!settings.shortcutOverrides().contains(u"journal.today"_s));
        QVERIFY(!settings.shortcutOverrides().contains(u"journal.yesterday"_s));
    }

    void conflictsAndTakeOver()
    {
        AppSettings settings(nextFile(), m_tmp.path());
        ShortcutMap map(&settings);
        map.setActions(sampleActions());
        // Refused without takeOver; nothing changes.
        QVERIFY(!map.assign(u"journal.today"_s, u"Ctrl+K"_s));
        QCOMPARE(map.sequence(u"palette"_s), u"Ctrl+K"_s);
        QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+J"_s);
        // Taken over: the palette is left without one.
        QVERIFY(map.assign(u"journal.today"_s, u"Ctrl+K"_s, true));
        QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+K"_s);
        QCOMPARE(map.sequence(u"palette"_s), QString());
        QVERIFY(map.isCustom(u"palette"_s));
        QCOMPARE(map.actionFor(u"Ctrl+K"_s), u"journal.today"_s);
        // Reset the palette: it takes Ctrl+K back, today's journal (which had it) has none.
        QCOMPARE(map.reset(u"palette"_s), u"journal.today"_s);
        QCOMPARE(map.sequence(u"palette"_s), u"Ctrl+K"_s);
        QCOMPARE(map.sequence(u"journal.today"_s), QString());
        // Reset today's journal: its default is free again.
        QCOMPARE(map.reset(u"journal.today"_s), QString());
        QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+J"_s);
        QCOMPARE(map.customCount(), 0);
        // A reserved key is never taken over.
        QVERIFY(!map.assign(u"journal.today"_s, u"Ctrl+C"_s, true));
        // Reset all.
        QVERIFY(map.assign(u"view.focus"_s, u"F9"_s));
        QVERIFY(map.assign(u"nav.back"_s, u"Alt+Shift+Left"_s));
        map.resetAll();
        QCOMPARE(map.customCount(), 0);
        QCOMPARE(map.sequence(u"view.focus"_s), u"F8"_s);
        QVERIFY(settings.shortcutOverrides().isEmpty());
    }

    void entries()
    {
        AppSettings settings(nextFile(), m_tmp.path());
        ShortcutMap map(&settings);
        map.setActions(sampleActions());
        QCOMPARE(map.entries().size(), sampleActions().size());
        // Title without diacritics or case, the id, the shortcut.
        const auto ids = [&](const QString &query) {
            QStringList out;
            for (const QVariant &v : map.entries(query))
                out.append(v.toMap().value(u"id"_s).toString());
            return out;
        };
        QCOMPARE(ids(u"DZIENNIK"_s), (QStringList{u"journal.today"_s, u"journal.yesterday"_s}));
        QCOMPARE(ids(u"polecen"_s), QStringList{u"palette"_s});
        QCOMPARE(ids(u"view.focus"_s), QStringList{u"view.focus"_s});
        QCOMPARE(ids(u"alt+left"_s), QStringList{u"nav.back"_s});
        QVERIFY(ids(u"żaden"_s).isEmpty());
        QVERIFY(map.assign(u"journal.yesterday"_s, u"Ctrl+Alt+Y"_s));
        const QVariantMap e = map.entries(u"wczoraj"_s).value(0).toMap();
        QCOMPARE(e.value(u"sequence"_s).toString(), u"Ctrl+Alt+Y"_s);
        QCOMPARE(e.value(u"defaultSequence"_s).toString(), QString());
        QVERIFY(e.value(u"custom"_s).toBool());
    }

    void keys_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<int>("modifiers");
        QTest::addColumn<QString>("sequence");

        QTest::newRow("ctrl+j") << int(Qt::Key_J) << int(Qt::ControlModifier) << u"Ctrl+J"_s;
        QTest::newRow("keypad") << int(Qt::Key_5) << int(Qt::ControlModifier | Qt::KeypadModifier) << u"Ctrl+5"_s;
        QTest::newRow("backtab") << int(Qt::Key_Backtab) << int(Qt::ShiftModifier) << u"Shift+Tab"_s;
        QTest::newRow("function key") << int(Qt::Key_F8) << 0 << u"F8"_s;
        QTest::newRow("modifier alone") << int(Qt::Key_Control) << int(Qt::ControlModifier) << QString();
        QTest::newRow("altgr alone") << int(Qt::Key_AltGr) << 0 << QString();
        QTest::newRow("unknown") << int(Qt::Key_unknown) << 0 << QString();
        QTest::newRow("no key") << 0 << int(Qt::ControlModifier) << QString();
    }
    void keys()
    {
        QFETCH(int, key);
        QFETCH(int, modifiers);
        QFETCH(QString, sequence);
        ShortcutMap map(nullptr);
        QCOMPARE(map.sequenceForKey(key, modifiers), sequence);
    }

    void malformedSettings()
    {
        const QString file = nextFile();
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(u"shortcuts/journal.today"_s, u"Ctrl+K, Ctrl+S"_s);                       // two chords
            raw.setValue(u"shortcuts/journal.yesterday"_s, QStringList{u"Ctrl+1"_s, u"Ctrl+2"_s}); // a list
            raw.setValue(u"shortcuts/go.settings"_s, u"Ctrl+C"_s);                                 // reserved
            raw.setValue(u"shortcuts/view.focus"_s, u"Q"_s);                                       // types text
            raw.setValue(u"shortcuts/nav.back"_s, u"Ctrl+Alt+Brak"_s);                             // no such key
            raw.setValue(u"shortcuts/nie.ma"_s, u"Ctrl+Alt+N"_s);                                  // no such command
            raw.setValue(u"shortcuts/panel.toggle"_s, 42); // a number: "42" types text
            raw.sync();
        }
        AppSettings settings(file, m_tmp.path());
        ShortcutMap map(&settings);
        map.setActions(sampleActions());
        QCOMPARE(map.customCount(), 0);
        for (const ShortcutMap::Action &a : sampleActions())
            QCOMPARE(map.sequence(a.id), a.defaultSequence);
        // A list written by hand into the file (unquoted commas).
        const QString handEdited = nextFile();
        {
            QFile f(handEdited);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[shortcuts]\njournal.today=Ctrl+K, Ctrl+S\njournal.yesterday=\"Ctrl+Alt+Y\"\npalette=\n");
        }
        AppSettings edited(handEdited, m_tmp.path());
        ShortcutMap map2(&edited);
        map2.setActions(sampleActions());
        QCOMPARE(map2.sequence(u"journal.today"_s), u"Ctrl+J"_s);
        QCOMPARE(map2.sequence(u"journal.yesterday"_s), u"Ctrl+Alt+Y"_s);
        QCOMPARE(map2.sequence(u"palette"_s), QString()); // stored as none
        QVERIFY(map2.isCustom(u"palette"_s));
    }

    void storedConflicts()
    {
        // Two custom ones on one key: the earlier action keeps it; a default taken by a custom one
        // gives way (its action has none).
        const QString file = nextFile();
        {
            QSettings raw(file, QSettings::IniFormat);
            raw.setValue(u"shortcuts/journal.yesterday"_s, u"Ctrl+Alt+Y"_s);
            raw.setValue(u"shortcuts/view.focus"_s, u"Ctrl+Alt+Y"_s);
            raw.setValue(u"shortcuts/nav.back"_s, u"Ctrl+J"_s);
            raw.sync();
        }
        AppSettings settings(file, m_tmp.path());
        ShortcutMap map(&settings);
        map.setActions(sampleActions());
        QCOMPARE(map.sequence(u"journal.yesterday"_s), u"Ctrl+Alt+Y"_s);
        QCOMPARE(map.sequence(u"view.focus"_s), u"F8"_s);
        QCOMPARE(map.sequence(u"nav.back"_s), u"Ctrl+J"_s);
        QCOMPARE(map.sequence(u"journal.today"_s), QString());
        QCOMPARE(map.actionFor(u"Ctrl+J"_s), u"nav.back"_s);
        // Every shortcut belongs to one action.
        QSet<QString> seen;
        for (const QVariant &v : map.bindings()) {
            const QString s = v.toMap().value(u"sequence"_s).toString();
            QVERIFY2(!seen.contains(s), qPrintable(s));
            seen.insert(s);
        }
    }

    // AppShell's Shortcut objects: every binding but the views' own and the editor's Ctrl+Shift+C.
    void shellBindings()
    {
        ShortcutMap map(nullptr);
        QList<ShortcutMap::Action> actions = sampleActions();
        actions.append({u"page.find"_s, u"Znajdź w notatce"_s, u"Ctrl+F"_s});
        actions.append({u"page.source"_s, u"Źródło Markdown"_s, u"Ctrl+E"_s});
        actions.append({u"block.copyRef"_s, u"Kopiuj odwołanie do bloku"_s, u"Ctrl+Shift+C"_s});
        map.setActions(actions);
        const auto ids = [](const QVariantList &rows) {
            QStringList out;
            for (const QVariant &row : rows) {
                const QVariantMap r = row.toMap();
                // A delegate cannot name a property "id": the same as "action".
                if (r.value(u"action"_s) != r.value(u"id"_s))
                    out.append(u"!"_s);
                out.append(r.value(u"id"_s).toString());
            }
            return out;
        };
        QCOMPARE(map.bindings().size(), 9);
        const QStringList shell = ids(map.shellBindings());
        QCOMPARE(shell, (QStringList{u"palette"_s, u"journal.today"_s, u"go.settings"_s, u"panel.toggle"_s,
                                     u"view.focus"_s, u"nav.back"_s}));
        // Another key for the block reference: the shell's Shortcut handles it.
        QVERIFY(map.assign(u"block.copyRef"_s, u"Ctrl+Alt+R"_s));
        QVERIFY(ids(map.shellBindings()).contains(u"block.copyRef"_s));
        QVERIFY(!ids(map.shellBindings()).contains(u"page.find"_s));
    }

    void withoutSettings()
    {
        ShortcutMap map(nullptr);
        map.setActions(sampleActions());
        QVERIFY(map.assign(u"journal.today"_s, u"Ctrl+Alt+J"_s));
        QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+Alt+J"_s);
        map.resetAll();
        QCOMPARE(map.sequence(u"journal.today"_s), u"Ctrl+J"_s);
    }
};

QTEST_MAIN(TestShortcutMap)
#include "tst_shortcutmap.moc"
