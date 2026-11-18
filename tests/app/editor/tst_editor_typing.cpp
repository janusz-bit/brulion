// Typing costs the edited block, not the page (docs/PERFORMANCE.md "Typing: a keystroke costs the
// block"): BlockListModel's typing fast path (an edit that changes only the text of blocks
// refreshes those rows and the rows that follow every change of the page) against the full
// refresh of every row, and PageController's dirty check while one block is typed in against the
// comparison of the whole text.
//
// Two editors open the same page, one with the model's fast path switched off
// (BlockListModel::setTypingFastPath(false)); scripted and random edits go to both, and after
// every step the rows a view looks at (a window of rows that moves, as a scrolled list would)
// must have the same value in every role in both, and every value that changed since the step
// before must have been told (dataChanged with its role, or with none) — a view keeps what it was
// told. The dirty flag must say whether the text differs from the file's.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Pisanie.md"_s;

// A page with everything that follows other blocks: a task's subtask progress, references to a
// block of this page, an inline calculation over the frontmatter, a calc block, a table with a
// formula, a chart of it, an embed of this page's section.
const QString Content = u"---\ntytuł: Próba\nbudżet: 100\n---\n# Pisanie w miejscu\n\n"
                        u"- [ ] Zadanie główne\n  - [ ] Podzadanie pierwsze\n  - [x] Podzadanie drugie ^abc\n"
                        u"  - Zwykły punkt z #tagiem\n- Odwołanie do ((abc)) i [[#^abc]]\n"
                        u"- Koszt: `= budżet * 2` zł\n- ![[#Plan]]\n\n## Plan\n\n- Krok pierwszy\n- Krok drugi\n"
                        u"  - Głębiej: żółć\n\n```calc\na = 5\nb = a * 2\n```\n\n"
                        u"| Pozycja | Cena |\n|---|---|\n| Chleb | 4 |\n| Masło | 7 |\n| Suma | =sum(above) |\n\n"
                        u"```chart\ntype: bar\nx: Pozycja\ny: Cena\n```\n\n"
                        u"Akapit zwykły z [[Inna strona]] i ==zaznaczeniem==.\n"_s;

/// A value as text (NaN equals NaN here: charts hold them).
QString dump(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::QVariantMap: {
        const QVariantMap map = v.toMap();
        QString out = u"{"_s;
        for (auto it = map.cbegin(); it != map.cend(); ++it)
            out += it.key() + u':' + dump(it.value()) + u',';
        return out + u'}';
    }
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        QString out = u"["_s;
        for (const QVariant &item : v.toList())
            out += dump(item) + u',';
        return out + u']';
    }
    case QMetaType::Double:
    case QMetaType::Float: {
        const double d = v.toDouble();
        return std::isnan(d) ? u"NaN"_s : QString::number(d, 'g', 17);
    }
    default: return QString::fromLatin1(v.typeName() ? v.typeName() : "null") + u'=' + v.toString();
    }
}

struct Editor {
    FakeEditorHost host;
    PageController page;
    /// Told since the last look: row -> roles (an empty list: all).
    QHash<int, QList<QList<int>>> told;
    bool structureChanged = false;

    explicit Editor(const QString &content, bool fast)
    {
        host.setFile(Page, content);
        page.setNow(u"2026-11-18 10:00"_s);
        page.setSaveDelay(1000000); // saved by the tests only
        page.setHost(&host);
        page.setPath(Page);
        model()->setTypingFastPath(fast);
        QObject::connect(model(), &QAbstractItemModel::dataChanged,
                         [this](const QModelIndex &from, const QModelIndex &to, const QList<int> &roles) {
                             for (int r = from.row(); r <= to.row(); ++r)
                                 told[r].append(roles);
                         });
        for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
            QObject::connect(model(), signal, [this] { structureChanged = true; });
        QObject::connect(model(), &QAbstractItemModel::rowsMoved, [this] { structureChanged = true; });
        QObject::connect(model(), &QAbstractItemModel::modelReset, [this] { structureChanged = true; });
    }

    BlockListModel *model() const { return page.blocks(); }
    int count() const { return model()->rowCount(); }
    bool wasTold(int row, int role) const
    {
        for (const QList<int> &roles : told.value(row)) {
            if (roles.isEmpty() || roles.contains(role))
                return true;
        }
        return false;
    }
};

/// The roles of the rows a view looks at, by row.
using Look = QHash<int, QHash<int, QString>>;

Look lookAt(const BlockListModel *m, const QList<int> &rows)
{
    QList<int> roles = m->roleNames().keys();
    roles.append(Qt::DisplayRole);
    std::ranges::sort(roles);
    Look out;
    for (const int row : rows) {
        QHash<int, QString> values;
        for (const int role : std::as_const(roles))
            values.insert(role, dump(m->data(m->index(row), role)));
        out.insert(row, values);
    }
    return out;
}

/// Both editors after a step, looking at `rows`: the same values, every change told.
struct Pair {
    Editor fast;
    Editor full;
    Look lastLook;           ///< the fast editor's at the step before
    Look fullLook;           ///< the full one's
    QList<quint64> lastUids; ///< its rows' uids then
    QString step;

    explicit Pair(const QString &content) : fast(content, true), full(content, false) { }

    template<typename F>
    void both(F &&op)
    {
        op(fast);
        op(full);
    }

    QList<quint64> uids() const
    {
        QList<quint64> out;
        for (int r = 0; r < fast.count(); ++r)
            out.append(fast.model()->uidAt(r));
        return out;
    }

    /// Compares, then starts the next step. Fails the test with what differs.
    bool check(const QList<int> &rows)
    {
        if (fast.count() != full.count()) {
            qWarning("%s: %d rows against %d", qPrintable(step), fast.count(), full.count());
            return false;
        }
        if (fast.page.text() != full.page.text()) {
            qWarning("%s: the texts differ", qPrintable(step));
            return false;
        }
        for (const Editor *e : {&fast, &full}) {
            const bool dirty = e->page.text() != e->page.savedText();
            if (e->page.dirty() != dirty) {
                qWarning("%s: dirty %d, the text %s the file's", qPrintable(step), e->page.dirty(),
                         dirty ? "differs from" : "is");
                return false;
            }
        }
        QList<int> shown;
        for (const int r : rows) {
            if (r >= 0 && r < fast.count())
                shown.append(r);
        }
        const Look a = lookAt(fast.model(), shown);
        const Look b = lookAt(full.model(), shown);
        const QHash<int, QByteArray> names = fast.model()->roleNames();
        for (const int r : std::as_const(shown)) {
            for (auto it = a[r].cbegin(); it != a[r].cend(); ++it) {
                // (uids are unique in the process: each document has its own)
                if (it.key() != BlockListModel::UidRole && it.value() != b[r].value(it.key())) {
                    qWarning("%s: row %d, role %s: %s against %s", qPrintable(step), r,
                             names.value(it.key(), "display").constData(), qPrintable(it.value()),
                             qPrintable(b[r].value(it.key())));
                    return false;
                }
            }
        }
        // What a view kept from the step before is told when it changed (the rows kept their place).
        const QList<quint64> now = uids();
        for (Editor *e : {&fast, &full}) {
            const Look &before = e == &fast ? lastLook : fullLook;
            if (!e->structureChanged && now == lastUids) {
                const Look after = e == &fast ? a : b;
                for (auto row = before.cbegin(); row != before.cend(); ++row) {
                    if (!after.contains(row.key()))
                        continue;
                    for (auto it = row->cbegin(); it != row->cend(); ++it) {
                        if (after[row.key()].value(it.key()) != it.value() && !e->wasTold(row.key(), it.key())) {
                            qWarning("%s: %s model, row %d: role %s changed untold (%s -> %s)", qPrintable(step),
                                     e == &fast ? "fast" : "full", row.key(),
                                     names.value(it.key(), "display").constData(), qPrintable(it.value()),
                                     qPrintable(after[row.key()].value(it.key())));
                            return false;
                        }
                    }
                }
            }
            e->told.clear();
            e->structureChanged = false;
        }
        lastLook = a;
        fullLook = b;
        lastUids = now;
        return true;
    }
};

QList<int> allRows(int n)
{
    QList<int> rows;
    for (int r = 0; r < n; ++r)
        rows.append(r);
    return rows;
}

int rowStarting(const Editor &e, const QString &prefix)
{
    for (int r = 0; r < e.count(); ++r) {
        if (e.model()->editTextAt(r).startsWith(prefix))
            return r;
    }
    return -1;
}

/// A page of `blocks` list items in nested groups (the benchmarks' outline).
QString outline(int blocks)
{
    QString out;
    for (int i = 0; i < blocks; ++i) {
        const int depth = i % 5 == 0 ? 0 : i % 5 == 4 ? 2 : 1;
        out += QString(depth * 2, u' ') + u"- Punkt %1 z tekstem: zażółć gęślą jaźń #tag%2\n"_s.arg(i).arg(i % 7);
    }
    return out;
}

} // namespace

class TstEditorTyping : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void scriptedTypingMatchesFullRefresh()
    {
        Pair p(Content);
        QVERIFY(p.check(allRows(p.fast.count())));
        const int sub = rowStarting(p.fast, u"Podzadanie pierwsze"_s);
        const int ref = rowStarting(p.fast, u"Odwołanie"_s);
        const int step1 = rowStarting(p.fast, u"Krok pierwszy"_s);
        const int calc = rowStarting(p.fast, u"a = 5"_s);
        const int table = rowStarting(p.fast, u"| Pozycja"_s);
        const int para = rowStarting(p.fast, u"Akapit"_s);
        QVERIFY(sub > 0 && ref > sub && step1 > ref && calc > step1 && table > calc && para > table);
        const QList<int> all = allRows(p.fast.count());
        const auto type = [&](int row, const QString &text, const QString &what) {
            p.step = what;
            p.both([&](Editor &e) {
                if (e.page.editingRow() != row)
                    e.page.startEditing(row, 0);
                e.page.setEditText(row, text, int(text.size()));
            });
            return p.check(all);
        };
        // Typing in the referenced block: the references follow it.
        QVERIFY(type(sub + 1, u"Podzadanie drugie ąę ^abc"_s, u"referenced block"_s));
        // A subtask done and undone by its text: the parent's progress follows.
        QVERIFY(type(sub, u"[x] Podzadanie pierwsze"_s, u"subtask done"_s));
        QVERIFY(type(sub, u"Podzadanie pierwsze ść"_s, u"subtask text"_s));
        // A plain item made a task by its text, a subtask ticked: the parent's "1/3".
        QVERIFY(type(sub + 2, u"[ ] Zwykły punkt z #tagiem"_s, u"item made a task"_s));
        p.step = u"subtask ticked"_s;
        p.both([&](Editor &e) { e.page.toggleTask(sub); });
        QVERIFY(p.check(all));
        // The embedded section's text.
        QVERIFY(type(step1, u"Krok pierwszy, ale łatwiejszy"_s, u"embedded section"_s));
        // A calc block's definition: the inline calculation and the table do not use it, the chart
        // follows the page.
        QVERIFY(type(calc, u"a = 7\nb = a * 2"_s, u"calc definition"_s));
        QVERIFY(type(table, u"| Pozycja | Cena |\n|---|---|\n| Chleb | 5 |\n| Masło | 7 |\n| Suma | =sum(above) |"_s,
                     u"table cell"_s));
        // A paragraph turned into a heading (another kind: the full way), and back.
        QVERIFY(type(para, u"# Akapit zwykły"_s, u"paragraph to heading"_s));
        QVERIFY(type(para, u"Akapit zwykły znowu"_s, u"heading to paragraph"_s));
        // A line break: the lines after it move.
        QVERIFY(type(step1, u"Krok pierwszy\ndruga linia"_s, u"line break"_s));
        QVERIFY(type(step1, u"Krok pierwszy"_s, u"line break gone"_s));
        // The frontmatter: the inline calculation over its number.
        QVERIFY(type(0, u"tytuł: Próba\nbudżet: 250"_s, u"frontmatter"_s));
        // Undo and redo of all that, a structural edit, typing after it.
        for (int k = 0; k < 4; ++k) {
            p.step = u"undo %1"_s.arg(k);
            p.both([](Editor &e) { e.page.undo(); });
            QVERIFY(p.check(allRows(p.fast.count())));
        }
        p.step = u"split"_s;
        p.both([&](Editor &e) { e.page.split(ref, 3); });
        QVERIFY(p.check(allRows(p.fast.count())));
        QVERIFY(type(ref + 1, u"wołanie do ((abc)) dalej"_s, u"typing after a split"_s));
    }

    void randomTypingMatchesFullRefresh_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<quint32>("seed");
        QTest::newRow("all kinds") << Content << 1u;
        QTest::newRow("all kinds, CRLF") << QString(Content).replace(u"\n"_s, u"\r\n"_s) << 2u;
        QTest::newRow("outline") << outline(120) << 3u;
    }

    void randomTypingMatchesFullRefresh()
    {
        QFETCH(QString, content);
        QFETCH(quint32, seed);
        QRandomGenerator rng(seed);
        Pair p(content);
        QVERIFY(p.check(allRows(p.fast.count())));
        static const QString letters = u"abcząęłóśżź ńĆ#[]()^=*-:|0123456789"_s; // (no ` : a fence is saved closed)
        for (int step = 0; step < 1500; ++step) {
            const int n = p.fast.count();
            int row = p.fast.page.editingRow();
            if (row < 0 || rng.bounded(12) == 0) {
                row = int(rng.bounded(n));
                p.both([row](Editor &e) { e.page.startEditing(row, 0); });
            }
            const QString text = p.fast.model()->editTextAt(row);
            const int what = int(rng.bounded(100));
            QString now = text;
            p.step = u"step %1 (%2) row %3"_s.arg(step).arg(what).arg(row);
            if (what < 55) { // a letter
                now.insert(int(rng.bounded(now.size() + 1)), letters[int(rng.bounded(letters.size()))]);
            } else if (what < 80) { // Backspace
                if (!now.isEmpty())
                    now.remove(int(rng.bounded(now.size())), 1);
            } else if (what < 83) { // a line break typed with Shift+Enter
                now.insert(int(rng.bounded(now.size() + 1)), u'\n');
            } else if (what < 86) {
                p.both([](Editor &e) { e.page.undo(); });
            } else if (what < 88) {
                p.both([](Editor &e) { e.page.redo(); });
            } else if (what < 90) {
                p.both([row](Editor &e) { e.page.split(row, 0); });
            } else if (what < 92) {
                p.both([row](Editor &e) { e.page.indent(row); });
            } else if (what < 94) {
                p.both([row](Editor &e) { e.page.toggleTask(row); });
            } else if (what < 96) {
                p.both([](Editor &e) { QVERIFY(e.page.save()); });
            } else { // a letter typed and taken back: the text as it was (the file's after a save)
                const QString typed = QString(text).insert(int(rng.bounded(text.size() + 1)), u'ł');
                p.both([&](Editor &e) {
                    const int r = e.page.editingRow() >= 0 ? e.page.editingRow() : row;
                    e.page.setEditText(r, typed, int(typed.size()));
                });
            }
            if (now != text)
                p.both([&](Editor &e) {
                    const int r = e.page.editingRow() >= 0 ? e.page.editingRow() : row;
                    e.page.setEditText(r, now, int(now.size()));
                });
            // A window of rows a view shows, now and then all of them.
            const int count = p.fast.count();
            QList<int> rows;
            if (rng.bounded(10) == 0) {
                rows = allRows(count);
            } else {
                const int first = int(rng.bounded(count));
                for (int r = first; r < std::min(count, first + 12); ++r)
                    rows.append(r);
            }
            QVERIFY2(p.check(rows), qPrintable(p.step));
        }
    }

    void typedBackIsClean()
    {
        // Dirty while the block differs from the file, clean when typed back — also after another
        // block was edited and saved, and while another block is dirty.
        Editor e(outline(50), true);
        e.page.startEditing(3, 0);
        const QString was = e.model()->editTextAt(3);
        e.page.setEditText(3, was + u"ą"_s, int(was.size()) + 1);
        QVERIFY(e.page.dirty());
        e.page.setEditText(3, was + u"ąę"_s, int(was.size()) + 2);
        QVERIFY(e.page.dirty());
        e.page.setEditText(3, was + u"ą"_s, int(was.size()) + 1);
        e.page.setEditText(3, was, int(was.size()));
        QVERIFY(!e.page.dirty());
        e.page.setEditText(3, was + u"ż"_s, int(was.size()) + 1);
        QVERIFY(e.page.save());
        QVERIFY(!e.page.dirty());
        e.page.setEditText(3, was, int(was.size()));
        QVERIFY(e.page.dirty());
        e.page.setEditText(3, was + u"ż"_s, int(was.size()) + 1);
        QVERIFY(!e.page.dirty());
        // Another block dirty: typing here never makes the page clean.
        e.page.startEditing(10, 0);
        const QString other = e.model()->editTextAt(10);
        e.page.setEditText(10, other + u"x"_s, int(other.size()) + 1);
        e.page.startEditing(3, 0);
        e.page.setEditText(3, was + u"żó"_s, int(was.size()) + 2);
        QVERIFY(e.page.dirty());
        e.page.setEditText(3, was + u"ż"_s, int(was.size()) + 1);
        QVERIFY(e.page.dirty());
        e.page.startEditing(10, 0);
        e.page.setEditText(10, other, int(other.size()));
        QVERIFY(!e.page.dirty());
    }

    void keystrokeCostsTheBlock()
    {
        // A keystroke in a 5000-block outline: setEditText() (the document, the undo step, the
        // dirty check, the model's refresh) with the fast paths and with the model's off. Times of
        // one process (Debug: compared, not judged against a budget).
        qint64 times[2] = {0, 0};
        for (const bool fast : {false, true}) {
            Editor e(outline(5000), fast);
            const int row = 2500;
            e.page.startEditing(row, 0);
            QString text = e.model()->editTextAt(row);
            QElapsedTimer clock;
            clock.start();
            for (int k = 0; k < 200; ++k) {
                text += k % 10 == 9 ? u' ' : u'ą';
                e.page.setEditText(row, text, int(text.size()));
            }
            times[fast] = clock.nsecsElapsed() / 200;
            QVERIFY(e.page.dirty());
        }
        qInfo("keystroke in 5000 blocks: %.3f ms with the full refresh, %.3f ms with the fast path",
              double(times[0]) / 1e6, double(times[1]) / 1e6);
        QVERIFY(times[1] < times[0]);
    }
};

QTEST_MAIN(TstEditorTyping)
#include "tst_editor_typing.moc"
