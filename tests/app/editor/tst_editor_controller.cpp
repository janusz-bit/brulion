// PageController + BlockListModel (src/app/editor): loading, every editing command, undo/redo,
// byte-for-byte round trip of untouched blocks, CRLF, zoom and folding, tokens, completions,
// slash commands, tables, links, debounced saving and changes made outside the editor.
// Runs with a FakeEditorHost (in-memory files) and a fixed clock.
#include "blocklistmodel.h"
#include "blocktext.h"
#include "dayclock.h"
#include "drawing/backdrop.h"
#include "drawing/drawing.h"
#include "drawing/svgwrite.h"
#include "fakeeditorhost.h"
#include "index/vaultindex.h"
#include "markdown/inline.h"
#include "markdown/inlinerenderer.h"
#include "markdown/parse.h"
#include "markdown/tokenedit.h"
#include "notebook/links.h"
#include "pagecontroller.h"
#include "pickedfiles.h"

#include <QAbstractItemModelTester>
#include <QBuffer>
#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QMimeData>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Strona.md"_s;
const QString Now = u"2026-10-15 10:00"_s;

/// A controller on `content` (with a model tester watching every signal, unless `NoModelTester`).
struct Fixture {
    /// The model tester checks the whole model after every signal: on pages of thousands of
    /// blocks it takes most of the time (the small pages of the other tests check the signals).
    struct NoModelTester { };

    FakeEditorHost host;
    PageController page;
    std::unique_ptr<QAbstractItemModelTester> tester;

    explicit Fixture(const QString &content, int saveDelay = 0, const QString &path = Page,
                     const QStringList &others = {})
        : Fixture(content, saveDelay, path, others, NoModelTester{})
    {
        tester = std::make_unique<QAbstractItemModelTester>(page.blocks(),
                                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
    }

    Fixture(const QString &content, int saveDelay, NoModelTester)
        : Fixture(content, saveDelay, Page, {}, NoModelTester{})
    { }

    Fixture(const QString &content, int saveDelay, const QString &path, const QStringList &others, NoModelTester)
    {
        host.setFile(path, content);
        host.setFile(u"Inna.md"_s, u"- Blok z innej strony #inne ^inny1\n- Drugi blok\n"_s);
        for (const QString &other : others)
            host.setFile(other, u"- inna strona o tej nazwie\n"_s);
        page.setNow(Now);
        page.setSaveDelay(saveDelay);
        page.setHost(&host);
        page.setPath(path);
    }

    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        const QHash<int, QByteArray> roles = model()->roleNames();
        return model()->data(model()->index(row), roles.key(name));
    }
    QString edit(int row) const { return role(row, "editText").toString(); }
    QString text() const { return page.text(); }
    int count() const { return model()->rowCount(); }
    QList<bool> visible() const
    {
        QList<bool> out;
        for (int r = 0; r < count(); ++r)
            out.append(role(r, "visible").toBool());
        return out;
    }
};

/// The blocks of a document: kind, depth, block id and body of each.
QStringList blocksOf(const markdown::BlockDocument &doc)
{
    QStringList out;
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &e = doc.at(i);
        out.append(kindName(e.kind) + u'/' + QString::number(e.depth) + u'/' + e.blockId + u'/' + e.body);
    }
    return out;
}

/// What the index takes from a note: tasks, block ids, links, tags and reminders.
QString indexedOf(const QString &content)
{
    const ParsedNote n = markdown::parseNote(Page, content);
    QStringList tasks;
    for (const Task &t : n.tasks)
        tasks.append(t.text);
    QStringList ids = n.blockIds.keys();
    ids.sort();
    QStringList links;
    for (const Link &l : n.links)
        links.append(l.target);
    QStringList reminders;
    for (const Reminder &r : n.reminders)
        reminders.append(r.text);
    return u"tasks="_s + tasks.join(u'|') + u" ids="_s + ids.join(u',') + u" links="_s + links.join(u',') + u" tags="_s
           + n.tags.join(u',') + u" reminders="_s + reminders.join(u'|');
}

/// The page saved and opened again in a new controller: its blocks are the ones the editor had.
void verifyReload(Fixture &f)
{
    QVERIFY(f.page.save());
    const QString saved = f.host.file(Page);
    QCOMPARE(saved, f.text());
    QCOMPARE(blocksOf(markdown::BlockDocument(saved)), blocksOf(f.model()->document()));
    PageController reopened;
    reopened.setNow(Now);
    reopened.setHost(&f.host);
    reopened.setPath(Page);
    QCOMPARE(reopened.blocks()->rowCount(), f.count());
    for (int r = 0; r < f.count(); ++r)
        QCOMPARE(reopened.blocks()->editTextAt(r), f.edit(r));
}

/// What a view knows of a model that it only learns about through its signals: the rows'
/// roles, fetched when rows are inserted, refetched on dataChanged (only the roles named) and
/// on reset. stale() lists every role whose value the view still shows wrongly. Block uids are
/// left out: no view shows them (they are internal and change with every reload).
class ViewMirror {
public:
    explicit ViewMirror(const QAbstractListModel *model) : m_model(model)
    {
        const QHash<int, QByteArray> names = model->roleNames();
        for (auto it = names.begin(); it != names.end(); ++it) {
            if (it.value() != "uid")
                m_roles.insert(it.key(), QString::fromLatin1(it.value()));
        }
        reset();
        QObject::connect(model, &QAbstractItemModel::rowsInserted, &m_context, [this](const QModelIndex &, int first, int last) {
            for (int r = first; r <= last; ++r)
                m_rows.insert(r, fetch(r));
        });
        QObject::connect(model, &QAbstractItemModel::rowsRemoved, &m_context, [this](const QModelIndex &, int first, int last) {
            m_rows.remove(first, last - first + 1);
        });
        QObject::connect(model, &QAbstractItemModel::rowsMoved, &m_context,
                         [this](const QModelIndex &, int first, int last, const QModelIndex &, int destination) {
                             const QList<QHash<int, QVariant>> part = m_rows.mid(first, last - first + 1);
                             m_rows.remove(first, part.size());
                             const int dest = destination > first ? destination - int(part.size()) : destination;
                             for (int k = 0; k < part.size(); ++k)
                                 m_rows.insert(dest + k, part[k]);
                         });
        QObject::connect(model, &QAbstractItemModel::modelReset, &m_context, [this] { reset(); });
        QObject::connect(model, &QAbstractItemModel::dataChanged, &m_context,
                         [this](const QModelIndex &from, const QModelIndex &to, const QList<int> &roles) {
                             for (int r = from.row(); r <= to.row(); ++r) {
                                 if (roles.isEmpty()) {
                                     m_rows[r] = fetch(r);
                                     continue;
                                 }
                                 for (const int role : roles) {
                                     if (m_roles.contains(role))
                                         m_rows[r].insert(role, value(r, role));
                                 }
                             }
                         });
    }

    /// "row/role: shown → actual" for every stale value (empty: the view is current).
    QStringList stale() const
    {
        QStringList out;
        if (m_rows.size() != m_model->rowCount())
            out.append(u"rows: %1 → %2"_s.arg(m_rows.size()).arg(m_model->rowCount()));
        for (int r = 0; r < std::min(int(m_rows.size()), m_model->rowCount()); ++r) {
            for (auto it = m_roles.begin(); it != m_roles.end(); ++it) {
                const QVariant actual = value(r, it.key());
                if (m_rows[r].value(it.key()) != actual)
                    out.append(u"%1/%2: %3 → %4"_s.arg(r).arg(it.value(), m_rows[r].value(it.key()).toString(), actual.toString()));
            }
        }
        return out;
    }

private:
    QVariant value(int row, int role) const { return m_model->data(m_model->index(row), role); }
    QHash<int, QVariant> fetch(int row) const
    {
        QHash<int, QVariant> out;
        for (auto it = m_roles.begin(); it != m_roles.end(); ++it)
            out.insert(it.key(), value(row, it.key()));
        return out;
    }
    void reset()
    {
        m_rows.clear();
        for (int r = 0; r < m_model->rowCount(); ++r)
            m_rows.append(fetch(r));
    }

    const QAbstractListModel *m_model;
    QObject m_context;
    QHash<int, QString> m_roles;
    QList<QHash<int, QVariant>> m_rows;
};

/// A prepared backdrop as DrawingBackdrops gives it (a PNG of one colour).
QVariantMap preparedBackdrop(const QString &path, int page, const QString &name, QSize size = QSize(240, 135))
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(QColor(u"#74c0fc"_s));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return {{u"bytes"_s, png},          {u"mimeType"_s, u"image/png"_s},
            {u"width"_s, size.width()}, {u"height"_s, size.height()},
            {u"path"_s, path},          {u"page"_s, page},
            {u"name"_s, name},          {u"error"_s, QString()}};
}

} // namespace

class TestPageController : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- loading and roles --------------------------------------------------------------------------

    void loadsAndRoundTrips_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("sample") << FakeEditorHost::sampleNote();
        QTest::newRow("crlf") << u"- a\r\n\t- b\r\n\r\nAkapit\r\n"_s;
        QTest::newRow("no final newline") << u"- jeden\n- dwa"_s;
        QTest::newRow("empty") << QString();
        QTest::newRow("bom") << u"﻿# Tytuł\n\nTreść ąćęłńóśźż\n"_s;
        QTest::newRow("mixed") << u"1. a\n2. b\n   1) c\n+ d\n* e\n\n```\nkod\n```\n"_s;
    }

    void loadsAndRoundTrips()
    {
        QFETCH(QString, content);
        Fixture f(content);
        QVERIFY(f.page.loaded());
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.dirty());
        QCOMPARE(f.page.title(), u"Strona"_s);
        QCOMPARE(f.count(), f.model()->document().count());
        // Reading every role of every row never crashes and is stable.
        for (int r = 0; r < f.count(); ++r) {
            for (const QByteArray &name : f.model()->roleNames())
                (void)f.role(r, name.constData());
        }
        QCOMPARE(f.text(), content);
    }

    void sampleRoles()
    {
        Fixture f(FakeEditorHost::sampleNote());
        QCOMPARE(f.host.saveAttachment(u"łazienka-szkic.png"_s, QByteArray("\x89PNG")), u"assets/łazienka-szkic.png"_s);
        f.model()->invalidateRendering();
        QHash<QString, int> views;
        for (int r = 0; r < f.count(); ++r)
            views[f.role(r, "view").toString()] += 1;
        for (const char *v : {"frontmatter", "text", "heading", "calc", "table", "callout", "query", "rule", "quote"})
            QVERIFY2(views.value(QString::fromLatin1(v)) > 0, v);
        // get() (for QML: the page view finds "# Title" and [[Page#Heading]] targets) carries
        // the heading level.
        for (int r = 0; r < f.count(); ++r) {
            const QVariantMap info = f.model()->get(r);
            QCOMPARE(info.value(u"headingLevel"_s).toInt(), f.role(r, "headingLevel").toInt());
            if (info.value(u"view"_s).toString() == u"heading")
                QVERIFY(info.value(u"headingLevel"_s).toInt() > 0);
        }
        // Tasks, depth and children.
        int kup = -1;
        for (int r = 0; r < f.count(); ++r) {
            if (f.edit(r).startsWith(u"Kupić płytki"_s))
                kup = r;
        }
        QVERIFY(kup > 0);
        QCOMPARE(f.role(kup, "taskStatus").toString(), u"todo"_s);
        QCOMPARE(f.role(kup, "depth").toInt(), 1);
        QVERIFY(f.role(kup, "hasChildren").toBool());
        const QString html = f.role(kup, "html").toString();
        QVERIFY(html.contains(u"tag:zakupy"_s));
        QVERIFY(html.contains(u"📅"_s));
        QVERIFY(!html.contains(u"[ ]"_s)); // the delegate draws the checkbox
        // Calc results, table formulas, query and embeds.
        for (int r = 0; r < f.count(); ++r) {
            const QString view = f.role(r, "view").toString();
            if (view == u"calc") {
                const QVariantList results = f.role(r, "calcResults").toList();
                QCOMPARE(results.size(), f.edit(r).count(u'\n') + 1);
                QCOMPARE(results.last().toMap().value(u"value"_s).toString(), u"3558,50 zł"_s);
            } else if (view == u"table") {
                const QVariantMap table = f.role(r, "tableData").toMap();
                QCOMPARE(table.value(u"header"_s).toStringList().size(), 4);
                const QVariantList rows = table.value(u"rows"_s).toList();
                QCOMPARE(rows.size(), 4);
                QCOMPARE(rows[0].toList()[3].toMap().value(u"display"_s).toString(), u"578,5"_s);
                QCOMPARE(rows[3].toList()[3].toMap().value(u"display"_s).toString(), u"834,5"_s);
                QVERIFY(rows[3].toList()[3].toMap().value(u"formula"_s).toBool());
            } else if (view == u"query") {
                QCOMPARE(f.role(r, "query").toMap().value(u"filters"_s).toList().size(), 4);
            } else if (view == u"frontmatter") {
                QCOMPARE(f.role(r, "properties").toList().size(), 4);
            } else if (f.role(r, "onlyEmbeds").toBool()) {
                // Pictures, or embedded sections (their frames come from `transclusions`).
                const QVariantList embeds = f.role(r, "embeds").toList();
                const QVariantList transclusions = f.role(r, "transclusions").toList();
                QCOMPARE(embeds.size() + transclusions.size(), 1);
                if (!embeds.isEmpty())
                    QVERIFY(embeds[0].toMap().value(u"url"_s).toUrl().isLocalFile());
                QVERIFY(f.role(r, "html").toString().isEmpty());
            }
        }
        // Inline calculation with frontmatter and calc-block variables.
        bool inlineFound = false;
        for (int r = 0; r < f.count(); ++r) {
            if (f.edit(r).startsWith(u"Zostaje z budżetu"_s)) {
                QVERIFY2(f.role(r, "html").toString().contains(u"8441,5"_s), qPrintable(f.role(r, "html").toString()));
                inlineFound = true;
            }
        }
        QVERIFY(inlineFound);
    }

    void reportedSampleCodeBlocks()
    {
        // The reported note (trimmed): inline code with '#', a code block followed directly by a
        // paragraph, consecutive fences, trailing backslashes and a blank line before the close.
        const QString sample = uR"(paczka llama.cpp: `github:janusz-bit/nixos#llama-cpp-prism`

```
nix shell nixpkgs#python314Packages.huggingface-hub.out -c hf download prism-ml/Ternary-Bonsai-2-27B-gguf Ternary-Bonsai-2-27B-PQ2_0.gguf --local-dir . \
                                     --ctx-size 200000 \
                                     -ctk q8_0 -ctv q8_0
```
Udało się zmieścić model na GPU!!!!

Dokumentacja mówi:

```
llama serve -m Ternary-Bonsai-2-27B-PQ2_0.gguf \
    -ngl 99 -fa on -c 32768 \
    -p "Explain quantum computing in simple terms." -n 16384
```


ja będę robić tak:

```
GGML_CUDA_ENABLE_UNIFIED_MEMORY=1 llama serve -m Ternary-Bonsai-2-27B-PTQ1_0.gguf \
-ngl 99 -fa on -c 65536 \
-ctk q8_0 -ctv q4_0 \

```
)"_s;
        Fixture f(sample);
        QCOMPARE(f.text(), sample);
        QStringList views;
        for (int r = 0; r < f.count(); ++r)
            views.append(f.role(r, "view").toString());
        QCOMPARE(views, (QStringList{u"text"_s, u"code"_s, u"text"_s, u"text"_s, u"code"_s,
                                     u"text"_s, u"code"_s}));
        QCOMPARE(f.edit(0), u"paczka llama.cpp: `github:janusz-bit/nixos#llama-cpp-prism`"_s);
        QVERIFY(f.edit(1).endsWith(u"-ctk q8_0 -ctv q8_0"_s));
        QCOMPARE(f.edit(2), u"Udało się zmieścić model na GPU!!!!"_s);
        QCOMPARE(f.edit(3), u"Dokumentacja mówi:"_s);
        QVERIFY(f.edit(4).contains(uR"(-p "Explain quantum computing in simple terms.")"_s));
        QCOMPARE(f.edit(5), u"ja będę robić tak:"_s);
        QVERIFY(f.edit(6).endsWith(u"\\\n"_s)); // the blank line before the closing fence
    }

    void unresolvedLinksAndPalette()
    {
        Fixture f(u"- [[Inna]] i [[Nie istnieje]]\n"_s);
        const QString html = f.role(0, "html").toString();
        QVERIFY(html.contains(u"#2c6aa3"_s, Qt::CaseInsensitive));
        QVERIFY(html.contains(u"#7a5ea3"_s, Qt::CaseInsensitive));
        f.page.setPalette({{u"link"_s, u"#112233"_s}, {u"unresolved"_s, QColor(u"#445566"_s)}});
        const QString themed = f.role(0, "html").toString();
        QVERIFY(themed.contains(u"#112233"_s, Qt::CaseInsensitive));
        QVERIFY(themed.contains(u"#445566"_s, Qt::CaseInsensitive));
    }

    void onlyListItemsAreTasks()
    {
        // SYNTAX §5: a paragraph starting with "TODO" or "[ ]" is text, a list item a task (its
        // marker is drawn by the delegate, so it is not part of the html).
        Fixture f(u"TODO zadzwonić do Łucji\n\n[ ] to nie zadanie\n\n- TODO zadanie\n- [ ] kupić żółty ser\n"_s);
        QCOMPARE(f.count(), 4);
        QCOMPARE(f.role(0, "taskStatus").toString(), QString());
        QVERIFY2(f.role(0, "html").toString().startsWith(u"TODO zadzwonić"_s),
                 qPrintable(f.role(0, "html").toString()));
        QCOMPARE(f.edit(0), u"TODO zadzwonić do Łucji"_s);
        QCOMPARE(f.role(1, "taskStatus").toString(), QString());
        QVERIFY2(f.role(1, "html").toString().startsWith(u"[ ] to nie"_s), qPrintable(f.role(1, "html").toString()));
        QCOMPARE(f.role(2, "taskStatus").toString(), u"todo"_s);
        QVERIFY(!f.role(2, "html").toString().contains(u"TODO"_s));
        QCOMPARE(f.edit(2), u"zadanie"_s);
        QCOMPARE(f.role(3, "taskStatus").toString(), u"todo"_s);
        QVERIFY(!f.role(3, "html").toString().contains(u"[ ]"_s));
        // A heading item's further lines are text as well.
        Fixture h(u"- # Plan\n  TODO nie zadanie\n"_s);
        QVERIFY2(h.role(0, "html").toString().contains(u"TODO nie"_s), qPrintable(h.role(0, "html").toString()));
    }

    void journalLinksShowDates()
    {
        // [[2026-10-13]] reads as a date in the editor (the file keeps the ISO date).
        Fixture f(u"- Wczoraj: [[2026-10-14]], potem [[2026-10-13|wtorek]] i [[Inna]]\n"_s);
        const QString nb = QString(QChar(0x00A0));
        const QString html = f.role(0, "html").toString();
        QVERIFY2(html.contains(u"śr"_s + nb + u"14"_s + nb + u"paź"_s), qPrintable(html));
        // A day of the journal is a day: link (its date menu); other pages stay page: links.
        QVERIFY2(html.contains(u"href=\"day:"_s) && !html.contains(u"page:2026-10-14"_s), qPrintable(html));
        QVERIFY(html.contains(u"page:Inna"_s));
        QVERIFY(html.contains(u">wtorek<"_s));
        QVERIFY(!html.contains(u">2026-10-14<"_s));
        QCOMPARE(f.text(), u"- Wczoraj: [[2026-10-14]], potem [[2026-10-13|wtorek]] i [[Inna]]\n"_s);
        f.page.setLocale(u"en"_s);
        QVERIFY2(f.role(0, "html").toString().contains(u"Wed,"_s + nb + u"Oct"_s + nb + u"14"_s),
                 qPrintable(f.role(0, "html").toString()));
    }

    void dayClockFollowsMidnight()
    {
        DayClock *clock = DayClock::instance();
        QDateTime now(QDate(2026, 10, 16), QTime(23, 59, 59));
        clock->setClock([&now] { return now; });
        QCOMPARE(clock->today(), QDate(2026, 10, 16));
        QSignalSpy changed(clock, &DayClock::dayChanged);
        clock->check();
        QCOMPARE(changed.size(), 0);
        // The timer is armed for just after midnight (by this clock: 2 s).
        now = QDateTime(QDate(2026, 10, 17), QTime(0, 0, 1));
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 5000);
        QCOMPARE(changed.first().first().toDate(), QDate(2026, 10, 17));
        clock->check();
        QCOMPARE(changed.size(), 1);
        // The clock jumps (travel, suspend): noticed at the next check.
        now = QDateTime(QDate(2026, 10, 15), QTime(12, 0));
        clock->check();
        QCOMPARE(changed.size(), 2);
        clock->setClock({});
    }

    void midnightRefreshesAnOpenPage()
    {
        // The real clock (no fixed `now`): a page rendered yesterday follows the DayClock —
        // "jutro" becomes "dziś", a task due yesterday turns red, query results run again.
        const QDate today = QDate::currentDate();
        const QString due = today.toString(Qt::ISODate);
        const QString past = today.addDays(-1).toString(Qt::ISODate);
        FakeEditorHost host;
        host.setFile(Page, u"- [ ] Zadzwonić 📅 %1\n- [ ] Spóźnione 📅 %2\n"_s.arg(due, past));
        PageController page;
        page.setHost(&host);
        page.setPath(Page);
        BlockListModel *model = page.blocks();
        const auto html = [&](int row) { return model->data(model->index(row), BlockListModel::HtmlRole).toString(); };
        QVERIFY2(html(0).contains(u"dziś"_s), qPrintable(html(0)));
        model->setToday(today.addDays(-1)); // as rendered before midnight
        QVERIFY2(html(0).contains(u"jutro"_s), qPrintable(html(0)));
        QVERIFY(!html(1).contains(markdown::Palette().overdue.foreground, Qt::CaseInsensitive));
        QSignalSpy todayChanged(&page, &PageController::todayChanged);
        QSignalSpy dataChanged(model, &QAbstractItemModel::dataChanged);
        QDateTime fake(today.addDays(1), QTime(0, 0, 1));
        DayClock::instance()->setClock([&fake] { return fake; }); // "midnight"
        QCOMPARE(todayChanged.size(), 1);
        QVERIFY(dataChanged.size() >= 1);
        QVERIFY2(html(0).contains(u"dziś"_s), qPrintable(html(0)));
        QVERIFY2(html(1).contains(markdown::Palette().overdue.foreground, Qt::CaseInsensitive), qPrintable(html(1)));
        DayClock::instance()->setClock({});
        QCOMPARE(todayChanged.size(), 1); // the same day for the page: nothing again
    }

    // --- typing and undo ------------------------------------------------------------------------------

    void typingIsOneUndoStepPerBlock()
    {
        const QString content = u"- pierwszy\n- drugi\n"_s;
        Fixture f(content);
        f.page.startEditing(0, 9);
        f.page.setEditText(0, u"pierwszy!"_s, 9);
        f.page.setEditText(0, u"pierwszy!!"_s, 10);
        f.page.setEditText(0, u"pierwszy!!!"_s, 11);
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi\n"_s);
        f.page.startEditing(1, 5);
        f.page.setEditText(1, u"drugi?"_s, 6);
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi?\n"_s);
        QVERIFY(f.page.canUndo());
        f.page.undo();
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        QCOMPARE(f.page.editingRow(), 0);
        f.page.redo();
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi\n"_s);
        f.page.redo();
        QCOMPARE(f.text(), u"- pierwszy!!!\n- drugi?\n"_s);
        QVERIFY(!f.page.canRedo());
    }

    void undoKeepsRowsAndSendsEditTextReset()
    {
        Fixture f(u"- a\n- b\n- c\n"_s);
        QSignalSpy resets(f.model(), &QAbstractItemModel::modelReset);
        QSignalSpy textResets(&f.page, &PageController::editTextReset);
        f.page.startEditing(1, 1);
        f.page.setEditText(1, u"bę"_s, 2);
        f.page.undo();
        QCOMPARE(resets.size(), 0); // rows of unchanged blocks survive undo
        QVERIFY(!textResets.isEmpty());
        QCOMPARE(textResets.last().at(0).toInt(), 1);
        QCOMPARE(textResets.last().at(1).toString(), u"b"_s);
    }

    // A block put at the very start of a page (row 0), then undone and redone: the rows a view
    // learns about from the model's signals match the model (the file line numbers of the rows
    // below too), the caret goes back to the block (and place) it was in, and only that row is
    // the edited one.
    void undoOfAnInsertionAtTheStart_data()
    {
        QTest::addColumn<QString>("content");
        // "enter": Enter at the edited place; "insert": insertBlockAfter(-1); "paste": two list
        // items pasted at the edited place; "up": Alt+Up of the edited block
        QTest::addColumn<QString>("command");
        QTest::addColumn<int>("editRow");      // edited before the command (-1: nothing)
        QTest::addColumn<int>("editCursor");
        QTest::addColumn<QString>("inserted"); // the text after the command
        QTest::addColumn<int>("rowAfter");     // edited after the command
        QTest::addColumn<int>("cursorAfter");  // its caret

        QTest::newRow("enter-list") << u"- Zażółć gęślą\n- jaźń\n"_s << u"enter"_s << 0 << 0
                                    << u"- \n- Zażółć gęślą\n- jaźń\n"_s << 1 << 0;
        QTest::newRow("enter-children") << u"- Kupić ćwikłę\n\t- dziecko\n- dalej\n"_s << u"enter"_s << 0 << 0
                                        << u"- \n- Kupić ćwikłę\n\t- dziecko\n- dalej\n"_s << 1 << 0;
        QTest::newRow("enter-task") << u"- [ ] Kupić ćwikłę\n- dalej\n"_s << u"enter"_s << 0 << 0
                                    << u"- [ ] \n- [ ] Kupić ćwikłę\n- dalej\n"_s << 1 << 0;
        // The editor hides a task's marker: its caret 0 is the start of the text. The text keeps
        // its status and children.
        QTest::newRow("enter-task-done") << u"- [x] Kupić ćwikłę\n- dalej\n"_s << u"enter"_s << 0 << 0
                                         << u"- [ ] \n- [x] Kupić ćwikłę\n- dalej\n"_s << 1 << 0;
        QTest::newRow("enter-task-children") << u"- [ ] Kupić ćwikłę\n\t- dziecko\n"_s << u"enter"_s << 0 << 0
                                             << u"- [ ] \n- [ ] Kupić ćwikłę\n\t- dziecko\n"_s << 1 << 0;
        QTest::newRow("enter-task-logseq") << u"- DONE łódź\n"_s << u"enter"_s << 0 << 0
                                           << u"- TODO \n- DONE łódź\n"_s << 1 << 0;
        QTest::newRow("enter-task-crlf") << u"- [x] źdźbło\r\n- b\r\n"_s << u"enter"_s << 0 << 0
                                         << u"- [ ] \r\n- [x] źdźbło\r\n- b\r\n"_s << 1 << 0;
        QTest::newRow("enter-paragraph") << u"Pierwszy akapit źdźbła\n\nDrugi\n"_s << u"enter"_s << 0 << 0
                                         << u"\n\nPierwszy akapit źdźbła\n\nDrugi\n"_s << 1 << 0;
        QTest::newRow("enter-title") << u"# Strona\n- treść\n"_s << u"enter"_s << 0 << 0
                                     << u"\n\n# Strona\n- treść\n"_s << 1 << 0;
        QTest::newRow("enter-ordered") << u"1. jeden\n2. dwa\n3. trzy\n"_s << u"enter"_s << 0 << 0
                                       << u"1. \n2. jeden\n3. dwa\n4. trzy\n"_s << 1 << 0;
        QTest::newRow("enter-empty-first") << u"- \n- \n- b\n"_s << u"enter"_s << 0 << 0
                                           << u"- \n- \n- \n- b\n"_s << 1 << 0;
        QTest::newRow("enter-after-properties") << u"---\ntags: [dom]\n---\n- Zażółć gęślą\n"_s << u"enter"_s << 1 << 0
                                                << u"---\ntags: [dom]\n---\n- \n- Zażółć gęślą\n"_s << 2 << 0;
        QTest::newRow("enter-crlf") << u"- ąę\r\n- b\r\n"_s << u"enter"_s << 0 << 0 << u"- \r\n- ąę\r\n- b\r\n"_s << 1 << 0;
        QTest::newRow("insert-nothing-edited") << u"- a\n- b\n"_s << u"insert"_s << -1 << -1
                                               << u"- \n- a\n- b\n"_s << 0 << 0;
        QTest::newRow("insert-editing-below") << u"- a\n- bęben\n"_s << u"insert"_s << 1 << 3
                                              << u"- \n- a\n- bęben\n"_s << 0 << 0;
        QTest::newRow("insert-same-as-first") << u"- \n- b\n"_s << u"insert"_s << 0 << 0 << u"- \n- \n- b\n"_s << 0 << 0;
        // In document mode a frontmatter with nothing but the mode is hidden: row 0 is invisible.
        const QString document = u"---\nbrulion: document\n---\n"_s;
        QTest::newRow("document-enter") << document + u"Pierwszy akapit źdźbła\n\nDrugi\n"_s << u"enter"_s << 1 << 0
                                        << document + u"\n\nPierwszy akapit źdźbła\n\nDrugi\n"_s << 2 << 0;
        QTest::newRow("document-enter-task") << document + u"- [x] zadanie żółwia\n"_s << u"enter"_s << 1 << 0
                                             << document + u"- [ ] \n- [x] zadanie żółwia\n"_s << 2 << 0;
        QTest::newRow("document-insert") << document + u"Pierwszy akapit źdźbła\n"_s << u"insert"_s << -1 << -1
                                         << document + u"\n\nPierwszy akapit źdźbła\n"_s << 1 << 0;
        QTest::newRow("document-up") << document + u"Pierwszy\n\nDrugi łęg\n"_s << u"up"_s << 2 << 3
                                     << document + u"Drugi łęg\n\nPierwszy\n"_s << 1 << 3;
        // Several blocks pasted at the start of the first one: the first joins the text before the
        // caret (none), the text after the caret goes to the end of the last one.
        QTest::newRow("paste-list") << u"- Zażółć gęślą\n- jaźń\n"_s << u"paste"_s << 0 << 0
                                    << u"- wklejone ą\n- drugie ęZażółć gęślą\n- jaźń\n"_s << 1 << 8;
        QTest::newRow("paste-crlf") << u"- ąę\r\n- b\r\n"_s << u"paste"_s << 0 << 0
                                    << u"- wklejone ą\r\n- drugie ęąę\r\n- b\r\n"_s << 1 << 8;
        QTest::newRow("up-list") << u"- pierwszy\n- Zażółć gęślą\n- trzeci\n"_s << u"up"_s << 1 << 4
                                 << u"- Zażółć gęślą\n- pierwszy\n- trzeci\n"_s << 0 << 4;
        QTest::newRow("up-children") << u"- pierwszy\n- Zażółć\n\t- dziecko ść\n- trzeci\n"_s << u"up"_s << 1 << 2
                                     << u"- Zażółć\n\t- dziecko ść\n- pierwszy\n- trzeci\n"_s << 0 << 2;
        QTest::newRow("up-after-properties") << u"---\ntags: [dom]\n---\n- pierwszy\n- Zażółć\n"_s << u"up"_s << 2 << 1
                                             << u"---\ntags: [dom]\n---\n- Zażółć\n- pierwszy\n"_s << 1 << 1;
    }

    void undoOfAnInsertionAtTheStart()
    {
        QFETCH(QString, content);
        QFETCH(QString, command);
        QFETCH(int, editRow);
        QFETCH(int, editCursor);
        QFETCH(QString, inserted);
        QFETCH(int, rowAfter);
        QFETCH(int, cursorAfter);

        Fixture f(content);
        if (editRow >= 0)
            f.page.startEditing(editRow, editCursor);
        const ViewMirror view(f.model());
        QSignalSpy textResets(&f.page, &PageController::editTextReset);
        // The blocks of each version (the edited row is the same block, not only the same row).
        const auto blocks = [&] {
            QStringList out;
            for (int r = 0; r < f.count(); ++r)
                out.append(f.role(r, "kind").toString() + u':' + f.edit(r));
            return out;
        };
        const QStringList blocksBefore = blocks();
        const auto editedRows = [&] {
            QList<int> rows;
            for (int r = 0; r < f.count(); ++r) {
                if (f.role(r, "isEditing").toBool())
                    rows.append(r);
            }
            return rows;
        };
        // A moved block keeps its text editor (the row moves): its text is not sent again.
        const auto check = [&](const QString &text, int row, int cursor, bool textReset = true) {
            QCOMPARE(f.text(), text);
            QVERIFY2(view.stale().isEmpty(), qPrintable(view.stale().join(u"; "_s)));
            QCOMPARE(f.page.editingRow(), row);
            QCOMPARE(editedRows(), row >= 0 ? QList<int>{row} : QList<int>{});
            if (row < 0)
                return;
            if (cursor >= 0)
                QCOMPARE(f.page.cursorPosition(), cursor);
            if (!textReset)
                return;
            // The edited row's text editor gets that row's text.
            QVERIFY(!textResets.isEmpty());
            QCOMPARE(textResets.last().at(0).toInt(), row);
            QCOMPARE(textResets.last().at(1).toString(), f.edit(row));
            QCOMPARE(textResets.last().at(2).toInt(), f.page.cursorPosition());
        };

        const QVariant uidBefore = editRow >= 0 ? f.role(editRow, "uid") : QVariant();
        const bool keepsBlock = command == u"enter"_s && editRow >= 0 && !f.edit(editRow).isEmpty();
        if (command == u"enter"_s)
            f.page.split(editRow, editCursor);
        else if (command == u"paste"_s)
            f.page.pasteText(editRow, editCursor, editCursor, u"- wklejone ą\n- drugie ę\n"_s);
        else if (command == u"up"_s)
            QVERIFY(f.page.moveUp(editRow));
        else
            f.page.insertBlockAfter(-1);
        check(inserted, rowAfter, cursorAfter, command != u"up"_s);
        if (QTest::currentTestFailed())
            return;
        // Enter at the start of a text keeps the caret in the same block: its text did not move.
        if (keepsBlock)
            QCOMPARE(f.role(rowAfter, "uid"), uidBefore);
        const int afterCursor = f.page.cursorPosition();
        // An inserted empty paragraph is no Markdown block of its own (just blank lines): the
        // step remembers its line, so redo, which reads the version's text again, keeps it.
        const QStringList blocksAfter = blocks();

        for (int round = 0; round < 2; ++round) {
            textResets.clear();
            f.page.undo();
            check(content, editRow, editCursor);
            if (QTest::currentTestFailed())
                return;
            QCOMPARE(blocks(), blocksBefore);
            QVERIFY(!f.page.canUndo());
            textResets.clear();
            f.page.redo();
            check(inserted, rowAfter, afterCursor);
            if (QTest::currentTestFailed())
                return;
            QCOMPARE(blocks(), blocksAfter);
        }
        f.page.undo();
        // Typing goes into the block the caret is back in.
        if (editRow >= 0) {
            const QString typed = f.edit(editRow) + u"ż"_s;
            f.page.setEditText(editRow, typed, int(typed.size()));
            QCOMPARE(f.edit(editRow), typed);
            QCOMPARE(view.stale(), QStringList());
        }
        verifyReload(f);
    }

    // Empty paragraphs are blank lines in the text: every undo and redo step brings back the ones
    // of its version (not only the step that made them), so rows and the caret stay right.
    // Steps: "enter row cursor", "set row text" (typing), "insert row"; "+" merges the step with
    // the previous one (typing on in the same block).
    void undoKeepsEmptyParagraphs_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("steps");

        QTest::newRow("typing-elsewhere") << u"Pierwszy akapit źdźbła\n\nDrugi\n"_s
                                          << QStringList{u"enter 0 0"_s, u"set 2 Drugi ż"_s};
        QTest::newRow("typed-away") << u"Ala\n\nŁąka\n\nOla\n"_s
                                    << QStringList{u"set 1 Łą"_s, u"+set 1"_s, u"insert 1"_s};
        QTest::newRow("two-in-a-row") << u"Ala\n\nOla\n"_s << QStringList{u"enter 1 0"_s, u"enter 2 0"_s};
        QTest::newRow("heading-then-list") << u"# Strona\n- treść\n"_s
                                           << QStringList{u"enter 0 0"_s, u"enter 2 5"_s, u"set 3 ąę"_s};
        QTest::newRow("end-no-final-newline") << u"Ala ma kota"_s << QStringList{u"enter 0 11"_s, u"set 0 Ala ma psa"_s};
        QTest::newRow("crlf") << u"Żółw\r\n\r\nDrugi\r\n"_s << QStringList{u"enter 0 0"_s, u"set 2 Drugi ą"_s};
        const QString document = u"---\nbrulion: document\n---\n"_s;
        QTest::newRow("document") << document + u"Pierwszy akapit\n\nDrugi łęg\n"_s
                                  << QStringList{u"enter 1 0"_s, u"set 3 Drugi łęgi"_s, u"enter 1 0"_s};
        // A line break typed into the empty paragraph (Shift+Enter): one block of two lines.
        QTest::newRow("soft-break") << u"Pierwszy akapit źdźbła\n\nDrugi\n"_s
                                    << QStringList{u"enter 0 0"_s, u"set 0 \n"_s, u"set 2 Drugi ż"_s};
        QTest::newRow("soft-breaks-crlf") << u"Żółw\r\n\r\nDrugi\r\n"_s
                                          << QStringList{u"enter 0 0"_s, u"set 0 \n\n"_s, u"set 2 Drugi ą"_s};
        QTest::newRow("document-soft-break") << document + u"Pierwszy akapit\n\nDrugi łęg\n"_s
                                             << QStringList{u"enter 1 0"_s, u"set 1 \n"_s, u"set 3 Drugi łęgi"_s};
    }

    void undoKeepsEmptyParagraphs()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, steps);

        Fixture f(content);
        const ViewMirror view(f.model());
        struct State {
            QString text;
            QStringList blocks;
            int editing = -1;
        };
        const auto state = [&] {
            State st{f.text(), {}, f.page.editingRow()};
            for (int r = 0; r < f.count(); ++r)
                st.blocks.append(f.role(r, "kind").toString() + u':' + f.edit(r));
            return st;
        };
        const auto check = [&](const State &want, const char *what, int step) {
            const State now = state();
            const QByteArray where = QByteArray(what) + " of step " + QByteArray::number(step);
            QVERIFY2(now.text == want.text, where.constData());
            QVERIFY2(now.blocks == want.blocks,
                     qPrintable(QString::fromLatin1(where) + u": "_s + now.blocks.join(u" | "_s) + u" != "_s
                                + want.blocks.join(u" | "_s)));
            QVERIFY2(now.editing == want.editing, where.constData());
            QVERIFY2(view.stale().isEmpty(), qPrintable(view.stale().join(u"; "_s)));
        };

        QList<std::pair<State, State>> history; // (before, after) of each undo step
        for (const QString &step : steps) {
            const bool merge = step.startsWith(u'+');
            const QStringList w = (merge ? step.sliced(1) : step).split(u' ');
            const int row = w.value(1).toInt();
            if (w[0] == u"enter"_s)
                f.page.startEditing(row, w.value(2).toInt());
            else if (w[0] == u"set"_s && !merge)
                f.page.startEditing(row, int(f.edit(row).size()));
            const State before = state();
            if (w[0] == u"enter"_s) {
                f.page.split(row, w.value(2).toInt());
            } else if (w[0] == u"set"_s) {
                const QString typed = w.mid(2).join(u' ');
                f.page.setEditText(row, typed, int(typed.size()));
            } else {
                f.page.insertBlockAfter(row);
            }
            if (merge)
                history.last().second = state();
            else
                history.append({before, state()});
        }
        QVERIFY(std::any_of(history.begin(), history.end(),
                            [](const auto &h) { return h.second.blocks.contains(u"paragraph:"_s); }));

        for (int round = 0; round < 2; ++round) {
            for (qsizetype k = history.size() - 1; k >= 0; --k) {
                f.page.undo();
                check(history[k].first, "undo", int(k));
                if (QTest::currentTestFailed())
                    return;
            }
            QVERIFY(!f.page.canUndo());
            for (qsizetype k = 0; k < history.size(); ++k) {
                f.page.redo();
                check(history[k].second, "redo", int(k));
                if (QTest::currentTestFailed())
                    return;
            }
            QVERIFY(!f.page.canRedo());
        }
        // Typing after a redo goes into the block the caret is in.
        if (f.page.editingRow() >= 0) {
            const int r = f.page.editingRow();
            const QString typed = f.edit(r) + u"ź"_s;
            f.page.setEditText(r, typed, int(typed.size()));
            QCOMPARE(f.edit(r), typed);
            QVERIFY(view.stale().isEmpty());
            f.page.undo();
        }
        while (f.page.canUndo())
            f.page.undo();
        QCOMPARE(f.text(), content);
        verifyReload(f);
    }

    void undoKeepsChangedRowsInPlace()
    {
        // Undo and redo change the row of a block they change in place — the same block, its
        // delegate and fold stay: dataChanged for it, no removal and insertion.
        const QString content = u"- a\n- [ ] rodzic żółw\n\t- dziecko\n- c\n"_s;
        Fixture f(content);
        const QVariant first = f.role(0, "uid");
        f.page.startEditing(0, 1);
        f.page.setEditText(0, u"aąę"_s, 3);
        QSignalSpy removed(f.model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(f.model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        const auto onlyRow = [&](int row) {
            QVERIFY(!changed.isEmpty());
            for (const QList<QVariant> &args : std::as_const(changed)) {
                QCOMPARE(args.at(0).toModelIndex().row(), row);
                QCOMPARE(args.at(1).toModelIndex().row(), row);
            }
            QCOMPARE(removed.size(), 0);
            QCOMPARE(inserted.size(), 0);
            changed.clear();
        };
        f.page.undo();
        QCOMPARE(f.text(), content);
        QCOMPARE(f.role(0, "uid"), first);
        onlyRow(0);
        f.page.redo();
        QCOMPARE(f.text(), u"- aąę\n- [ ] rodzic żółw\n\t- dziecko\n- c\n"_s);
        QCOMPARE(f.role(0, "uid"), first);
        onlyRow(0);
        f.page.undo();

        // A folded task completed and the completion undone and redone: it stays folded.
        f.page.stopEditing();
        f.page.setFolded(1, true);
        const QVariantList folds = f.page.foldState();
        QCOMPARE(folds, QVariantList{1});
        const QVariant parent = f.role(1, "uid");
        f.page.toggleTask(1);
        QVERIFY(f.text().startsWith(u"- a\n- [x] rodzic żółw ✅"_s));
        QSignalSpy foldSignals(&f.page, &PageController::foldStateChanged);
        for (int round = 0; round < 2; ++round) {
            f.page.undo();
            QCOMPARE(f.text(), content);
            QCOMPARE(f.page.foldState(), folds);
            QVERIFY(f.role(1, "collapsed").toBool());
            QCOMPARE(f.role(1, "uid"), parent);
            f.page.redo();
            QVERIFY(f.role(1, "collapsed").toBool());
            QCOMPARE(f.role(1, "uid"), parent);
        }
        QCOMPARE(foldSignals.size(), 0); // the folded lines stayed: nothing new to store
        QCOMPARE(removed.size(), 0);
        QCOMPARE(inserted.size(), 0);
    }

    void undoRedoRandomCommands_data()
    {
        QTest::addColumn<quint32>("seed");
        QTest::newRow("1") << 1u;
        QTest::newRow("7") << 7u;
        QTest::newRow("2026") << 2026u;
    }

    void undoRedoRandomCommands()
    {
        // Random commands (typing, Enter, Backspace, Tab, moves, tasks, deletions, Markdown pasted,
        // the note mode, a deleted selection): undone all and redone all, the page is each
        // version again and the rows a view knows match the model.
        QFETCH(quint32, seed);
        Fixture f(u"# Zakupy\n\n- [ ] Kupić żółty ser 📅 2026-10-30\n- [ ] Podlać kwiaty 🔁 co tydzień\n"
                  u"\t- notatka ąę\n- Zażółć gęślą jaźń 😀\n\nAkapit końcowy\n"_s);
        const ViewMirror view(f.model());
        const auto rnd = [&](quint32 n) {
            seed = (seed * 1103515245u + 12345u) % 2147483648u;
            return n ? int(seed % n) : 0;
        };
        QStringList texts{f.text()};
        QString token = f.page.undoToken();
        for (int step = 0; step < 40; ++step) {
            const int n = f.count();
            const int row = rnd(quint32(n));
            switch (n ? rnd(13) : 12) {
            case 0: f.page.setEditText(row, f.edit(row) + u" zażółć"_s, -1); break;
            case 1: f.page.split(row, rnd(quint32(f.edit(row).size() + 1))); break;
            case 2: f.page.mergeWithPrevious(row); break;
            case 3: f.page.indent(row); break;
            case 4: f.page.outdent(row); break;
            case 5: f.page.moveUp(row); break;
            case 6: f.page.moveDown(row); break;
            case 7: f.page.toggleTask(row); break;
            case 8: f.page.deleteBlock(row); break;
            case 9: f.page.insertMarkdown(row, u"- wklejony ą\n- drugi ę\n"_s); break;
            case 10: f.page.setMode(rnd(2) ? u"document"_s : u"outline"_s); break;
            case 11:
                f.page.selectRange(row, std::min(row + 1, n - 1));
                f.page.deleteSelection();
                break;
            default: f.page.insertBlockAfter(row - 1, u"nowy łoś"_s); break;
            }
            // A new step, or the typing of the last one continued.
            if (f.page.undoToken() != token)
                texts.append(f.text());
            else if (f.text() != texts.last())
                texts.last() = f.text();
            token = f.page.undoToken();
            QVERIFY2(view.stale().isEmpty(), qPrintable(view.stale().join(u"; "_s)));
        }
        QVERIFY(texts.size() > 10);
        for (qsizetype k = texts.size() - 2; k >= 0; --k) {
            QVERIFY(f.page.canUndo());
            f.page.undo();
            QVERIFY2(f.text() == texts[k], qPrintable(u"undo to version %1"_s.arg(k)));
            QVERIFY2(view.stale().isEmpty(), qPrintable(view.stale().join(u"; "_s)));
        }
        QVERIFY(!f.page.canUndo());
        for (qsizetype k = 1; k < texts.size(); ++k) {
            QVERIFY(f.page.canRedo());
            f.page.redo();
            QVERIFY2(f.text() == texts[k], qPrintable(u"redo to version %1"_s.arg(k)));
            QVERIFY2(view.stale().isEmpty(), qPrintable(view.stale().join(u"; "_s)));
        }
        QVERIFY(!f.page.canRedo());
        verifyReload(f);
    }

    void undoOfOnlyTheEdges_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("edited");
        const QString bom(QChar(0xFEFF));
        QTest::newRow("empty page, blank lines") << QString() << u"\n\n\n"_s;
        QTest::newRow("blank lines cleared") << u"\n\n"_s << QString();
        QTest::newRow("only a BOM, cleared") << bom << QString();
        QTest::newRow("a BOM before blank lines") << u"\r\n\r\n"_s << bom + u"\r\n"_s;
        QTest::newRow("blank lines before a block") << u"- żółw ąę\n"_s << u"\n\n- żółw ąę\n"_s;
        QTest::newRow("a BOM before a block") << bom + u"Zażółć gęślą jaźń\r\n"_s << u"Zażółć gęślą jaźń\r\n"_s;
    }

    void undoOfOnlyTheEdges()
    {
        // A step that changed only the document's edges (a BOM, blank lines before the first
        // block), also on a page with no blocks, where no row can tell of it: undo and redo
        // change the text the page has and saves.
        QFETCH(QString, content);
        QFETCH(QString, edited);
        Fixture f(content);
        QCOMPARE(f.text(), content);
        f.page.setSourceText(edited);
        QCOMPARE(f.text(), edited);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), edited);
        for (int round = 0; round < 2; ++round) {
            QVERIFY(f.page.canUndo());
            f.page.undo();
            QCOMPARE(f.text(), content);
            QCOMPARE(f.page.sourceText(), content);
            QVERIFY(f.page.save());
            QCOMPARE(f.host.file(Page), content);
            f.page.redo();
            QCOMPARE(f.text(), edited);
            QVERIFY(f.page.save());
            QCOMPARE(f.host.file(Page), edited);
        }
    }

    void editorSpaceJoinsAFarStep()
    {
        // The space the editor wrote after a completed tag goes as the caret leaves the block,
        // within the last step also when that step changed a block far below (a task toggled
        // while the block was edited): no step of its own that would only bring the space back.
        QString content = u"- Kupić #zak\n"_s;
        for (int i = 0; i < 2 * markdown::BlockDocument::MaxComposeGap; ++i)
            content += u"- [ ] zadanie %1 ąę\n"_s.arg(i);
        Fixture f(content);
        f.page.startEditing(0, int(f.edit(0).size()));
        f.page.applyCompletion(0, completionTriggerAt(f.edit(0), int(f.edit(0).size())).toMap(),
                               {{u"insertText"_s, u"zakupy"_s}, {u"kind"_s, u"tag"_s}});
        QVERIFY(f.text().startsWith(u"- Kupić #zakupy \n- [ ] zadanie 0 ąę\n"_s));
        const QString completed = f.text();
        const int far = f.count() - 2;
        f.page.toggleTask(far);
        QCOMPARE(f.page.editingRow(), 0);
        QVERIFY(f.text().contains(u"- [x] zadanie %1 ąę"_s.arg(far - 1)));
        const QString token = f.page.undoToken();
        f.page.stopEditing();
        QCOMPARE(f.page.undoToken(), token); // the last step, amended
        const QString done = f.text();
        QVERIFY(done.startsWith(u"- Kupić #zakupy\n"_s));
        QVERIFY(done.contains(u"- [x] zadanie %1 ąę"_s.arg(far - 1)));
        QCOMPARE(f.host.file(Page), done);
        // One undo takes the toggle back (with the space, as the step before it left the block).
        f.page.undo();
        QCOMPARE(f.text(), completed);
        f.page.undo();
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        f.page.redo();
        QCOMPARE(f.text(), completed);
        f.page.redo();
        QCOMPARE(f.text(), done);
        QVERIFY(!f.page.canRedo());
    }

    void typingTaskMarkerBecomesCheckbox()
    {
        Fixture f(u"- zwykły\n"_s);
        QSignalSpy textResets(&f.page, &PageController::editTextReset);
        f.page.startEditing(0, 0);
        f.page.setEditText(0, u"[ ] zwykły"_s, 4);
        QCOMPARE(f.text(), u"- [ ] zwykły\n"_s);
        QCOMPARE(f.role(0, "taskStatus").toString(), u"todo"_s);
        QCOMPARE(f.edit(0), u"zwykły"_s);
        QCOMPARE(textResets.size(), 1);
        QCOMPARE(textResets[0].at(1).toString(), u"zwykły"_s);
        QCOMPARE(textResets[0].at(2).toInt(), 0);
        // Typing inside a task keeps the marker.
        f.page.setEditText(0, u"zwykły tekst"_s, 12);
        QCOMPARE(f.text(), u"- [ ] zwykły tekst\n"_s);
    }

    void crlfIsPreserved()
    {
        Fixture f(u"- a\r\n- b\r\n\r\nKoniec\r\n"_s);
        f.page.startEditing(0, 1);
        f.page.setEditText(0, u"ała\ndruga linia"_s, 3);
        QCOMPARE(f.text(), u"- ała\r\n  druga linia\r\n- b\r\n\r\nKoniec\r\n"_s);
        f.page.split(1, 1);
        QCOMPARE(f.text(), u"- ała\r\n  druga linia\r\n- b\r\n- \r\n\r\nKoniec\r\n"_s);
    }

    // --- structure commands ------------------------------------------------------------------------

    void splitCommands()
    {
        Fixture f(u"- [ ] Kup mleko\n- abc\n"_s);
        f.page.startEditing(0, 9);
        QVariantMap r = f.page.split(0, 9);
        QCOMPARE(f.text(), u"- [ ] Kup mleko\n- [ ] \n- abc\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0); // after the hidden marker
        QCOMPARE(f.page.editingRow(), 1);
        // Enter at the start of a task's text: an empty task above, the caret stays in the text.
        const QVariant uid = f.role(0, "uid");
        f.page.startEditing(0, 0);
        r = f.page.split(0, 0);
        QCOMPARE(f.text(), u"- [ ] \n- [ ] Kup mleko\n- [ ] \n- abc\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0);
        QCOMPARE(f.role(1, "uid"), uid);
        QCOMPARE(f.edit(1), u"Kup mleko"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Kup mleko\n- [ ] \n- abc\n"_s);
        f.page.startEditing(2, 1);
        r = f.page.split(2, 1);
        QCOMPARE(f.text(), u"- [ ] Kup mleko\n- [ ] \n- a\n- bc\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 3);
        QCOMPARE(f.role(3, "isEditing").toBool(), true);
        QCOMPARE(f.role(2, "isEditing").toBool(), false);
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Kup mleko\n- [ ] \n- abc\n"_s);
        QCOMPARE(f.page.editingRow(), 2);
        QCOMPARE(f.page.cursorPosition(), 1);
    }

    void splitSpecialCases()
    {
        // "```calc" + Enter opens a code block.
        Fixture fence(u"- ```calc\n"_s);
        const QVariantMap r = fence.page.split(0, 7);
        QCOMPARE(fence.text(), u"- ```calc\n\n  ```\n"_s);
        QCOMPARE(fence.role(0, "view").toString(), u"calc"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0);
        // Any fence length and info string Markdown accepts opens a block; the closing fence
        // matches the opening one.
        Fixture four(u"````\n"_s);
        QCOMPARE(four.page.split(0, 4).value(u"row"_s).toInt(), 0);
        QCOMPARE(four.text(), u"````\n\n````\n"_s);
        QCOMPARE(four.role(0, "view").toString(), u"code"_s);
        Fixture sharp(u"```c#\n"_s);
        sharp.page.split(0, 5);
        QCOMPARE(sharp.text(), u"```c#\n\n```\n"_s);
        QCOMPARE(sharp.role(0, "view").toString(), u"code"_s);
        QCOMPARE(sharp.role(0, "lang").toString(), u"c#"_s);
        Fixture tildes(u"~~~~ js\n"_s);
        tildes.page.split(0, 7);
        QCOMPARE(tildes.text(), u"~~~~ js\n\n~~~~\n"_s);
        QCOMPARE(tildes.role(0, "view").toString(), u"code"_s);
        // Enter on an empty nested item outdents it.
        Fixture nested(u"- a\n\t- \n"_s);
        nested.page.startEditing(1, 0);
        nested.page.split(1, 0);
        QCOMPARE(nested.text(), u"- a\n- \n"_s);
        // A task's text is not a fence: Enter starts the next task.
        Fixture task(u"- [ ] ```js\n"_s);
        task.page.split(0, 5);
        QCOMPARE(task.text(), u"- [ ] ```js\n- [ ] \n"_s);
        // Code blocks do not split.
        Fixture code(u"```\nx\n```\n"_s);
        QCOMPARE(code.page.split(0, 1).value(u"row"_s).toInt(), 0);
        QCOMPARE(code.text(), u"```\nx\n```\n"_s);
    }

    void mergeCommands()
    {
        Fixture f(u"- ab\n- cd\n\t- dziecko\n- \n"_s);
        QVariantMap r = f.page.mergeWithPrevious(1);
        QCOMPARE(f.text(), u"- abcd\n\t- dziecko\n- \n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 2);
        r = f.page.mergeWithPrevious(2); // empty block: removed
        QCOMPARE(f.text(), u"- abcd\n\t- dziecko\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(f.page.mergeWithPrevious(0).value(u"row"_s).toInt(), -1);
        // Delete at the end merges the next block in.
        Fixture next(u"- ab\n- cd\n"_s);
        r = next.page.mergeNext(0);
        QCOMPARE(next.text(), u"- abcd\n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 2);
        // Previous block is code: nothing merges, the caret moves there.
        Fixture code(u"```\nx\n```\n- a\n"_s);
        r = code.page.mergeWithPrevious(1);
        QCOMPARE(code.text(), u"```\nx\n```\n- a\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
    }

    void mergeUnfoldsHiddenPrevious()
    {
        Fixture f(u"- a\n\t- ukryty\n- b\n"_s);
        f.page.setFolded(0, true);
        QCOMPARE(f.visible(), QList<bool>({true, false, true}));
        f.page.mergeWithPrevious(2);
        QCOMPARE(f.text(), u"- a\n\t- ukrytyb\n"_s);
        QCOMPARE(f.visible(), QList<bool>({true, true}));
    }

    void indentOutdentMove()
    {
        const QString content = u"Wstęp  \n\n- a\n- b\n\t- b1\n- c\n\nKoniec ąę\n"_s;
        Fixture f(content);
        QVERIFY(f.page.indent(2));
        QCOMPARE(f.text(), u"Wstęp  \n\n- a\n\t- b\n\t\t- b1\n- c\n\nKoniec ąę\n"_s);
        QVERIFY(f.page.outdent(2));
        QCOMPARE(f.text(), content);
        QVERIFY(f.page.moveDown(2));
        QCOMPARE(f.text(), u"Wstęp  \n\n- a\n- c\n- b\n\t- b1\n\nKoniec ąę\n"_s);
        QVERIFY(f.page.moveUp(3));
        QCOMPARE(f.text(), content);
        f.page.undo();
        f.page.undo();
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), content);
        Fixture first(u"- a\n- b\n"_s);
        QVERIFY(!first.page.indent(0)); // no previous sibling
        QVERIFY(!first.page.outdent(0));
    }

    void movingKeepsEditingBlock()
    {
        Fixture f(u"- a\n- b\n- c\n"_s);
        f.page.startEditing(2, 1);
        f.page.moveUp(2);
        QCOMPARE(f.page.editingRow(), 1);
        QCOMPARE(f.edit(1), u"c"_s);
    }

    void tasks()
    {
        Fixture f(u"- Zadanie\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- [ ] Zadanie\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- [/] Zadanie\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- [x] Zadanie ✅ 2026-10-15\n"_s);
        f.page.cycleTask(0);
        QCOMPARE(f.text(), u"- Zadanie\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [x] Zadanie ✅ 2026-10-15\n"_s);
        f.page.toggleTask(0);
        QCOMPARE(f.text(), u"- [ ] Zadanie\n"_s);
        f.page.setTaskStatus(0, u"cancelled"_s);
        QCOMPARE(f.text(), u"- [-] Zadanie ❌ 2026-10-15\n"_s);
        f.page.removeTaskMarker(0);
        QCOMPARE(f.text(), u"- Zadanie\n"_s);
    }

    void recurringTaskInsertsNextOccurrence()
    {
        Fixture f(u"- [ ] Podlać kwiaty 📅 2026-10-15 🔁 every week\n"_s);
        f.page.startEditing(0, 3);
        f.page.toggleTask(0);
        QCOMPARE(f.count(), 2);
        QVERIFY(f.text().startsWith(u"- [ ] Podlać kwiaty 📅 2026-10-22"_s));
        QVERIFY(f.text().contains(u"- [x] Podlać kwiaty 📅 2026-10-15 🔁 every week ✅ 2026-10-15"_s));
        QCOMPARE(f.page.editingRow(), 1); // still the completed block
    }

    void deleteAndInsert()
    {
        Fixture f(u"- a\n\t- a1\n- b\n"_s);
        f.page.startEditing(0, 0);
        const int focus = f.page.deleteBlock(0);
        QCOMPARE(f.text(), u"- b\n"_s);
        QCOMPARE(focus, 0);
        const int row = f.page.insertBlockAfter(0, u"nowy ż"_s);
        QCOMPARE(row, 1);
        QCOMPARE(f.text(), u"- b\n- nowy ż\n"_s);
        QCOMPARE(f.page.editingRow(), 1);
        // A trailing empty block is reused.
        Fixture empty(u"- a\n- \n"_s);
        QCOMPARE(empty.page.appendEmptyBlock(), 1);
        QCOMPARE(empty.text(), u"- a\n- \n"_s);
        Fixture para(u"Akapit\n"_s);
        QCOMPARE(para.page.appendEmptyBlock(), 1);
        QCOMPARE(para.model()->document().at(1).kind, BlockKind::ListItem);
        Fixture none{QString()};
        QCOMPARE(none.page.appendEmptyBlock(), 0);
        QCOMPARE(none.count(), 1);
    }

    void insertMarkdownAndTemplates()
    {
        Fixture f(u"- a\n\t- a1\n- \n"_s);
        int row = f.page.insertMarkdown(2, u"- [ ] x\n- [ ] y"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n- [ ] x\n- [ ] y\n"_s);
        QCOMPARE(row, 2);
        row = f.page.insertMarkdown(1, u"## Sekcja"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\n## Sekcja\n\n- [ ] x\n- [ ] y\n"_s);
        QCOMPARE(f.model()->document().at(row).kind, BlockKind::Heading);
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n\t- a1\n- \n"_s);
        // Templates are filled ({{date}}, {{title}}).
        f.host.setFile(u"templates/T.md"_s, u"- Notatka {{date}} — {{title}}\n"_s);
        f.page.insertTemplate(2, u"templates/T.md"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n- Notatka 2026-10-15 — Strona\n"_s);
    }

    // --- zoom and folding ----------------------------------------------------------------------------

    void zoom()
    {
        Fixture f(u"- a\n\t- a1\n\t\t- a11\n\t- a2\n- b\n"_s);
        QSignalSpy zoomSpy(&f.page, &PageController::zoomChanged);
        f.page.zoomInto(1);
        QCOMPARE(f.page.zoomBlock(), 1);
        QCOMPARE(f.visible(), QList<bool>({false, true, true, false, false}));
        QCOMPARE(f.role(1, "depth").toInt(), 0);
        QCOMPARE(f.role(2, "depth").toInt(), 1);
        QCOMPARE(f.role(2, "docDepth").toInt(), 2);
        QVERIFY(f.role(1, "isZoomRoot").toBool());
        const QVariantList crumbs = f.page.breadcrumb();
        QCOMPARE(crumbs.size(), 2);
        QCOMPARE(crumbs[0].toMap().value(u"title"_s).toString(), u"a"_s);
        QCOMPARE(crumbs[1].toMap().value(u"row"_s).toInt(), 1);
        // Enter at the end of the zoomed block adds a first child inside the zoom.
        f.page.startEditing(1, 2);
        const QVariantMap r = f.page.split(1, 2);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t\t- \n\t\t- a11\n\t- a2\n- b\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QVERIFY(f.role(2, "visible").toBool());
        // At its start too: a block above the zoom root would be outside the zoom.
        f.page.startEditing(1, 0);
        const QVariantMap atStart = f.page.split(1, 0);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t\t- \n\t\t- \n\t\t- a11\n\t- a2\n- b\n"_s);
        QCOMPARE(atStart.value(u"row"_s).toInt(), 2);
        QCOMPARE(f.page.editingRow(), 2);
        QVERIFY(f.role(2, "visible").toBool());
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t\t- \n\t\t- a11\n\t- a2\n- b\n"_s);
        // The zoom root cannot be moved or outdented; its children cannot leave it.
        QVERIFY(!f.page.moveUp(1));
        QVERIFY(!f.page.outdent(2));
        f.page.zoomOut();
        QCOMPARE(f.page.zoomBlock(), 0);
        f.page.zoomOut();
        QCOMPARE(f.page.zoomBlock(), -1);
        QCOMPARE(f.visible(), QList<bool>(6, true));
        QVERIFY(zoomSpy.size() >= 3);
    }

    // Enter at the start of a zoomed task: a new first child, the task keeps its text and status.
    void zoomRootTaskEnterAtTheStart()
    {
        Fixture f(u"- a\n\t- [x] zadanie ść\n\t\t- a11\n- b\n"_s);
        f.page.zoomInto(1);
        f.page.startEditing(1, 0);
        const QVariantMap r = f.page.split(1, 0);
        QCOMPARE(f.text(), u"- a\n\t- [x] zadanie ść\n\t\t- \n\t\t- a11\n- b\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0);
        QCOMPARE(f.page.zoomBlock(), 1);
        QCOMPARE(f.visible(), QList<bool>({false, true, true, true, false}));
        QCOMPARE(f.role(1, "taskStatus").toString(), u"done"_s);
    }

    // Enter at the start of a zoomed heading or paragraph (no list item, no children): the old
    // split, an empty paragraph above, and the caret stays in the zoom root, which stays shown.
    void zoomRootTextBlockEnterAtTheStart_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QString>("rootText");

        QTest::newRow("heading") << u"# Nagłówek\n\n- a\n"_s << u"\n\n# Nagłówek\n\n- a\n"_s << u"# Nagłówek"_s;
        QTest::newRow("paragraph") << u"Akapit źdźbła\n\n- a\n"_s << u"\n\nAkapit źdźbła\n\n- a\n"_s
                                   << u"Akapit źdźbła"_s;
    }

    void zoomRootTextBlockEnterAtTheStart()
    {
        QFETCH(QString, content);
        QFETCH(QString, expected);
        QFETCH(QString, rootText);
        Fixture f(content);
        f.page.zoomInto(0);
        QCOMPARE(f.page.zoomBlock(), 0);
        f.page.startEditing(0, 0);
        const QVariantMap r = f.page.split(0, 0);
        const int zoom = f.page.zoomBlock();
        QVERIFY(zoom >= 0);
        QCOMPARE(f.edit(zoom), rootText);
        QCOMPARE(f.page.editingRow(), zoom);
        QCOMPARE(r.value(u"row"_s).toInt(), zoom);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0);
        QVERIFY(f.role(zoom, "visible").toBool());
        QCOMPARE(f.text(), expected);
        // Typing goes into the shown zoom root.
        f.page.setEditText(zoom, rootText + u" ż"_s, int(rootText.size()) + 2);
        QCOMPARE(f.page.zoomBlock(), zoom);
        QVERIFY(f.text().contains(rootText + u" ż\n"_s));
    }

    // Enter at the end or in the middle of a zoomed heading or paragraph: it has no children (only
    // list items nest), so the block Enter makes below it cannot be inside the zoom. The zoom ends
    // and Enter does exactly what it does on the page (the same text, the caret in the new block,
    // never in a hidden one); undo restores the text and the zoom, redo ends it again.
    void zoomRootTextBlockEnterLeavesZoom_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("row");
        QTest::addColumn<int>("cursor"); // -1: the end of the edit text

        QTest::newRow("nagłówek, Enter na końcu") << u"# Nagłówek\n\n- a\n"_s << 0 << -1;
        QTest::newRow("akapit, Enter na końcu") << u"Akapit źdźbła\n\n- a\n"_s << 0 << -1;
        QTest::newRow("nagłówek, Enter w środku") << u"# Nagłówek\n\n- a\n"_s << 0 << 5;
        QTest::newRow("akapit, Enter w środku") << u"Akapit źdźbła\n\n- a\n"_s << 0 << 7;
        QTest::newRow("nagłówek między listami") << u"- a\n\t- a1\n\n## Środek ąę\n\n- b\n"_s << 2 << -1;
        QTest::newRow("akapit po frontmatterze") << u"---\ntags: [x]\n---\nZażółć gęślą jaźń\n\n- punkt\n"_s << 1 << -1;
        QTest::newRow("CRLF") << u"Akapit ćma\r\n\r\n- a\r\n"_s << 0 << -1;
        QTest::newRow("CRLF, Enter w środku") << u"# Ćma nocą\r\n\r\n- a\r\n"_s << 0 << 5;
        QTest::newRow("akapit z łamaniem wiersza") << u"Wiersz pierwszy\nwiersz drugi ż\n\n- a\n"_s << 0 << -1;
        QTest::newRow("akapit z łamaniem, Enter po pierwszym wierszu")
                << u"Wiersz pierwszy\nwiersz drugi ż\n\n- a\n"_s << 0 << 15;
        QTest::newRow("nagłówek z identyfikatorem bloku") << u"## Plan ^plan1\n\n- a\n"_s << 0 << -1;
        QTest::newRow("ostatni blok bez końca wiersza") << u"- a\n\nKoniec źródła"_s << 1 << -1;
        QTest::newRow("jedyny blok strony") << u"Tylko akapit ąę\n"_s << 0 << -1;
        QTest::newRow("nie nagłówek: #bez spacji") << u"#Tag na początku\n"_s << 0 << -1;
        QTest::newRow("nagłówek H7 (akapit)") << u"####### za dużo krzyżyków\n"_s << 0 << -1;
    }

    void zoomRootTextBlockEnterLeavesZoom()
    {
        QFETCH(QString, content);
        QFETCH(int, row);
        QFETCH(int, cursor);
        Fixture plain(content);
        Fixture zoomed(content);
        const QString view = zoomed.role(row, "view").toString();
        QVERIFY2(view == u"text"_s || view == u"heading"_s, qPrintable(view));
        zoomed.page.zoomInto(row);
        QCOMPARE(zoomed.page.zoomBlock(), row);
        const int at = cursor < 0 ? int(zoomed.edit(row).size()) : cursor;
        plain.page.startEditing(row, at);
        zoomed.page.startEditing(row, at);
        QCOMPARE(zoomed.page.zoomBlock(), row); // the zoom root is shown: editing it keeps the zoom
        QSignalSpy zoomSpy(&zoomed.page, &PageController::zoomChanged);

        const QVariantMap expected = plain.page.split(row, at);
        const QVariantMap r = zoomed.page.split(row, at);
        const QString after = plain.text();
        QVERIFY(after != content);
        QCOMPARE(zoomed.text(), after);
        QCOMPARE(r, expected);
        if (content.contains(u"\r\n"_s))
            QCOMPARE(after.count(u'\n'), after.count(u"\r\n"_s)); // CRLF kept, no bare line feed
        const int focusRow = r.value(u"row"_s).toInt();
        QVERIFY(focusRow > row); // the new block below
        QCOMPARE(zoomed.page.zoomBlock(), -1);
        QVERIFY(!zoomSpy.isEmpty());
        QVERIFY(!zoomed.role(row, "isZoomRoot").toBool());
        QCOMPARE(zoomed.visible(), QList<bool>(zoomed.count(), true));
        QCOMPARE(zoomed.page.breadcrumb(), QVariantList());
        QCOMPARE(zoomed.page.editingRow(), focusRow);
        QCOMPARE(zoomed.page.cursorPosition(), r.value(u"cursor"_s).toInt());
        QVERIFY(zoomed.role(focusRow, "visible").toBool());
        QVERIFY(zoomed.role(focusRow, "isEditing").toBool());
        QCOMPARE(zoomed.edit(focusRow), plain.edit(focusRow));

        // Undo: the text and the zoom, the caret where Enter was pressed.
        zoomed.page.undo();
        QCOMPARE(zoomed.text(), content);
        QCOMPARE(zoomed.page.zoomBlock(), row);
        QVERIFY(zoomed.role(row, "isZoomRoot").toBool());
        QVERIFY(zoomed.role(row, "visible").toBool());
        QCOMPARE(zoomed.page.editingRow(), row);
        QCOMPARE(zoomed.page.cursorPosition(), at);
        // Redo: the zoom ends again, the caret in the new block.
        zoomed.page.redo();
        QCOMPARE(zoomed.text(), after);
        QCOMPARE(zoomed.page.zoomBlock(), -1);
        QCOMPARE(zoomed.visible(), QList<bool>(zoomed.count(), true));
        QCOMPARE(zoomed.page.editingRow(), focusRow);
        QCOMPARE(zoomed.page.cursorPosition(), r.value(u"cursor"_s).toInt());
        // Undo once more: back in the zoom; typing goes into the shown zoom root.
        zoomed.page.undo();
        QCOMPARE(zoomed.page.zoomBlock(), row);
        const QString rootText = zoomed.edit(row);
        zoomed.page.setEditText(row, rootText + u"ż"_s, int(rootText.size()) + 1);
        QCOMPARE(zoomed.page.zoomBlock(), row);
        QCOMPARE(zoomed.edit(row), rootText + u"ż"_s);
    }

    // Enter on an empty zoomed paragraph (a blank block): the new block is below it, out of the
    // zoom — the zoom ends as for any other zoomed paragraph.
    void zoomRootEmptyParagraphEnter()
    {
        const QString content = u"Akapit ąę\n\n- a\n"_s;
        Fixture plain(content);
        Fixture zoomed(content);
        for (Fixture *f : {&plain, &zoomed}) {
            f->page.startEditing(0, -1);
            const QVariantMap made = f->page.split(0, 9);
            QCOMPARE(made.value(u"row"_s).toInt(), 1);
            QCOMPARE(f->edit(1), QString());
        }
        const QString blank = zoomed.text();
        zoomed.page.zoomInto(1);
        QCOMPARE(zoomed.page.zoomBlock(), 1);
        QCOMPARE(zoomed.page.editingRow(), 1);
        const QVariantMap expected = plain.page.split(1, 0);
        const QVariantMap r = zoomed.page.split(1, 0);
        QCOMPARE(zoomed.text(), plain.text());
        QCOMPARE(r, expected);
        const int focusRow = r.value(u"row"_s).toInt();
        QCOMPARE(focusRow, 2);
        QCOMPARE(zoomed.page.zoomBlock(), -1);
        QCOMPARE(zoomed.page.editingRow(), focusRow);
        QCOMPARE(zoomed.visible(), QList<bool>(zoomed.count(), true));
        zoomed.page.undo();
        QCOMPARE(zoomed.text(), blank);
        QCOMPARE(zoomed.page.zoomBlock(), 1);
        QCOMPARE(zoomed.page.editingRow(), 1);
        QCOMPARE(zoomed.edit(1), QString());
    }

    // Enter in the middle of a zoomed list item: its tail becomes the zoom root's first child,
    // inside the zoom — also when it had no children (a split makes the next sibling then) or is
    // folded (the zoom root's children are shown whatever its fold). The zoom stays, and undo /
    // redo keep it although the zoomed block's own text changed.
    void zoomRootListItemEnterInTheMiddle_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<bool>("fold");
        QTest::addColumn<int>("row");
        QTest::addColumn<int>("cursor");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("focusRow");
        QTest::addColumn<QString>("focusEdit");

        QTest::newRow("punkt bez dzieci") << u"- a\n\t- a1 źdźbło\n- b\n"_s << false << 1 << 3
                                          << u"- a\n\t- a1 \n\t\t- źdźbło\n- b\n"_s << 2 << u"źdźbło"_s;
        QTest::newRow("zadanie bez dzieci") << u"- [ ] kup mleko ść\n- b\n"_s << false << 0 << 4
                                            << u"- [ ] kup \n\t- [ ] mleko ść\n- b\n"_s << 1 << u"mleko ść"_s;
        QTest::newRow("zwinięty punkt z dziećmi") << u"- ab\n\t- dziecko\n"_s << true << 0 << 1
                                                  << u"- a\n\t- b\n\t- dziecko\n"_s << 1 << u"b"_s;
        QTest::newRow("punkt z dziećmi (bez zmian)") << u"- ab\n\t- dziecko\n"_s << false << 0 << 1
                                                     << u"- a\n\t- b\n\t- dziecko\n"_s << 1 << u"b"_s;
        QTest::newRow("punkt numerowany bez dzieci") << u"1. pierwszy drugi\n2. trzeci\n"_s << false << 0 << 9
                                                     << u"1. pierwszy \n\t1. drugi\n2. trzeci\n"_s << 1 << u"drugi"_s;
        QTest::newRow("ostatni punkt bez końca wiersza") << u"- wstęp\n- ąę źć"_s << false << 1 << 3
                                                         << u"- wstęp\n- ąę \n\t- źć"_s << 2 << u"źć"_s;
        QTest::newRow("CRLF") << u"- ąę źć\r\n- b\r\n"_s << false << 0 << 3
                              << u"- ąę \r\n\t- źć\r\n- b\r\n"_s << 1 << u"źć"_s;
        // At the end and at the start: a new empty first child (as before).
        QTest::newRow("punkt, Enter na końcu") << u"- a\n\t- a1 ąę\n- b\n"_s << false << 1 << 5
                                               << u"- a\n\t- a1 ąę\n\t\t- \n- b\n"_s << 2 << QString();
        QTest::newRow("punkt, Enter na początku") << u"- a\n\t- a1 ąę\n- b\n"_s << false << 1 << 0
                                                  << u"- a\n\t- a1 ąę\n\t\t- \n- b\n"_s << 2 << QString();
    }

    void zoomRootListItemEnterInTheMiddle()
    {
        QFETCH(QString, content);
        QFETCH(bool, fold);
        QFETCH(int, row);
        QFETCH(int, cursor);
        QFETCH(QString, expected);
        QFETCH(int, focusRow);
        QFETCH(QString, focusEdit);
        Fixture f(content);
        if (fold)
            f.page.setFolded(row, true);
        const QString status = f.role(row, "taskStatus").toString();
        f.page.zoomInto(row);
        f.page.startEditing(row, cursor);
        const QVariantMap r = f.page.split(row, cursor);
        QCOMPARE(f.text(), expected);
        QCOMPARE(r.value(u"row"_s).toInt(), focusRow);
        QCOMPARE(f.page.editingRow(), focusRow);
        QCOMPARE(f.edit(focusRow), focusEdit);
        QCOMPARE(f.page.cursorPosition(), 0);
        QCOMPARE(f.page.zoomBlock(), row); // the zoom stays
        QVERIFY(f.role(focusRow, "visible").toBool());
        QCOMPARE(f.model()->document().parent(focusRow), row);
        QCOMPARE(f.role(row, "taskStatus").toString(), status);
        QCOMPARE(f.role(focusRow, "taskStatus").toString(), status); // a task goes on as a task
        // One undo step.
        f.page.undo();
        QCOMPARE(f.text(), content);
        QCOMPARE(f.page.zoomBlock(), row);
        QCOMPARE(f.page.editingRow(), row);
        QCOMPARE(f.page.cursorPosition(), cursor);
        f.page.redo();
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.page.editingRow(), focusRow);
        QCOMPARE(f.page.zoomBlock(), row);
    }

    // Enter that edits the zoomed block in place keeps the zoom: "```js" + Enter opens a code
    // block in it; a quote does not split.
    void zoomRootEnterKeepsZoomForInPlaceEdits()
    {
        Fixture fence(u"- a\n\n```js\n"_s);
        QCOMPARE(fence.role(1, "view").toString(), u"text"_s);
        fence.page.zoomInto(1);
        fence.page.startEditing(1, -1);
        const QVariantMap r = fence.page.split(1, 5);
        QCOMPARE(fence.text(), u"- a\n\n```js\n\n```\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(fence.page.zoomBlock(), 1);
        QCOMPARE(fence.page.editingRow(), 1);
        QCOMPARE(fence.role(1, "view").toString(), u"code"_s);
        fence.page.undo();
        QCOMPARE(fence.text(), u"- a\n\n```js\n"_s);
        QCOMPARE(fence.page.zoomBlock(), 1);

        Fixture quote(u"> cytat ąę\n\n- a\n"_s);
        quote.page.zoomInto(0);
        quote.page.startEditing(0, 3);
        quote.page.split(0, 3);
        QCOMPARE(quote.text(), u"> cytat ąę\n\n- a\n"_s);
        QCOMPARE(quote.page.zoomBlock(), 0);
    }

    // Undo / redo of the other steps leave the zoom as it is (it follows its block): only a step
    // that ended a zoom brings it back.
    void zoomUndoOfOtherSteps()
    {
        Fixture f(u"# Nagłówek\n\n- a\n- b\n"_s);
        f.page.split(1, 1); // a new item below "a", on the whole page (no block edited before)
        QCOMPARE(f.text(), u"# Nagłówek\n\n- a\n- \n- b\n"_s);
        f.page.stopEditing();
        f.page.zoomInto(3);
        f.page.undo();
        QCOMPARE(f.text(), u"# Nagłówek\n\n- a\n- b\n"_s);
        QCOMPARE(f.page.zoomBlock(), 2); // still "- b", not the page the split was made on
        QVERIFY(f.role(2, "isZoomRoot").toBool());

        Fixture typed(u"- a\n\t- a1\n- b\n"_s);
        typed.page.zoomInto(0);
        typed.page.startEditing(1, -1);
        typed.page.setEditText(1, u"a1 ąę"_s, 5);
        typed.page.stopEditing();
        typed.page.zoomTo(-1);
        typed.page.undo();
        QCOMPARE(typed.text(), u"- a\n\t- a1\n- b\n"_s);
        QCOMPARE(typed.page.zoomBlock(), -1); // not the zoom the typing was done in
        QCOMPARE(typed.visible(), QList<bool>(3, true));
    }

    // Undo / redo never leave the caret in a hidden block. Enter made in a zoom brings its zoom
    // back; the step next to it, made on the page after the breadcrumb, then ends that zoom again
    // (the zoom a step leaves as it is stays only while it shows the restored caret).
    void zoomUndoRedoNeverHideTheCaret()
    {
        const auto caretShown = [](const Fixture &f) {
            const int row = f.page.editingRow();
            return row >= 0 && f.role(row, "visible").toBool() && f.role(row, "isEditing").toBool();
        };
        // Redo: Enter in a zoom, the breadcrumb, typing on the page; undo both, redo both.
        Fixture f(u"- a\n- b\n\nAkapit C ąę\n"_s);
        f.page.zoomInto(0);
        f.page.startEditing(0, -1);
        f.page.split(0, 1);
        QCOMPARE(f.text(), u"- a\n\t- \n- b\n\nAkapit C ąę\n"_s);
        f.page.zoomTo(-1);
        f.page.startEditing(3, -1);
        f.page.setEditText(3, u"Akapit C ąęż"_s, 12);
        f.page.undo(); // the typing
        QCOMPARE(f.page.zoomBlock(), -1);
        QCOMPARE(f.page.editingRow(), 3);
        QVERIFY(caretShown(f));
        f.page.undo(); // Enter: back in its zoom
        QCOMPARE(f.text(), u"- a\n- b\n\nAkapit C ąę\n"_s);
        QCOMPARE(f.page.zoomBlock(), 0);
        QCOMPARE(f.page.editingRow(), 0);
        QVERIFY(caretShown(f));
        f.page.redo(); // Enter again, in its zoom
        QCOMPARE(f.page.zoomBlock(), 0);
        QCOMPARE(f.page.editingRow(), 1);
        QVERIFY(caretShown(f));
        QSignalSpy zoomSpy(&f.page, &PageController::zoomChanged);
        f.page.redo(); // the typing on the page: the zoom on "a" ends
        QCOMPARE(f.text(), u"- a\n\t- \n- b\n\nAkapit C ąęż\n"_s);
        QCOMPARE(f.page.editingRow(), 3);
        QCOMPARE(f.page.zoomBlock(), -1);
        QVERIFY(caretShown(f));
        QCOMPARE(f.visible(), QList<bool>(4, true));
        QVERIFY(!zoomSpy.isEmpty());
        QCOMPARE(f.page.cursorPosition(), 12);
        f.page.setEditText(3, u"Akapit C ąężź"_s, 13);
        QCOMPARE(f.text(), u"- a\n\t- \n- b\n\nAkapit C ąężź\n"_s);

        // Undo: typing on the page, Enter in a zoom, the breadcrumb; undo both.
        Fixture g(u"- L\n\nAkapit P\n"_s);
        g.page.startEditing(1, -1);
        g.page.setEditText(1, u"Akapit P źdźbło"_s, 15);
        g.page.zoomInto(0);
        g.page.startEditing(0, -1);
        g.page.split(0, 1);
        QCOMPARE(g.text(), u"- L\n\t- \n\nAkapit P źdźbło\n"_s);
        g.page.zoomTo(-1);
        g.page.undo(); // Enter: back in its zoom
        QCOMPARE(g.page.zoomBlock(), 0);
        QCOMPARE(g.page.editingRow(), 0);
        QVERIFY(caretShown(g));
        g.page.undo(); // the typing on the page: the zoom ends
        QCOMPARE(g.text(), u"- L\n\nAkapit P\n"_s);
        QCOMPARE(g.page.editingRow(), 1);
        QCOMPARE(g.page.zoomBlock(), -1);
        QVERIFY(caretShown(g));
        g.page.redo();
        QCOMPARE(g.page.editingRow(), 1);
        QCOMPARE(g.page.zoomBlock(), -1);
        QVERIFY(caretShown(g));

        // Without Enter: typing on the page, a zoom elsewhere, undo.
        Fixture h(u"- a\n\t- a1\n- b\n"_s);
        h.page.startEditing(2, -1);
        h.page.setEditText(2, u"b ść"_s, 4);
        h.page.stopEditing();
        h.page.zoomInto(0);
        h.page.undo();
        QCOMPARE(h.text(), u"- a\n\t- a1\n- b\n"_s);
        QCOMPARE(h.page.editingRow(), 2);
        QCOMPARE(h.page.zoomBlock(), -1);
        QVERIFY(caretShown(h));

        // A zoom that holds the block stays: undo inside it.
        Fixture inside(u"- a\n\t- a1\n- b\n"_s);
        inside.page.startEditing(1, -1);
        inside.page.setEditText(1, u"a1 żółw"_s, 7);
        inside.page.zoomInto(0);
        inside.page.undo();
        QCOMPARE(inside.page.zoomBlock(), 0);
        QCOMPARE(inside.page.editingRow(), 1);
        QVERIFY(caretShown(inside));

        // A fold made after the step: undo opens it (as editing a hidden block does).
        Fixture folded(u"- a\n\t- a1\n- b\n"_s);
        folded.page.startEditing(1, -1);
        folded.page.setEditText(1, u"a1 ść"_s, 5);
        folded.page.setFolded(0, true);
        QCOMPARE(folded.page.editingRow(), 0); // the caret left the folded subtree
        folded.page.undo();
        QCOMPARE(folded.text(), u"- a\n\t- a1\n- b\n"_s);
        QCOMPARE(folded.page.editingRow(), 1);
        QVERIFY(caretShown(folded));
        QCOMPARE(folded.page.foldState(), QVariantList());
    }

    // The same for a block selection undo / redo brings back: a zoom that hides it ends.
    void zoomUndoRedoNeverHideTheSelection()
    {
        Fixture f(u"- a\n- b\n\nAkapit C\n\nAkapit D ąę\n"_s);
        f.page.zoomInto(0);
        f.page.startEditing(0, -1);
        f.page.split(0, 1);
        QCOMPARE(f.text(), u"- a\n\t- \n- b\n\nAkapit C\n\nAkapit D ąę\n"_s);
        f.page.zoomTo(-1);
        f.page.stopEditing();
        f.page.selectRange(4, 4);
        QCOMPARE(f.edit(4), u"Akapit D ąę"_s);
        f.page.deleteSelection();
        const QString deleted = f.text();
        QVERIFY(!deleted.contains(u"Akapit D"_s));
        QCOMPARE(f.page.selectedRow(), 3);
        f.page.undo(); // the deletion: the paragraph is back, selected
        QCOMPARE(f.page.selectedRow(), 4);
        QCOMPARE(f.page.selectionCount(), 1);
        f.page.undo(); // Enter: back in its zoom
        QCOMPARE(f.page.zoomBlock(), 0);
        f.page.redo(); // Enter again, in its zoom
        QCOMPARE(f.page.zoomBlock(), 0);
        QCOMPARE(f.page.editingRow(), 1);
        f.page.redo(); // the deletion made on the page: "Akapit C" selected, shown
        QCOMPARE(f.text(), deleted);
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.page.zoomBlock(), -1);
        QCOMPARE(f.page.selectedRow(), 3);
        QCOMPARE(f.page.selectionCount(), 1);
        QVERIFY(f.role(3, "visible").toBool());

        // Undo: a deletion on the page, Enter in a zoom; undo both.
        Fixture g(u"- a\n- b\n\nAkapit C\n\nAkapit D ąę\n"_s);
        g.page.selectRange(3, 3);
        g.page.deleteSelection();
        g.page.zoomInto(0);
        g.page.startEditing(0, -1);
        g.page.split(0, 1);
        g.page.undo();
        QCOMPARE(g.page.zoomBlock(), 0);
        g.page.undo(); // the deletion: "Akapit D ąę" selected again, the zoom ends
        QCOMPARE(g.text(), u"- a\n- b\n\nAkapit C\n\nAkapit D ąę\n"_s);
        QCOMPARE(g.page.zoomBlock(), -1);
        QCOMPARE(g.page.selectedRow(), 3);
        QCOMPARE(g.page.selectionCount(), 1);
        QVERIFY(g.role(3, "visible").toBool());
    }

    // Zooming out (the breadcrumb's arrow or one of its entries) keeps the edited block shown: the
    // zoom root's children are shown whatever its fold, so leaving a folded zoom root (or one
    // below a fold) opens the folds over the block. A zoom that does not hold it stops editing.
    void zoomOutKeepsTheEditedBlockShown_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QList<int>>("folds");    // rows folded before zooming
        QTest::addColumn<int>("zoom");            // the row zoomed into
        QTest::addColumn<int>("edit");            // the row edited in it
        QTest::addColumn<int>("enterAt");         // >= 0: Enter in the zoom root there instead
        QTest::addColumn<int>("target");          // -2: zoomOut(), else zoomTo(target)
        QTest::addColumn<int>("zoomAfter");
        QTest::addColumn<int>("editingAfter");
        QTest::addColumn<QVariantList>("foldsAfter"); // lines

        QTest::newRow("zwinięty korzeń, strzałka") << u"- ab\n\t- dziecko ąę\n- c\n"_s << QList<int>{0} << 0 << 1
                                                    << -1 << -2 << -1 << 1 << QVariantList();
        QTest::newRow("zwinięty korzeń, okruszek strony") << u"- ab\n\t- dziecko ąę\n- c\n"_s << QList<int>{0} << 0
                                                          << 1 << -1 << -1 << -1 << 1 << QVariantList();
        QTest::newRow("Enter w środku zwiniętego korzenia") << u"- ab\n\t- dziecko\n- c\n"_s << QList<int>{0} << 0
                                                            << -1 << 1 << -2 << -1 << 1 << QVariantList();
        QTest::newRow("zwinięty rodzic korzenia, okruszek strony")
                << u"- p\n\t- q\n\t\t- r źdźbło\n- c\n"_s << QList<int>{1, 0} << 1 << 2 << -1 << -1 << -1 << 2
                << QVariantList();
        QTest::newRow("o poziom wyżej: rodzic zostaje zwinięty")
                << u"- p\n\t- q\n\t\t- r źdźbło\n- c\n"_s << QList<int>{1, 0} << 1 << 2 << -1 << -2 << 0 << 2
                << QVariantList{0};
        QTest::newRow("bez zwinięć") << u"- a\n\t- a1 ść\n- b\n"_s << QList<int>{} << 0 << 1 << -1 << -2 << -1 << 1
                                     << QVariantList();
        QTest::newRow("przybliżenie bez edytowanego bloku")
                << u"- a\n\t- a1\n- b\n\t- b1\n"_s << QList<int>{} << 0 << 1 << -1 << 2 << 2 << -1 << QVariantList();
        QTest::newRow("zwinięcie poza blokiem zostaje") << u"- a\n\t- a1 ść\n- b\n\t- b1\n"_s << QList<int>{2} << 0
                                                        << 1 << -1 << -2 << -1 << 1 << QVariantList{2};
    }

    void zoomOutKeepsTheEditedBlockShown()
    {
        QFETCH(QString, content);
        QFETCH(QList<int>, folds);
        QFETCH(int, zoom);
        QFETCH(int, edit);
        QFETCH(int, enterAt);
        QFETCH(int, target);
        QFETCH(int, zoomAfter);
        QFETCH(int, editingAfter);
        QFETCH(QVariantList, foldsAfter);
        Fixture f(content);
        for (const int row : folds)
            f.page.setFolded(row, true);
        f.page.zoomInto(zoom);
        QCOMPARE(f.page.zoomBlock(), zoom);
        if (enterAt >= 0) {
            f.page.startEditing(zoom, enterAt);
            f.page.split(zoom, enterAt);
            QCOMPARE(f.page.zoomBlock(), zoom);
        } else {
            f.page.startEditing(edit, -1);
            QCOMPARE(f.page.zoomBlock(), zoom); // shown in the zoom: editing it keeps the zoom
        }
        const int edited = f.page.editingRow();
        QVERIFY(f.role(edited, "visible").toBool());
        const QString text = f.text();
        QSignalSpy zoomSpy(&f.page, &PageController::zoomChanged);
        if (target == -2)
            f.page.zoomOut();
        else
            f.page.zoomTo(target);
        QCOMPARE(f.page.zoomBlock(), zoomAfter);
        QCOMPARE(zoomSpy.size(), 1);
        QCOMPARE(f.page.editingRow(), editingAfter);
        if (editingAfter >= 0) {
            QVERIFY(f.role(editingAfter, "visible").toBool());
            QVERIFY(f.role(editingAfter, "isEditing").toBool());
        }
        QCOMPARE(f.page.foldState(), foldsAfter);
        QCOMPARE(f.text(), text); // view state only: no edit
    }

    void zoomFollowsUndo()
    {
        Fixture f(u"- a\n\t- a1\n- b\n"_s);
        f.page.zoomInto(0);
        f.page.setEditText(1, u"a1 zmienione"_s, 12);
        f.page.undo();
        QCOMPARE(f.page.zoomBlock(), 0);
        QCOMPARE(f.visible(), QList<bool>({true, true, false}));
    }

    void folding()
    {
        Fixture f(u"- a\n\t- a1\n\t\t- a11\n- b\n\t- b1\n"_s);
        QSignalSpy foldSpy(&f.page, &PageController::foldStateChanged);
        f.page.startEditing(2, 1);
        f.page.toggleFold(0);
        QCOMPARE(f.visible(), QList<bool>({true, false, false, true, true}));
        QVERIFY(f.role(0, "collapsed").toBool());
        QCOMPARE(f.page.editingRow(), 0); // the caret moved out of the folded subtree
        QCOMPARE(f.page.foldState(), QVariantList{0});
        QCOMPARE(f.page.nextVisibleRow(0), 3);
        QCOMPARE(f.page.previousVisibleRow(3), 0);
        // The shown rows the scroll bar counts (RowScroll): a, b, b1.
        QCOMPARE(f.model()->visibleCount(), 3);
        QCOMPARE(f.model()->visibleRowsBefore(0), 0);
        QCOMPARE(f.model()->visibleRowsBefore(1), 1);
        QCOMPARE(f.model()->visibleRowsBefore(3), 1);
        QCOMPARE(f.model()->visibleRowsBefore(4), 2);
        QCOMPARE(f.model()->visibleRowsBefore(5), 3);
        QCOMPARE(f.model()->visibleRowAt(0), 0);
        QCOMPARE(f.model()->visibleRowAt(1), 3);
        QCOMPARE(f.model()->visibleRowAt(2), 4);
        QCOMPARE(f.model()->visibleRowAt(3), -1);
        QCOMPARE(f.model()->visibleRowAt(-1), -1);
        f.page.toggleFold(1); // hidden but has children: folds too
        f.page.toggleFold(0);
        QCOMPARE(f.visible(), QList<bool>({true, true, false, true, true}));
        QCOMPARE(f.page.foldState(), QVariantList{1});
        f.page.toggleFold(4); // no children: nothing
        QCOMPARE(f.page.foldState(), QVariantList{1});
        f.page.foldAll(true);
        QCOMPARE(f.visible(), QList<bool>({true, false, false, true, false}));
        f.page.foldAll(false);
        QCOMPARE(f.visible(), QList<bool>(5, true));
        QCOMPARE(f.model()->visibleRowsBefore(4), 4);
        QCOMPARE(f.model()->visibleRowAt(4), 4);
        f.page.setFoldState({3, 99});
        QCOMPARE(f.visible(), QList<bool>({true, true, true, true, false}));
        QVERIFY(foldSpy.size() >= 5);
        // Editing a hidden block unfolds it.
        f.page.startEditing(4, 0);
        QCOMPARE(f.visible(), QList<bool>(5, true));
        // Split of a folded block puts the new block after its subtree.
        f.page.setFolded(0, true);
        f.page.split(0, 1);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t\t- a11\n- \n- b\n\t- b1\n"_s);
    }

    void revealAndLookups()
    {
        Fixture f(u"- a ^ida\n\t- a1\n\t\t- a11 ^deep\n- b\n"_s);
        QCOMPARE(f.page.rowOfBlockId(u"deep"_s), 2);
        QCOMPARE(f.page.rowOfBlockId(u"nope"_s), -1);
        QCOMPARE(f.page.rowOfLine(3), 3);
        QCOMPARE(f.page.rowOfLine(99), -1);
        f.page.setFolded(0, true);
        f.page.setFolded(1, true);
        f.page.zoomInto(3);
        QVERIFY(!f.role(2, "visible").toBool());
        f.page.reveal(2);
        QVERIFY(f.role(2, "visible").toBool());
        QCOMPARE(f.page.zoomBlock(), -1);
        QCOMPARE(f.page.foldState(), QVariantList());
    }

    void collapsedPropertyFoldsInitially()
    {
        Fixture f(u"- a\n  collapsed:: true\n\t- a1\n- b\n"_s);
        QCOMPARE(f.visible(), QList<bool>({true, false, true}));
        QVERIFY(!f.role(0, "html").toString().contains(u"collapsed"_s));
    }

    // --- tokens ---------------------------------------------------------------------------------------

    void tokenCommands()
    {
        Fixture f(u"- [ ] Zadzwonić do mamy ^m1\n"_s);
        f.page.startEditing(0, 4);
        f.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰ 2026-10-16 18:00 ^m1\n"_s);
        QCOMPARE(f.page.cursorPosition(), 4);
        // Edit the reminder through its chip range (body coordinates).
        const QString body = f.model()->document().at(0).body;
        const int from = int(body.indexOf(u"⏰"_s));
        const int to = from + int(u"⏰ 2026-10-16 18:00"_s.size());
        const QVariantMap reminder = f.page.reminderAt(0, from, to);
        QCOMPARE(reminder.value(u"at"_s).toString(), u"2026-10-16 18:00"_s);
        QCOMPARE(reminder.value(u"alarm"_s).toBool(), false);
        QCOMPARE(reminder.value(u"context"_s).toString(), u"Zadzwonić do mamy"_s);
        f.page.setReminder(0, u"2026-10-17 07:30"_s, true, u"co tydzień w sobotę"_s, from, to);
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 🔁 every week on Saturday ^m1\n"_s);
        QCOMPARE(f.page.blockDates(0).value(u"recurrence"_s).toString(), u"every week on Saturday"_s);
        // Removing the recurrence while editing the reminder.
        const QString body2 = f.model()->document().at(0).body;
        const int from2 = int(body2.indexOf(u"⏰"_s));
        f.page.setReminder(0, u"2026-10-17 07:30"_s, true, QString(), from2, from2 + int(u"⏰! 2026-10-17 07:30"_s.size()));
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 ^m1\n"_s);
        f.page.setDateToken(0, u"due"_s, u"2026-10-20"_s);
        f.page.setDateToken(0, u"scheduled"_s, u"2026-10-18 09:00"_s);
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 📅 2026-10-20 ⏳ 2026-10-18 09:00 ^m1\n"_s);
        QCOMPARE(f.page.blockDates(0).value(u"due"_s).toString(), u"2026-10-20"_s);
        f.page.setDateToken(0, u"due"_s, QString());
        f.page.setRecurrence(0, u"codziennie"_s);
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 ⏳ 2026-10-18 09:00 🔁 every day ^m1\n"_s);
        f.page.setDateToken(0, u"nonsense"_s, u"2026-10-20"_s);
        f.page.setDateToken(0, u"due"_s, u"jutro"_s); // not a canonical spec: ignored
        f.page.setReminder(0, u"zła data"_s, false, QString());
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 ⏳ 2026-10-18 09:00 🔁 every day ^m1\n"_s);
        const QString body3 = f.model()->document().at(0).body;
        const int from3 = int(body3.indexOf(u"⏳"_s));
        f.page.removeToken(0, from3, from3 + int(u"⏳ 2026-10-18 09:00"_s.size()));
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do mamy ⏰! 2026-10-17 07:30 🔁 every day ^m1\n"_s);
        // insertToken / replaceRange in edit-text coordinates.
        Fixture g(u"- [ ] Kupić\n"_s);
        QCOMPARE(g.page.insertToken(0, 5, u"⏫"_s), 8);
        QCOMPARE(g.text(), u"- [ ] Kupić ⏫ \n"_s);
        QCOMPARE(g.page.replaceRange(0, 0, 5, u"Sprzedać"_s), 8);
        QCOMPARE(g.text(), u"- [ ] Sprzedać ⏫ \n"_s);
    }

    void dateChipMenu()
    {
        Fixture f(u"- [ ] Zapłacić za prąd 📅 2026-10-20 ⏰ 2026-10-16 18:00 ✅ 2026-10-14 ^p1\n"_s);
        const QString html = f.model()->data(f.model()->index(0), BlockListModel::HtmlRole).toString();
        static const QRegularExpression links(u"href=\"((?:date|reminder):\\d+-\\d+)\""_s);
        QStringList urls;
        for (auto it = links.globalMatch(html); it.hasNext();)
            urls.append(it.next().captured(1));
        QCOMPARE(urls.size(), 3);
        const auto chip = [&](int i) { return f.page.dateChip(0, urls.at(i)); };
        QCOMPARE(chip(0).value(u"kind"_s).toString(), u"due"_s);
        QCOMPARE(chip(0).value(u"at"_s).toString(), u"2026-10-20"_s);
        QCOMPARE(chip(0).value(u"time"_s).toString(), QString());
        QCOMPARE(chip(1).value(u"kind"_s).toString(), u"reminder"_s);
        QCOMPARE(chip(1).value(u"time"_s).toString(), u"18:00"_s);
        QCOMPARE(chip(1).value(u"alarm"_s).toBool(), false);
        QCOMPARE(chip(2).value(u"kind"_s).toString(), u"done"_s);
        QVERIFY(f.page.dateChip(0, u"page:x"_s).isEmpty());
        QVERIFY(f.page.dateChip(0, u"date:0-3"_s).isEmpty()); // no date token there
        QVERIFY(f.page.dateChip(5, urls.at(0)).isEmpty());

        // A click on a date: link does nothing (the text is edited instead).
        QVERIFY(f.page.activateLink(0, urls.at(0)).isEmpty());

        const auto range = [&](int i) {
            const QVariantMap c = chip(i);
            return std::pair{c.value(u"from"_s).toInt(), c.value(u"to"_s).toInt()};
        };
        auto [from, to] = range(0);
        f.page.setDateTokenAt(0, from, to, u"2026-10-27"_s);
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-10-27 ⏰ 2026-10-16 18:00 ✅ 2026-10-14 ^p1\n"_s);
        f.page.setDateTokenAt(0, from, to, u"pojutrze"_s); // not a canonical spec: ignored
        f.page.setDateTokenAt(0, 0, 3, u"2026-10-28"_s);   // no token there: ignored
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-10-27 ⏰ 2026-10-16 18:00 ✅ 2026-10-14 ^p1\n"_s);
        f.page.shiftDateTokenAt(0, from, to, 7);
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-16 18:00 ✅ 2026-10-14 ^p1\n"_s);
        f.page.moveDateTokenTo(0, from, to, 1); // tomorrow (Now: 2026-10-15)
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-10-16 ⏰ 2026-10-16 18:00 ✅ 2026-10-14 ^p1\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-16 18:00 ✅ 2026-10-14 ^p1\n"_s);

        std::tie(from, to) = range(1);
        f.page.shiftDateTokenAt(0, from, to, 0, 60);
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-16 19:00 ✅ 2026-10-14 ^p1\n"_s);
        f.page.moveDateTokenTo(0, from, to, 1); // tomorrow at this time
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-16 19:00 ✅ 2026-10-14 ^p1\n"_s);
        f.page.moveDateTokenTo(0, from, to, 0);
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-15 19:00 ✅ 2026-10-14 ^p1\n"_s);
        f.page.setReminderAlarmAt(0, from, to, true);
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰! 2026-10-15 19:00 ✅ 2026-10-14 ^p1\n"_s);
        QCOMPARE(chip(1).value(u"alarm"_s).toBool(), true);
        std::tie(from, to) = range(1);
        f.page.setReminderAlarmAt(0, from, to, false);
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-15 19:00 ✅ 2026-10-14 ^p1\n"_s);

        std::tie(from, to) = range(2);
        f.page.setDateTokenAt(0, from, to, QString()); // removed with its space
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-15 19:00 ^p1\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Zapłacić za prąd 📅 2026-11-03 ⏰ 2026-10-15 19:00 ✅ 2026-10-14 ^p1\n"_s);
    }

    /// The links of a scheme in row `row`'s rendered text, in order.
    static QStringList linksOf(const Fixture &f, int row, const QString &scheme)
    {
        const QString html = f.model()->data(f.model()->index(row), BlockListModel::HtmlRole).toString();
        const QRegularExpression re(u"href=\"("_s + scheme + u":[^\"]*)\""_s);
        QStringList out;
        for (auto it = re.globalMatch(html); it.hasNext();)
            out.append(it.next().captured(1));
        return out;
    }

    void recurrenceChipMenu()
    {
        Fixture f(u"- [ ] Podlać kwiaty 🔁 co tydzień w poniedziałek 📅 2026-10-19\r\n- inne\r\n"_s);
        const QStringList urls = linksOf(f, 0, u"recur"_s);
        QCOMPARE(urls.size(), 1);
        QVariantMap c = f.page.dateChip(0, urls.first());
        QCOMPARE(c.value(u"kind"_s).toString(), u"recurrence"_s);
        QCOMPARE(c.value(u"rule"_s).toString(), u"every week on Monday"_s);
        QVERIFY(c.value(u"isTask"_s).toBool());
        QVERIFY(f.page.activateLink(0, urls.first()).isEmpty()); // a click edits the text
        const int from = c.value(u"from"_s).toInt();
        const int to = c.value(u"to"_s).toInt();
        QVERIFY(f.page.applyTokenAction(0, from, to, u"recurrence"_s, u"co miesiąc"_s));
        QCOMPARE(f.text(), u"- [ ] Podlać kwiaty 🔁 every month 📅 2026-10-19\r\n- inne\r\n"_s);
        c = f.page.dateChip(0, linksOf(f, 0, u"recur"_s).first());
        QVERIFY(f.page.applyTokenAction(0, c.value(u"from"_s).toInt(), c.value(u"to"_s).toInt(), u"recurrence"_s,
                                        u"every month when done"_s));
        QCOMPARE(f.text(), u"- [ ] Podlać kwiaty 🔁 every month when done 📅 2026-10-19\r\n- inne\r\n"_s);
        c = f.page.dateChip(0, linksOf(f, 0, u"recur"_s).first());
        QVERIFY(f.page.applyTokenAction(0, c.value(u"from"_s).toInt(), c.value(u"to"_s).toInt(), u"remove"_s));
        QCOMPARE(f.text(), u"- [ ] Podlać kwiaty 📅 2026-10-19\r\n- inne\r\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Podlać kwiaty 🔁 every month when done 📅 2026-10-19\r\n- inne\r\n"_s);
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Podlać kwiaty 🔁 co tydzień w poniedziałek 📅 2026-10-19\r\n- inne\r\n"_s);
        f.page.redo();
        QCOMPARE(f.text(), u"- [ ] Podlać kwiaty 🔁 every month 📅 2026-10-19\r\n- inne\r\n"_s);
        QVERIFY(!f.page.applyTokenAction(0, 0, 3, u"recurrence"_s, u"every day"_s)); // no rule there
    }

    void priorityChipMenu_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("choice");
        QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("logseq");
        QTest::addColumn<QString>("expected");
        QTest::newRow("emoji") << u"- [ ] Zażółć gęślą jaźń ⏫ #praca\n"_s << u"lowest"_s << u"high"_s << false
                               << u"- [ ] Zażółć gęślą jaźń ⏬ #praca\n"_s;
        QTest::newRow("logseq") << u"- TODO [#B] Zażółć gęślą jaźń\n"_s << u"high"_s << u"medium"_s << true
                                << u"- TODO [#A] Zażółć gęślą jaźń\n"_s;
        QTest::newRow("none") << u"- [ ] Zażółć 🔽 gęślą\n"_s << QString() << u"low"_s << false
                              << u"- [ ] Zażółć gęślą\n"_s;
    }

    void priorityChipMenu()
    {
        QFETCH(QString, content);
        QFETCH(QString, choice);
        QFETCH(QString, current);
        QFETCH(bool, logseq);
        QFETCH(QString, expected);
        Fixture f(content);
        const QStringList urls = linksOf(f, 0, u"prio"_s);
        QCOMPARE(urls.size(), 1);
        const QVariantMap c = f.page.dateChip(0, urls.first());
        QCOMPARE(c.value(u"kind"_s).toString(), u"priority"_s);
        QCOMPARE(c.value(u"priority"_s).toString(), current);
        QCOMPARE(c.value(u"logseq"_s).toBool(), logseq);
        QVERIFY(f.page.applyTokenAction(0, c.value(u"from"_s).toInt(), c.value(u"to"_s).toInt(), u"priority"_s,
                                        choice));
        QCOMPARE(f.text(), expected);
        f.page.undo();
        QCOMPARE(f.text(), content);
    }

    void reminderMenuRepeatAndPriority()
    {
        Fixture f(u"- [ ] Zadzwonić do hydraulika ⏰ 2026-10-16 18:00 #telefon\n"_s);
        const QStringList urls = linksOf(f, 0, u"reminder"_s);
        QCOMPARE(urls.size(), 1);
        QVariantMap c = f.page.dateChip(0, urls.first());
        QCOMPARE(c.value(u"kind"_s).toString(), u"reminder"_s);
        QCOMPARE(c.value(u"rule"_s).toString(), QString());
        QCOMPARE(c.value(u"priority"_s).toString(), QString());
        QVERIFY(c.value(u"isTask"_s).toBool());
        const int from = c.value(u"from"_s).toInt();
        const int to = c.value(u"to"_s).toInt();
        QVERIFY(f.page.applyTokenAction(0, from, to, u"blockRecurrence"_s, u"every weekday"_s));
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do hydraulika ⏰ 2026-10-16 18:00 #telefon 🔁 every weekday\n"_s);
        QVERIFY(f.page.applyTokenAction(0, from, to, u"blockPriority"_s, u"high"_s));
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do hydraulika ⏰ 2026-10-16 18:00 #telefon 🔁 every weekday ⏫\n"_s);
        c = f.page.dateChip(0, linksOf(f, 0, u"reminder"_s).first());
        QCOMPARE(c.value(u"rule"_s).toString(), u"every weekday"_s);
        QCOMPARE(c.value(u"priority"_s).toString(), u"high"_s);
        QVERIFY(f.page.applyTokenAction(0, from, to, u"blockRecurrence"_s, QString()));
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do hydraulika ⏰ 2026-10-16 18:00 #telefon ⏫\n"_s);
        f.page.undo();
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Zadzwonić do hydraulika ⏰ 2026-10-16 18:00 #telefon\n"_s);
    }

    void journalLinkMenu()
    {
        Fixture f(u"- Spotkanie [[2026-10-20]] w sali\r\n"_s);
        const QStringList urls = linksOf(f, 0, u"day"_s);
        QCOMPARE(urls.size(), 1);
        QVariantMap c = f.page.dateChip(0, urls.first());
        QCOMPARE(c.value(u"kind"_s).toString(), u"day"_s);
        QCOMPARE(c.value(u"date"_s).toString(), u"2026-10-20"_s);
        QCOMPARE(c.value(u"url"_s).toString(), u"page:2026-10-20"_s);
        QCOMPARE(c.value(u"copyText"_s).toString(), u"[[2026-10-20]]"_s);
        // A click opens the day; in the side panel too.
        f.page.activateLink(0, urls.first());
        QCOMPARE(f.host.lastRoute().value(u"target"_s).toString(), u"2026-10-20"_s);
        QSignalSpy navigated(&f.host, &FakeEditorHost::navigated);
        f.page.activateLink(0, urls.first(), true);
        QCOMPARE(navigated.size(), 1);
        QCOMPARE(navigated.first().at(1).toBool(), true);
        const int from = c.value(u"from"_s).toInt();
        const int to = c.value(u"to"_s).toInt();
        QVERIFY(f.page.applyTokenAction(0, from, to, u"shift"_s, 7));
        QCOMPARE(f.text(), u"- Spotkanie [[2026-10-27]] w sali\r\n"_s);
        QVERIFY(f.page.applyTokenAction(0, from, to, u"move"_s, 1)); // tomorrow (Now: 2026-10-15)
        QCOMPARE(f.text(), u"- Spotkanie [[2026-10-16]] w sali\r\n"_s);
        QVERIFY(f.page.applyTokenAction(0, from, to, u"set"_s, u"2026-12-24"_s));
        QCOMPARE(f.text(), u"- Spotkanie [[2026-12-24]] w sali\r\n"_s);
        f.page.undo();
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"- Spotkanie [[2026-10-20]] w sali\r\n"_s);
    }

    void linkMenuCopyText()
    {
        Fixture f(u"- Zobacz [[Inna#Drugi blok]], [[Nowa strona]], #[[ważne sprawy]] i #zakupy\n"_s);
        QVariantMap m = f.page.linkMenu(u"page:Inna#Drugi%20blok"_s);
        QCOMPARE(m.value(u"kind"_s).toString(), u"page"_s);
        QVERIFY(!m.value(u"unresolved"_s).toBool());
        QCOMPARE(m.value(u"path"_s).toString(), u"Inna.md"_s);
        QCOMPARE(m.value(u"copyText"_s).toString(), u"[[Inna#Drugi blok]]"_s);
        m = f.page.linkMenu(u"page:Nowa%20strona"_s);
        QVERIFY(m.value(u"unresolved"_s).toBool());
        QCOMPARE(m.value(u"copyText"_s).toString(), u"[[Nowa strona]]"_s);
        // [[#Heading]] in an item of another page names that page.
        m = f.page.linkMenu(u"page:#Plan"_s, u"Inna.md"_s);
        QCOMPARE(m.value(u"copyText"_s).toString(), u"[[Inna#Plan]]"_s);
        m = f.page.linkMenu(u"tag:wa%C5%BCne%20sprawy"_s);
        QCOMPARE(m.value(u"kind"_s).toString(), u"tag"_s);
        QCOMPARE(m.value(u"tag"_s).toString(), u"ważne sprawy"_s);
        QCOMPARE(m.value(u"copyText"_s).toString(), u"#[[ważne sprawy]]"_s);
        QCOMPARE(f.page.linkMenu(u"tag:zakupy"_s).value(u"copyText"_s).toString(), u"#zakupy"_s);
        QCOMPARE(f.page.linkMenu(u"block:abc"_s).value(u"copyText"_s).toString(), u"((abc))"_s);
        QCOMPARE(f.page.linkMenu(u"ext:https://example.org/a"_s).value(u"copyText"_s).toString(),
                 u"https://example.org/a"_s);
        QVERIFY(f.page.linkMenu(u"date:1-2"_s).isEmpty());
        QVERIFY(f.page.linkMenu(u"nonsense"_s).isEmpty());
        // The tag view and Tasks are routes through the host.
        f.page.navigate({{u"view"_s, u"tag"_s}, {u"tag"_s, u"zakupy"_s}, {u"overview"_s, true}});
        QCOMPARE(f.host.lastRoute().value(u"overview"_s).toBool(), true);
        QCOMPARE(f.text(), u"- Zobacz [[Inna#Drugi blok]], [[Nowa strona]], #[[ważne sprawy]] i #zakupy\n"_s);
    }

    void tokenMenusOfOtherFiles()
    {
        Fixture f(u"- strona z zapytaniem\n"_s);
        const QString other =
                u"# Plan\r\n\r\n- [ ] Zapłacić za żłobek 📅 2026-10-20 ⏫\r\n- [ ] inne 📅 2026-10-20\r\n"_s;
        f.host.setFile(u"Plan.md"_s, other);
        // The item as a query result has it (its block's Markdown) and a link of its rendered text.
        const QString raw = u"- [ ] Zapłacić za żłobek 📅 2026-10-20 ⏫"_s;
        markdown::RenderContext ctx = f.model()->renderContextFor(u"Plan.md"_s, other, 2);
        const QString html = markdown::InlineRenderer(ctx).render(u"[ ] Zapłacić za żłobek 📅 2026-10-20 ⏫"_s);
        static const QRegularExpression dateLink(u"href=\"(date:\\d+-\\d+)\""_s);
        const QString url = dateLink.match(html).captured(1);
        QVERIFY2(!url.isEmpty(), qPrintable(html));
        const QVariantMap chip = f.page.tokenChipIn(raw, url);
        QCOMPARE(chip.value(u"kind"_s).toString(), u"due"_s);
        const QVariantMap ref = chip.value(u"ref"_s).toMap();
        QVERIFY(!ref.isEmpty());
        QVERIFY(f.page.tokenChipIn(raw, u"day:1-3"_s).isEmpty());

        QVariantMap r = f.page.editTokenIn(u"Plan.md"_s, 2, raw, ref, u"shift"_s, 1, 0);
        QVERIFY(r.value(u"ok"_s).toBool());
        const int undo = r.value(u"undo"_s).toInt();
        QVERIFY(undo > 0);
        const QString after =
                u"# Plan\r\n\r\n- [ ] Zapłacić za żłobek 📅 2026-10-21 ⏫\r\n- [ ] inne 📅 2026-10-20\r\n"_s;
        QCOMPARE(f.host.file(u"Plan.md"_s), after);
        // The token moved on: the same ref finds nothing to change (the date is another now).
        r = f.page.editTokenIn(u"Plan.md"_s, 2, raw, ref, u"shift"_s, 1, 0);
        QCOMPARE(r.value(u"reason"_s).toString(), u"gone"_s);
        QVERIFY(f.page.undoTokenEdit(undo));
        QCOMPARE(f.host.file(u"Plan.md"_s), other);
        QVERIFY(!f.page.undoTokenEdit(undo)); // once

        // Undo is refused once the file changed again.
        r = f.page.editTokenIn(u"Plan.md"_s, 2, raw, ref, u"remove"_s);
        QVERIFY(r.value(u"ok"_s).toBool());
        f.host.setFile(u"Plan.md"_s, f.host.file(u"Plan.md"_s) + u"- dopisane\r\n"_s);
        QVERIFY(!f.page.undoTokenEdit(r.value(u"undo"_s).toInt()));
        QVERIFY(f.host.file(u"Plan.md"_s).endsWith(u"- dopisane\r\n"_s));

        // Undo after the page was renamed or deleted meanwhile: the old name never comes back.
        f.host.setFile(u"Plan.md"_s, other);
        r = f.page.editTokenIn(u"Plan.md"_s, 2, raw, ref, u"shift"_s, 1, 0);
        QVERIFY(r.value(u"ok"_s).toBool());
        f.host.setFile(u"Archiwum/Plan.md"_s, f.host.file(u"Plan.md"_s));
        f.host.removeFile(u"Plan.md"_s);
        QVERIFY(!f.page.undoTokenEdit(r.value(u"undo"_s).toInt()));
        bool exists = true;
        f.host.read(u"Plan.md"_s, &exists);
        QVERIFY(!exists);
        QCOMPARE(f.host.file(u"Archiwum/Plan.md"_s), after);

        // A block the file does not have any more (its text changed): nothing written.
        f.host.setFile(u"Plan.md"_s, other);
        r = f.page.editTokenIn(u"Plan.md"_s, 2, u"- [ ] Zapłacić za żłobek i przedszkole 📅 2026-10-20 ⏫"_s, ref,
                               u"shift"_s, 1, 0);
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"reason"_s).toString(), u"gone"_s);
        QCOMPARE(f.host.file(u"Plan.md"_s), other);
        QVERIFY(!f.page.editTokenIn(u"Plan.md"_s, 2, raw, QVariantMap(), u"shift"_s, 1, 0).value(u"ok"_s).toBool());

        // The copy's line is a moment behind (a line was added above, the index does not know yet)
        // and now holds another block with an equal token: the copy's own block is edited.
        const QString twins = u"- [ ] Kupić chleb 📅 2026-10-20\n- [ ] Zapłacić rachunek 📅 2026-10-20\n"_s;
        f.host.setFile(u"Dom.md"_s, u"- nowa linia\n"_s + twins);
        const QString bill = u"- [ ] Zapłacić rachunek 📅 2026-10-20"_s;
        const auto billRef = markdown::tokenRefAt(u"[ ] Zapłacić rachunek 📅 2026-10-20"_s, 22, 35);
        QVERIFY(billRef);
        r = f.page.editTokenIn(u"Dom.md"_s, 1, bill, tokenRefToMap(*billRef), u"shift"_s, 1, 0);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(f.host.file(u"Dom.md"_s),
                 u"- nowa linia\n- [ ] Kupić chleb 📅 2026-10-20\n- [ ] Zapłacić rachunek 📅 2026-10-21\n"_s);
    }

    void tokenMenusOfThisPage()
    {
        // An item of this very page (a query result, an embed): an undo step of the editor.
        Fixture f(u"- [ ] pierwsze 📅 2026-10-20\n- [ ] drugie ⏰ 2026-10-16 09:00\n"_s);
        const QString raw = u"- [ ] drugie ⏰ 2026-10-16 09:00"_s;
        const auto ref = markdown::tokenRefAt(u"[ ] drugie ⏰ 2026-10-16 09:00"_s, 11, 29);
        QVERIFY(ref);
        const QVariantMap r = f.page.editTokenIn(Page, 1, raw, tokenRefToMap(*ref), u"alarm"_s, true);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"undo"_s).toInt(), 0);
        QCOMPARE(f.text(), u"- [ ] pierwsze 📅 2026-10-20\n- [ ] drugie ⏰! 2026-10-16 09:00\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] pierwsze 📅 2026-10-20\n- [ ] drugie ⏰ 2026-10-16 09:00\n"_s);
        QCOMPARE(f.page.tokenChipIn(raw, u"reminder:11-29"_s).value(u"kind"_s).toString(), u"reminder"_s);

        // A block typed above the item (not saved, not in the index): the item's line names the
        // block before it now, which has an equal date — the item's own block is still the one edited.
        Fixture twins(u"- [ ] A 📅 2026-10-27\n- [ ] B 📅 2026-10-27\n"_s);
        twins.page.insertBlockAfter(-1, u"nowy"_s);
        QCOMPARE(twins.text(), u"- nowy\n- [ ] A 📅 2026-10-27\n- [ ] B 📅 2026-10-27\n"_s);
        const auto bRef = markdown::tokenRefAt(u"[ ] B 📅 2026-10-27"_s, 6, 19);
        QVERIFY(bRef);
        const QVariantMap moved =
                twins.page.editTokenIn(Page, 1, u"- [ ] B 📅 2026-10-27"_s, tokenRefToMap(*bRef), u"shift"_s, 1, 0);
        QVERIFY(moved.value(u"ok"_s).toBool());
        QCOMPARE(twins.text(), u"- nowy\n- [ ] A 📅 2026-10-27\n- [ ] B 📅 2026-10-28\n"_s);
        // A block whose text changed meanwhile: nothing is edited.
        const QVariantMap gone =
                twins.page.editTokenIn(Page, 2, u"- [ ] B 📅 2026-10-27"_s, tokenRefToMap(*bRef), u"shift"_s, 1, 0);
        QCOMPARE(gone.value(u"reason"_s).toString(), u"gone"_s);
        QCOMPARE(twins.text(), u"- nowy\n- [ ] A 📅 2026-10-27\n- [ ] B 📅 2026-10-28\n"_s);
    }

    void tableCellTokens()
    {
        Fixture f(u"| Co | Kiedy |\n| --- | --- |\n| Przegląd | 📅 2026-10-20 |\n"_s);
        const QVariantMap table = f.role(0, "tableData").toMap();
        const QVariantList rows = table.value(u"rows"_s).toList();
        const QString cellHtml = rows.at(0).toList().at(1).toMap().value(u"html"_s).toString();
        static const QRegularExpression dateLink(u"href=\"(date:\\d+-\\d+)\""_s);
        const QString url = dateLink.match(cellHtml).captured(1);
        QCOMPARE(url, u"date:0-13"_s); // the cell's own range
        const QVariantMap chip = f.page.tableCellChip(0, 0, 1, url);
        QCOMPARE(chip.value(u"kind"_s).toString(), u"due"_s);
        QVERIFY(f.page.tableCellChip(0, 0, 0, url).isEmpty()); // no date in that cell
        QVERIFY(f.page.applyTableCellToken(0, 0, 1, chip.value(u"from"_s).toInt(), chip.value(u"to"_s).toInt(),
                                           u"shift"_s, 7, 0));
        QCOMPARE(f.text(), u"| Co | Kiedy |\n| --- | --- |\n| Przegląd | 📅 2026-10-27 |\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"| Co | Kiedy |\n| --- | --- |\n| Przegląd | 📅 2026-10-20 |\n"_s);
    }

    void tableCellTokensKeepTheTable_data()
    {
        QTest::addColumn<QString>("table");
        QTest::addColumn<int>("tableRow");
        QTest::addColumn<int>("column");
        QTest::addColumn<QString>("action");
        QTest::addColumn<QVariant>("a");
        QTest::addColumn<QString>("expected");
        // A hand-written table keeps its spacing: only the token's bytes change.
        const QString compact = u"|Zadanie|Termin|\n|-|-|\n|Faktura|📅 2026-10-20|\n|Raport|x|\n"_s;
        QTest::newRow("compact, a day later") << compact << 0 << 1 << u"shift"_s << QVariant(1)
                                              << u"|Zadanie|Termin|\n|-|-|\n|Faktura|📅 2026-10-21|\n|Raport|x|\n"_s;
        QTest::newRow("compact, removed") << compact << 0 << 1 << u"remove"_s << QVariant()
                                          << u"|Zadanie|Termin|\n|-|-|\n|Faktura||\n|Raport|x|\n"_s;
        const QString aligned = u"| Co       | Kiedy                |\n| -------- | -------------------- |\n"
                                u"| Przegląd | jutro 📅 2026-10-20 ⏫ |\n"_s;
        QTest::newRow("aligned, a week later")
                << aligned << 0 << 1 << u"shift"_s << QVariant(7) << QString(aligned).replace(u"20 ⏫"_s, u"27 ⏫"_s);
        QTest::newRow("aligned, priority")
                << QString(aligned).replace(u"📅 2026-10-20"_s, u"po obiedzie"_s) << 0 << 1 << u"priority"_s
                << QVariant(u"low"_s)
                << QString(aligned).replace(u"📅 2026-10-20"_s, u"po obiedzie"_s).replace(u"⏫"_s, u"🔽"_s);
        const QString header = u"Plan 📅 2026-10-20 | b\n--- | ---\nx | y\n"_s;
        QTest::newRow("header without outer pipes")
                << header << -1 << 0 << u"shift"_s << QVariant(1) << u"Plan 📅 2026-10-21 | b\n--- | ---\nx | y\n"_s;
        const QString escaped = u"| a \\| b ⏰ 2026-10-20 09:00 | c |\n| --- | --- |\n| x | y |\n"_s;
        QTest::newRow("escaped pipe in the cell")
                << escaped << -1 << 0 << u"alarm"_s << QVariant(true) << QString(escaped).replace(u"⏰ "_s, u"⏰! "_s);
    }

    void tableCellTokensKeepTheTable()
    {
        QFETCH(QString, table);
        QFETCH(int, tableRow);
        QFETCH(int, column);
        QFETCH(QString, action);
        QFETCH(QVariant, a);
        QFETCH(QString, expected);
        Fixture f(table);
        const QVariantMap data = f.role(0, "tableData").toMap();
        const QVariantList cells = tableRow < 0 ? data.value(u"headerHtml"_s).toList()
                                                : data.value(u"rows"_s).toList().at(tableRow).toList();
        const QVariant cell = cells.at(column);
        const QString html = tableRow < 0 ? cell.toString() : cell.toMap().value(u"html"_s).toString();
        static const QRegularExpression tokenLink(u"href=\"((?:date|reminder|prio):\\d+-\\d+)\""_s);
        const QString url = tokenLink.match(html).captured(1);
        QVERIFY2(!url.isEmpty(), qPrintable(html));
        const QVariantMap chip = f.page.tableCellChip(0, tableRow, column, url);
        QVERIFY(!chip.isEmpty());
        QVERIFY(f.page.applyTableCellToken(0, tableRow, column, chip.value(u"from"_s).toInt(),
                                           chip.value(u"to"_s).toInt(), action, a, 0));
        QCOMPARE(f.text(), expected);
        f.page.undo();
        QCOMPARE(f.text(), table);
    }

    void tableCellDayLinksArePageLinks()
    {
        // A [[day]] in a cell opens the daily note (a page link with the link menu): its range would
        // name the cell's text, not the table's.
        Fixture f(u"| Dzień | Notatka |\n|---|---|\n| [[2026-10-20]] | x |\n"_s);
        const QString html = f.role(0, "tableData")
                                     .toMap()
                                     .value(u"rows"_s)
                                     .toList()
                                     .at(0)
                                     .toList()
                                     .at(0)
                                     .toMap()
                                     .value(u"html"_s)
                                     .toString();
        QVERIFY2(html.contains(u"href=\"page:2026-10-20\""_s) && !html.contains(u"day:"_s), qPrintable(html));
    }

    void chipsOnLinesAfterAHeadingInAListItem()
    {
        // "- # A …" with a second line: that line's chips name ranges of the body too.
        Fixture f(u"- # A 📅 2026-10-20\n  B c 📅 2026-10-21 🔁 every day\n"_s);
        const QString html = f.role(0, "html").toString();
        static const QRegularExpression link(u"href=\"((?:date|recur):\\d+-\\d+)\""_s);
        QStringList urls;
        for (auto it = link.globalMatch(html); it.hasNext();)
            urls.append(it.next().captured(1));
        QCOMPARE(urls.size(), 3);
        const QVariantMap second = f.page.dateChip(0, urls.at(1));
        QCOMPARE(second.value(u"at"_s).toString(), u"2026-10-21"_s);
        QCOMPARE(f.page.dateChip(0, urls.at(0)).value(u"at"_s).toString(), u"2026-10-20"_s);
        QCOMPARE(f.page.dateChip(0, urls.at(2)).value(u"rule"_s).toString(), u"every day"_s);
        QVERIFY(f.page.applyTokenAction(0, second.value(u"from"_s).toInt(), second.value(u"to"_s).toInt(), u"shift"_s,
                                        1, 0));
        QCOMPARE(f.text(), u"- # A 📅 2026-10-20\n  B c 📅 2026-10-22 🔁 every day\n"_s);
        // The same block as a copy (a query result, an embed).
        const QVariantMap copy = f.page.tokenChipIn(u"- # A 📅 2026-10-20\n  B c 📅 2026-10-21"_s, urls.at(1));
        QCOMPARE(copy.value(u"at"_s).toString(), u"2026-10-21"_s);
    }

    void recurrenceDialogKeepsTheWordsAfterTheRule()
    {
        // The block menu's "Powtarzaj" and the reminder menu's "Powtarzanie > Zmień regułę…": only the
        // rule's text changes; the same rule in another phrasing stays as written.
        Fixture f(u"- Zadzwonić ⏰ 2026-10-20 09:00 🔁 co tydzień do mamy\n"_s);
        QCOMPARE(f.page.blockDates(0).value(u"recurrence"_s).toString(), u"every week"_s);
        f.page.setRecurrence(0, u"every week"_s);
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-20 09:00 🔁 co tydzień do mamy\n"_s);
        f.page.setRecurrence(0, u"codziennie"_s);
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-20 09:00 🔁 every day do mamy\n"_s);
        f.page.undo();
        // The reminder dialog with only the time changed keeps the rule as written.
        const QString body = f.model()->document().at(0).body;
        const int from = int(body.indexOf(u"⏰"_s));
        const int to = from + int(u"⏰ 2026-10-20 09:00"_s.size());
        f.page.setReminder(0, u"2026-10-20 10:00"_s, false, u"every week"_s, from, to);
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-20 10:00 🔁 co tydzień do mamy\n"_s);
        f.page.setReminder(0, u"2026-10-20 10:00"_s, false, QString(), from, to);
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-20 10:00 do mamy\n"_s);
        f.page.setRecurrence(0, QString());
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-20 10:00 do mamy\n"_s);
    }

    void dateChipsInHeadingsAndReminderWithoutTime()
    {
        // The chip's range is the body's, after the "## " the renderer strips.
        Fixture f(u"## Wyjazd ⏳ 2026-10-18\n\n- przypomnij ⏰ 2026-10-16\n"_s);
        const auto firstLink = [&](int row) {
            const QString html = f.model()->data(f.model()->index(row), BlockListModel::HtmlRole).toString();
            static const QRegularExpression link(u"href=\"((?:date|reminder):\\d+-\\d+)\""_s);
            return link.match(html).captured(1);
        };
        QVariantMap c = f.page.dateChip(0, firstLink(0));
        QCOMPARE(c.value(u"kind"_s).toString(), u"scheduled"_s);
        f.page.shiftDateTokenAt(0, c.value(u"from"_s).toInt(), c.value(u"to"_s).toInt(), 1);
        QCOMPARE(f.text(), u"## Wyjazd ⏳ 2026-10-19\n\n- przypomnij ⏰ 2026-10-16\n"_s);
        const int row = f.model()->rowCount() - 1;
        c = f.page.dateChip(row, firstLink(row));
        QCOMPARE(c.value(u"kind"_s).toString(), u"reminder"_s);
        // An hour later than the default reminder time (09:00).
        f.page.shiftDateTokenAt(row, c.value(u"from"_s).toInt(), c.value(u"to"_s).toInt(), 0, 60);
        QCOMPARE(f.text(), u"## Wyjazd ⏳ 2026-10-19\n\n- przypomnij ⏰ 2026-10-16 10:00\n"_s);
    }

    void reminderWithoutTimeUsesDefault()
    {
        Fixture f(u"- x ⏰ 2026-10-16\n"_s);
        const QString body = f.model()->document().at(0).body;
        QCOMPARE(f.page.reminderAt(0, 2, int(body.size())).value(u"at"_s).toString(), u"2026-10-16 09:00"_s);
        QVERIFY(f.page.reminderAt(0, 0, 1).isEmpty());
    }

    void blockRefs()
    {
        Fixture f(u"- cel\n- inny\n"_s);
        const QString id = f.page.ensureBlockRef(0);
        QCOMPARE(id.size(), 6);
        QCOMPARE(f.text(), u"- cel ^"_s + id + u"\n- inny\n"_s);
        QCOMPARE(f.page.ensureBlockRef(0), id); // stable
        QCOMPARE(f.page.copyBlockRef(0), id);
        QCOMPARE(QGuiApplication::clipboard()->text(), u"(("_s + id + u"))"_s);
        QCOMPARE(f.page.journalLink(u"2026-10-15"_s), u"[[2026-10-15]]"_s);
    }

    // --- copying links (docs/SYNTAX.md §2 "Copied links") ------------------------------------------

    /// A page with every kind of block: rows 0 frontmatter, 1 "# Wyjazd", 2 a paragraph,
    /// 3 "## Plan #praca", 4 a list item, 5 a task, 6 a Logseq heading item, 7 its child,
    /// 8 code, 9 a table, 10 a block that has an id.
    static QString linkPage()
    {
        return u"---\ntytuł: Wyjazd\n---\n# Wyjazd\n\nAkapit o wyjeździe\nw dwóch liniach.\n\n## Plan #praca\n\n"
               u"- Kupić bilety\n- [ ] Spakować się 📅 2026-10-20\n- ## Lista rzeczy\n\t- szczoteczka\n\n"
               u"```js\nconst a = 1;\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\nGotowy blok ^gotowy\n"_s;
    }

    void copyLinks_data()
    {
        QTest::addColumn<int>("row");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("pattern"); // the copied text; ID: the block's id
        QTest::addColumn<bool>("writesId");
        QTest::newRow("paragraph ref") << 2 << u"blockRef"_s << u"((ID))"_s << true;
        QTest::newRow("paragraph link") << 2 << u"blockLink"_s << u"[[Strona#^ID]]"_s << true;
        QTest::newRow("paragraph embed") << 2 << u"embed"_s << u"!((ID))"_s << true;
        QTest::newRow("heading") << 3 << u"heading"_s << u"[[Strona#Plan praca]]"_s << false;
        QTest::newRow("heading embed") << 3 << u"embed"_s << u"![[Strona#Plan praca]]"_s << false;
        QTest::newRow("heading ref") << 3 << u"blockRef"_s << u"((ID))"_s << true;
        QTest::newRow("heading block link") << 1 << u"blockLink"_s << u"[[Strona#^ID]]"_s << true;
        QTest::newRow("list item") << 4 << u"blockRef"_s << u"((ID))"_s << true;
        QTest::newRow("task") << 5 << u"blockLink"_s << u"[[Strona#^ID]]"_s << true;
        QTest::newRow("logseq heading") << 6 << u"heading"_s << u"[[Strona#Lista rzeczy]]"_s << false;
        QTest::newRow("child") << 7 << u"embed"_s << u"!((ID))"_s << true;
        QTest::newRow("code") << 8 << u"blockLink"_s << u"[[Strona#^ID]]"_s << true;
        QTest::newRow("table") << 9 << u"blockRef"_s << u"((ID))"_s << true;
        QTest::newRow("existing id") << 10 << u"blockLink"_s << u"[[Strona#^gotowy]]"_s << false;
        QTest::newRow("page") << -1 << u"page"_s << u"[[Strona]]"_s << false;
        QTest::newRow("page from a row") << 4 << u"page"_s << u"[[Strona]]"_s << false;
    }

    void copyLinks()
    {
        QFETCH(int, row);
        QFETCH(QString, kind);
        QFETCH(QString, pattern);
        QFETCH(bool, writesId);
        Fixture f(linkPage());
        const QString before = f.text();
        const bool couldUndo = f.page.canUndo();
        QGuiApplication::clipboard()->clear();
        const QVariantMap r = f.page.copyLink(row, kind);
        QVERIFY2(r.value(u"ok"_s).toBool(), qPrintable(r.value(u"reason"_s).toString()));
        const QString id = row >= 0 ? f.model()->document().at(row).blockId : QString();
        const QString expected = QString(pattern).replace(u"ID"_s, id);
        QCOMPARE(r.value(u"text"_s).toString(), expected);
        QCOMPARE(QGuiApplication::clipboard()->text(), expected);
        if (writesId) {
            // Only the id was added — one undo step takes it away again.
            QVERIFY(id.size() == 6);
            QCOMPARE(f.text().count(QString(u"^"_s + id)), 1);
            QCOMPARE(QString(f.text()).remove(QRegularExpression(u"\\n?[ ]?\\^"_s + id)), before);
            QVERIFY(f.page.canUndo());
            f.page.undo();
            QCOMPARE(f.text(), before);
            f.page.redo();
            // A second copy gives the same link and writes nothing.
            const QString withId = f.text();
            QCOMPARE(f.page.copyLink(row, kind).value(u"text"_s).toString(), expected);
            QCOMPARE(f.text(), withId);
        } else {
            QCOMPARE(f.text(), before);
            QCOMPARE(f.page.canUndo(), couldUndo);
        }
        // What was copied reads back as a link to that block / heading.
        const markdown::InlineScan scan = markdown::extractInline(expected);
        QCOMPARE(scan.links.size(), 1);
        const Link &link = scan.links.first();
        if (!link.heading.isEmpty())
            QCOMPARE(f.page.rowOfHeading(link.heading), row);
        else if (!link.blockId.isEmpty())
            QCOMPARE(f.page.rowOfBlockId(link.blockId), row);
        else if (link.kind == LinkKind::BlockRef || link.kind == LinkKind::BlockEmbed)
            QCOMPARE(f.page.rowOfBlockId(link.target), row);
    }

    void copyLinkFailures()
    {
        Fixture f(linkPage());
        const QString before = f.text();
        const auto reason = [&f](int row, const QString &kind) {
            const QVariantMap r = f.page.copyLink(row, kind);
            return r.value(u"ok"_s).toBool() ? QString() : r.value(u"reason"_s).toString();
        };
        QCOMPARE(reason(-1, u"blockRef"_s), u"noRow"_s); // nothing edited or selected
        QCOMPARE(reason(99, u"heading"_s), u"noRow"_s);
        QCOMPARE(reason(2, u"heading"_s), u"notHeading"_s); // a paragraph has no heading link
        QCOMPARE(reason(0, u"blockRef"_s), u"noId"_s);      // the frontmatter cannot carry an id
        QCOMPARE(reason(2, u"nieznany"_s), u"noRow"_s);
        QCOMPARE(f.text(), before);
        // The documentation notebook: no new ids (its pages stay as shipped), other links work.
        f.page.setAllowNewBlockIds(false);
        QCOMPARE(reason(2, u"blockRef"_s), u"readOnly"_s);
        QCOMPARE(reason(4, u"embed"_s), u"readOnly"_s);
        QCOMPARE(reason(3, u"heading"_s), QString());
        QCOMPARE(reason(10, u"blockRef"_s), QString()); // an id the block has
        QCOMPARE(reason(-1, u"page"_s), QString());
        QCOMPARE(f.text(), before);
        QVERIFY(!f.page.canUndo());
        f.page.setAllowNewBlockIds(true);
        // Source mode: the blocks are not being edited.
        f.page.setSourceMode(true);
        QCOMPARE(reason(2, u"blockRef"_s), u"sourceMode"_s);
        QCOMPARE(reason(-1, u"page"_s), QString());
        f.page.setSourceMode(false);
        // No page.
        PageController empty;
        QCOMPARE(empty.copyLink(-1, u"page"_s).value(u"reason"_s).toString(), u"noPage"_s);
        QCOMPARE(empty.linkTarget(), QString());
    }

    void copyLinkTargets()
    {
        // The title, or the path when the title leads to another page; a daily note by its name.
        Fixture f(u"- a\n"_s);
        QCOMPARE(f.page.linkTarget(), u"Strona"_s);
        f.host.setFile(u"Stare/Strona.md"_s, u"- stara\n"_s); // "Strona" now leads there (shorter path)
        QCOMPARE(f.page.linkTarget(), u"Notatki/Strona"_s);
        QCOMPARE(f.page.copyLink(0, u"blockLink"_s).value(u"text"_s).toString(),
                 u"[[Notatki/Strona#^"_s + f.model()->document().at(0).blockId + u"]]"_s);
        QCOMPARE(f.page.copyLink(-1, u"page"_s).value(u"text"_s).toString(), u"[[Notatki/Strona]]"_s);
        // Deeper: the shortest folder path that tells the pages apart (VaultIndex::linkTarget),
        // not the whole path.
        f.host.setFile(u"Archiwum/2025/Notatki/Strona.md"_s, u"- archiwalna\n"_s);
        PageController deep;
        deep.setHost(&f.host);
        deep.setPath(u"Archiwum/2025/Notatki/Strona.md"_s);
        QCOMPARE(deep.linkTarget(), u"2025/Notatki/Strona"_s);
        QCOMPARE(deep.copyLink(-1, u"page"_s).value(u"text"_s).toString(), u"[[2025/Notatki/Strona]]"_s);
        QCOMPARE(f.page.linkTarget(), u"Notatki/Strona"_s); // an exact path: still this page
        // A page not written yet: its title.
        PageController fresh;
        fresh.setHost(&f.host);
        fresh.setPath(u"Pomysły/Żółw.md"_s);
        QCOMPARE(fresh.linkTarget(), u"Żółw"_s);
        // A daily note, also before it exists.
        PageController day;
        day.setHost(&f.host);
        day.setPath(u"journals/2026-10-22.md"_s);
        QCOMPARE(day.linkTarget(), u"2026-10-22"_s);
        const int row = day.startNewPage(QString());
        day.setEditText(row, u"Spotkanie o 9"_s);
        const QString text = day.copyLink(row, u"blockLink"_s).value(u"text"_s).toString();
        QVERIFY2(QRegularExpression(u"^\\[\\[2026-10-22#\\^[a-z0-9]{6}\\]\\]$"_s).match(text).hasMatch(),
                 qPrintable(text));
    }

    void copyDuplicateHeadings()
    {
        Fixture f(u"# Projekt\n## Plan\n### Zadania\n## Budżet\n### Zadania\n# Archiwum\n## Plan\n"
                  u"# A\n## B\n# A\n## B\n"_s);
        const auto copied = [&f](int row, const QString &kind = u"heading"_s) {
            return f.page.copyLink(row, kind).value(u"text"_s).toString();
        };
        QCOMPARE(copied(0), u"[[Strona#Projekt]]"_s);
        QCOMPARE(copied(1), u"[[Strona#Projekt#Plan]]"_s);
        QCOMPARE(copied(2), u"[[Strona#Plan#Zadania]]"_s);
        QCOMPARE(copied(4), u"[[Strona#Budżet#Zadania]]"_s);
        QCOMPARE(copied(6), u"[[Strona#Archiwum#Plan]]"_s);
        QCOMPARE(copied(4, u"embed"_s), u"![[Strona#Budżet#Zadania]]"_s);
        QCOMPARE(f.page.rowOfHeading(u"Budżet#Zadania"_s), 4);
        QCOMPARE(f.page.rowOfHeading(u"budzet#ZADANIA"_s), 4);
        QCOMPARE(f.page.rowOfHeading(u"Archiwum#Plan"_s), 6);
        QCOMPARE(f.page.rowOfHeading(u"Zadania"_s), 2);
        QCOMPARE(f.page.rowOfHeading(u"Nieznany"_s), -1);
        QVERIFY(!f.page.canUndo()); // nothing written so far
        // The same path twice: a block link, the id written into the heading.
        const QString link = copied(10);
        const QString id = f.model()->document().at(10).blockId;
        QCOMPARE(link, u"[[Strona#^"_s + id + u"]]"_s);
        QVERIFY(f.text().endsWith(u"# A\n## B ^"_s + id + u"\n"_s));
        QCOMPARE(copied(10, u"embed"_s), u"![[Strona#^"_s + id + u"]]"_s);
    }

    // `brulion-cli block link` (notebook::blockLinks) gives the links the editor copies, row by row,
    // and both name the page alike (notebook::pageLinkTarget on the real index — the page menu's
    // "Kopiuj link" too): also next to pages of the same name in other folders.
    void copiedLinksMatchTheCommandLine_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QStringList>("others"); // pages of the same name elsewhere
        QTest::addColumn<QString>("target");     // the page part of every link
        const QString headings = u"# Projekt\n## Plan\n### Zadania\n## Budżet\n### Zadania\n# Archiwum\n"
                                 u"## Plan\n# A\n## B\n# A\n## B\n## Zakupy\n- mleko\n## Zakupy\n"_s;
        QTest::newRow("blocks") << linkPage() << Page << QStringList() << u"Strona"_s;
        QTest::newRow("repeated headings") << headings << Page << QStringList() << u"Strona"_s;
        QTest::newRow("same name in another folder")
                << linkPage() << Page << QStringList{u"Stare/Strona.md"_s} << u"Notatki/Strona"_s;
        QTest::newRow("same name, deeper: the shortest path that tells them apart")
                << headings << u"Archiwum/2025/Projekty/Żółw.md"_s << QStringList{u"Żółw.md"_s, u"Projekty/Żółw.md"_s}
                << u"2025/Projekty/Żółw"_s;
        QTest::newRow("the shorter of two pages keeps its title")
                << linkPage() << u"Ogród/Żółw.md"_s << QStringList{u"Zwierzęta/Gady/Żółw.md"_s} << u"Żółw"_s;
        QTest::newRow("a daily note named like a page") << linkPage() << u"journals/2026-10-22.md"_s
                                                        << QStringList{u"Stare/2026-10-22.md"_s} << u"2026-10-22"_s;
    }
    void copiedLinksMatchTheCommandLine()
    {
        QFETCH(QString, content);
        QFETCH(QString, path);
        QFETCH(QStringList, others);
        QFETCH(QString, target);
        Fixture f(content, 0, path, others);
        const QString page = f.page.linkTarget();
        QCOMPARE(page, target);
        // The command line's page part: the same rule on an index of the same files.
        VaultIndex index(vaultConfigFromMap(f.host.vaultConfig()));
        for (const QString &file : f.host.files())
            index.update(file, f.host.file(file));
        QCOMPARE(notebook::pageLinkTarget(path, index.config(), &index), page);
        QCOMPARE(f.page.copyLink(-1, u"page"_s).value(u"text"_s).toString(), u"[["_s + page + u"]]"_s);
        for (int row = 0; row < f.count(); ++row) {
            const auto copied = [&](const QString &kind) {
                const QVariantMap r = f.page.linkText(row, kind);
                return r.value(u"ok"_s).toBool() ? r.value(u"text"_s).toString() : QString();
            };
            const QString ref = copied(u"blockRef"_s); // adds the id first, as `block link` does
            const QString blockLink = copied(u"blockLink"_s);
            const QString embed = copied(u"embed"_s);
            const QString heading = copied(u"heading"_s);
            const auto cli = notebook::blockLinks(f.text(), f.model()->document().at(row).line, page);
            const QString what = u"row %1: %2"_s.arg(row).arg(f.model()->document().at(row).body);
            QVERIFY2(cli.has_value() == !ref.isEmpty(), qPrintable(what));
            if (!cli)
                continue;
            QVERIFY2(!cli->added, qPrintable(what)); // the editor wrote the same id
            QCOMPARE(cli->ref, ref);
            QCOMPARE(cli->wikiLink, blockLink);
            QCOMPARE(cli->embed, embed);
            if (f.page.isHeading(row))
                QCOMPARE(cli->headingLink.isEmpty() ? cli->wikiLink : cli->headingLink, heading);
            else
                QVERIFY2(cli->headingLink.isEmpty(), qPrintable(what));
        }
    }

    void headingSections()
    {
        Fixture f(u"Wstęp\n\n# Zażółć gęślą jaźń\n\nAkapit\n\n- punkt\n\t- głębiej\n## Podsekcja\n\nTekst\n"_s);
        QCOMPARE(f.page.headingRowFor(0), -1); // above the first heading
        QCOMPARE(f.page.headingRowFor(1), 1);
        QCOMPARE(f.page.headingRowFor(3), 1);
        QCOMPARE(f.page.headingRowFor(4), 1);
        QCOMPARE(f.page.headingRowFor(5), 5);
        QCOMPARE(f.page.headingRowFor(6), 5);
        QCOMPARE(f.page.headingRowFor(-1), -1);
        QVERIFY(f.page.isHeading(1));
        QVERIFY(!f.page.isHeading(2));
        QCOMPARE(f.page.rowOfHeading(u"zazolc gesla jazn"_s), 1);
        QCOMPARE(f.page.rowOfHeading(u"Zażółć gęślą jaźń#Podsekcja"_s), 5);
    }

    void sameHostIdsAreAvoided()
    {
        // New ids avoid every id of the vault — also one whose block has no text (blockText()
        // is "" for it, so the host is asked blockIdTaken()).
        struct RefusingHost : FakeEditorHost {
            mutable QStringList asked;
            bool blockIdTaken(const QString &id) const override
            {
                asked.append(id);
                return asked.size() < 3;
            }
        };
        RefusingHost host;
        host.setFile(Page, u"- nowy blok\n"_s);
        PageController page;
        page.setHost(&host);
        page.setPath(Page);
        const QString id = page.ensureBlockRef(0);
        QCOMPARE(host.asked.size(), 3);
        QCOMPARE(id, host.asked.last());
        FakeEditorHost real;
        real.setFile(u"Pusta.md"_s, u"- \n  id:: pusty1\n"_s);
        QVERIFY(real.blockText(u"pusty1"_s).isEmpty());
        QVERIFY(real.blockIdTaken(u"pusty1"_s));
        QVERIFY(!real.blockIdTaken(u"brak"_s));
    }

    void sameAndMovedLinks()
    {
        // Links to this page scroll here every time (the route would not change); a block moved
        // to another page is opened there; a missing target says so.
        Fixture f(u"## Plan na wrzesień\n\n- blok ^abc\n- [[Strona#Plan na wrzesień]] [[#^abc]]\n"_s);
        QSignalSpy nav(&f.host, &FakeEditorHost::navigated);
        const auto activate = [&f](const QString &url) { return f.page.activateLink(2, url); };
        const auto scrolledTo = [](const QVariantMap &r) {
            return r.value(u"action"_s).toString() == u"scrollTo"_s ? r.value(u"row"_s).toInt() : -1;
        };
        for (int twice = 0; twice < 2; ++twice) {
            QCOMPARE(scrolledTo(activate(u"page:Strona#Plan%20na%20wrzesie%C5%84"_s)), 0);
            QCOMPARE(scrolledTo(activate(u"page:Strona#%5Eabc"_s)), 1);
            QCOMPARE(scrolledTo(activate(u"page:#%5Eabc"_s)), 1);
            QCOMPARE(scrolledTo(activate(u"page:Notatki%2FStrona#plan%20na%20wrzesien"_s)), 0);
            QCOMPARE(scrolledTo(activate(u"page:#Plan%20na%20wrzesie%C5%84"_s)), 0);
            QCOMPARE(scrolledTo(activate(u"page:Strona#Inna%20sekcja#Plan%20na%20wrzesie%C5%84"_s)), 0);
            QCOMPARE(scrolledTo(activate(u"block:abc"_s)), 1);
        }
        QCOMPARE(nav.size(), 0);
        // Missing here and anywhere: said, not navigated.
        QVariantMap r = activate(u"page:Strona#Nieznany"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"notFound"_s);
        QCOMPARE(r.value(u"kind"_s).toString(), u"heading"_s);
        QCOMPARE(r.value(u"name"_s).toString(), u"Nieznany"_s);
        r = activate(u"page:Strona#%5Ebrak"_s);
        QCOMPARE(r.value(u"kind"_s).toString(), u"block"_s);
        QCOMPARE(r.value(u"name"_s).toString(), u"brak"_s);
        r = activate(u"block:brak"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"notFound"_s);
        QCOMPARE(r.value(u"kind"_s).toString(), u"ref"_s); // a ((reference)): nowhere in the notebook
        QCOMPARE(nav.size(), 0);
        // The block is on another page now (Inna.md has ^inny1): the shell is asked to open it.
        r = activate(u"page:Strona#%5Einny1"_s);
        QVERIFY(r.isEmpty());
        QCOMPARE(nav.size(), 1);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), Page);
        QCOMPARE(f.host.lastRoute().value(u"blockId"_s).toString(), u"inny1"_s);
        // In a new pane (the side panel) even this page is opened by the shell.
        f.page.activateLink(2, u"page:Strona#Plan%20na%20wrzesie%C5%84"_s, true);
        QCOMPARE(nav.size(), 2);
        QCOMPARE(f.host.lastRoute().value(u"heading"_s).toString(), u"Plan na wrzesień"_s);
        // Another page's heading: a route with the heading.
        f.page.activateLink(2, u"page:Inna#Sekcja"_s);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Inna.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"heading"_s).toString(), u"Sekcja"_s);
        // [[Inna#^inny1]] without an alias reads like the block.
        Fixture other(u"- [[Inna#^inny1]]\n"_s);
        QVERIFY(other.role(0, "html").toString().contains(u"Inna › Blok z innej strony"_s));
    }

    void linksWaitForTheIndex()
    {
        // Right after opening the index is built in the background: a ((id)) or a moved block of a
        // page not indexed yet must not be said to be missing, nor a new id be taken by one.
        struct LateIndexHost : FakeEditorHost {
            int finished = 0;
            void finishIndexing() override { ++finished; }
            bool blockIdTaken(const QString &id) const override
            {
                return finished > 0 && FakeEditorHost::blockIdTaken(id);
            }
        };
        LateIndexHost host;
        host.setFile(Page, u"- ((inny1)) [[Strona#^inny1]]\n- nowy blok\n"_s);
        host.setFile(u"Inna.md"_s, u"- Blok z innej strony ^inny1\n"_s);
        PageController page;
        page.setHost(&host);
        page.setPath(Page);
        QSignalSpy nav(&host, &FakeEditorHost::navigated);
        QVERIFY(page.activateLink(0, u"block:inny1"_s).isEmpty());
        QCOMPARE(host.finished, 1);
        QCOMPARE(nav.size(), 1);
        QCOMPARE(host.lastRoute().value(u"blockId"_s).toString(), u"inny1"_s);
        QVERIFY(page.activateLink(0, u"page:Strona#%5Einny1"_s).isEmpty()); // moved: the shell opens it
        QCOMPARE(nav.size(), 2);
        // Here: no index needed.
        page.ensureBlockRef(1);
        const int before = host.finished;
        const QString id = page.blocks()->document().at(1).blockId;
        QCOMPARE(page.activateLink(0, u"block:"_s + id).value(u"action"_s).toString(), u"scrollTo"_s);
        QCOMPARE(host.finished, before);
        // A new id: the index is complete before the ids are checked.
        LateIndexHost fresh;
        fresh.setFile(Page, u"- blok\n"_s);
        PageController other;
        other.setHost(&fresh);
        other.setPath(Page);
        QVERIFY(!other.ensureBlockRef(0).isEmpty());
        QCOMPARE(fresh.finished, 1);
    }

    void blockLinkLabels()
    {
        // [[Page#^id]] shows the text of that page's block: the same id on another page is another
        // block (Obsidian ids are per page); a block of this page shows its live text; a block
        // moved away the text where the link now leads.
        FakeEditorHost host;
        host.setFile(Page, u"- Moje podsumowanie ^summary\n- [[Inna#^summary]] i [[#^summary]]\n- ((summary))\n"
                           u"- [[Strona#^przen]]\n"_s);
        host.setFile(u"Inna.md"_s, u"- Podsumowanie innej strony ^summary\n"_s);
        host.setFile(u"Archiwum.md"_s, u"- Blok przeniesiony do archiwum ^przen\n"_s);
        PageController page;
        page.setHost(&host);
        page.setPath(Page);
        const auto html = [&page](int row) {
            const QHash<int, QByteArray> roles = page.blocks()->roleNames();
            return page.blocks()->data(page.blocks()->index(row), roles.key("html")).toString();
        };
        QVERIFY2(html(1).contains(u"Inna › Podsumowanie innej strony"_s), qPrintable(html(1)));
        QVERIFY(html(1).contains(u">Moje podsumowanie<"_s));
        QVERIFY(!html(1).contains(u"Inna › Moje"_s));
        QVERIFY2(html(3).contains(u"Strona › Blok przeniesiony do archiwum"_s), qPrintable(html(3)));
        // Editing the block re-renders the links to it (as ((id)) always did).
        page.startEditing(0);
        page.setEditText(0, u"Nowy tekst ^summary"_s);
        QVERIFY2(html(1).contains(u"Nowy tekst"_s) && !html(1).contains(u"Moje podsumowanie"_s), qPrintable(html(1)));
        QVERIFY(html(1).contains(u"Inna › Podsumowanie innej strony"_s));
        QVERIFY(html(2).contains(u"Nowy tekst"_s));
    }

    // --- completions and slash commands ------------------------------------------------------------

    void completions()
    {
        Fixture f(u"- zobacz [[Inn\n"_s);
        f.page.startEditing(0, 12);
        QVariantMap trigger = completionTriggerAt(f.edit(0), 12).toMap();
        QVariantMap r = f.page.applyCompletion(0, trigger, {{u"insertText"_s, u"Inna"_s}, {u"kind"_s, u"page"_s}});
        QCOMPARE(f.text(), u"- zobacz [[Inna]]\n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 15);
        // Existing closing brackets are reused.
        Fixture closed(u"- [[In]] x\n"_s);
        trigger = completionTriggerAt(closed.edit(0), 4).toMap();
        r = closed.page.applyCompletion(0, trigger, {{u"insertText"_s, u"Inna"_s}});
        QCOMPARE(closed.text(), u"- [[Inna]] x\n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 8);
        // Tags with spaces become #[[…]].
        Fixture tag(u"- #zak\n"_s);
        tag.page.applyCompletion(0, completionTriggerAt(tag.edit(0), 4).toMap(), {{u"insertText"_s, u"zakupy domowe"_s}});
        QCOMPARE(tag.text(), u"- #[[zakupy domowe]] \n"_s);
        // So do page names that are no simple tag (a tag names a page: docs/SYNTAX.md §3); a simple
        // one stays simple, Polish letters too; no second space before an existing one.
        const auto completeTag = [](const QString &name) {
            Fixture t(u"- #c x\n"_s);
            t.page.applyCompletion(0, completionTriggerAt(t.edit(0), 2).toMap(),
                                   {{u"insertText"_s, name}, {u"kind"_s, u"tag"_s}});
            return t.text();
        };
        QCOMPARE(completeTag(u"C++"_s), u"- #[[C++]] x\n"_s);
        QCOMPARE(completeTag(u"2026"_s), u"- #[[2026]] x\n"_s);
        QCOMPARE(completeTag(u"Dr. Nowak"_s), u"- #[[Dr. Nowak]] x\n"_s);
        QCOMPARE(completeTag(u"Jan Kowalski"_s), u"- #[[Jan Kowalski]] x\n"_s);
        QCOMPARE(completeTag(u"Żółw"_s), u"- #Żółw x\n"_s);
        QCOMPARE(completeTag(u"Projekty/Dom"_s), u"- #Projekty/Dom x\n"_s);
        // Block refs: a block of this page gets an id; another page's through the host.
        Fixture refs(u"- cel\n- ((ce\n"_s);
        refs.page.startEditing(1, 4);
        refs.page.applyCompletion(1, completionTriggerAt(refs.edit(1), 4).toMap(),
                                  {{u"path"_s, Page}, {u"line"_s, 0}, {u"blockId"_s, QString()}});
        const QString id = refs.model()->document().at(0).blockId;
        QCOMPARE(id.size(), 6);
        QCOMPARE(refs.text(), u"- cel ^"_s + id + u"\n- (("_s + id + u"))\n"_s);
        refs.page.setEditText(1, u"(("_s, 2);
        refs.page.applyCompletion(1, completionTriggerAt(u"(("_s, 2).toMap(),
                                  {{u"path"_s, u"Inna.md"_s}, {u"line"_s, 1}, {u"blockId"_s, QString()}});
        const QString other = refs.host.file(u"Inna.md"_s);
        QVERIFY(other.contains(u"- Drugi blok ^"_s));
        QVERIFY(refs.edit(1).startsWith(u"(("_s) && refs.edit(1).endsWith(u"))"_s));
        // Dates.
        Fixture date(u"- Zadzwonić ⏰ jutro o 9\n"_s);
        date.page.applyCompletion(0, completionTriggerAt(date.edit(0), int(date.edit(0).size())).toMap(),
                                  {{u"insertText"_s, u"2026-10-16 09:00"_s}, {u"kind"_s, u"date"_s}});
        QCOMPARE(date.text(), u"- Zadzwonić ⏰ 2026-10-16 09:00 \n"_s);
        Fixture at(u"- spotkanie @jutro\n"_s);
        at.page.applyCompletion(0, completionTriggerAt(at.edit(0), int(at.edit(0).size())).toMap(),
                                {{u"insertText"_s, u"[[2026-10-16]]"_s}, {u"kind"_s, u"journal"_s}});
        QCOMPARE(at.text(), u"- spotkanie [[2026-10-16]] \n"_s);
    }

    // A reminder added from the "/" menu leaves the caret after a space: "/" typed next opens the
    // menu again and its command keeps the token whole (TODO: „/” zaraz po tokenie przypomnienia).
    void slashAfterReminder()
    {
        Fixture f(u"- /przyp\n"_s);
        f.page.startEditing(0, 6);
        QVariantMap r = f.page.applySlashCommand(0, 0, 6, u"reminder"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"reminder"_s);
        QCOMPARE(f.page.cursorPosition(), 0);
        f.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(f.text(), u"- ⏰ 2026-10-16 18:00 \n"_s);
        QCOMPARE(f.page.cursorPosition(), int(f.edit(0).size()));
        QString typed = f.edit(0) + u"/jut"_s;
        f.page.setEditText(0, typed, int(typed.size()));
        const CompletionTrigger t = completionTriggerAt(typed, int(typed.size()));
        QCOMPARE(t.mode, u"slash"_s);
        QCOMPARE(t.filter, u"jut"_s);
        f.page.applySlashCommand(0, t.from, t.to, u"tomorrow"_s);
        QCOMPARE(f.text(), u"- ⏰ 2026-10-16 18:00 [[2026-10-16]]\n"_s);
        const QString body = f.model()->document().at(0).body;
        const int from = int(body.indexOf(u"⏰"_s));
        QCOMPARE(f.page.reminderAt(0, from, from + int(u"⏰ 2026-10-16 18:00"_s.size())).value(u"at"_s).toString(),
                 u"2026-10-16 18:00"_s);

        // An alarm on a task; then another reminder from "/" right after it.
        Fixture task(u"- [ ] Wstać /alarm\n"_s);
        task.page.startEditing(0, 12);
        task.page.applySlashCommand(0, 6, 12, u"alarm"_s);
        task.page.setReminder(0, u"2026-10-16 06:30"_s, true, QString());
        QCOMPARE(task.text(), u"- [ ] Wstać ⏰! 2026-10-16 06:30 \n"_s);
        typed = task.edit(0) + u"/"_s;
        task.page.setEditText(0, typed, int(typed.size()));
        QCOMPARE(completionTriggerAt(typed, int(typed.size())).mode, u"slash"_s);
        task.page.applySlashCommand(0, int(typed.size()) - 1, int(typed.size()), u"reminder"_s);
        task.page.setReminder(0, u"2026-10-16 06:40"_s, false, QString());
        QCOMPARE(task.text(), u"- [ ] Wstać ⏰! 2026-10-16 06:30 ⏰ 2026-10-16 06:40 \n"_s);

        // A space already there is not doubled.
        Fixture spaced(u"- Zadzwonić /przyp\n"_s);
        spaced.page.startEditing(0, 16);
        spaced.page.applySlashCommand(0, 10, 16, u"reminder"_s);
        spaced.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(spaced.text(), u"- Zadzwonić ⏰ 2026-10-16 18:00 \n"_s);
        QCOMPARE(spaced.page.cursorPosition(), int(spaced.edit(0).size()));

        // The caret elsewhere, or no block being edited: no space is added.
        Fixture middle(u"- Zadzwonić\n"_s);
        middle.page.startEditing(0, 4);
        middle.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(middle.text(), u"- Zadzwonić ⏰ 2026-10-16 18:00\n"_s);
        QCOMPARE(middle.page.cursorPosition(), 4);
        Fixture idle(u"- Zadzwonić\n"_s);
        idle.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(idle.text(), u"- Zadzwonić ⏰ 2026-10-16 18:00\n"_s);
        // A second line: the caret at its end is not next to the token on the first line.
        Fixture lines(u"- Zadzwonić\n  do mamy\n"_s);
        lines.page.startEditing(0, int(lines.edit(0).size()));
        lines.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(lines.text(), u"- Zadzwonić ⏰ 2026-10-16 18:00\n  do mamy\n"_s);
    }

    // The space the editor writes after a token at the end of the edited block (so the caret does
    // not stick to it) is not saved while nothing follows it, and goes when the caret leaves.
    void editorSpaceNotSaved()
    {
        Fixture f(u"- Zadzwonić /przyp\n- Drugi\n"_s);
        f.page.startEditing(0, 16);
        f.page.applySlashCommand(0, 10, 16, u"reminder"_s);
        f.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-16 18:00 \n- Drugi\n"_s);
        QCOMPARE(f.page.cursorPosition(), int(f.edit(0).size()));
        QCOMPARE(f.host.file(Page), u"- Zadzwonić ⏰ 2026-10-16 18:00\n- Drugi\n"_s);
        QVERIFY(!f.page.dirty());
        f.page.startEditing(1);
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-16 18:00\n- Drugi\n"_s);
        QCOMPARE(f.host.file(Page), f.text());
        QVERIFY(!f.page.dirty());
        // No undo step of its own: undo takes the reminder away, redo brings it back without the space.
        f.page.undo();
        QCOMPARE(f.text(), u"- Zadzwonić \n- Drugi\n"_s);
        f.page.redo();
        QCOMPARE(f.text(), u"- Zadzwonić ⏰ 2026-10-16 18:00\n- Drugi\n"_s);
        QCOMPARE(f.host.file(Page), f.text());

        // Typed after it, the space stays; typed and deleted again, it goes as the caret leaves.
        Fixture typed(u"- /przyp\n"_s);
        typed.page.startEditing(0, 6);
        typed.page.applySlashCommand(0, 0, 6, u"reminder"_s);
        typed.page.setReminder(0, u"2026-10-16 18:00"_s, true, u"every day"_s);
        const QString written = typed.edit(0);
        QCOMPARE(typed.text(), u"- ⏰! 2026-10-16 18:00 🔁 every day \n"_s);
        QCOMPARE(typed.host.file(Page), u"- ⏰! 2026-10-16 18:00 🔁 every day\n"_s);
        typed.page.setEditText(0, written + u"żółw"_s, int(written.size()) + 4);
        QCOMPARE(typed.host.file(Page), u"- ⏰! 2026-10-16 18:00 🔁 every day żółw\n"_s);
        typed.page.setEditText(0, written, int(written.size()));
        typed.page.stopEditing();
        QCOMPARE(typed.text(), u"- ⏰! 2026-10-16 18:00 🔁 every day\n"_s);
        QCOMPARE(typed.host.file(Page), typed.text());
        Fixture kept(u"- /przyp\n"_s);
        kept.page.startEditing(0, 6);
        kept.page.applySlashCommand(0, 0, 6, u"reminder"_s);
        kept.page.setReminder(0, u"2026-10-16 18:00"_s, false, QString());
        QString text = kept.edit(0) + u"zadzwonić"_s;
        kept.page.setEditText(0, text, int(text.size()));
        kept.page.stopEditing();
        QCOMPARE(kept.host.file(Page), u"- ⏰ 2026-10-16 18:00 zadzwonić\n"_s);

        // A completed date and a completed tag, the same.
        Fixture date(u"- Zadzwonić ⏰ jutro o 9\n- Drugi\n"_s);
        date.page.startEditing(0, int(date.edit(0).size()));
        date.page.applyCompletion(0, completionTriggerAt(date.edit(0), int(date.edit(0).size())).toMap(),
                                  {{u"insertText"_s, u"2026-10-16 09:00"_s}, {u"kind"_s, u"date"_s}});
        QCOMPARE(date.text(), u"- Zadzwonić ⏰ 2026-10-16 09:00 \n- Drugi\n"_s);
        QCOMPARE(date.host.file(Page), u"- Zadzwonić ⏰ 2026-10-16 09:00\n- Drugi\n"_s);
        date.page.startEditing(1);
        QCOMPARE(date.text(), u"- Zadzwonić ⏰ 2026-10-16 09:00\n- Drugi\n"_s);
        Fixture tag(u"- Kupić #zak\n"_s);
        tag.page.startEditing(0, int(tag.edit(0).size()));
        tag.page.applyCompletion(0, completionTriggerAt(tag.edit(0), int(tag.edit(0).size())).toMap(),
                                 {{u"insertText"_s, u"zakupy domowe"_s}, {u"kind"_s, u"tag"_s}});
        QCOMPARE(tag.text(), u"- Kupić #[[zakupy domowe]] \n"_s);
        QCOMPARE(tag.host.file(Page), u"- Kupić #[[zakupy domowe]]\n"_s);
        tag.page.stopEditing();
        QCOMPARE(tag.text(), u"- Kupić #[[zakupy domowe]]\n"_s);

        // A space the user typed is the user's: saved and kept.
        Fixture own(u"- Zadzwonić\n"_s);
        own.page.startEditing(0, 9);
        own.page.setEditText(0, u"Zadzwonić "_s, 10);
        own.page.stopEditing();
        QCOMPARE(own.host.file(Page), u"- Zadzwonić \n"_s);
    }

    void slashCommands()
    {
        Fixture f(u"- Zrobić /zad\n"_s);
        f.page.startEditing(0, 11);
        QVariantMap r = f.page.applySlashCommand(0, 7, 11, u"task"_s);
        QCOMPARE(f.text(), u"- [ ] Zrobić \n"_s);
        QCOMPARE(r.value(u"action"_s).toString(), QString());
        f.page.undo();
        QCOMPARE(f.text(), u"- Zrobić /zad\n"_s); // one undo step
        // Headings.
        Fixture h(u"- Sekcja /h2\n"_s);
        h.page.applySlashCommand(0, 7, 10, u"h2"_s);
        QCOMPARE(h.text(), u"- ## Sekcja \n"_s);
        QCOMPARE(h.role(0, "view").toString(), u"heading"_s);
        Fixture hp(u"Akapit /h1\n"_s);
        hp.page.applySlashCommand(0, 7, 10, u"h1"_s);
        QCOMPARE(hp.text(), u"# Akapit \n"_s);
        // Blocks from templates replace an empty block.
        Fixture calc(u"- /obl\n"_s);
        calc.page.startEditing(0, 4);
        r = calc.page.applySlashCommand(0, 0, 4, u"calc"_s);
        QCOMPARE(calc.text(), u"- ```calc\n\n  ```\n"_s);
        QCOMPARE(calc.role(0, "view").toString(), u"calc"_s);
        QCOMPARE(calc.page.editingRow(), 0);
        QCOMPARE(calc.page.cursorPosition(), 0);
        Fixture table(u"Tekst /tab\n"_s);
        r = table.page.applySlashCommand(0, 6, 10, u"table"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"table"_s);
        QCOMPARE(table.count(), 2);
        QCOMPARE(table.role(1, "view").toString(), u"table"_s);
        QCOMPARE(table.edit(0), u"Tekst "_s);
        // Dialog commands just remove the trigger and report the action.
        Fixture rem(u"- Zadzwonić /przyp\n"_s);
        r = rem.page.applySlashCommand(0, 10, 16, u"reminder"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"reminder"_s);
        QCOMPARE(rem.text(), u"- Zadzwonić \n"_s);
        // Journal links and embeds.
        Fixture today(u"- spotkanie /dzi\n"_s);
        today.page.applySlashCommand(0, 10, 14, u"tomorrow"_s);
        QCOMPARE(today.text(), u"- spotkanie [[2026-10-16]]\n"_s);
        Fixture embed(u"- /osa\n"_s);
        r = embed.page.applySlashCommand(0, 0, 4, u"embed"_s);
        QCOMPARE(embed.text(), u"- ![[\n"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"complete"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 3);
        // Divider replaces the empty block with a top-level rule.
        Fixture rule(u"- a\n- /lin\n"_s);
        rule.page.applySlashCommand(1, 0, 4, u"rule"_s);
        QCOMPARE(rule.text(), u"- a\n---\n"_s);
        QCOMPARE(rule.role(1, "view").toString(), u"rule"_s);
        rule.page.undo();
        QCOMPARE(rule.text(), u"- a\n- /lin\n"_s);
    }

    void taskStatusSlashCommands_data()
    {
        // The block, the row being edited, the typed "/command" (it is found in the row's edit
        // text), the command; the page and the caret (edit text position) after it.
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("row");
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("cursor");
        QTest::newRow("list item becomes a task")
                << u"- Kupić mleko /zad\n"_s << 0 << u"/zad"_s << u"task"_s << u"- [ ] Kupić mleko \n"_s << 12;
        QTest::newRow("paragraph becomes a done task") << u"Kupić mleko /zrob\n"_s << 0 << u"/zrob"_s << u"taskDone"_s
                                                       << u"- [x] Kupić mleko ✅ 2026-10-15\n"_s << 12;
        QTest::newRow("command between words")
                << u"- Kupić /toku mleko\n"_s << 0 << u"/toku"_s << u"taskDoing"_s << u"- [/] Kupić  mleko\n"_s << 6;
        QTest::newRow("done task back to do")
                << u"- [x] Zapłacić ✅ 2026-10-14 /do\n"_s << 0 << u"/do"_s << u"task"_s << u"- [ ] Zapłacić \n"_s << 9;
        QTest::newRow("waiting") << u"- [ ] Odpisać Żanecie /czeka\n"_s << 0 << u"/czeka"_s << u"taskWaiting"_s
                                 << u"- [>] Odpisać Żanecie \n"_s << 16;
        QTest::newRow("cancelled") << u"- [/] Wizyta /anul u dentysty\n"_s << 0 << u"/anul"_s << u"taskCancelled"_s
                                   << u"- [-] Wizyta  u dentysty ❌ 2026-10-15\n"_s << 7;
        QTest::newRow("no status on a task")
                << u"- [/] Pisać raport /bez\n"_s << 0 << u"/bez"_s << u"taskNone"_s << u"- Pisać raport \n"_s << 13;
        QTest::newRow("no status on a done task")
                << u"- [x] Pranie ✅ 2026-10-01 /bez\n"_s << 0 << u"/bez"_s << u"taskNone"_s << u"- Pranie \n"_s << 7;
        QTest::newRow("no status on a plain item")
                << u"- Pisać /bez\n"_s << 0 << u"/bez"_s << u"taskNone"_s << u"- Pisać \n"_s << 6;
        QTest::newRow("no status on a paragraph")
                << u"Akapit /bez\n"_s << 0 << u"/bez"_s << u"taskNone"_s << u"Akapit \n"_s << 7;
        QTest::newRow("same status") << u"- [/] W toku /toku\n"_s << 0 << u"/toku"_s << u"taskDoing"_s
                                     << u"- [/] W toku \n"_s << 7;
        QTest::newRow("nested item") << u"- rodzic\n\t- dziecko /zad\n"_s << 1 << u"/zad"_s << u"task"_s
                                     << u"- rodzic\n\t- [ ] dziecko \n"_s << 8;
        QTest::newRow("numbered item") << u"1. pierwszy /zrob\n"_s << 0 << u"/zrob"_s << u"taskDone"_s
                                       << u"1. [x] pierwszy ✅ 2026-10-15\n"_s << 9;
        QTest::newRow("first line of a multi-line item")
                << u"- pierwsza /zrob\n  druga\n"_s << 0 << u"/zrob"_s << u"taskDone"_s
                << u"- [x] pierwsza ✅ 2026-10-15\n  druga\n"_s << 9;
        QTest::newRow("second line of a multi-line item") << u"- pierwsza\n  druga /zad\n"_s << 0 << u"/zad"_s
                                                          << u"task"_s << u"- [ ] pierwsza\n  druga \n"_s << 15;
        QTest::newRow("Logseq todo to doing") << u"- TODO Zadzwonić /toku\n"_s << 0 << u"/toku"_s << u"taskDoing"_s
                                              << u"- DOING Zadzwonić \n"_s << 10;
        QTest::newRow("Logseq later to now")
                << u"- LATER Zadzwonić /toku\n"_s << 0 << u"/toku"_s << u"taskDoing"_s << u"- NOW Zadzwonić \n"_s << 10;
        QTest::newRow("Logseq waiting") << u"- TODO Paczka /czeka\n"_s << 0 << u"/czeka"_s << u"taskWaiting"_s
                                        << u"- WAITING Paczka \n"_s << 7;
        QTest::newRow("Logseq no status")
                << u"- DONE Paczka ✅ 2026-10-01 /bez\n"_s << 0 << u"/bez"_s << u"taskNone"_s << u"- Paczka \n"_s << 7;
        QTest::newRow("crlf kept") << u"- a\r\n- b /zad\r\n- c\r\n"_s << 1 << u"/zad"_s << u"task"_s
                                   << u"- a\r\n- [ ] b \r\n- c\r\n"_s << 2;
        QTest::newRow("empty block") << u"- /zad\n"_s << 0 << u"/zad"_s << u"task"_s << u"- [ ] \n"_s << 0;
    }

    void taskStatusSlashCommands()
    {
        QFETCH(QString, content);
        QFETCH(int, row);
        QFETCH(QString, typed);
        QFETCH(QString, command);
        QFETCH(QString, expected);
        QFETCH(int, cursor);
        Fixture f(content);
        const int from = int(f.edit(row).indexOf(typed));
        QVERIFY(from >= 0);
        const int to = from + int(typed.size());
        f.page.startEditing(row, to);
        const QVariantMap r = f.page.applySlashCommand(row, from, to, command);
        QCOMPARE(f.text(), expected);
        QCOMPARE(r.value(u"action"_s).toString(), QString());
        // Still editing that block, the caret where the command was (in edit text coordinates).
        QCOMPARE(r.value(u"row"_s).toInt(), row);
        QCOMPARE(f.page.editingRow(), row);
        QCOMPARE(f.page.cursorPosition(), cursor);
        QCOMPARE(r.value(u"cursor"_s).toInt(), cursor);
        // One undo step, the command's text back.
        f.page.undo();
        QCOMPARE(f.text(), content);
        f.page.redo();
        QCOMPARE(f.text(), expected);
    }

    void taskStatusSlashCommandNames()
    {
        QCOMPARE(PageController::slashTaskStatus(u"task"_s), std::optional<QString>(u"todo"_s));
        QCOMPARE(PageController::slashTaskStatus(u"taskDoing"_s), std::optional<QString>(u"doing"_s));
        QCOMPARE(PageController::slashTaskStatus(u"taskDone"_s), std::optional<QString>(u"done"_s));
        QCOMPARE(PageController::slashTaskStatus(u"taskWaiting"_s), std::optional<QString>(u"deferred"_s));
        QCOMPARE(PageController::slashTaskStatus(u"taskCancelled"_s), std::optional<QString>(u"cancelled"_s));
        QCOMPARE(PageController::slashTaskStatus(u"taskNone"_s), std::optional<QString>(QString()));
        QVERIFY(!PageController::slashTaskStatus(u"reminder"_s));
        QVERIFY(!PageController::slashTaskStatus(QString()));
        // Each of them is a command of the "/" menu.
        for (const QString &id :
             {u"task"_s, u"taskDoing"_s, u"taskDone"_s, u"taskWaiting"_s, u"taskCancelled"_s, u"taskNone"_s})
            QVERIFY2(slashCommand(id), qPrintable(id));
    }

    void taskStatusKeepsTheCaret()
    {
        // The status menu / Ctrl+Enter while editing: the caret stays in its words when a date
        // before it goes or one after it comes.
        Fixture f(u"- [x] Kupić ✅ 2026-10-01 mleko\n"_s);
        f.page.startEditing(0, 20); // after "mleko"'s "m"
        QCOMPARE(f.edit(0).mid(20), u"leko"_s);
        f.page.setTaskStatus(0, u"doing"_s);
        QCOMPARE(f.text(), u"- [/] Kupić mleko\n"_s);
        QCOMPARE(f.edit(0).mid(f.page.cursorPosition()), u"leko"_s);
        f.page.setTaskStatus(0, u"done"_s);
        QCOMPARE(f.text(), u"- [x] Kupić mleko ✅ 2026-10-15\n"_s);
        QCOMPARE(f.edit(0).mid(f.page.cursorPosition()), u"leko ✅ 2026-10-15"_s);
        f.page.setTaskStatus(0, QString());
        QCOMPARE(f.text(), u"- Kupić mleko\n"_s);
        QCOMPARE(f.edit(0).mid(f.page.cursorPosition()), u"leko"_s);
        QCOMPARE(f.page.editingRow(), 0);
        f.page.setTaskStatus(0, u"bogus"_s); // nothing happens
        QCOMPARE(f.text(), u"- Kupić mleko\n"_s);
    }

    void taskStatusOnRecurringTask()
    {
        // Done from a "/" command: the next occurrence goes above, in the same undo step as the
        // status and the command's removal; editing stays on the completed task.
        const QString recurring = u"- [ ] Podlać kwiaty 📅 2026-10-15 🔁 every week /zrob\n"_s;
        const QString completed = u"- [ ] Podlać kwiaty 📅 2026-10-22 🔁 every week\n"
                                  u"- [x] Podlać kwiaty 📅 2026-10-15 🔁 every week ✅ 2026-10-15\n"_s;
        Fixture f(recurring);
        const int from = int(f.edit(0).indexOf(u"/zrob"_s));
        f.page.startEditing(0, from + 5);
        const QVariantMap r = f.page.applySlashCommand(0, from, from + 5, u"taskDone"_s);
        QCOMPARE(f.text(), completed);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(f.page.editingRow(), 1);
        QCOMPARE(f.edit(1).mid(f.page.cursorPosition()), u"✅ 2026-10-15"_s);
        f.page.undo();
        QCOMPARE(f.text(), recurring);
        f.page.redo();
        QCOMPARE(f.text(), completed);

        // The status menu: the same, one undo step; cancelled and no status make no new occurrence.
        const QString open = u"- [/] Podlać kwiaty 📅 2026-10-15 🔁 every week\n"_s;
        Fixture m(open);
        m.page.setTaskStatus(0, u"done"_s);
        QCOMPARE(m.text(), completed);
        m.page.undo();
        QCOMPARE(m.text(), open);
        m.page.setTaskStatus(0, u"cancelled"_s);
        QCOMPARE(m.text(), u"- [-] Podlać kwiaty 📅 2026-10-15 🔁 every week ❌ 2026-10-15\n"_s);
        m.page.setTaskStatus(0, QString());
        QCOMPARE(m.text(), u"- Podlać kwiaty 📅 2026-10-15 🔁 every week\n"_s);
        m.page.undo();
        QCOMPARE(m.text(), u"- [-] Podlać kwiaty 📅 2026-10-15 🔁 every week ❌ 2026-10-15\n"_s);
        // In another file (Tasks, the calendar, query results): the same text.
        m.host.setNow(Now);
        m.host.setFile(u"Inna.md"_s, open);
        QVERIFY(m.page.setTaskStatusAt(u"Inna.md"_s, 0, u"done"_s));
        QCOMPARE(m.host.file(u"Inna.md"_s), completed);
    }

    void pasting()
    {
        // A list pasted into an empty block becomes blocks, nesting kept.
        Fixture f(u"- a\n- \n- z\n"_s);
        f.page.startEditing(1, 0);
        QVariantMap r = f.page.pasteText(1, 0, 0, u"- [ ] mleko\n- chleb\n\t- żytni\n"_s);
        QCOMPARE(f.text(), u"- a\n- [ ] mleko\n- chleb\n\t- żytni\n- z\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 3);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 5);
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n- \n- z\n"_s);
        // Text before the caret joins the first pasted block, text after it ends the last one.
        Fixture mid(u"- początek|koniec\n"_s);
        r = mid.page.pasteText(0, 8, 9, u"- jeden\n- dwa"_s);
        QCOMPARE(mid.text(), u"- początekjeden\n- dwakoniec\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 3);
        // Paragraphs separated by blank lines become blocks too; one paragraph stays one block.
        Fixture para(u"- x\n"_s);
        para.page.pasteText(0, 1, 1, u" pierwszy\ndruga linia"_s);
        QCOMPARE(para.text(), u"- x pierwszy\n  druga linia\n"_s);
        Fixture two(u"- \n"_s);
        two.page.pasteText(0, 0, 0, u"Akapit pierwszy\n\nAkapit drugi"_s);
        QCOMPARE(two.count(), 2);
        QCOMPARE(two.edit(1), u"Akapit drugi"_s);
        // Code blocks keep pasted text as it is.
        Fixture code(u"```\nx\n```\n"_s);
        code.page.pasteText(0, 1, 1, u"\n- a\n- b"_s);
        QCOMPARE(code.text(), u"```\nx\n- a\n- b\n```\n"_s);
        // Pasting a fenced example into a code block widens the hidden fence instead of ending
        // the block at the pasted ``` line (and keeps the next block out of it).
        Fixture nested(u"```\nold\n```\n\nPo\n"_s);
        nested.page.startEditing(0, 3);
        nested.page.pasteText(0, 3, 3, u"\n```\nnew\n```"_s);
        QCOMPARE(nested.text(), u"````\nold\n```\nnew\n```\n````\n\nPo\n"_s);
        QCOMPARE(nested.count(), 2);
        QCOMPARE(nested.edit(0), u"old\n```\nnew\n```"_s);
    }

    void pastingCodeBlocksMidText()
    {
        // A pasted code block (or table, quote…) is never glued to the text around the caret:
        // "a```js" / "```bc" would no longer be a fence.
        Fixture f(u"- abc\n- z\n"_s);
        f.page.startEditing(0, 1);
        const QVariantMap r = f.page.pasteText(0, 1, 1, u"```js\nkod ąę\n```\npo"_s);
        QCOMPARE(f.edit(0), u"a"_s);
        QCOMPARE(f.role(1, "view").toString(), u"code"_s);
        QCOMPARE(f.edit(1), u"kod ąę"_s);
        QCOMPARE(f.edit(2), u"pobc"_s);
        QCOMPARE(f.edit(3), u"z"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 2);
        f.page.undo();
        QCOMPARE(f.text(), u"- abc\n- z\n"_s);
        // Ending with a code block: the text after the caret becomes a block of its own.
        f.page.pasteText(0, 1, 1, u"x\n\n| A | B |\n| --- | --- |\n| 1 | 2 |"_s);
        QCOMPARE(f.edit(0), u"ax"_s);
        QCOMPARE(f.role(1, "view").toString(), u"table"_s);
        QCOMPARE(f.edit(2), u"bc"_s);
        f.page.undo();
        // Only a code block, pasted mid-text: it still becomes a block between the two halves.
        f.page.pasteText(0, 1, 1, u"```\nkod\n```"_s);
        QCOMPARE(f.edit(0), u"a"_s);
        QCOMPARE(f.role(1, "view").toString(), u"code"_s);
        QCOMPARE(f.edit(1), u"kod"_s);
        QCOMPARE(f.edit(2), u"bc"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- abc\n- z\n"_s);
        // Into an empty block: the code block takes its place.
        Fixture e(u"- \n"_s);
        e.page.pasteText(0, 0, 0, u"```\nkod\n```\n\ntekst"_s);
        QCOMPARE(e.role(0, "view").toString(), u"code"_s);
        QCOMPARE(e.edit(1), u"tekst"_s);
        QCOMPARE(e.count(), 2);
    }

    void editingCodeBlocksWithFenceLinesInside()
    {
        // Two code blocks one after another, edited from the block view: the fence lines are
        // hidden, so ``` typed as content must not close either block (a reload would split it
        // into extra blocks and swallow the text after it).
        Fixture f(u"```\nfirst\n```\n\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(f.count(), 3);
        QCOMPARE(f.role(0, "view").toString(), u"code"_s);
        QCOMPARE(f.role(1, "view").toString(), u"code"_s);
        f.page.setEditText(0, u"first\n```\nstill code"_s);
        QCOMPARE(f.text(), u"````\nfirst\n```\nstill code\n````\n\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(f.count(), 3);
        // The widened fences are part of the edit and undo as one step.
        f.page.undo();
        QCOMPARE(f.text(), u"```\nfirst\n```\n\n```\nsecond\n```\n\nPo\n"_s);
        f.page.redo();
        QCOMPARE(f.text(), u"````\nfirst\n```\nstill code\n````\n\n```\nsecond\n```\n\nPo\n"_s);
        f.page.setEditText(1, u"second\n```\nstill code too"_s);
        QCOMPARE(f.text(), u"````\nfirst\n```\nstill code\n````\n\n````\nsecond\n```\nstill code too\n````\n\nPo\n"_s);
        QCOMPARE(f.count(), 3);
        // A fresh parse (reopening the page) sees exactly the same blocks.
        const markdown::BlockDocument reloaded(f.text());
        QCOMPARE(reloaded.count(), 3);
        QCOMPARE(reloaded.at(0).kind, BlockKind::Code);
        QCOMPARE(reloaded.at(1).kind, BlockKind::Code);
        QCOMPARE(reloaded.at(2).kind, BlockKind::Paragraph);
        QCOMPARE(reloaded.at(2).body, u"Po"_s);
        QCOMPARE(f.edit(0), u"first\n```\nstill code"_s);
        QCOMPARE(f.edit(1), u"second\n```\nstill code too"_s);
        // Fences are enough of a separator themselves: no blank line between two code blocks is
        // not a reason to merge them or insert one when one of them is edited.
        Fixture adjacent(u"```\nfirst\n```\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(adjacent.count(), 3);
        QCOMPARE(adjacent.role(0, "view").toString(), u"code"_s);
        QCOMPARE(adjacent.role(1, "view").toString(), u"code"_s);
        adjacent.page.setEditText(0, u"first\n```\nstill code"_s);
        QCOMPARE(adjacent.text(), u"````\nfirst\n```\nstill code\n````\n```\nsecond\n```\n\nPo\n"_s);
        QCOMPARE(adjacent.count(), 3);
    }

    // One keystroke in the code of a code block followed by its block id, properties or more
    // text of its list item changes that character in the file and nothing else: the tasks,
    // ids, links and reminders after the code stay in the index, and a reload sees the blocks
    // the editor has.
    void typingInCodeKeepsWhatFollowsIt_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("row");
        QTest::addColumn<QString>("editText");
        QTest::newRow("code with id, task after")
                << u"```js\nlet a = 1;\n```\n^kod123\n\n- [ ] Zadzwonić do mamy #praca ⏰ 2026-10-22 09:00\n- Spotkanie [[Inna]]\n"_s
                << 0 << u"let a = 1;"_s;
        QTest::newRow("id after a blank line")
                << u"```js\nlet a = 1;\n```\n\n^kod123\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s << 0
                << u"let a = 1;"_s;
        QTest::newRow("logseq id:: and a child task")
                << u"- ```js\n  let a = 1;\n  ```\n  id:: 650f1c2a-0000-4000-8000-000000000001\n  - [ ] dziecko #ważne ⏰ 2026-10-22 09:00\n- dalej [[Inna]]\n"_s
                << 0 << u"let a = 1;"_s;
        QTest::newRow("item with id and a child")
                << u"- ```js\n  let a = 1;\n  ```\n  ^kod123\n  - [ ] dziecko #ważne\n- dalej\n"_s << 0
                << u"let a = 1;"_s;
        QTest::newRow("item text after code")
                << u"- ```js\n  let a = 1;\n  ```\n  opis [[Inna]] #tag\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- dalej\n"_s
                << 0 << u"let a = 1;\n```\nopis [[Inna]] #tag"_s;
        QTest::newRow("item with two fences")
                << u"- ```js\n  let a = 1;\n  ```\n  #tag\n  ```py\n  b = 2\n  ```\n  - [ ] dziecko\n- dalej\n"_s << 0
                << u"let a = 1;\n```\n#tag\n```py\nb = 2\n```"_s;
        QTest::newRow("crlf")
                << u"```js\r\nlet a = 1;\r\n```\r\n^kod123\r\n\r\n- [ ] zadanie #praca\r\n- [[Inna]]\r\n"_s << 0
                << u"let a = 1;"_s;
        QTest::newRow("tabs")
                << u"- rodzic\n\t- ```js\n\t  let a = 1;\n\t  ```\n\t  ^kod123\n\t\t- [ ] wnuk #praca\n\t- dalej [[Inna]]\n"_s
                << 1 << u"let a = 1;"_s;
        QTest::newRow("tildes with info")
                << u"~~~python {.numberLines}\nlet a = 1;\n~~~\n^kod123\n\n- [ ] zadanie\n"_s << 0 << u"let a = 1;"_s;
    }

    void typingInCodeKeepsWhatFollowsIt()
    {
        QFETCH(QString, content);
        QFETCH(int, row);
        QFETCH(QString, editText);
        Fixture f(content);
        const QString indexed = indexedOf(content);
        QVERIFY(!markdown::parseNote(Page, content).tasks.isEmpty());
        f.page.startEditing(row, 9);
        f.page.setEditText(row, f.edit(row).replace(u"let a = 1;"_s, u"let a = 2;"_s), 9);
        QCOMPARE(f.text(), QString(content).replace(u"let a = 1;"_s, u"let a = 2;"_s));
        QCOMPARE(indexedOf(f.text()), indexed);
        verifyReload(f);
        // The code editor shows the code: no fence lines, no block id or properties.
        QCOMPARE(f.edit(row), QString(editText).replace(u"1;"_s, u"2;"_s));
    }

    void displayFormulaLeftOpenKeepsTheItemsAfterIt()
    {
        // Review regression: deleting the closing `$$` of a formula in an outline (or Enter inside
        // it) saved a page whose `$$` line ran on to the `$$` line of a later item: reopened, the
        // items between were formula text, their task and reminder gone from the index.
        const QString content =
                u"- Wzór:\n  $$\n  a^2\n  $$\n- [ ] Zadanie ąę ⏰ 2026-10-22 09:00\n- Drugi:\n  $$\n  b\n  $$\n"_s;
        Fixture f(content);
        QCOMPARE(f.count(), 3);
        const QString indexed = indexedOf(content);
        f.page.startEditing(0, 0);
        f.page.setEditText(0, u"Wzór:\n$$\na^2"_s, 12);
        QCOMPARE(f.count(), 3);
        QCOMPARE(indexedOf(f.text()), indexed);
        verifyReload(f);
        f.page.setEditText(0, u"Wzór:\n$$\na^2\n$$"_s, 15);
        QCOMPARE(indexedOf(f.text()), indexed);
        verifyReload(f);
        // Enter inside the second formula: two items, the formula's lines as text.
        f.page.startEditing(2, 0);
        f.page.split(2, 10);
        QCOMPARE(f.count(), 4);
        QCOMPARE(indexedOf(f.text()), indexed);
        verifyReload(f);
    }

    void codeBlockReferenceThenTyping()
    {
        // "Copy block reference" writes the id on its own line after the closing fence; typing in
        // the code afterwards — a fence-like line too — keeps it and the task after the block.
        Fixture f(u"```js\nlet a = 1;\n```\n\n- [ ] zadanie ąę ⏰ 2026-10-22 09:00\n"_s);
        const QString id = f.page.ensureBlockRef(0);
        QVERIFY(!id.isEmpty());
        QCOMPARE(f.text(), u"```js\nlet a = 1;\n```\n^"_s + id + u"\n\n- [ ] zadanie ąę ⏰ 2026-10-22 09:00\n"_s);
        f.page.setEditText(0, u"// komentarz\n"_s + f.edit(0));
        QCOMPARE(f.text(),
                 u"```js\n// komentarz\nlet a = 1;\n```\n^"_s + id + u"\n\n- [ ] zadanie ąę ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(f.edit(0), u"// komentarz\nlet a = 1;"_s);
        f.page.setEditText(0, u"// komentarz\nlet a = 1;\n```"_s);
        QCOMPARE(f.text(), u"````js\n// komentarz\nlet a = 1;\n```\n````\n^"_s + id
                                   + u"\n\n- [ ] zadanie ąę ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(f.edit(0), u"// komentarz\nlet a = 1;\n```"_s);
        const ParsedNote n = markdown::parseNote(Page, f.text());
        QVERIFY(n.blockIds.contains(id));
        QCOMPARE(n.tasks.size(), 1);
        QCOMPARE(n.reminders.size(), 1);
        verifyReload(f);
    }

    void typedClosingFenceClosesUnclosedCode()
    {
        // A code block whose fence was never closed (a file written elsewhere) runs on over the
        // task after it; typing the closing fence closes it, as before widening existed.
        Fixture f(u"Akapit\n\n```js\nkod\n\n- [ ] zadanie\n"_s);
        QCOMPARE(f.count(), 2);
        QCOMPARE(f.edit(1), u"kod\n\n- [ ] zadanie"_s);
        f.page.setEditText(1, u"kod\n```\n\n- [ ] zadanie"_s);
        QCOMPARE(f.text(), u"Akapit\n\n```js\nkod\n```\n\n- [ ] zadanie\n"_s);
        const markdown::BlockDocument reloaded(f.text());
        QCOMPARE(reloaded.count(), 3);
        QCOMPARE(reloaded.at(2).kind, BlockKind::ListItem);
        QCOMPARE(markdown::parseNote(Page, f.text()).tasks.size(), 1);
        // At the end of the page: the typed fence becomes the hidden closing fence.
        Fixture end(u"Akapit\n\n```js\nkod\n"_s);
        end.page.startEditing(1, 3);
        end.page.setEditText(1, u"kod\n```"_s, 7);
        QCOMPARE(end.text(), u"Akapit\n\n```js\nkod\n```\n"_s);
        QCOMPARE(end.edit(1), u"kod"_s);
        QCOMPARE(end.page.cursorPosition(), 3);
        verifyReload(end);
        // In a list item (where the fence ended at the next item).
        Fixture item(u"- ```\n  code\n- next\n"_s);
        item.page.setEditText(0, u"code\n```"_s);
        QCOMPARE(item.text(), u"- ```\n  code\n  ```\n- next\n"_s);
        QCOMPARE(item.edit(0), u"code"_s);
        verifyReload(item);
    }

    void fenceLikeLineInADisplayFormulaIsNoFence()
    {
        // Review regression: a ``` or ~~~ line typed inside a formula between `$$` lines got a
        // closing fence after the formula, and that one opened a code block running over every
        // block after it (the line inside the formula is formula, not a fence).
        const QString content = u"$$\na\nb\n$$\n\n- [ ] Zadanie ąę ⏰ 2026-10-22 09:00\n"_s;
        Fixture f(content);
        const QString indexed = indexedOf(content);
        f.page.startEditing(0, 4);
        f.page.setEditText(0, u"$$\na\n~~~\nb\n$$"_s, 7);
        QCOMPARE(f.text(), u"$$\na\n~~~\nb\n$$\n\n- [ ] Zadanie ąę ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(indexedOf(f.text()), indexed);
        verifyReload(f);
        // In a list item too: the task after it stays a task.
        Fixture item(u"- Wzór:\n  $$\n  a\n  $$\n- [ ] Zadanie ⏰ 2026-10-22 09:00\n"_s);
        item.page.startEditing(0, 0);
        item.page.setEditText(0, u"Wzór:\n$$\na\n```\n$$"_s, 14);
        QCOMPARE(item.text(), u"- Wzór:\n  $$\n  a\n  ```\n  $$\n- [ ] Zadanie ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(markdown::parseNote(Page, item.text()).tasks.size(), 1);
        verifyReload(item);
    }

    void fenceTypedInTextBlockIsClosed()
    {
        // ``` typed on a new line of a paragraph (Shift+Enter) opens a fence: its closing fence
        // is added at the end of the block, so the task after it stays a task in the file.
        Fixture f(u"Akapit\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        QSignalSpy textResets(&f.page, &PageController::editTextReset);
        f.page.startEditing(0, 6);
        f.page.setEditText(0, u"Akapit\n```"_s, 10);
        QCOMPARE(f.text(), u"Akapit\n```\n```\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(f.edit(0), u"Akapit\n```\n```"_s);
        // The caret stays where the user typed, before the added fence.
        QCOMPARE(textResets.size(), 1);
        QCOMPARE(textResets[0].at(1).toString(), u"Akapit\n```\n```"_s);
        QCOMPARE(textResets[0].at(2).toInt(), 10);
        QCOMPARE(f.page.cursorPosition(), 10);
        // The pair follows the opening fence: one backtick more, then Backspace over all three.
        f.page.setEditText(0, u"Akapit\n````\n```"_s, 11);
        QCOMPARE(f.edit(0), u"Akapit\n````\n````"_s);
        QCOMPARE(f.page.cursorPosition(), 11);
        f.page.setEditText(0, u"Akapit\n```\n````"_s, 10);
        f.page.setEditText(0, u"Akapit\n``\n````"_s, 9);
        QCOMPARE(f.edit(0), u"Akapit\n``"_s);
        QCOMPARE(f.page.cursorPosition(), 9);
        f.page.setEditText(0, u"Akapit\n```"_s, 10);
        QCOMPARE(f.edit(0), u"Akapit\n```\n```"_s);
        f.page.setEditText(0, u"Akapit\n```js\n```"_s, 12);
        QCOMPARE(f.text(), u"Akapit\n```js\n```\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        const ParsedNote n = markdown::parseNote(Page, f.text());
        QCOMPARE(n.tasks.size(), 1);
        QCOMPARE(n.reminders.size(), 1);
        QCOMPARE(markdown::BlockDocument(f.text()).count(), 3);
        // A whole block typed as a fenced snippet becomes a code block.
        Fixture code(u"Akapit\n\n- [ ] zadanie\n"_s);
        code.page.setEditText(0, u"```js\nkod"_s);
        QCOMPARE(code.text(), u"```js\nkod\n```\n\n- [ ] zadanie\n"_s);
        QCOMPARE(code.role(0, "view").toString(), u"code"_s);
        QCOMPARE(code.edit(0), u"kod"_s);
        verifyReload(code);
        // A lone opening fence stays text while it is typed; the next line makes it a code block.
        Fixture lone(u"Akapit\n\n- [ ] zadanie\n"_s);
        lone.page.setEditText(0, u"```js"_s);
        QCOMPARE(lone.text(), u"```js\n\n- [ ] zadanie\n"_s);
        QCOMPARE(lone.role(0, "view").toString(), u"text"_s);
        lone.page.setEditText(0, u"```js\n"_s);
        QCOMPARE(lone.text(), u"```js\n\n```\n\n- [ ] zadanie\n"_s);
        QCOMPARE(lone.role(0, "view").toString(), u"code"_s);
        verifyReload(lone);
        // In a list item the fence would take the item's children.
        Fixture item(u"- a\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- b\n"_s);
        item.page.setEditText(0, u"a\n```"_s);
        QCOMPARE(item.text(), u"- a\n  ```\n  ```\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- b\n"_s);
        QCOMPARE(markdown::parseNote(Page, item.text()).reminders.size(), 1);
        QCOMPARE(blocksOf(markdown::BlockDocument(item.text())).size(), 3);
        // Pasted fenced text without its closing fence gets one too.
        Fixture paste(u"Akapit\n\nx\n\n- [ ] zadanie\n"_s);
        paste.page.startEditing(1, 1);
        paste.page.pasteText(1, 0, 1, u"```js\nkod"_s);
        QCOMPARE(paste.text(), u"Akapit\n\n```js\nkod\n```\n\n- [ ] zadanie\n"_s);
        QCOMPARE(markdown::BlockDocument(paste.text()).count(), 3);
        Fixture mid(u"- abc\n- z\n"_s);
        mid.page.startEditing(0, 1);
        mid.page.pasteText(0, 1, 1, u"x\n\n```js\nkod ąę"_s);
        QCOMPARE(mid.text(), u"- ax\n- ```js\n  kod ąę\n  ```\n- bc\n- z\n"_s);
        verifyReload(mid);
    }

    void enterBetweenFenceLinesOfTextBlock()
    {
        // Shift+Enter, ```js (its closing fence is added), then Enter: a line break between the
        // fence lines, as in a code block. A split would leave "```js" unclosed in one block and
        // a lone "```" in the next, and typing the code there would turn the task into code.
        Fixture f(u"Akapit\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        f.page.startEditing(0, 6);
        f.page.setEditText(0, u"Akapit\n```"_s, 10);
        f.page.setEditText(0, u"Akapit\n```js\n```"_s, 12);
        QCOMPARE(f.page.split(0, 12), (QVariantMap{{u"row"_s, 0}, {u"cursor"_s, 13}}));
        QCOMPARE(f.count(), 2);
        QCOMPARE(f.edit(0), u"Akapit\n```js\n\n```"_s);
        QCOMPARE(f.page.cursorPosition(), 13);
        f.page.setEditText(0, u"Akapit\n```js\nkod\n```"_s, 16);
        // Enter in the code adds a line too.
        f.page.split(0, 16);
        QCOMPARE(f.edit(0), u"Akapit\n```js\nkod\n\n```"_s);
        f.page.setEditText(0, u"Akapit\n```js\nkod\nżółw\n```"_s, 21);
        QCOMPARE(f.text(), u"Akapit\n```js\nkod\nżółw\n```\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(markdown::parseNote(Page, f.text()).reminders.size(), 1);
        // Enter after the closing fence starts the next block.
        QCOMPARE(f.page.split(0, int(f.edit(0).size())).value(u"row"_s).toInt(), 1);
        QCOMPARE(f.count(), 3);
        QCOMPARE(markdown::parseNote(Page, f.text()).reminders.size(), 1);
        // Each Enter is one undo step.
        f.page.undo(); // the new block
        f.page.undo(); // "żółw"
        QCOMPARE(f.text(), u"Akapit\n```js\nkod\n\n```\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        f.page.undo(); // the line break
        QCOMPARE(f.text(), u"Akapit\n```js\nkod\n```\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        // Enter before the opening fence splits the code off into a code block of its own.
        Fixture start(u"Akapit\n"_s);
        start.page.setEditText(0, u"Akapit\n```js\nkod\n```"_s);
        start.page.split(0, 7);
        QCOMPARE(start.count(), 2);
        QCOMPARE(start.role(1, "view").toString(), u"code"_s);
        QCOMPARE(start.edit(1), u"kod"_s);
        verifyReload(start);
        // In a list item the child task stays a child.
        Fixture item(u"- Przykład:\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- dalej #tag\n"_s);
        item.page.startEditing(0, 9);
        item.page.setEditText(0, u"Przykład:\n```"_s, 13);
        item.page.setEditText(0, u"Przykład:\n```js\n```"_s, 15);
        QCOMPARE(item.page.split(0, 15).value(u"row"_s).toInt(), 0);
        item.page.setEditText(0, u"Przykład:\n```js\nkod\n```"_s, 19);
        QCOMPARE(item.text(),
                 u"- Przykład:\n  ```js\n  kod\n  ```\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- dalej #tag\n"_s);
        QCOMPARE(markdown::parseNote(Page, item.text()).reminders.size(), 1);
        verifyReload(item);
        // A snippet pasted between the fence lines goes in as code, blank lines and all.
        Fixture paste(u"- Przykład:\n- dalej\n"_s);
        paste.page.startEditing(0, 9);
        paste.page.setEditText(0, u"Przykład:\n```py\n```"_s, 15);
        paste.page.split(0, 15);
        QCOMPARE(paste.page.cursorPosition(), 16);
        paste.page.pasteText(0, 16, 16, u"a = 1\n\nb = 2"_s);
        QCOMPARE(paste.count(), 2);
        QCOMPARE(paste.edit(0), u"Przykład:\n```py\na = 1\n\nb = 2\n```"_s);
        QCOMPARE(paste.page.cursorPosition(), 28);
        verifyReload(paste);
        // A fence that was open in the file already (it ends at the next item): Enter splits.
        Fixture open(u"- a\n  ```\n  b\n- c\n"_s);
        QCOMPARE(open.page.split(0, 7).value(u"row"_s).toInt(), 1);
        QCOMPARE(open.count(), 3);
    }

    void loneFenceClosedWhenLeft()
    {
        // "```js" typed in a block without Enter stays text while the block has the caret (the
        // language can still be typed), but the saved file never leaves it open: on reload the
        // code would run on over the blocks after it (the task and its reminder would be lost).
        // Leaving the block gives it its closing fence: an empty code block.
        const QString task = u"- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s;
        Fixture f(u"Akapit\n\n"_s + task);
        f.page.startEditing(0, 6);
        f.page.setEditText(0, u"```js"_s, 5);
        QCOMPARE(f.edit(0), u"```js"_s);
        QCOMPARE(f.role(0, "view").toString(), u"text"_s);
        f.page.setEditText(0, u"```json"_s, 7);
        QCOMPARE(f.edit(0), u"```json"_s);
        QCOMPARE(f.text(), u"```json\n\n"_s + task);
        // Saved while it is edited (save timer, the app going to the background): closed.
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"```json\n```\n\n"_s + task);
        QCOMPARE(markdown::parseNote(Page, f.host.file(Page)).reminders.size(), 1);
        // The caret moves on: the block itself gets the closing fence.
        f.page.startEditing(1, 0);
        QCOMPARE(f.page.editingRow(), 1);
        QCOMPARE(f.count(), 2);
        QCOMPARE(f.role(0, "view").toString(), u"code"_s);
        QCOMPARE(f.text(), u"```json\n```\n\n"_s + task);
        verifyReload(f);
        // One undo step with the typing: the text before it, with the caret there (a snapshot with
        // the lone fence line would be read as code over the task); redo gives the closed fence.
        f.page.undo();
        QCOMPARE(f.text(), u"Akapit\n\n"_s + task);
        QCOMPARE(f.page.editingRow(), 0);
        QCOMPARE(markdown::parseNote(Page, f.host.file(Page)).reminders.size(), 1);
        f.page.redo();
        QCOMPARE(f.text(), u"```json\n```\n\n"_s + task);
        QCOMPARE(f.count(), 2);
        QCOMPARE(markdown::parseNote(Page, f.host.file(Page)).reminders.size(), 1);

        // A list item: its children would become code. Editing stopped (Escape, a click
        // outside) closes it too, with the children kept.
        Fixture item(u"- a\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- c\n"_s);
        item.page.startEditing(0, 1);
        item.page.setEditText(0, u"```py"_s, 5);
        QVERIFY(item.page.save());
        QCOMPARE(item.host.file(Page), u"- ```py\n  ```\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- c\n"_s);
        item.page.stopEditing();
        QCOMPARE(item.text(), u"- ```py\n  ```\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- c\n"_s);
        QCOMPARE(markdown::parseNote(Page, item.text()).reminders.size(), 1);
        verifyReload(item);

        // Followed by a sibling only: nothing runs on, the file keeps the line while it is edited;
        // leaving the edited block still closes it.
        Fixture sibling(u"- a\n- b ⏰ 2026-10-22 09:00\n"_s);
        sibling.page.startEditing(0, 1);
        sibling.page.setEditText(0, u"~~~"_s, 3);
        QVERIFY(sibling.page.save());
        QCOMPARE(sibling.host.file(Page), u"- ~~~\n- b ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(markdown::parseNote(Page, sibling.host.file(Page)).reminders.size(), 1);
        sibling.page.startEditing(1, 0);
        QCOMPARE(sibling.text(), u"- ~~~\n  ~~~\n- b ⏰ 2026-10-22 09:00\n"_s);
        verifyReload(sibling);

        // At the end of the page: typed and left, it is closed; a fence line that came with the
        // file and was only looked at stays as it is.
        Fixture last(u"Akapit\n"_s);
        last.page.startEditing(0, 6);
        last.page.setEditText(0, u"```"_s, 3);
        last.page.stopEditing();
        QCOMPARE(last.text(), u"```\n```\n"_s);
        verifyReload(last);
        Fixture file(u"Akapit\n\n```js\n"_s);
        file.page.startEditing(1, 2);
        file.page.startEditing(0, 0);
        file.page.stopEditing();
        QCOMPARE(file.text(), u"Akapit\n\n```js\n"_s);
        QVERIFY(file.page.save());
        QCOMPARE(file.host.file(Page), u"Akapit\n\n```js\n"_s);

        // A task's text that starts with a fence is no fence.
        Fixture taskText(u"- [ ] a\n- b\n"_s);
        taskText.page.startEditing(0, 1);
        taskText.page.setEditText(0, u"```js"_s, 5);
        taskText.page.startEditing(1, 0);
        QCOMPARE(taskText.text(), u"- [ ] ```js\n- b\n"_s);
    }

    void loneFenceComparedAndMergedAsSaved()
    {
        // While "```js" is typed the page's text is compared with the file, merged with an
        // external change and offered to the merge dialog as it is saved: closed. Merged open,
        // the page would read as code running on over the task (its reminder lost everywhere).
        const QString task = u"- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s;
        Fixture f(u"Akapit\n\n"_s + task + u"- c\n"_s, 10000);
        QString mergedLocal;
        f.page.setMergeFunction([&mergedLocal](const QString &, const QString &local, const QString &remote) {
            mergedLocal = local;
            return MergeOutcome{QString(local).replace(u"- c\n"_s, u"- c zdalnie\n"_s),
                                remote.endsWith(u"- c zdalnie\n"_s)};
        });
        f.page.startEditing(0, 6);
        f.page.setEditText(0, u"```js"_s, 5);
        QVERIFY(f.page.save());
        const QString saved = u"```js\n```\n\n"_s + task + u"- c\n"_s;
        QCOMPARE(f.host.file(Page), saved);
        QCOMPARE(f.page.sourceText(), saved);
        // Typed away from the saved text and back: nothing to save.
        f.page.setEditText(0, u"```jsx"_s, 6);
        QVERIFY(f.page.dirty());
        f.page.setEditText(0, u"```js"_s, 5);
        QVERIFY(!f.page.dirty());
        // Dirty when the other device's change comes in: merged closed.
        f.page.setEditText(0, u"```json"_s, 7);
        QVERIFY(f.page.dirty());
        f.host.externalWrite(Page, u"```js\n```\n\n"_s + task + u"- c zdalnie\n"_s);
        QVERIFY(!f.page.conflict());
        QCOMPARE(mergedLocal, u"```json\n```\n\n"_s + task + u"- c\n"_s);
        QCOMPARE(f.text(), u"```json\n```\n\n"_s + task + u"- c zdalnie\n"_s);
        QCOMPARE(markdown::parseNote(Page, f.text()).reminders.size(), 1);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"```json\n```\n\n"_s + task + u"- c zdalnie\n"_s);
    }

    void mergeKeepsClosingFenceOnItsLine()
    {
        // Backspace at the start of the block after code written in a text block: the text goes on
        // the line after the closing fence ("```b" closes nothing: the task after it would become
        // code).
        Fixture f(u"Akapit\n\nb\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        f.page.startEditing(0, 6);
        f.page.setEditText(0, u"Akapit\n```js\nkod\n```"_s, 20);
        f.page.startEditing(1, 0);
        QCOMPARE(f.page.mergeWithPrevious(1), (QVariantMap{{u"row"_s, 0}, {u"cursor"_s, 21}}));
        QCOMPARE(f.edit(0), u"Akapit\n```js\nkod\n```\nb"_s);
        QCOMPARE(f.page.cursorPosition(), 21);
        QCOMPARE(f.text(), u"Akapit\n```js\nkod\n```\nb\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        QCOMPARE(markdown::parseNote(Page, f.text()).reminders.size(), 1);
        f.page.undo(); // one step
        QCOMPARE(f.text(), u"Akapit\n```js\nkod\n```\n\nb\n\n- [ ] zadanie ⏰ 2026-10-22 09:00\n"_s);
        // Delete at the end of a list item whose code (from the file) ends it.
        Fixture item(u"- a\n  ```\n  kod\n  ```\n- b\n  - [ ] dziecko ⏰ 2026-10-22 09:00\n- c\n"_s);
        item.page.startEditing(0, 13);
        QCOMPARE(item.page.mergeNext(0).value(u"cursor"_s).toInt(), 14);
        QCOMPARE(item.edit(0), u"a\n```\nkod\n```\nb"_s);
        QCOMPARE(markdown::parseNote(Page, item.text()).reminders.size(), 1);
        verifyReload(item);
        // Other text joins as before.
        Fixture plain(u"- a ```\n- b\n"_s);
        plain.page.mergeWithPrevious(1);
        QCOMPARE(plain.text(), u"- a ```b\n"_s);
    }

    // --- tables ----------------------------------------------------------------------------------------

    void tables()
    {
        Fixture f(u"Przed\n\n| Pozycja | Cena |\n| --- | ---: |\n| Płytki | 89 |\n| Suma | =sum(above) |\n\nPo\n"_s);
        QCOMPARE(f.role(1, "view").toString(), u"table"_s);
        QVariantList rows = f.role(1, "tableData").toMap().value(u"rows"_s).toList();
        QCOMPARE(rows[1].toList()[1].toMap().value(u"display"_s).toString(), u"89"_s);
        f.page.setTableCell(1, 0, 1, u"100"_s);
        rows = f.role(1, "tableData").toMap().value(u"rows"_s).toList();
        QCOMPARE(rows[1].toList()[1].toMap().value(u"display"_s).toString(), u"100"_s);
        QVERIFY(f.text().startsWith(u"Przed\n\n"_s));
        QVERIFY(f.text().endsWith(u"\n\nPo\n"_s));
        f.page.addTableRow(1);
        f.page.addTableColumn(1);
        const QVariantMap data = f.role(1, "tableData").toMap();
        QCOMPARE(data.value(u"header"_s).toStringList(), QStringList({u"Pozycja"_s, u"Cena"_s, u"Kolumna 3"_s}));
        QCOMPARE(data.value(u"rows"_s).toList().size(), 3);
        f.page.setTableCell(1, -1, 0, u"Rzecz"_s);
        QCOMPARE(f.role(1, "tableData").toMap().value(u"header"_s).toStringList().first(), u"Rzecz"_s);
        f.page.removeTableRow(1, 2);
        QCOMPARE(f.role(1, "tableData").toMap().value(u"rows"_s).toList().size(), 2);
        f.page.setTableCell(1, 99, 0, u"x"_s); // out of range: nothing
        f.page.undo();
        f.page.undo();
        f.page.undo();
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"Przed\n\n| Pozycja | Cena |\n| --- | ---: |\n| Płytki | 89 |\n| Suma | =sum(above) |\n\nPo\n"_s);
    }

    void tableCellsAreRendered()
    {
        Fixture f(u"| Rzecz | Cena |\n| --- | ---: |\n| **Płytki** z [[Inna]] #zakupy | 89 |\n| <b>x</b> ⏰ 2026-10-16 09:00 | =2*3 |\n| Brak [[Nie ma]] | =1+ |\n"_s);
        QVariantMap data = f.role(0, "tableData").toMap();
        const QVariantList rows = data.value(u"rows"_s).toList();
        const QVariantMap tiles = rows[0].toList()[0].toMap();
        const QString html = tiles.value(u"html"_s).toString();
        QVERIFY2(html.contains(u"<b>Płytki</b>"_s) && html.contains(u"href=\"page:Inna\""_s) && html.contains(u"tag:zakupy"_s),
                 qPrintable(html));
        // The visible text measures the column; the raw text stays what the cell editor edits.
        // "z" keeps the next word on its line (a no-break space, measured as such).
        QCOMPARE(tiles.value(u"plain"_s).toString(), u"Płytki z\u00a0Inna #zakupy"_s);
        QCOMPARE(tiles.value(u"text"_s).toString(), u"**Płytki** z [[Inna]] #zakupy"_s);
        // Note text that looks like HTML stays text; chips render.
        const QString hostile = rows[1].toList()[0].toMap().value(u"html"_s).toString();
        QVERIFY2(hostile.contains(u"&lt;b&gt;x&lt;/b&gt;"_s) && !hostile.contains(u"<b>x"_s), qPrintable(hostile));
        QVERIFY(hostile.contains(u"⏰&nbsp;jutro&nbsp;09:00"_s));
        // Formula results are values (escaped), not Markdown.
        const QVariantMap six = rows[1].toList()[1].toMap();
        QCOMPARE(six.value(u"html"_s).toString(), u"6"_s);
        QCOMPARE(six.value(u"plain"_s).toString(), u"6"_s);
        QVERIFY(!rows[2].toList()[1].toMap().value(u"error"_s).toString().isEmpty());
        QCOMPARE(rows[2].toList()[1].toMap().value(u"html"_s).toString(),
                 rows[2].toList()[1].toMap().value(u"display"_s).toString().toHtmlEscaped());
        // Header cells are rendered too.
        QCOMPARE(data.value(u"headerHtml"_s).toList().size(), 2);
        QCOMPARE(data.value(u"headerHtml"_s).toList()[0].toString(), u"Rzecz"_s);
        // Theme colours and the index re-render the cells (a page that appears resolves its link).
        QVERIFY(rows[2].toList()[0].toMap().value(u"html"_s).toString().contains(u"#7a5ea3"_s, Qt::CaseInsensitive));
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.page.setPalette({{u"link"_s, u"#102030"_s}, {u"tagFg"_s, u"#405060"_s}});
        QVERIFY(changed.size() > 0);
        data = f.role(0, "tableData").toMap();
        QVERIFY(data.value(u"rows"_s).toList()[0].toList()[0].toMap().value(u"html"_s).toString().contains(u"#102030"_s));
        f.host.setFile(u"Nie ma.md"_s, u"- teraz jest\n"_s);
        Q_EMIT f.host.indexChanged();
        data = f.role(0, "tableData").toMap();
        QVERIFY(data.value(u"rows"_s).toList()[2].toList()[0].toMap().value(u"html"_s).toString().contains(u"#102030"_s));
    }

    void transclusions()
    {
        Fixture f(u"- Wstęp ^wst\n- ![[Budżet#Październik]]\n- Zobacz ![[Inna#^inny1]] i !((kont))\n- ![[Brak strony]] ![[Budżet#Grudzień]]\n"_s);
        f.host.setFile(u"Budżet.md"_s,
                       u"# Budżet\n\n## Październik\n\n- Czynsz **1800 zł** #opłaty\n\t- [ ] Zapłacić 📅 2026-10-20\n- Jedzenie\n\n## Listopad\n\n- Prezenty\n"_s);
        f.host.setFile(u"Kontakty.md"_s, u"- Hydraulik: pan Marek ^kont\n\t- 600 100 200\n"_s);
        f.model()->invalidateRendering();
        // A section: the blocks under the heading up to the next one of its level.
        QVERIFY(f.role(1, "onlyEmbeds").toBool());
        QVERIFY(f.role(1, "html").toString().isEmpty());
        QVariantList t = f.role(1, "transclusions").toList();
        QCOMPARE(t.size(), 1);
        QVariantMap section = t[0].toMap();
        QCOMPARE(section.value(u"kind"_s).toString(), u"heading"_s);
        QVERIFY(section.value(u"found"_s).toBool());
        QCOMPARE(section.value(u"title"_s).toString(), u"Budżet › Październik"_s);
        QCOMPARE(section.value(u"href"_s).toString(), u"page:Bud%C5%BCet#Pa%C5%BAdziernik"_s);
        QCOMPARE(section.value(u"path"_s).toString(), u"Budżet.md"_s);
        QVariantList blocks = section.value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 3);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"<b>1800 zł</b>"_s));
        QCOMPARE(blocks[0].toMap().value(u"depth"_s).toInt(), 0);
        QCOMPARE(blocks[1].toMap().value(u"depth"_s).toInt(), 1);
        QCOMPARE(blocks[1].toMap().value(u"task"_s).toString(), u"todo"_s);
        QCOMPARE(blocks[1].toMap().value(u"path"_s).toString(), u"Budżet.md"_s);
        QCOMPARE(blocks[1].toMap().value(u"line"_s).toInt(), 5);
        QVERIFY(!blocks[1].toMap().value(u"html"_s).toString().contains(u"[ ]"_s));
        QVERIFY(blocks[2].toMap().value(u"html"_s).toString().contains(u"Jedzenie"_s));
        // A block of a page (with its children) and a vault-wide block embed, next to text.
        QVERIFY(!f.role(2, "onlyEmbeds").toBool());
        QVERIFY(!f.role(2, "html").toString().isEmpty());
        t = f.role(2, "transclusions").toList();
        QCOMPARE(t.size(), 2);
        QCOMPARE(t[0].toMap().value(u"kind"_s).toString(), u"block"_s);
        QCOMPARE(t[0].toMap().value(u"title"_s).toString(), u"Inna"_s);
        QCOMPARE(t[0].toMap().value(u"href"_s).toString(), u"page:Inna#^inny1"_s);
        QVERIFY(t[0].toMap()
                        .value(u"blocks"_s)
                        .toList()[0]
                        .toMap()
                        .value(u"html"_s)
                        .toString()
                        .contains(u"Blok z\u00a0innej strony"_s));
        const QVariantMap contact = t[1].toMap();
        QCOMPARE(contact.value(u"href"_s).toString(), u"block:kont"_s);
        QCOMPARE(contact.value(u"path"_s).toString(), u"Kontakty.md"_s);
        QCOMPARE(contact.value(u"title"_s).toString(), u"Kontakty"_s);
        blocks = contact.value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 2);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"pan Marek"_s));
        QCOMPARE(blocks[1].toMap().value(u"depth"_s).toInt(), 1);
        // Missing page and section.
        t = f.role(3, "transclusions").toList();
        QCOMPARE(t.size(), 2);
        QVERIFY(!t[0].toMap().value(u"found"_s).toBool());
        QCOMPARE(t[0].toMap().value(u"message"_s).toString(), u"missingPage"_s);
        QCOMPARE(t[0].toMap().value(u"title"_s).toString(), u"Brak strony"_s);
        QCOMPARE(t[1].toMap().value(u"message"_s).toString(), u"missingSection"_s);
        // The index changed (another device edited the page): the embed follows.
        f.host.setFile(u"Budżet.md"_s, u"## Październik\n\n- Czynsz 1900 zł\n"_s);
        Q_EMIT f.host.indexChanged();
        blocks = f.role(1, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 1);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"1900"_s));
    }

    void transcludedCodeBlocksShowTheirTextOnly()
    {
        Fixture f(u"- ![[Inna]]\n"_s);
        f.host.setFile(u"Inna.md"_s,
                       u"```js\nlet a = 1 < 2;\n```\n\n~~~\nb\n~~~\n\n```calc\nx = 2\n```\n"_s);
        f.model()->invalidateRendering();
        const QVariantList t = f.role(0, "transclusions").toList();
        QCOMPARE(t.size(), 1);
        const QVariantList blocks = t[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 3);
        QCOMPARE(blocks[0].toMap().value(u"view"_s).toString(), u"code"_s);
        QCOMPARE(blocks[0].toMap().value(u"html"_s).toString(), u"<pre>let a = 1 &lt; 2;</pre>"_s);
        QCOMPARE(blocks[1].toMap().value(u"html"_s).toString(), u"<pre>b</pre>"_s);
        QCOMPARE(blocks[2].toMap().value(u"view"_s).toString(), u"code"_s);
        QCOMPARE(blocks[2].toMap().value(u"html"_s).toString(), u"<pre>x = 2</pre>"_s);
        // A code block embedded by its id: the code only, without the fences and the id line.
        Fixture byId(u"- ![[Inna#^kod123]]\n- ![[Inna#^kod456]]\n"_s);
        byId.host.setFile(u"Inna.md"_s,
                          u"```js\nlet a = 1 < 2;\n```\n^kod123\n\n- ```py\n  b = 2\n  ```\n  ^kod456\n"_s);
        byId.model()->invalidateRendering();
        for (int r = 0; r < 2; ++r) {
            const QVariantList embeds = byId.role(r, "transclusions").toList();
            QCOMPARE(embeds.size(), 1);
            const QVariantList shown = embeds[0].toMap().value(u"blocks"_s).toList();
            QCOMPARE(shown.size(), 1);
            QCOMPARE(shown[0].toMap().value(u"view"_s).toString(), u"code"_s);
            QCOMPARE(shown[0].toMap().value(u"html"_s).toString(),
                     r == 0 ? u"<pre>let a = 1 &lt; 2;</pre>"_s : u"<pre>b = 2</pre>"_s);
        }
    }

    void transclusionsOfThisPageAndCycles()
    {
        // ![[#Heading]] shows this page's section as edited (unsaved too); a section embedding
        // itself is shown once, with a note instead of the repetition.
        Fixture f(u"## Plan\n\n- Krok pierwszy\n- ![[#Plan]]\n\n## Inne\n\n- ![[#Plan]]\n"_s, 100000);
        int inner = -1;
        int outer = -1;
        for (int r = 0; r < f.count(); ++r) {
            if (f.edit(r) == u"![[#Plan]]"_s)
                (inner < 0 ? inner : outer) = r;
        }
        QVERIFY(inner > 0 && outer > inner);
        QVariantList blocks = f.role(outer, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 2);
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"Krok pierwszy"_s));
        QCOMPARE(blocks[1].toMap().value(u"kind"_s).toString(), u"embed"_s);
        QCOMPARE(blocks[1].toMap().value(u"message"_s).toString(), u"cycle"_s);
        QCOMPARE(f.role(outer, "transclusions").toList()[0].toMap().value(u"href"_s).toString(), u"page:#Plan"_s);
        // The embed follows edits of this page right away.
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.page.setEditText(1, u"Krok pierwszy ąę"_s);
        blocks = f.role(outer, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QVERIFY(blocks[0].toMap().value(u"html"_s).toString().contains(u"Krok pierwszy ąę"_s));
        bool outerChanged = false;
        for (const QList<QVariant> &args : std::as_const(changed)) {
            if (args.at(0).toModelIndex().row() <= outer && args.at(1).toModelIndex().row() >= outer)
                outerChanged = true;
        }
        QVERIFY(outerChanged);
        // The whole page embedded in itself.
        Fixture self(u"- Jedyny blok\n- ![[Strona]]\n"_s);
        blocks = self.role(1, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[1].toMap().value(u"message"_s).toString(), u"cycle"_s);
    }

    void transclusionsAreDepthLimited()
    {
        // A chain of pages embedding the next one: MaxTransclusionDepth levels, then a note.
        Fixture f(u"- ![[P1]]\n"_s);
        for (int i = 1; i <= 6; ++i)
            f.host.setFile(u"P%1.md"_s.arg(i), u"- Poziom %1\n- ![[P%2]]\n"_s.arg(i).arg(i + 1));
        f.model()->invalidateRendering();
        const QVariantList blocks = f.role(0, "transclusions").toList()[0].toMap().value(u"blocks"_s).toList();
        int levels = 0;
        QString note;
        for (const QVariant &b : blocks) {
            const QVariantMap m = b.toMap();
            if (m.value(u"kind"_s).toString() == u"block"_s)
                ++levels;
            if (!m.value(u"message"_s).toString().isEmpty())
                note = m.value(u"message"_s).toString();
        }
        QCOMPARE(levels, BlockListModel::MaxTransclusionDepth);
        QCOMPARE(note, u"depth"_s);
        // Big pages: at most MaxTransclusionBlocks blocks, the rest counted.
        QString big;
        for (int i = 0; i < 100; ++i)
            big += u"- Wiersz %1\n"_s.arg(i);
        f.host.setFile(u"Duża.md"_s, big);
        f.page.setEditText(0, u"![[Duża]]"_s);
        const QVariantMap t = f.role(0, "transclusions").toList()[0].toMap();
        QCOMPARE(t.value(u"blocks"_s).toList().size(), BlockListModel::MaxTransclusionBlocks);
        QCOMPARE(t.value(u"more"_s).toInt(), 100 - BlockListModel::MaxTransclusionBlocks);
    }

    void titleRow()
    {
        Fixture f(u"---\ntags: [a]\n---\n# Strona\n\nTreść\n"_s);
        QCOMPARE(f.page.titleRow(), 1);
        QSignalSpy spy(&f.page, &PageController::titleRowChanged);
        f.page.setBody(1, u"# Inny tytuł"_s);
        QCOMPARE(f.page.titleRow(), -1);
        f.page.undo();
        QCOMPARE(f.page.titleRow(), 1);
        QVERIFY(spy.size() >= 2);
        // Diacritics and case do not matter, a Logseq "- # Title" item counts; a level-2 heading
        // or a heading after other blocks is no title.
        Fixture folded(u"# STRONA\n- a\n"_s);
        QCOMPARE(folded.page.titleRow(), 0);
        Fixture h2(u"---\na: 1\n---\n## Strona\n"_s);
        QCOMPARE(h2.page.titleRow(), -1);
        Fixture item(u"---\na: 1\n---\n- # Strona\n\t- dziecko\n"_s);
        QCOMPARE(item.page.titleRow(), 1);
        Fixture later(u"Wstęp\n\n# Strona\n"_s);
        QCOMPARE(later.page.titleRow(), -1);
        Fixture polish(u"# zazolc gesla\n- treść\n"_s);
        QCOMPARE(polish.page.titleRow(), -1);
        polish.page.followRename(Page, u"Notatki/Zażółć gęślą.md"_s); // diacritics folded
        QCOMPARE(polish.page.titleRow(), 0);
        // While zoomed there is no title row.
        Fixture zoom(u"---\na: 1\n---\n# Strona\n- rodzic\n\t- dziecko\n"_s);
        QCOMPARE(zoom.page.titleRow(), 1);
        zoom.page.zoomInto(2);
        QCOMPARE(zoom.page.titleRow(), -1);
        zoom.page.zoomOut();
        QCOMPARE(zoom.page.titleRow(), 1);
    }

    void noStatusHereAndElsewhere_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("expected");
        QTest::newRow("todo") << u"- [ ] Kupić mleko\n"_s << 0 << u"- Kupić mleko\n"_s;
        QTest::newRow("done date") << u"- [x] Zapłacić czynsz 📅 2026-10-10 ✅ 2026-10-09\n"_s << 0
                                   << u"- Zapłacić czynsz 📅 2026-10-10\n"_s;
        QTest::newRow("cancelled date inside")
                << u"- [-] Wizyta ❌ 2026-10-02 u dentysty\n"_s << 0 << u"- Wizyta u dentysty\n"_s;
        QTest::newRow("date first") << u"- [x] ✅ 2026-10-14 Pranie\n"_s << 0 << u"- Pranie\n"_s;
        QTest::newRow("logseq done") << u"- DONE Paczka ✅ 2026-10-01\n"_s << 0 << u"- Paczka\n"_s;
        QTest::newRow("logseq planning") << u"- LATER Żurek\n  SCHEDULED: <2026-10-15 Thu>\n"_s << 0
                                         << u"- Żurek\n  SCHEDULED: <2026-10-15 Thu>\n"_s;
        QTest::newRow("block id, crlf") << u"- [/] a ^id1\r\n- b\r\n"_s << 0 << u"- a ^id1\r\n- b\r\n"_s;
        QTest::newRow("nested") << u"- rodzic\n\t- [>] dziecko #ąę\n"_s << 1 << u"- rodzic\n\t- dziecko #ąę\n"_s;
        QTest::newRow("numbered") << u"1. [ ] pierwszy\n2. drugi\n"_s << 0 << u"1. pierwszy\n2. drugi\n"_s;
        QTest::newRow("date in code") << u"- [x] użyj `✅ 2026-01-01` ✅ 2026-10-01\n"_s << 0
                                      << u"- użyj `✅ 2026-01-01`\n"_s;
        QTest::newRow("continuation") << u"- [ ] a\n  szczegóły\n"_s << 0 << u"- a\n  szczegóły\n"_s;
    }

    void noStatusHereAndElsewhere()
    {
        // "No status" on this page (an editing command) and in another file (QueryHost,
        // markdown::removeTaskMarker) writes the same text.
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QString, expected);
        Fixture f(content);
        f.host.setFile(u"Inna.md"_s, content);
        QVERIFY(f.page.setTaskStatusAt(Page, line, QString()));
        QVERIFY(f.page.setTaskStatusAt(u"Inna.md"_s, line, QString()));
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.host.file(u"Inna.md"_s), expected);
        f.page.undo();
        QCOMPARE(f.text(), content);
    }

    void tasksElsewhere()
    {
        Fixture f(u"- [ ] Tutaj 📅 2026-10-20\n- zwykły\n"_s);
        f.host.setNow(Now);
        f.host.setFile(u"Zakupy.md"_s, u"- [ ] Mleko\n"_s);
        QVERIFY(f.page.queriesAvailable());
        // This page: an editing command.
        QVERIFY(f.page.toggleTaskAt(Page, 0, u"todo"_s));
        QCOMPARE(f.text(), u"- [x] Tutaj 📅 2026-10-20 ✅ 2026-10-15\n- zwykły\n"_s);
        QVERIFY(!f.page.setTaskStatusAt(Page, 0, u"done"_s)); // already
        QVERIFY(!f.page.setTaskStatusAt(Page, 1, u"done"_s)); // no task
        QVERIFY(!f.page.setTaskStatusAt(Page, 0, u"nonsense"_s));
        f.page.undo();
        QVERIFY(f.text().startsWith(u"- [ ] Tutaj"_s));
        // Another file: through the host's QueryHost.
        QVERIFY(f.page.toggleTaskAt(u"Zakupy.md"_s, 0, u"todo"_s));
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- [x] Mleko ✅ 2026-10-15\n"_s);
        QVERIFY(!f.page.toggleTaskAt(u"Nie ma.md"_s, 0, u"todo"_s));
        // The status menu: any status, "" = no status (the marker goes), here and elsewhere.
        QVERIFY(f.page.setTaskStatusAt(u"Zakupy.md"_s, 0, u"deferred"_s));
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- [>] Mleko\n"_s);
        QVERIFY(f.page.setTaskStatusAt(u"Zakupy.md"_s, 0, QString()));
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- Mleko\n"_s);
        QVERIFY(!f.page.setTaskStatusAt(u"Zakupy.md"_s, 0, QString())); // nothing left to remove
        QVERIFY(f.page.setTaskStatusAt(Page, 0, QString()));
        QCOMPARE(f.text(), u"- Tutaj 📅 2026-10-20\n- zwykły\n"_s);
        QVERIFY(!f.page.setTaskStatusAt(Page, 0, QString()));
        f.page.undo();
        QCOMPARE(f.text(), u"- [ ] Tutaj 📅 2026-10-20\n- zwykły\n"_s);
        QCOMPARE(PageController::toggledStatus(u"doing"_s), u"done"_s);
        QCOMPARE(PageController::toggledStatus(u"cancelled"_s), u"todo"_s);
        QCOMPARE(PageController::toggledStatus(QString()), QString());
        // Locations: this page scrolls, others navigate.
        QCOMPARE(f.page.openLocation(Page, 1).value(u"row"_s).toInt(), 1);
        QVERIFY(f.page.openLocation(u"Zakupy.md"_s, 0).isEmpty());
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Zakupy.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"line"_s).toInt(), 0);
    }

    void webPictures()
    {
        const QString kot = u"https://example.com/zdj%C4%99cia/kot.jpg"_s;
        const QString pies = u"https://example.com/pies"_s;
        const QString brak = u"https://example.com/brak.png"_s;
        const QByteArray png = QByteArray::fromBase64(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
        Fixture f(u"- ![Mój kot](%1)\n- Pies: ![](%2) i tekst\n- ![](%3)\n- `![kod](%1)` ![kot](%1)\n"_s.arg(kot, pies,
                                                                                                             brak));
        f.host.setWebPicture(kot, png);
        f.host.setWebPicture(pies, png);
        f.host.setWebPicture(brak, QByteArray("<html>404</html>"));
        QCOMPARE(f.page.webPictureUrls(), (QStringList{kot, pies, brak}));
        // Nothing is fetched by showing the page: each picture waits for "Pokaż".
        QVariantMap e = f.role(0, "embeds").toList().value(0).toMap();
        QCOMPARE(e.value(u"web"_s).toBool(), true);
        QCOMPARE(e.value(u"state"_s).toString(), u"ask"_s);
        QCOMPARE(e.value(u"host"_s).toString(), u"example.com"_s);
        QCOMPARE(e.value(u"name"_s).toString(), u"example.com › kot.jpg"_s);
        QCOMPARE(e.value(u"alt"_s).toString(), u"Mój kot"_s);
        QVERIFY(e.value(u"url"_s).toUrl().isEmpty());
        QCOMPARE(f.role(0, "onlyEmbeds").toBool(), true);
        QCOMPARE(f.host.webFetches(), 0);
        // "Pokaż": loading, then the cached file.
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.page.showWebPicture(kot);
        QCOMPARE(f.role(0, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"loading"_s);
        QTRY_COMPARE(f.role(0, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ready"_s);
        e = f.role(0, "embeds").toList().value(0).toMap();
        QVERIFY(e.value(u"url"_s).toUrl().isLocalFile());
        QCOMPARE(e.value(u"width"_s).toInt(), 1);
        QCOMPARE(e.value(u"height"_s).toInt(), 1);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(f.host.webFetches(), 1);
        QCOMPARE(f.role(1, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ask"_s);
        // The "always" setting: the others load when asked for.
        f.host.setWebPicturesShown(true);
        QCOMPARE(f.role(1, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"loading"_s);
        QTRY_COMPARE(f.role(1, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ready"_s);
        QTRY_COMPARE(f.role(2, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"failed"_s);
        f.host.setWebPicturesShown(false);
        // Download one picture: an attachment, every picture of that URL points at it (not the
        // one in code), one undo step.
        const QString before = f.text();
        QSignalSpy done(&f.page, &PageController::webPicturesDownloaded);
        QCOMPARE(f.page.downloadWebPictures(kot), 1);
        QCOMPARE(done.size(), 1); // cached: at once
        QCOMPARE(done.at(0).at(0).toInt(), 1);
        QVERIFY(done.at(0).at(1).toStringList().isEmpty());
        QCOMPARE(
                f.text(),
                u"- ![Mój kot](assets/kot.png)\n- Pies: ![](%2) i tekst\n- ![](%3)\n- `![kod](%1)` ![kot](assets/kot.png)\n"_s
                        .arg(kot, pies, brak));
        QCOMPARE(f.role(0, "embeds").toList().value(0).toMap().value(u"web"_s).toBool(), false);
        f.page.undo();
        QCOMPARE(f.text(), before);
        f.page.redo();
        QVERIFY(f.text().contains(u"assets/kot.png"_s));
        // The rest of the page: the failing one is named, the others stored.
        done.clear();
        QCOMPARE(f.page.downloadWebPictures(), 2);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 1);
        QCOMPARE(done.at(0).at(1).toStringList(), QStringList{u"example.com › brak.png"_s});
        QVERIFY(f.text().contains(u"- Pies: ![](assets/pies.png) i tekst"_s));
        QVERIFY(f.text().contains(brak));
        QCOMPARE(f.page.webPictureUrls(), QStringList{brak});
        // A URL not on the page: nothing; source mode: refused (-1, not "no pictures").
        QCOMPARE(f.page.downloadWebPictures(u"https://example.com/inny.png"_s), 0);
        f.page.setSourceMode(true);
        QCOMPARE(f.page.downloadWebPictures(), -1);
        QCOMPARE(f.page.downloadWebPictures(brak), -1);
        QCOMPARE(f.page.webPictureDownloads(), 0);
    }

    void insertWebPicture()
    {
        const QByteArray png = QByteArray::fromBase64(
                "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAAIAAAUAAen63NgAAAAASUVORK5CYII=");
        Fixture f(u"- Zdjęcie /obraz\n\n| a |\n| --- |\n| 1 |\n"_s);
        QVariantMap r = f.page.applySlashCommand(0, 8, 14, u"webImage"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"webImage"_s);
        QCOMPARE(f.edit(0), u"Zdjęcie "_s);
        QVERIFY(f.page.isWebPictureUrl(u" https://example.com/a.png "_s));
        QVERIFY(!f.page.isWebPictureUrl(u"example.com/a.png"_s));
        // Not a web address: nothing.
        QCOMPARE(f.page.insertWebPicture(0, 8, 8, u"assets/a.png"_s, false).value(u"row"_s).toInt(), -1);
        // At the caret, as a picture to be shown when asked.
        r = f.page.insertWebPicture(0, 8, 8, u" https://example.com/kot.jpg "_s, false);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(f.edit(0), u"Zdjęcie ![](https://example.com/kot.jpg)"_s);
        QCOMPARE(f.role(0, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ask"_s);
        // Blanks and parentheses: between angle brackets; with the download right away.
        const QString odd = u"https://example.com/mój kot (1).png"_s;
        f.host.setWebPicture(odd, png);
        QSignalSpy done(&f.page, &PageController::webPicturesDownloaded);
        r = f.page.insertWebPicture(1, 0, 0, odd, true); // a table: a block after it
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(f.edit(2), u"![](<https://example.com/mój kot (1).png>)"_s);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 1);
        QCOMPARE(f.edit(2), u"![](<assets/mój%20kot%20%281%29.png>)"_s);
    }

    void webPictureRemovedWhileFetched()
    {
        const QString url = u"https://example.com/a.png"_s;
        Fixture f(u"- ![a](%1)\n- tekst\n"_s.arg(url));
        f.host.setWebPicture(url, QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgA"
                                                         "AIAAAUAAen63NgAAAAASUVORK5CYII="));
        QSignalSpy done(&f.page, &PageController::webPicturesDownloaded);
        QCOMPARE(f.page.downloadWebPictures(), 1);
        QCOMPARE(f.host.webFetches(), 1); // fetched even though pictures are not shown
        f.page.setBody(0, u"już bez zdjęcia"_s);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 0);
        QCOMPARE(done.at(0).at(1).toStringList().size(), 1);
        QCOMPARE(f.host.attachmentCount(), 0); // nothing to point at it: no file left behind
    }

    void webPictureFormatFromTheHost()
    {
        // The browser build hands out data: URLs: the format comes from the host, not from the
        // URL's file name — nor from the web address (an SVG badge without ".svg").
        const QString kot = u"https://example.com/kot.png"_s;
        const QString badge = u"https://img.example.com/badge/build-passing"_s;
        Fixture f(u"- ![kot](%1)\n- ![](%2)\n"_s.arg(kot, badge));
        f.host.setWebPicture(kot,
                             QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgAA"
                                                    "IAAAUAAen63NgAAAAASUVORK5CYII="));
        f.host.setWebPictureText(badge, u"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"90\" height=\"20\"/>"_s);
        f.host.setWebPictureDataUrls(true);
        f.page.showWebPicture(kot);
        f.page.showWebPicture(badge);
        QTRY_COMPARE(f.role(0, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ready"_s);
        QVariantMap e = f.role(0, "embeds").toList().value(0).toMap();
        QCOMPARE(e.value(u"url"_s).toUrl().scheme(), u"data"_s);
        QCOMPARE(e.value(u"format"_s).toString(), u"PNG"_s);
        QCOMPARE(e.value(u"decodable"_s).toBool(), true);
        QTRY_COMPARE(f.role(1, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ready"_s);
        QCOMPARE(f.role(1, "embeds").toList().value(0).toMap().value(u"format"_s).toString(), u"SVG"_s);
        f.host.setWebPictureDataUrls(false);
        f.page.blocks()->webPictureChanged(QString());
        e = f.role(1, "embeds").toList().value(0).toMap();
        QVERIFY(e.value(u"url"_s).toUrl().isLocalFile());
        QCOMPARE(e.value(u"format"_s).toString(), u"SVG"_s);
    }

    void webPictureEscapedDestination()
    {
        // `\_` and `\(` in the destination: the picture is named as parsed (a_b(1).png), its row
        // follows the host's state, and storing it rewrites the destination as written.
        const QString url = u"https://example.com/a_b(1).png"_s;
        Fixture f(u"- Zdjęcie ![żółw](https://example.com/a\\_b\\(1\\).png \"https://example.com/a_b(1).png\")\n"_s);
        f.host.setWebPicture(url, QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgA"
                                                         "AIAAAUAAen63NgAAAAASUVORK5CYII="));
        QCOMPARE(f.page.webPictureUrls(), QStringList{url});
        QCOMPARE(f.role(0, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ask"_s);
        f.page.showWebPicture(url);
        QTRY_COMPARE(f.role(0, "embeds").toList().value(0).toMap().value(u"state"_s).toString(), u"ready"_s);
        QSignalSpy done(&f.page, &PageController::webPicturesDownloaded);
        QCOMPARE(f.page.downloadWebPictures(url), 1);
        QCOMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 1);
        QCOMPARE(f.text(), u"- Zdjęcie ![żółw](assets/a_b%281%29.png \"https://example.com/a_b(1).png\")\n"_s);
        QCOMPARE(f.host.attachmentCount(), 1);
        QVERIFY(f.page.webPictureUrls().isEmpty());
    }

    void webPicturesInCodeStay()
    {
        // Pictures in code, formulas and HTML are text: never fetched, stored or rewritten.
        const QString url = u"https://example.com/kot.png"_s;
        const QString content = u"- ![kot](%1)\n\n~~~markdown\n![](%1)\n~~~\n\n```\n![](%1)\n```\n\n"
                                u"<div>\n![](%1)\n</div>\n"_s.arg(url);
        Fixture f(content);
        f.host.setWebPicture(url, QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgA"
                                                         "AIAAAUAAen63NgAAAAASUVORK5CYII="));
        QCOMPARE(f.page.webPictureUrls(), QStringList{url});
        QSignalSpy done(&f.page, &PageController::webPicturesDownloaded);
        QCOMPARE(f.page.downloadWebPictures(), 1);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 1);
        QCOMPARE(f.text(), u"- ![kot](assets/kot.png)\n"_s + content.sliced(content.indexOf(u'\n') + 1));
        QVERIFY(f.page.webPictureUrls().isEmpty());
        // Only a block showing pictures is no picture of the page: code alone has none.
        Fixture code(u"~~~\n![](%1)\n~~~\n"_s.arg(url));
        QVERIFY(code.page.webPictureUrls().isEmpty());
        QCOMPARE(code.page.downloadWebPictures(url), 0);
    }

    void webPictureFinishedInSourceMode()
    {
        // The fetch ends while the page is shown as source: the blocks are not what is saved, so
        // the picture is not stored (named as failed) and no attachment is left behind.
        const QString url = u"https://example.com/kot.png"_s;
        Fixture f(u"- ![kot](%1)\n"_s.arg(url));
        f.host.setWebPicture(url, QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgA"
                                                         "AIAAAUAAen63NgAAAAASUVORK5CYII="));
        QSignalSpy done(&f.page, &PageController::webPicturesDownloaded);
        QCOMPARE(f.page.downloadWebPictures(), 1);
        f.page.setSourceMode(true);
        f.page.setSourceText(u"- ![kot](%1) dopisane w źródle\n"_s.arg(url));
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 0);
        QCOMPARE(done.at(0).at(1).toStringList(), QStringList{u"example.com › kot.png"_s});
        QCOMPARE(f.host.attachmentCount(), 0);
        f.page.setSourceMode(false);
        QCOMPARE(f.text(), u"- ![kot](%1) dopisane w źródle\n"_s.arg(url));
    }

    void webPicturesAlreadyBeingStored()
    {
        // "Save all" while the page's only picture is already on its way: counted, not "none".
        const QString url = u"https://example.com/kot.png"_s;
        Fixture f(u"- ![kot](%1)\n"_s.arg(url));
        f.host.setWebPicture(url, QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAAC0lEQVR42mNgA"
                                                         "AIAAAUAAen63NgAAAAASUVORK5CYII="));
        QSignalSpy done(&f.page, &PageController::webPicturesDownloaded);
        QCOMPARE(f.page.downloadWebPictures(url), 1);
        QCOMPARE(f.page.downloadWebPictures(), 1);
        QCOMPARE(f.page.webPictureDownloads(), 1);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.at(0).at(0).toInt(), 1);
        QCOMPARE(f.host.webFetches(), 1);
        QCOMPARE(f.text(), u"- ![kot](assets/kot.png)\n"_s);
    }

    void attachments()
    {
        Fixture f(u"- Szkic łazienki\n\n| a |\n| --- |\n| 1 |\n"_s);
        // Into the text at the caret, with spaces around the embed.
        f.page.startEditing(0, 5);
        QVariantMap r = f.page.insertAttachment(0, 5, 5, u"plan piętra.png"_s, QByteArray("\x89PNG"));
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(f.edit(0), u"Szkic ![[plan piętra.png]] łazienki"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 26);
        QCOMPARE(f.page.cursorPosition(), 26);
        QVERIFY(f.role(0, "embeds").toList().size() == 1);
        f.page.undo();
        QCOMPARE(f.edit(0), u"Szkic łazienki"_s);
        // Not into a table: a block of its own after it.
        const int table = 1;
        QCOMPARE(f.role(table, "view").toString(), u"table"_s);
        r = f.page.insertAttachment(table, 0, 0, u"zdjęcie.jpg"_s, QByteArray("JPEG"));
        QCOMPARE(r.value(u"row"_s).toInt(), table + 1);
        QCOMPARE(f.edit(table + 1), u"![[zdjęcie.jpg]]"_s);
        QCOMPARE(f.page.editingRow(), -1);
        // A name that would find another file is embedded by its path.
        QCOMPARE(f.page.embedFor(u"assets/plan piętra.png"_s), u"![[plan piętra.png]]"_s);
        f.host.setFile(u"plan piętra.png.md"_s, u"- strona o tej nazwie\n"_s);
        QCOMPARE(f.page.embedFor(u"assets/plan piętra.png"_s), u"![[assets/plan piętra.png]]"_s);
        // Saving fails: nothing inserted, a signal for the message.
        QSignalSpy failed(&f.page, &PageController::attachmentFailed);
        r = f.page.insertAttachment(0, 0, 0, u"pusty.png"_s, QByteArray());
        QCOMPARE(r.value(u"row"_s).toInt(), -1);
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.at(0).at(0).toString(), u"pusty.png"_s);
        // Files (drops, pickers): stored and embedded, after the block or in the edited text.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString a = dir.filePath(u"żółw.png"_s);
        const QString b = dir.filePath(u"umowa najmu.pdf"_s);
        for (const QString &path : {a, b}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("dane");
        }
        r = f.page.insertFilesAfter(0, {QUrl::fromLocalFile(a), QUrl::fromLocalFile(b), QUrl::fromLocalFile(dir.filePath(u"brak.png"_s))});
        QCOMPARE(r.value(u"count"_s).toInt(), 2);
        QCOMPARE(f.edit(r.value(u"row"_s).toInt()), u"![[żółw.png]] ![[umowa najmu.pdf]]"_s);
        QCOMPARE(failed.size(), 2); // the missing file
        r = f.page.insertFiles(0, -1, -1, {QUrl::fromLocalFile(a)});
        QVERIFY(f.edit(0).endsWith(u" ![[żółw 2.png]]"_s) || f.edit(0).endsWith(u" ![[żółw-2.png]]"_s)
                || f.edit(0).contains(u"![[żółw"_s));
        // At the end of an empty page.
        Fixture empty{QString()};
        r = empty.page.insertFilesAfter(-1, {QUrl::fromLocalFile(a)});
        QVERIFY2(empty.text().startsWith(u"- ![[żółw.png]]"_s), qPrintable(empty.text()));
    }

    /// The browser's pick of pictures (pickImageInBrowser(): the picker itself exists only in the
    /// browser build): what the receiver does with the files PickCollector hands over — skipped
    /// and empty ones refused with the others still inserted, names as the browser reports them,
    /// the block moved or deleted while the picker was open, one undo step for all.
    void pictureReceiver_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("row");
        QTest::addColumn<int>("at");             // from = to (-1: the end of the text)
        QTest::addColumn<QString>("change");     // while the picker is open: "", "insert above", "delete"
        QTest::addColumn<QStringList>("picked"); // "state|name|bytes": r read, L too large, T over the total
        QTest::addColumn<QString>("expected");
        QTest::addColumn<QStringList>("failed");
        QTest::newRow("trzy obrazy, jeden za duży")
                << u"- Szkic łazienki\n"_s << 0 << 5 << QString()
                << QStringList{u"r|żółw.png|PNG1"_s, u"L|ogromne zdjęcie.jpg|"_s, u"r|plan piętra.png|PNG2"_s}
                << u"- Szkic ![[żółw.png]] ![[plan piętra.png]] łazienki\n"_s << QStringList{u"ogromne zdjęcie.jpg"_s};
        QTest::newRow("ponad sumę wyboru")
                << u"- Szkic\n"_s << 0 << -1 << QString() << QStringList{u"r|a.png|A"_s, u"T|b.png|"_s, u"T|ćma.png|"_s}
                << u"- Szkic ![[a.png]]\n"_s << QStringList{u"b.png"_s, u"ćma.png"_s};
        QTest::newRow("pusty, bez nazwy, ze ścieżką, ze spacjami")
                << u"- Zdjęcia:\n"_s << 0 << -1 << QString()
                << QStringList{u"r||GIF8"_s, u"r|pusty.png|"_s, u"r|katalog/łąka.webp|RIFF"_s,
                               u"r|  spacje.png  |PNG"_s}
                << u"- Zdjęcia: ![[Attachment]] ![[łąka.webp]] ![[spacje.png]]\n"_s << QStringList{u"pusty.png"_s};
        QTest::newRow("blok przesunięty")
                << u"- pierwszy\n- drugi\n"_s << 1 << 5 << u"insert above"_s << QStringList{u"r|źrebię.png|PNG"_s}
                << u"- pierwszy\n- wstawiony\n- drugi ![[źrebię.png]]\n"_s << QStringList();
        QTest::newRow("blok usunięty") << u"- pierwszy\n- drugi\n"_s << 1 << 2 << u"delete"_s
                                       << QStringList{u"r|źrebię.png|PNG"_s, u"r|żaba.png|PNG"_s}
                                       << u"- pierwszy\n- ![[źrebię.png]] ![[żaba.png]]\n"_s << QStringList();
        QTest::newRow("żaden nie wszedł")
                << u"- Szkic\n"_s << 0 << -1 << QString() << QStringList{u"L|film.mp4|"_s, u"T|zdjęcie.jpg|"_s}
                << u"- Szkic\n"_s << QStringList{u"film.mp4"_s, u"zdjęcie.jpg"_s};
        QTest::newRow("anulowany") << u"- Szkic\n"_s << 0 << -1 << QString() << QStringList() << u"- Szkic\n"_s
                                   << QStringList();
    }
    void pictureReceiver()
    {
        QFETCH(QString, content);
        QFETCH(int, row);
        QFETCH(int, at);
        QFETCH(QString, change);
        QFETCH(QStringList, picked);
        QFETCH(QString, expected);
        QFETCH(QStringList, failed);
        Fixture f(content);
        QSignalSpy failedSpy(&f.page, &PageController::attachmentFailed);
        const auto receive = f.page.pictureReceiver(row, at, at);
        if (change == u"insert above")
            QVERIFY(f.page.insertBlockAfter(row - 1, u"wstawiony"_s) >= 0);
        else if (change == u"delete")
            f.page.deleteBlock(row);
        const QString beforePick = f.text();
        const int attachmentsBefore = f.host.attachmentCount();
        QList<platform::PickedFile> files;
        int read = 0;
        for (const QString &entry : picked) {
            const QStringList parts = entry.split(u'|');
            QCOMPARE(parts.size(), 3);
            platform::PickedFile file;
            file.name = parts.at(1);
            file.tooLarge = parts.at(0) == u"L";
            file.overTotal = parts.at(0) == u"T";
            file.size = file.tooLarge ? 300LL * 1024 * 1024 : parts.at(2).toUtf8().size();
            if (file.isRead())
                file.data = parts.at(2).toUtf8();
            if (file.isRead() && !file.data.isEmpty())
                ++read;
            files.append(file);
        }
        receive(files);
        QCOMPARE(f.text(), expected);
        QStringList failedNames;
        for (const QList<QVariant> &args : std::as_const(failedSpy))
            failedNames.append(args.at(0).toString());
        QCOMPARE(failedNames, failed);
        // Only the read pictures were stored; the skipped ones never reached the notebook.
        QCOMPARE(f.host.attachmentCount() - attachmentsBefore, read);
        // All of them in one step.
        if (expected != beforePick) {
            f.page.undo();
            QCOMPARE(f.text(), beforePick);
        }
        // A second answer (none should come) changes nothing: the block is no longer tracked.
        const QString after = f.text();
        if (!files.isEmpty() && read > 0) {
            receive({});
            QCOMPARE(f.text(), after);
        }
    }

    void pastingPictures()
    {
        Fixture f(u"- Zrzut: \n"_s);
        QClipboard *clipboard = QGuiApplication::clipboard();
        clipboard->setText(u"zwykły tekst"_s);
        QVERIFY(!f.page.clipboardHasImage());
        QCOMPARE(f.page.pasteImage(0, 7, 7).value(u"row"_s).toInt(), -1);
        QImage image(40, 20, QImage::Format_ARGB32);
        image.fill(QColor(u"#f2b33d"_s));
        clipboard->setImage(image);
        QVERIFY(f.page.clipboardHasImage());
        f.page.startEditing(0, 7);
        const QVariantMap r = f.page.pasteImage(0, 7, 7);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(f.edit(0), u"Zrzut: ![[Pasted image 20261015100000.png]]"_s);
        const QVariantList embeds = f.role(0, "embeds").toList();
        QCOMPARE(embeds.size(), 1);
        QVERIFY(embeds[0].toMap().value(u"image"_s).toBool());
        // Copied files (a file manager's copy) paste as attachments too.
        QTemporaryDir dir;
        const QString file = dir.filePath(u"skan.pdf"_s);
        {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write("%PDF");
        }
        auto *mime = new QMimeData;
        mime->setUrls({QUrl::fromLocalFile(file)});
        clipboard->setMimeData(mime);
        QVERIFY(f.page.clipboardHasImage());
        f.page.pasteImage(0, int(f.edit(0).size()), int(f.edit(0).size()));
        QVERIFY(f.edit(0).endsWith(u"![[skan.pdf]]"_s));
        clipboard->clear();
    }

    void embedsNameFormatsTheDeviceCannotDecode()
    {
        // A picture whose format this device has no decoder for (WebP, AVIF without Qt's image
        // plugins: EditorHost::canDecodePicture) is not loaded: the embed names its format
        // (EmbedView) instead of "Nie można wczytać".
        FakeEditorHost host;
        host.setUndecodablePictureFormats({u"webp"_s});
        QByteArray png;
        {
            QImage image(40, 30, QImage::Format_RGB32);
            image.fill(Qt::darkGreen);
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
        }
        QCOMPARE(host.saveAttachment(u"łąka.webp"_s, QByteArray("RIFF\0\0\0\0WEBPVP8 ", 16)), u"assets/łąka.webp"_s);
        QCOMPARE(host.saveAttachment(u"plan piętra.png"_s, png), u"assets/plan piętra.png"_s);
        QCOMPARE(host.saveAttachment(u"zdjęcie.AVIF"_s, QByteArray("\0\0\0 ftypavif", 12)), u"assets/zdjęcie.AVIF"_s);
        QCOMPARE(host.saveAttachment(u"umowa.pdf"_s, "%PDF"), u"assets/umowa.pdf"_s);
        host.setFile(Page, u"- Łąka ![[łąka.webp]] i ![[plan piętra.png]], ![[zdjęcie.AVIF]], ![[umowa.pdf]]\n"_s);
        PageController page;
        page.setNow(Now);
        page.setHost(&host);
        page.setPath(Page);
        const QHash<int, QByteArray> roles = page.blocks()->roleNames();
        const QVariantList embeds = page.blocks()->data(page.blocks()->index(0), roles.key("embeds")).toList();
        QCOMPARE(embeds.size(), 4);
        const auto embed = [&](int i, const char *key) { return embeds.at(i).toMap().value(QString::fromLatin1(key)); };
        QCOMPARE(embed(0, "name").toString(), u"łąka.webp"_s);
        QVERIFY(embed(0, "image").toBool());
        QVERIFY(!embed(0, "decodable").toBool());
        QCOMPARE(embed(0, "format").toString(), u"WEBP"_s);
        QCOMPARE(embed(0, "width").toInt(), 0);  // nothing read from a file nothing can show
        QVERIFY(embed(1, "decodable").toBool()); // Qt decodes PNG itself
        QCOMPARE(embed(1, "format").toString(), u"PNG"_s);
        QCOMPARE(embed(1, "width").toInt(), 40);
        QCOMPARE(embed(2, "decodable").toBool(), QImageReader::supportedImageFormats().contains("avif"));
        QCOMPARE(embed(2, "format").toString(), u"AVIF"_s);
        QVERIFY(!embed(3, "image").toBool()); // a file: a chip
        QCOMPARE(embed(3, "format").toString(), QString());
        // The fake host's default is the device's own decoders, like the shell's Workspace.
        QVERIFY(!host.canDecodePicture(u"assets/łąka.WEBP"_s));
        host.setUndecodablePictureFormats({});
        QCOMPARE(host.canDecodePicture(u"assets/łąka.webp"_s), QImageReader::supportedImageFormats().contains("webp"));
    }

    void imageSlashCommand()
    {
        Fixture f(u"- Zdjęcie /obr\n"_s);
        const QVariantMap r = f.page.applySlashCommand(0, 8, 12, u"image"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"image"_s);
        QCOMPARE(f.text(), u"- Zdjęcie \n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 8);
        QVERIFY(!f.page.imageNameFilters().isEmpty());
        // The file dialog offers the pictures the editor embeds as pictures (paths::pictureExtensions):
        // .ico too, which the HTML export already showed as a picture.
        const QString filter = f.page.imageNameFilters().constFirst();
        for (const QString &ext : {u"png"_s, u"jpg"_s, u"jpeg"_s, u"webp"_s, u"svg"_s, u"avif"_s, u"ico"_s})
            QVERIFY2(filter.contains(u"*."_s + ext + u' ') || filter.contains(u"*."_s + ext + u')'),
                     qPrintable(filter));
        QVERIFY(!f.page.pickImageInBrowser(0, 0, 0)); // a FilePicker outside the browser
    }

    void drawingSlashCommand()
    {
        // "/rysunek": an empty drawing stored as an attachment, embedded where the command was,
        // and the drawing editor opened on it (route "drawing").
        Fixture f(u"- Wykład z łączności /rys\n"_s);
        QVariantMap r = f.page.applySlashCommand(0, 19, 23, u"drawing"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"drawing"_s);
        QCOMPARE(f.text(), u"- Wykład z łączności \n"_s);
        const int cursor = r.value(u"cursor"_s).toInt();
        QCOMPARE(cursor, 19);
        r = f.page.insertDrawing(0, cursor, cursor);
        const QString path = r.value(u"path"_s).toString();
        QCOMPARE(path, u"assets/Drawing 2026-10-15 10.00.svg"_s);
        QCOMPARE(f.text(), u"- Wykład z łączności ![[Drawing 2026-10-15 10.00.svg]]\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        // The file: a picture (its size on the root) carrying an empty scene.
        QFile file(f.host.assetUrl(path).toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray svg = file.readAll();
        QVERIFY(svg.contains("width=\"320\" height=\"180\""));
        const std::optional<brulion::drawing::Drawing> d = brulion::drawing::readDrawing(svg);
        QVERIFY(d);
        QVERIFY(d->strokes.isEmpty());
        // Its embed is a drawing at that path.
        const QVariantMap embed = f.role(0, "embeds").toList().value(0).toMap();
        QVERIFY(embed.value(u"image"_s).toBool());
        QVERIFY(embed.value(u"drawing"_s).toBool());
        QCOMPARE(embed.value(u"path"_s).toString(), path);
        QCOMPARE(embed.value(u"width"_s).toInt(), 320);
        // A second one in the same minute gets a free name.
        r = f.page.insertDrawing(0, -1, -1);
        QCOMPARE(r.value(u"path"_s).toString(), u"assets/Drawing 2026-10-15 10.00 2.svg"_s);
        // Opening it saves the page first, then asks for the drawing route.
        f.page.openDrawing(path);
        QCOMPARE(f.host.file(Page), f.text());
        QCOMPARE(f.host.lastRoute(), (QVariantMap{{u"view"_s, u"drawing"_s}, {u"path"_s, path}}));
        // Undo takes the embed away (the file stays, as with pictures).
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), u"- Wykład z łączności \n"_s);
    }

    void drawingOnSlashCommand()
    {
        // "/slajd": the picker of a picture or a PDF (QML's EditorDialogs.pickDrawingBackdrop).
        Fixture f(u"- Notatki z wykładu /slaj\n"_s);
        const QVariantMap r = f.page.applySlashCommand(0, 18, 23, u"drawingOn"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"drawingOn"_s);
        QCOMPARE(f.text(), u"- Notatki z wykładu \n"_s);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 18);
        // Found by its Polish and English words.
        for (const QString &word : {u"slajd"_s, u"pdf"_s, u"podkład"_s, u"wykład"_s, u"annotate"_s}) {
            bool found = false;
            for (const SlashCommand *c : filterSlashCommands(word))
                found = found || c->id == u"drawingOn";
            QVERIFY2(found, qPrintable(word));
        }
    }

    void insertDrawingsOnPdfPages()
    {
        // Three pages of a lecture's PDF: three drawings, the first at the caret, each further one
        // a block of its own after it, in order — one undo step.
        Fixture f(u"- Wykład 3: \n- dalej\n"_s);
        const QString pdf = u"assets/wykład.pdf"_s;
        const QVariantList items{preparedBackdrop(pdf, 1, u"wykład"_s), preparedBackdrop(pdf, 2, u"wykład"_s),
                                 preparedBackdrop(pdf, 3, u"wykład"_s)};
        const QVariantMap r = f.page.insertDrawingsOn(0, 10, 10, items);
        const QStringList paths = r.value(u"paths"_s).toStringList();
        QCOMPARE(paths,
                 (QStringList{u"assets/wykład p. 1.svg"_s, u"assets/wykład p. 2.svg"_s, u"assets/wykład p. 3.svg"_s}));
        QCOMPARE(f.text(),
                 u"- Wykład 3: ![[wykład p. 1.svg]]\n- ![[wykład p. 2.svg]]\n- ![[wykład p. 3.svg]]\n- dalej\n"_s);
        // Each a drawing with the page as its backdrop, as the app shows it ("Jak motyw").
        for (int i = 0; i < 3; ++i) {
            QFile file(f.host.assetUrl(paths.at(i)).toLocalFile());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QByteArray svg = file.readAll();
            const std::optional<brulion::drawing::Drawing> d = brulion::drawing::readDrawing(svg);
            QVERIFY(d);
            QCOMPARE(d->theme, brulion::drawing::PaperTheme::Auto);
            const std::optional<brulion::drawing::BackdropSource> source = brulion::drawing::backdropSource(*d);
            QVERIFY(source);
            QCOMPARE(source->path, pdf);
            QCOMPARE(source->page, i + 1);
            QVERIFY(svg.contains("width=\"1232\" height=\"707\"")); // 1200 × 675 and the margin
            QVERIFY(f.role(i, "embeds").toList().value(0).toMap().value(u"drawing"_s).toBool());
        }
        // One undo step takes the three embeds (the files stay).
        f.page.undo();
        QCOMPARE(f.text(), u"- Wykład 3: \n- dalej\n"_s);
        QVERIFY(f.host.assetUrl(paths.at(2)).isLocalFile());
        // The same pages again: free names.
        const QVariantMap again = f.page.insertDrawingsOn(1, -1, -1, {preparedBackdrop(pdf, 1, u"wykład"_s)});
        QCOMPARE(again.value(u"paths"_s).toStringList(), QStringList{u"assets/wykład p. 1 2.svg"_s});
    }

    void insertDrawingOnPicture()
    {
        // A picture (an exported slide, a photo of the board): one drawing, light.
        Fixture f(u"- Tablica \n"_s);
        QSignalSpy failed(&f.page, &PageController::attachmentFailed);
        const QVariantMap r = f.page.insertDrawingsOn(
                0, 8, 8, {preparedBackdrop(u"assets/slajd.png"_s, 0, u"slajd"_s, QSize(80, 120))});
        QCOMPARE(r.value(u"paths"_s).toStringList(), QStringList{u"assets/slajd (drawing).svg"_s});
        QCOMPARE(f.text(), u"- Tablica ![[slajd (drawing).svg]]\n"_s);
        QFile file(f.host.assetUrl(u"assets/slajd (drawing).svg"_s).toLocalFile());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const std::optional<brulion::drawing::Drawing> d = brulion::drawing::readDrawing(file.readAll());
        QVERIFY(d);
        QCOMPARE(d->theme, brulion::drawing::PaperTheme::Light);
        QCOMPARE(d->strokes.first().box, QRectF(0, 0, 800, 1200)); // portrait: 1200 high
        QCOMPARE(brulion::drawing::backdropSource(*d)->page, 0);
        // Opening it: the drawing route.
        f.page.openDrawing(r.value(u"paths"_s).toStringList().first());
        QCOMPARE(f.host.lastRoute().value(u"view"_s).toString(), u"drawing"_s);
        // A name taken: " 2".
        const QVariantMap second =
                f.page.insertDrawingsOn(0, -1, -1, {preparedBackdrop(u"assets/slajd.png"_s, 0, u"slajd"_s)});
        QCOMPARE(second.value(u"paths"_s).toStringList(), QStringList{u"assets/slajd (drawing) 2.svg"_s});
        QCOMPARE(failed.size(), 0);
        // Items that cannot be used: refused one by one, nothing inserted for them.
        QVariantMap broken = preparedBackdrop(u"assets/x.png"_s, 0, u"x"_s);
        broken.insert(u"mimeType"_s, u"text/html"_s);
        QVariantMap failedItem = preparedBackdrop(u"assets/y.heic"_s, 0, u"y"_s);
        failedItem.insert(u"error"_s, u"format"_s);
        const QString before = f.text();
        const QVariantMap none = f.page.insertDrawingsOn(0, -1, -1, {broken, failedItem, QVariant()});
        QCOMPARE(none.value(u"row"_s).toInt(), -1);
        QVERIFY(none.value(u"paths"_s).toStringList().isEmpty());
        QCOMPARE(f.text(), before);
        QCOMPARE(failed.size(), 3);
    }

    void importBackdropSource()
    {
        // A picked file goes through the host's importAttachment (the fake: stored like any).
        Fixture f(u"- \n"_s);
        QTemporaryDir dir;
        const QString file = dir.filePath(u"Fizyka – wykład 3.pdf"_s);
        {
            QFile out(file);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write("%PDF-1.7");
        }
        QCOMPARE(f.page.importBackdropSource(QUrl::fromLocalFile(file)), u"assets/Fizyka – wykład 3.pdf"_s);
        QSignalSpy failed(&f.page, &PageController::attachmentFailed);
        QCOMPARE(f.page.importBackdropSource(QUrl::fromLocalFile(dir.filePath(u"nie ma.pdf"_s))), QString());
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), u"nie ma.pdf"_s);
        QCOMPARE(f.page.importBackdropSource(QUrl(u"https://example.org/a.pdf"_s)), QString());
    }

    void drawingEmbedsFollowTheirFile()
    {
        // A plain SVG is a picture; an SVG with an embedded scene is a drawing. A drawing saved
        // again (the drawing editor) has its embed computed again (its size here; its URL's
        // version in the Workspace).
        FakeEditorHost host;
        host.setTextAsset(u"assets/logo.svg"_s, u"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"40\"/>"_s);
        brulion::drawing::Drawing d;
        host.setTextAsset(u"assets/szkic ćwiczeń.svg"_s, QString::fromUtf8(brulion::drawing::toSvg(d)));
        host.setFile(Page, u"- ![[logo.svg]] ![[szkic ćwiczeń.svg]]\n"_s);
        PageController page;
        page.setNow(Now);
        page.setHost(&host);
        page.setPath(Page);
        const auto embeds = [&] {
            const QHash<int, QByteArray> roles = page.blocks()->roleNames();
            return page.blocks()->data(page.blocks()->index(0), roles.key("embeds")).toList();
        };
        QCOMPARE(embeds().size(), 2);
        QVERIFY(!embeds().at(0).toMap().value(u"drawing"_s).toBool());
        QVERIFY(embeds().at(1).toMap().value(u"drawing"_s).toBool());
        QCOMPARE(embeds().at(1).toMap().value(u"width"_s).toInt(), 320);
        brulion::drawing::Stroke s;
        s.id = u"a"_s;
        s.points = {{0, 0, 0.5}, {500, 200, 0.5}};
        d.strokes = {s};
        const QByteArray bigger = brulion::drawing::toSvg(d);
        // The file rewritten in place, reported as a file change (Workspace::fileChanged).
        QFile out(host.assetUrl(u"assets/szkic ćwiczeń.svg"_s).toLocalFile());
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(bigger);
        out.close();
        QSignalSpy changed(page.blocks(), &QAbstractItemModel::dataChanged);
        Q_EMIT host.fileChanged(u"assets/szkic ćwiczeń.svg"_s);
        QCOMPARE(changed.size(), 1);
        QVERIFY(embeds().at(1).toMap().value(u"width"_s).toInt() > 500);
        // Another attachment changing leaves the row alone.
        Q_EMIT host.fileChanged(u"assets/inne.png"_s);
        QCOMPARE(changed.size(), 1);
    }

    void tableCellTypingIsSavedAndOneUndoStep()
    {
        // A cell being typed in reaches the document (and the debounced save) at once — an app
        // suspended or closed with the cell still open loses nothing — and its typing is one
        // undo step per cell.
        const QString content = u"| Pozycja | Cena |\n| --- | ---: |\n| Płytki | 89 |\n"_s;
        Fixture f(content, 0);
        f.page.setTableCell(0, 0, 0, u"Ł"_s, true);
        f.page.setTableCell(0, 0, 0, u"Łą"_s, true);
        f.page.setTableCell(0, 0, 0, u"Łąka"_s, true);
        QVERIFY(f.host.file(Page).contains(u"Łąka"_s));
        f.page.setTableCell(0, 0, 1, u"90"_s, true); // another cell: another step
        f.page.setTableCell(0, 0, 1, u"901"_s, true);
        QVERIFY(f.host.file(Page).contains(u"901"_s));
        f.page.undo();
        QVERIFY(f.text().contains(u"Łąka"_s));
        QVERIFY(f.text().contains(u"89"_s));
        f.page.undo();
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        // Esc: the cell's typing is undone exactly (the table is reformatted by typing).
        f.page.setTableCell(0, -1, 1, u"Cena zł"_s, true);
        f.page.setTableCell(0, 0, 1, u"9"_s, true);
        f.page.setTableCell(0, 0, 1, u"99"_s, true);
        f.page.cancelTableCell(0, 0, 1);
        QVERIFY(f.text().contains(u"Cena zł"_s));
        QVERIFY(f.text().contains(u"89 |"_s) && !f.text().contains(u"99"_s));
        f.page.cancelTableCell(0, 0, 1); // nothing typed in this cell any more: no undo
        f.page.cancelTableCell(0, -1, 0);
        QVERIFY(f.text().contains(u"Cena zł"_s));
        f.page.undo();
        QCOMPARE(f.text(), content);
    }

    void calcResultsFollowEdits()
    {
        Fixture f(u"```calc\na = 2\nb = a * 3\n```\n\n```calc\nb + 1\n```\n"_s);
        QCOMPARE(f.role(1, "calcResults").toList()[0].toMap().value(u"value"_s).toString(), u"7"_s);
        f.page.setEditText(0, u"a = 5\nb = a * 3"_s, 5);
        QCOMPARE(f.role(0, "calcResults").toList()[1].toMap().value(u"value"_s).toString(), u"15"_s);
        QCOMPARE(f.role(1, "calcResults").toList()[0].toMap().value(u"value"_s).toString(), u"16"_s);
        QCOMPARE(f.text(), u"```calc\na = 5\nb = a * 3\n```\n\n```calc\nb + 1\n```\n"_s);
    }

    // --- links --------------------------------------------------------------------------------------------

    void links()
    {
        Fixture f(u"- [ ] zadanie ⏰ 2026-10-16 09:00 ^c1\n- [[Inna#^inny1]] ((c1))\n"_s);
        QSignalSpy nav(&f.host, &FakeEditorHost::navigated);
        QSignalSpy urls(&f.host, &FakeEditorHost::urlOpened);
        f.page.activateLink(1, u"page:Inna#%5Einny1"_s);
        QCOMPARE(nav.size(), 1);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Inna.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"blockId"_s).toString(), u"inny1"_s);
        f.page.activateLink(1, u"page:Nowa%20strona"_s);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), QString());
        QCOMPARE(f.host.lastRoute().value(u"target"_s).toString(), u"Nowa strona"_s);
        // A tag goes to the host as a tag route: the host opens the page it names.
        f.page.activateLink(0, u"tag:%C5%BC%C3%B3%C5%82w"_s);
        QCOMPARE(f.host.lastRoute().value(u"view"_s).toString(), u"tag"_s);
        QCOMPARE(f.host.lastRoute().value(u"tag"_s).toString(), u"żółw"_s);
        f.page.activateLink(0, u"ext:https://example.com/a"_s);
        QCOMPARE(urls.size(), 1);
        QVariantMap r = f.page.activateLink(1, u"block:c1"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"scrollTo"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        r = f.page.activateLink(0, u"reminder:10-30"_s);
        QCOMPARE(r.value(u"action"_s).toString(), u"reminder"_s);
        QCOMPARE(r.value(u"from"_s).toInt(), 10);
        f.page.activateLink(0, u"task:toggle"_s);
        QVERIFY(f.text().startsWith(u"- [x] zadanie"_s));
        // The rendered reminder chip links to the token's range in the body.
        const QString html = f.role(0, "html").toString();
        const qsizetype k = html.indexOf(u"reminder:"_s);
        QVERIFY(k > 0);
        const QString range = html.mid(k + 9, html.indexOf(u'"', k) - k - 9);
        const QStringList parts = range.split(u'-');
        const QString body = f.model()->document().at(0).body;
        QCOMPARE(body.mid(parts[0].toInt(), parts[1].toInt() - parts[0].toInt()), u"⏰ 2026-10-16 09:00"_s);
    }

    // --- saving -------------------------------------------------------------------------------------------

    void debouncedSave()
    {
        Fixture f(u"- a\n"_s, 60);
        QSignalSpy saved(&f.page, &PageController::saved);
        f.page.setEditText(0, u"ab"_s, 2);
        f.page.setEditText(0, u"abc"_s, 3);
        QVERIFY(f.page.dirty());
        QCOMPARE(f.host.writeCount(), 0);
        QTRY_COMPARE(f.host.writeCount(), 1);
        QCOMPARE(f.host.file(Page), u"- abc\n"_s);
        QVERIFY(!f.page.dirty());
        QCOMPARE(saved.size(), 1);
        // save() flushes at once.
        f.page.setEditText(0, u"abcd"_s, 4);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.writeCount(), 2);
        QTest::qWait(100);
        QCOMPARE(f.host.writeCount(), 2);
        // Undo back to the saved text is not dirty.
        f.page.cycleTask(0);
        QVERIFY(f.page.dirty());
        f.page.undo();
        QVERIFY(!f.page.dirty());
    }

    void failedSaveStaysDirty()
    {
        Fixture f(u"- a\n"_s, 0);
        QSignalSpy failed(&f.page, &PageController::saveFailed);
        f.host.setFailWrites(true);
        f.page.setEditText(0, u"b"_s, 1);
        QCOMPARE(failed.size(), 1);
        QVERIFY(f.page.dirty());
        f.host.setFailWrites(false);
        QVERIFY(f.page.save());
        QVERIFY(!f.page.dirty());
        QCOMPARE(f.host.file(Page), u"- b\n"_s);
    }

    void newPageIsCreatedByContent()
    {
        FakeEditorHost host;
        PageController page;
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(u"Nowa strona.md"_s);
        QVERIFY(page.loaded());
        QVERIFY(!page.exists());
        QCOMPARE(page.appendEmptyBlock(), 0);
        QVERIFY(!page.dirty());
        QCOMPARE(host.writeCount(), 0); // a lone empty bullet creates no file
        page.setEditText(0, u"Pierwsza myśl"_s, 13);
        QCOMPARE(host.writeCount(), 1);
        QVERIFY(page.exists());
        QCOMPARE(host.file(u"Nowa strona.md"_s), u"- Pierwsza myśl\n"_s);
    }

    void startNewPageWritesOnlyWhenChanged()
    {
        // A day without a note starts from the journal template, filled for that day; the note is
        // created by the first change, never by looking at the day or starting to write.
        FakeEditorHost host;
        host.setFile(u"templates/journal.md"_s, u"## {{date}} — {{weekday}}\n- \n"_s);
        host.setFile(u"templates/bez punktu.md"_s, u"## Plan dnia"_s);
        PageController page;
        page.setNow(Now);
        page.setSaveDelay(0);
        page.setHost(&host);
        const QString day = u"journals/2026-10-13.md"_s;
        page.setPath(day);
        QVERIFY(!page.exists());
        const int row = page.startNewPage(u"templates/journal.md"_s);
        QCOMPARE(page.text(), u"## 2026-10-13 — wtorek\n- \n"_s);
        QCOMPARE(row, 1);
        QVERIFY(!page.dirty());
        QVERIFY(!page.canUndo());
        QVERIFY(page.save());
        QCOMPARE(host.writeCount(), 0); // looked at, started, left: nothing written
        page.startEditing(row);
        page.setEditText(row, u"Kupić żółty ser"_s, 15);
        QCOMPARE(host.writeCount(), 1);
        QVERIFY(page.exists());
        QCOMPARE(host.file(day), u"## 2026-10-13 — wtorek\n- Kupić żółty ser\n"_s);
        // Undo goes back to the template (the starting text), not to an empty page.
        page.undo();
        QCOMPARE(page.text(), u"## 2026-10-13 — wtorek\n- \n"_s);
        page.redo();
        QCOMPARE(host.file(day), u"## 2026-10-13 — wtorek\n- Kupić żółty ser\n"_s);
        const int writes = host.writeCount();

        // A template without an empty bullet gets one to type into; leaving writes nothing.
        page.setPath(u"journals/2026-10-12.md"_s);
        const int r2 = page.startNewPage(u"templates/bez punktu.md"_s);
        QVERIFY(r2 >= 1);
        QVERIFY(page.text().startsWith(u"## Plan dnia"_s));
        QCOMPARE(page.editingRow(), -1);
        QVERIFY(!page.dirty());
        page.setPath(u"journals/2026-10-11.md"_s);
        QVERIFY(!host.files().contains(u"journals/2026-10-12.md"_s));
        QCOMPARE(host.writeCount(), writes);

        // No template (or a missing one): an empty bullet, as for any new page.
        QCOMPARE(page.startNewPage(u"templates/nie ma.md"_s), 0);
        QCOMPARE(page.startNewPage(QString()), 0);
        QCOMPARE(page.blocks()->count(), 1);
        // A page that exists keeps its text: the empty block goes at its end.
        page.setPath(day);
        QCOMPARE(page.startNewPage(u"templates/journal.md"_s), 2);
        QVERIFY(page.text().startsWith(u"## 2026-10-13 — wtorek\n- Kupić żółty ser\n"_s));
    }

    void startNewPageFollowsANoteCreatedElsewhere()
    {
        // Quick capture writes today's note while its day shows the template, untouched: the
        // editor takes the new file (it exists now) instead of the template.
        FakeEditorHost host;
        host.setFile(u"templates/journal.md"_s, u"## Plan\n- \n"_s);
        PageController page;
        page.setNow(Now);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(u"journals/2026-10-15.md"_s);
        page.startNewPage(u"templates/journal.md"_s);
        QSignalSpy exists(&page, &PageController::existsChanged);
        host.externalWrite(u"journals/2026-10-15.md"_s, u"- Kupić mleko\n"_s);
        QCOMPARE(page.text(), u"- Kupić mleko\n"_s);
        QVERIFY(page.exists());
        QCOMPARE(exists.size(), 1);
        QVERIFY(!page.dirty());
    }

    void saveOnDestructionAndPathChange()
    {
        FakeEditorHost host;
        host.setFile(Page, u"- a\n"_s);
        host.setFile(u"B.md"_s, u"- b\n"_s);
        {
            PageController page;
            page.setSaveDelay(10000);
            page.setHost(&host);
            page.setPath(Page);
            page.setEditText(0, u"zmiana"_s, 6);
            QCOMPARE(host.file(Page), u"- a\n"_s);
            page.setPath(u"B.md"_s);
            QCOMPARE(host.file(Page), u"- zmiana\n"_s);
            QCOMPARE(page.text(), u"- b\n"_s);
            page.setEditText(0, u"bb"_s, 2);
        }
        QCOMPARE(host.file(u"B.md"_s), u"- bb\n"_s);
    }

    // --- external changes ---------------------------------------------------------------------------

    void externalChangeReloadsWhenClean()
    {
        Fixture f(u"- a\n- b\n- c\n"_s);
        QSignalSpy resets(f.model(), &QAbstractItemModel::modelReset);
        QSignalSpy changed(&f.page, &PageController::externallyChanged);
        f.page.startEditing(2, 1);
        f.host.externalWrite(Page, u"- a\n- b zmienione przez Syncthing\n- c\n"_s);
        QCOMPARE(f.text(), u"- a\n- b zmienione przez Syncthing\n- c\n"_s);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(f.page.editingRow(), 2);
        QVERIFY(!f.page.dirty());
        QVERIFY(!f.page.canUndo());
        // Our own write coming back is ignored.
        f.page.setEditText(2, u"c!"_s, 2);
        f.page.save();
        f.host.externalWrite(Page, f.host.file(Page));
        QCOMPARE(changed.size(), 1);
    }

    void externalChangeMergesWhenDirty()
    {
        Fixture f(u"- a\n- b\n- c\n"_s, 10000);
        f.page.setMergeFunction([](const QString &base, const QString &local, const QString &remote) {
            // Test merge: the remote added a line at the end, the local changed the first line.
            Q_UNUSED(base);
            if (remote == u"- a\n- b\n- c\n- d\n"_s)
                return MergeOutcome{local + u"- d\n"_s, true};
            return MergeOutcome{local, false};
        });
        f.page.setEditText(0, u"a lokalnie"_s, 10);
        QVERIFY(f.page.dirty());
        f.host.externalWrite(Page, u"- a\n- b\n- c\n- d\n"_s);
        QCOMPARE(f.text(), u"- a lokalnie\n- b\n- c\n- d\n"_s);
        QVERIFY(f.page.dirty());
        QVERIFY(!f.page.conflict());
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- a lokalnie\n- b\n- c\n- d\n"_s);
        // A conflict keeps the local text and blocks saving until resolved.
        f.page.setEditText(1, u"b lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a zdalnie\n- b\n- c\n- d\n"_s);
        QVERIFY(f.page.conflict());
        QCOMPARE(f.page.conflictText(), u"- a zdalnie\n- b\n- c\n- d\n"_s);
        QCOMPARE(f.text(), u"- a lokalnie\n- b lokalnie\n- c\n- d\n"_s);
        QVERIFY(!f.page.save());
        QCOMPARE(f.host.file(Page), u"- a zdalnie\n- b\n- c\n- d\n"_s);
        f.page.resolveConflict(u"- a obie\n- b lokalnie\n- c\n- d\n"_s);
        QVERIFY(!f.page.conflict());
        QCOMPARE(f.host.file(Page), u"- a obie\n- b lokalnie\n- c\n- d\n"_s);
        QCOMPARE(f.text(), u"- a obie\n- b lokalnie\n- c\n- d\n"_s);
    }

    void defaultMergeIsThreeWay()
    {
        Fixture f(u"- a\n- b\n- c\n"_s, 10000);
        f.page.setEditText(0, u"a lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a\n- b\n- c zdalnie\n"_s);
#ifdef BRULION_TEST_NO_SYNC
        QVERIFY(f.page.conflict());
#else
        if (f.page.conflict())
            QSKIP("sync::merge3 is not part of this build");
        QCOMPARE(f.text(), u"- a lokalnie\n- b\n- c zdalnie\n"_s);
#endif
    }

    // The last block of today's journal being edited while an agent (brulion-cli journal append)
    // adds a block after it: both, no conflict banner (diff3 alone calls adjacent changes one).
    void editedLastBlockMergesWithAnAppendAfterIt()
    {
        Fixture f(u"- kawa\n- zadzwonić do hydraulika\n"_s, 10000);
        f.page.startEditing(1, -1);
        f.page.setEditText(1, u"zadzwonić do hydraulika — pilne"_s, 31);
        QCOMPARE(f.page.editingRow(), 1);
        f.host.externalWrite(Page, u"- kawa\n- zadzwonić do hydraulika\n- Agent: kupić uszczelki 15 mm\n"_s);
#ifdef BRULION_TEST_NO_SYNC
        QVERIFY(f.page.conflict());
#else
        QVERIFY(!f.page.conflict());
        QCOMPARE(f.text(), u"- kawa\n- zadzwonić do hydraulika — pilne\n- Agent: kupić uszczelki 15 mm\n"_s);
        QCOMPARE(f.page.editingRow(), 1); // the typing goes on in the same block
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- kawa\n- zadzwonić do hydraulika — pilne\n- Agent: kupić uszczelki 15 mm\n"_s);
#endif
    }

    // An agent (brulion-cli) or Syncthing writes the page while an edit waits for its debounced
    // save, and the host has not reported that write yet: the save never overwrites it.
    void saveKeepsAnUnreportedExternalAppend()
    {
        Fixture f(u"- a\n- b\n"_s, 10000);
        f.page.setMergeFunction([](const QString &base, const QString &local, const QString &remote) {
            // Test merge: the remote appended a line, the local edited the first one.
            if (remote == base + u"- od agenta: żółw\n"_s)
                return MergeOutcome{local + u"- od agenta: żółw\n"_s, true};
            return MergeOutcome{local, false};
        });
        f.page.setEditText(0, u"a lokalnie"_s, 10);
        QVERIFY(f.page.dirty());
        f.host.setFile(Page, u"- a\n- b\n- od agenta: żółw\n"_s); // no signal: not reported yet
        QSignalSpy merged(&f.page, &PageController::externallyChanged);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.casRefusals(), 1);
        QCOMPARE(merged.size(), 1);
        QCOMPARE(f.host.file(Page), u"- a lokalnie\n- b\n- od agenta: żółw\n"_s);
        QCOMPARE(f.text(), f.host.file(Page));
        QVERIFY(!f.page.dirty());
        // The file reported afterwards changes nothing.
        f.host.externalWrite(Page, f.host.file(Page));
        QCOMPARE(merged.size(), 1);
    }

    void saveWithAnUnreportedConflictWritesNothing()
    {
        Fixture f(u"- a\n"_s, 10000);
        f.page.setMergeFunction(
                [](const QString &, const QString &local, const QString &) { return MergeOutcome{local, false}; });
        f.page.setEditText(0, u"lokalnie"_s, 8);
        f.host.setFile(Page, u"- zdalnie\n"_s);
        QVERIFY(!f.page.save());
        QVERIFY(f.page.conflict());
        QCOMPARE(f.host.file(Page), u"- zdalnie\n"_s);
        QCOMPARE(f.page.conflictText(), u"- zdalnie\n"_s);
        // The user decides; then it is saved.
        f.page.keepLocal();
        QCOMPARE(f.host.file(Page), u"- lokalnie\n"_s);
    }

    // The page goes (another page, another host, the editor closing, the app sent to the
    // background) and its last save finds another program's change nobody has reported yet, one
    // that does not merge: the local text goes to a conflict copy — never lost, never written
    // over the other version.
    void flushWithAnUnreportedConflictKeepsTheLocalText_data()
    {
        QTest::addColumn<QString>("how");
        QTest::newRow("another page") << u"setPath"_s;
        QTest::newRow("destroyed") << u"destroy"_s;
        QTest::newRow("another host") << u"setHost"_s;
        QTest::newRow("background") << u"background"_s;
    }
    void flushWithAnUnreportedConflictKeepsTheLocalText()
    {
        QFETCH(QString, how);
        FakeEditorHost host;
        FakeEditorHost other;
        host.setFile(Page, u"- a\n"_s);
        host.setFile(u"Inna.md"_s, u"- inna\n"_s);
        auto page = std::make_unique<PageController>();
        page->setSaveDelay(10000);
        page->setMergeFunction(
                [](const QString &, const QString &local, const QString &) { return MergeOutcome{local, false}; });
        page->setHost(&host);
        page->setPath(Page);
        page->setEditText(0, u"LOKALNIE ważny tekst"_s, 20);
        QVERIFY(page->dirty());
        host.setFile(Page, u"- zdalnie\n"_s); // brulion-cli or Syncthing, not reported yet
        if (how == u"setPath")
            page->setPath(u"Inna.md"_s);
        else if (how == u"destroy")
            page.reset();
        else if (how == u"setHost")
            page->setHost(&other);
        else
            Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
        QCOMPARE(host.file(Page), u"- zdalnie\n"_s);
        QStringList copies;
        for (const QString &p : host.files()) {
            if (p.contains(u".conflict-"_s))
                copies.append(p);
        }
        QCOMPARE(copies.size(), 1);
        QCOMPARE(host.file(copies.first()), u"- LOKALNIE ważny tekst\n"_s);
        QVERIFY(other.files().isEmpty());
    }

    void saveOfACleanPageAfterAnUnreportedChangeReloads()
    {
        // Typed back to the saved text while the file changed: nothing to save, nothing written
        // (the other version arrives with the host's report, as for any clean page).
        Fixture f(u"- a\n"_s, 10000);
        f.page.setEditText(0, u"a2"_s, 2);
        f.host.setFile(Page, u"- a\n- b z Syncthinga\n"_s);
        f.page.setEditText(0, u"a"_s, 1); // as saved: clean again
        QVERIFY(!f.page.dirty());
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- a\n- b z Syncthinga\n"_s);
        QCOMPARE(f.host.casRefusals(), 0);
    }

    void conflictCopyOnDestruction()
    {
        FakeEditorHost host;
        host.setFile(Page, u"- a\n"_s);
        {
            PageController page;
            page.setSaveDelay(10000);
            page.setMergeFunction([](const QString &, const QString &local, const QString &) {
                return MergeOutcome{local, false};
            });
            page.setHost(&host);
            page.setPath(Page);
            page.setEditText(0, u"lokalnie"_s, 8);
            host.externalWrite(Page, u"- zdalnie\n"_s);
            QVERIFY(page.conflict());
        }
        QCOMPARE(host.file(Page), u"- zdalnie\n"_s);
        bool copied = false;
        for (const QString &p : host.files()) {
            if (p.contains(u".conflict-"_s)) {
                QCOMPARE(host.file(p), u"- lokalnie\n"_s);
                copied = true;
            }
        }
        QVERIFY(copied);
    }

    void conflictFollowsLaterChanges()
    {
        // A conflict is not frozen: when the other side changes again and the change now merges
        // (or equals the local text, or goes back to the base), the conflict is over — and
        // "Use the other version" never writes an outdated remote text over a newer file.
        Fixture f(u"- a\n- b\n- c\n"_s, 10000);
        f.page.setMergeFunction([](const QString &base, const QString &local, const QString &remote) {
            Q_UNUSED(base);
            if (remote.endsWith(u"- d\n"_s) && remote.startsWith(u"- a\n"_s))
                return MergeOutcome{local + u"- d\n"_s, true};
            return MergeOutcome{local, false};
        });
        f.page.setEditText(0, u"a lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a zdalnie\n- b\n- c\n"_s);
        QVERIFY(f.page.conflict());
        // The other device reverts its change and adds a line: merges cleanly now.
        f.host.externalWrite(Page, u"- a\n- b\n- c\n- d\n"_s);
        QVERIFY(!f.page.conflict());
        QCOMPARE(f.text(), u"- a lokalnie\n- b\n- c\n- d\n"_s);
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- a lokalnie\n- b\n- c\n- d\n"_s);

        // Conflict, then the other side writes exactly our text: nothing to resolve.
        f.page.setEditText(1, u"b lokalnie"_s, 10);
        f.host.externalWrite(Page, u"- a lokalnie\n- b zdalnie\n- c\n- d\n"_s);
        QVERIFY(f.page.conflict());
        f.host.externalWrite(Page, u"- a lokalnie\n- b lokalnie\n- c\n- d\n"_s);
        QVERIFY(!f.page.conflict());
        QVERIFY(!f.page.dirty());

        // Conflict, then the other side goes back to the common base: ours can be saved.
        f.page.setEditText(2, u"c lokalnie"_s, 10);
        const QString base = f.host.file(Page);
        f.host.externalWrite(Page, u"- inne\n"_s);
        QVERIFY(f.page.conflict());
        f.host.externalWrite(Page, base);
        QVERIFY(!f.page.conflict());
        QVERIFY(f.page.save());
        QCOMPARE(f.host.file(Page), u"- a lokalnie\n- b lokalnie\n- c lokalnie\n- d\n"_s);

        // A second conflicting change replaces the other version shown in the merge view.
        f.page.setEditText(0, u"a jeszcze raz"_s, 13);
        f.host.externalWrite(Page, u"- x\n"_s);
        f.host.externalWrite(Page, u"- y\n"_s);
        QVERIFY(f.page.conflict());
        QCOMPARE(f.page.conflictText(), u"- y\n"_s);
        f.page.takeRemote();
        QCOMPARE(f.host.file(Page), u"- y\n"_s);
        QCOMPARE(f.text(), u"- y\n"_s);
        QVERIFY(!f.page.dirty());

        // The other side emptied the page and we take that version: the file stays empty.
        f.page.setEditText(0, u"y lokalnie"_s, 10);
        f.host.externalWrite(Page, QString());
        QVERIFY(f.page.conflict());
        f.page.takeRemote();
        QCOMPARE(f.host.file(Page), QString());
        QCOMPARE(f.text(), QString());
        QVERIFY(!f.page.dirty());
    }

    void rawHtmlIsNeverRendered_data()
    {
        QTest::addColumn<QString>("content");
        const QString evil = u"<img src=\"https://tracker.example/p.png\"> <b>pogrubione</b> <a href=\"javascript:alert(1)\">x</a>"_s;
        QTest::newRow("list item") << u"- "_s + evil + u"\n"_s;
        QTest::newRow("task") << u"- [ ] "_s + evil + u"\n"_s;
        QTest::newRow("paragraph") << evil + u"\n"_s;
        QTest::newRow("heading") << u"# "_s + evil + u"\n"_s;
        QTest::newRow("heading item") << u"- ## "_s + evil + u"\n  druga linia "_s + evil + u"\n"_s;
        QTest::newRow("quote") << u"> "_s + evil + u"\n"_s;
        QTest::newRow("callout") << u"> [!warning] "_s + evil + u"\n> "_s + evil + u"\n"_s;
        QTest::newRow("html block") << u"<div>\n"_s + evil + u"\n</div>\n"_s;
        QTest::newRow("link text") << u"- [<img src=x onerror=alert(1)>](https://example.com) [[<b>strona</b>]]\n"_s;
    }

    void rawHtmlIsNeverRendered()
    {
        // Notes come from anywhere: markup in a note is shown as text, never interpreted by Text.
        QFETCH(QString, content);
        Fixture f(content);
        for (int r = 0; r < f.count(); ++r) {
            const QString html = f.role(r, "html").toString();
            QVERIFY2(!html.contains(u"<img"_s, Qt::CaseInsensitive), qPrintable(html));
            QVERIFY2(!html.contains(u"<b>pogrubione"_s), qPrintable(html));
            QVERIFY2(!html.contains(u"<div"_s, Qt::CaseInsensitive), qPrintable(html));
            QVERIFY2(!html.contains(u"javascript:"_s, Qt::CaseInsensitive) || html.contains(u"&lt;a href=&quot;javascript:"_s),
                     qPrintable(html));
            QVERIFY2(!html.contains(u"onerror=alert"_s) || html.contains(u"&lt;img"_s), qPrintable(html));
        }
    }

    void cursorsNeverSplitSurrogatePairs()
    {
        // UTF-16: a caret position between the two halves of an emoji (a stale position after the
        // text changed, a computed one) must never cut it into invalid text.
        Fixture f(u"- a😀b\n- 📅 jutro\n"_s);
        const QString edit = f.edit(0);
        QCOMPARE(edit.size(), 4); // a, high, low, b
        f.page.split(0, 2); // inside the pair
        QVERIFY2(f.text() == u"- a😀\n- b\n- 📅 jutro\n"_s || f.text() == u"- a\n- 😀b\n- 📅 jutro\n"_s,
                 qPrintable(f.text()));
        f.page.undo();
        QCOMPARE(f.text(), u"- a😀b\n- 📅 jutro\n"_s);
        f.page.replaceRange(0, 2, 2, u"X"_s);
        QVERIFY2(f.text().startsWith(u"- a😀X"_s) || f.text().startsWith(u"- aX😀"_s), qPrintable(f.text()));
        f.page.undo();
        f.page.insertToken(1, 1, u"⏰ 2026-10-16 09:00"_s);
        QVERIFY2(f.text().contains(u"📅"_s), qPrintable(f.text()));
        f.page.undo();
        const QVariantMap pasted = f.page.pasteText(0, 2, 2, u"x\ny"_s);
        Q_UNUSED(pasted);
        QVERIFY2(f.text().contains(u"😀"_s), qPrintable(f.text()));
        f.page.undo();
        f.page.startEditing(0, 2);
        QVERIFY(f.page.cursorPosition() == 1 || f.page.cursorPosition() == 3);
        // Every row's text is valid UTF-16.
        for (int r = 0; r < f.count(); ++r) {
            const QString t = f.edit(r);
            for (qsizetype i = 0; i < t.size(); ++i) {
                if (t[i].isHighSurrogate())
                    QVERIFY(i + 1 < t.size() && t[i + 1].isLowSurrogate());
                if (t[i].isLowSurrogate())
                    QVERIFY(i > 0 && t[i - 1].isHighSurrogate());
            }
        }
    }

    void trackedBlocksFollowChanges()
    {
        // A dialog opened for a block acts on that block even when the page changed meanwhile.
        Fixture f(u"- a\n- b ąę\n- c\n"_s, 10000);
        const int handle = f.page.trackBlock(1);
        QVERIFY(handle > 0);
        QCOMPARE(f.page.trackedRow(handle), 1);
        f.host.externalWrite(Page, u"- nowy z telefonu\n- a\n- b ąę\n- c\n"_s); // reload (not dirty)
        QCOMPARE(f.page.trackedRow(handle), 2);
        f.page.split(0, -1);
        QCOMPARE(f.page.trackedRow(handle), 3);
        f.page.undo();
        QCOMPARE(f.page.trackedRow(handle), 2);
        // Undo and redo of a change to the tracked block keep it: the same block.
        f.page.setEditText(2, u"b ąę zmienione"_s, 14);
        QCOMPARE(f.page.trackedRow(handle), 2);
        f.page.undo();
        QCOMPARE(f.page.trackedRow(handle), 2);
        QCOMPARE(f.edit(2), u"b ąę"_s);
        f.page.redo();
        QCOMPARE(f.page.trackedRow(handle), 2);
        f.page.undo();
        QVERIFY(f.page.save());
        f.host.externalWrite(Page, u"- nowy z telefonu\n- a\n- c\n"_s); // the block is gone
        QCOMPARE(f.page.trackedRow(handle), -1);
        const int other = f.page.trackBlock(2);
        QCOMPARE(f.page.trackedRow(other), 2);
        f.page.setPath(u"Inna.md"_s);
        QCOMPARE(f.page.trackedRow(other), -1);
        f.page.untrack(other);
        QCOMPARE(f.page.trackedRow(other), -1);
        QCOMPARE(f.page.trackBlock(99), 0);
    }

    void conflictCopyWhenSuspended()
    {
        // Android may kill a suspended app without running destructors: an unresolved conflict
        // keeps the local text only in memory, so suspending writes it to a conflict copy (one
        // file per conflict, updated, never a new one per suspend).
        FakeEditorHost host;
        host.setFile(Page, u"- a\n"_s);
        PageController page;
        page.setSaveDelay(10000);
        page.setMergeFunction([](const QString &, const QString &local, const QString &) {
            return MergeOutcome{local, false};
        });
        page.setHost(&host);
        page.setPath(Page);
        page.setEditText(0, u"lokalnie"_s, 8);
        host.externalWrite(Page, u"- zdalnie\n"_s);
        QVERIFY(page.conflict());
        const auto copies = [&] {
            QStringList out;
            for (const QString &p : host.files()) {
                if (p.contains(u".conflict-"_s))
                    out.append(p);
            }
            return out;
        };
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
        QCOMPARE(copies().size(), 1);
        QCOMPARE(host.file(copies().first()), u"- lokalnie\n"_s);
        QCOMPARE(host.file(Page), u"- zdalnie\n"_s); // the other version is not overwritten
        page.setEditText(0, u"lokalnie 2"_s, 10);
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationSuspended);
        Q_EMIT qGuiApp->applicationStateChanged(Qt::ApplicationActive);
        QCOMPARE(copies().size(), 1);
        QCOMPARE(host.file(copies().first()), u"- lokalnie 2\n"_s);
        // Resolving ends the conflict; a later conflict gets a copy of its own.
        page.keepLocal();
        QCOMPARE(host.file(Page), u"- lokalnie 2\n"_s);
    }

    void followsRenames()
    {
        // The page is renamed (shell dialog, sync) while it has unsaved edits: they go to the new
        // path; the old path is never written again (it would come back as a duplicate page).
        Fixture f(u"- a\n- b\n"_s, 10000);
        f.page.startEditing(1, 1);
        f.page.setEditText(1, u"b zmienione"_s, 11);
        QVERIFY(f.page.dirty());
        const QString renamed = u"Notatki/Nowa nazwa.md"_s;
        f.host.setFile(renamed, f.host.file(Page)); // what the vault's rename did
        f.host.removeFile(Page);
        f.page.followRename(Page, renamed);
        QCOMPARE(f.page.path(), renamed);
        QCOMPARE(f.page.title(), u"Nowa nazwa"_s);
        QCOMPARE(f.page.editingRow(), 1); // still editing, undo kept
        QVERIFY(f.page.canUndo());
        f.page.setPath(renamed); // the route follows too: no effect
        QVERIFY(f.page.save());
        QVERIFY(!f.host.files().contains(Page));
        QCOMPARE(f.host.file(renamed), u"- a\n- b zmienione\n"_s);
        // Other renames are not ours.
        f.page.followRename(u"Inna.md"_s, u"Inna2.md"_s);
        QCOMPARE(f.page.path(), renamed);
        // Renamed while clean, and the new file differs (sync): it is loaded.
        f.host.setFile(u"C.md"_s, u"- a\n- b zmienione\n- c z telefonu\n"_s);
        f.host.removeFile(renamed);
        f.page.followRename(renamed, u"C.md"_s);
        QCOMPARE(f.text(), u"- a\n- b zmienione\n- c z telefonu\n"_s);
        QVERIFY(!f.page.dirty());
    }

    void tellsTheHostWhichPageItShows()
    {
        // The host watches the pages editors show (Workspace: in-place rewrites by other
        // programs); every page reported as shown is reported as closed again.
        struct RecordingHost : FakeEditorHost {
            QStringList events;
            void editorOpened(const QString &path) override { events.append(u"+"_s + path); }
            void editorClosed(const QString &path) override { events.append(u"-"_s + path); }
        };
        RecordingHost host;
        RecordingHost other;
        host.setFile(Page, u"- a\n"_s);
        {
            PageController page;
            page.setHost(&host); // no page yet
            QCOMPARE(host.events, QStringList());
            page.setPath(Page);
            QCOMPARE(host.events, QStringList{u"+"_s + Page});
            page.setPath(Page);
            page.followRename(u"Inna.md"_s, u"Inna2.md"_s); // not ours
            QCOMPARE(host.events.size(), 1);
            host.events.clear();
            page.followRename(Page, u"Notatki/Zażółć.md"_s);
            QCOMPARE(host.events, (QStringList{u"-"_s + Page, u"+Notatki/Zażółć.md"_s}));
            page.setPath(u"B.md"_s);
            QCOMPARE(host.events.mid(2), (QStringList{u"-Notatki/Zażółć.md"_s, u"+B.md"_s}));
            host.events.clear();
            page.setHost(&other);
            QCOMPARE(host.events, QStringList{u"-B.md"_s});
            QCOMPARE(other.events, QStringList{u"+B.md"_s});
            page.setPath(QString());
            page.setPath(u"C.md"_s);
            page.setHost(nullptr);
            page.setHost(&other);
            QCOMPARE(other.events.mid(1), (QStringList{u"-B.md"_s, u"+C.md"_s, u"-C.md"_s, u"+C.md"_s}));
            other.events.clear();
        }
        QCOMPARE(other.events, QStringList{u"-C.md"_s}); // destroyed
        QCOMPARE(host.events, QStringList{u"-B.md"_s});

        // A host destroyed first is not told anything.
        auto *gone = new RecordingHost;
        PageController page;
        page.setHost(gone);
        page.setPath(Page);
        delete gone;
        page.setPath(u"D.md"_s);
        page.setHost(&host);
        QCOMPARE(host.events.last(), u"+D.md"_s);
    }

    void removedFileKeepsText()
    {
        Fixture f(u"- a\n"_s);
        QSignalSpy removed(&f.page, &PageController::fileRemoved);
        f.host.removeFile(Page);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(f.text(), u"- a\n"_s);
    }

    // --- source mode ----------------------------------------------------------------------------------

    void sourceMode()
    {
        Fixture f(u"- a\n- b\n"_s, 0);
        f.page.startEditing(0, 0);
        f.page.setSourceMode(true);
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.page.sourceText(), u"- a\n- b\n"_s);
        f.page.setSourceText(u"- a\n- b\n- c ąę\n"_s);
        QCOMPARE(f.host.file(Page), u"- a\n- b\n- c ąę\n"_s); // saved while in source mode
        QCOMPARE(f.count(), 2); // blocks follow when leaving
        f.page.setSourceMode(false);
        QCOMPARE(f.count(), 3);
        QCOMPARE(f.text(), u"- a\n- b\n- c ąę\n"_s);
        QVERIFY(!f.page.dirty());
        f.page.undo();
        QCOMPARE(f.text(), u"- a\n- b\n"_s);
    }

    void sourceModeKeepsLineEndings()
    {
        // The source editor's TextEdit reports "\n" for every line break: an edit there must not
        // rewrite a CRLF file (or lone "\r" lines) into LF.
        const QString content = u"---\r\ntytuł: Zażółć\r\n---\r\n- a\r\n- b\r\n\r\nKoniec\r\n"_s;
        Fixture f(content, 0);
        f.page.setSourceMode(true);
        QString shown = content;
        shown.replace(u"\r\n"_s, u"\n"_s);
        // What a TextEdit hands back when nothing changed: the file is untouched.
        f.page.setSourceText(shown);
        QCOMPARE(f.page.sourceText(), content);
        QVERIFY(!f.page.dirty());
        // A typed character and a new line.
        shown.replace(u"- b\n"_s, u"- bę\n- nowa\n"_s);
        f.page.setSourceText(shown);
        const QString expected = u"---\r\ntytuł: Zażółć\r\n---\r\n- a\r\n- bę\r\n- nowa\r\n\r\nKoniec\r\n"_s;
        QCOMPARE(f.page.sourceText(), expected);
        QCOMPARE(f.host.file(Page), expected);
        f.page.setSourceMode(false);
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.count(), 5);
    }

    // --- large pages ------------------------------------------------------------------------------

    void undoStoresChangesNotPages()
    {
        // Every undo step used to keep two copies of the whole page: on a long page (an imported
        // journal) a few hundred commands held hundreds of megabytes. Steps store the change.
        QString content;
        for (int i = 0; i < 3000; ++i)
            content += u"- Blok numer %1 z tekstem zażółć gęślą jaźń\n"_s.arg(i);
        Fixture f(content, 100000, Fixture::NoModelTester{});
        for (int k = 0; k < 60; ++k) {
            f.page.startEditing(k * 10, -1);
            f.page.split(k * 10, -1);
            f.page.setEditText(k * 10 + 1, u"nowy %1"_s.arg(k), 6);
        }
        QVERIFY(f.page.canUndo());
        QVERIFY2(f.page.undoHistorySize() < content.size() / 4,
                 qPrintable(u"undo history holds %1 characters for a %2 character page"_s.arg(f.page.undoHistorySize()).arg(content.size())));
        while (f.page.canUndo())
            f.page.undo();
        QCOMPARE(f.text(), content);
        while (f.page.canRedo())
            f.page.redo();
        QVERIFY(f.text().contains(u"- nowy 59\n"_s));
        QCOMPARE(f.count(), 3000 + 60);
    }

    void largePageTouchesOneRow()
    {
        // 5000 blocks (with calc and table blocks and inline calculations that depend on the
        // page): typing in one block changes only that row — no resets, no inserts/removes, no
        // dataChanged for other rows — and stays fast.
        QString content = u"# Dziennik budowy\n\n"_s;
        for (int i = 0; i < 5000; ++i) {
            if (i % 1000 == 500)
                content += u"- ```calc\n  cena = %1 zł\n  cena * 2\n  ```\n"_s.arg(i);
            else if (i % 1000 == 700)
                content += u"- Koszt `= cena * 3` łącznie #budowa\n"_s;
            else
                content += u"- Wpis %1: zażółć gęślą jaźń [[Strona %2]] #tag%3 📅 2026-10-%4\n"_s.arg(i).arg(i % 50).arg(i % 7).arg(10 + i % 18);
        }
        QElapsedTimer load;
        load.start();
        Fixture f(content, 100000, Fixture::NoModelTester{});
        const qint64 loadMs = load.elapsed();
        const int row = 2500;
        QVERIFY(f.edit(row).startsWith(u"Wpis"_s));
        f.page.startEditing(row, -1);
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        QSignalSpy reset(f.model(), &QAbstractItemModel::modelReset);
        QSignalSpy inserted(f.model(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(f.model(), &QAbstractItemModel::rowsRemoved);
        QElapsedTimer typing;
        typing.start();
        QString text = f.edit(row);
        for (int k = 0; k < 50; ++k) {
            text += QChar(u"ąćęłńóśźż"_s.at(k % 9));
            f.page.setEditText(row, text, int(text.size()));
        }
        const qint64 typingMs = typing.elapsed();
        QCOMPARE(reset.size(), 0);
        QCOMPARE(inserted.size(), 0);
        QCOMPARE(removed.size(), 0);
        for (const QList<QVariant> &args : std::as_const(changed)) {
            const QModelIndex from = args.at(0).toModelIndex();
            const QModelIndex to = args.at(1).toModelIndex();
            QCOMPARE(from.row(), row);
            QCOMPARE(to.row(), row);
        }
        QElapsedTimer ops;
        ops.start();
        f.page.split(row, -1);
        f.page.indent(row + 1);
        const QVariant edited = f.role(row, "uid");
        QCOMPARE(inserted.size(), 1); // the block Enter made
        changed.clear();
        inserted.clear();
        f.page.undo();
        f.page.undo();
        // Undo keeps the rows of the blocks it changes: only the block Enter made goes.
        QCOMPARE(inserted.size(), 0);
        QCOMPARE(removed.size(), 1);
        QCOMPARE(removed.first().at(1).toInt(), row + 1);
        QCOMPARE(f.role(row, "uid"), edited);
        // (The rows below learn their new file lines, the calculations their results.)
        const QList<int> derived{f.model()->roleNames().key("line"), f.model()->roleNames().key("calcResults"),
                                 f.model()->roleNames().key("html")};
        for (const QList<QVariant> &args : std::as_const(changed)) {
            const QList<int> roles = args.at(2).value<QList<int>>();
            const bool ownData = std::any_of(roles.begin(), roles.end(), [&](int r) { return !derived.contains(r); });
            if (roles.isEmpty() || ownData)
                QVERIFY(args.at(0).toModelIndex().row() >= row && args.at(1).toModelIndex().row() <= row + 1);
        }
        f.page.undo();
        const qint64 opsMs = ops.elapsed();
        QCOMPARE(f.text(), content);
        QCOMPARE(reset.size(), 0);
        qInfo("5000 blocks: load %lld ms, 50 keystrokes %lld ms, split+indent+3 undo %lld ms", loadMs, typingMs, opsMs);
        QVERIFY2(typingMs < 50 * 40 * BRULION_TEST_SLOWDOWN, "typing on a 5000-block page is too slow");

        // Undo and redo cost about what the command costs (they used to read the whole page
        // again): medians of five, compared as a ratio (machines and sanitizers alike).
        QList<qint64> splits;
        QList<qint64> undos;
        QList<qint64> redos;
        QElapsedTimer t;
        for (int k = 0; k < 5; ++k) {
            t.start();
            f.page.split(row, -1);
            splits.append(t.nsecsElapsed());
            t.start();
            f.page.undo();
            undos.append(t.nsecsElapsed());
            t.start();
            f.page.redo();
            redos.append(t.nsecsElapsed());
            f.page.undo();
        }
        QCOMPARE(f.text(), content);
        const auto median = [](QList<qint64> v) {
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        const qint64 enter = median(splits);
        qInfo("5000 blocks: Enter %.2f ms, undo %.2f ms, redo %.2f ms (medians of 5)", enter / 1e6, median(undos) / 1e6,
              median(redos) / 1e6);
        QVERIFY2(median(undos) <= 3 * enter + 2'000'000, "undo costs much more than the command");
        QVERIFY2(median(redos) <= 3 * enter + 2'000'000, "redo costs much more than the command");
        QCOMPARE(reset.size(), 0);
    }

    void typingAfterAFarChangeIsAStepOfItsOwn()
    {
        // A keystroke that also changed a block far away (typing at the top of a page renumbers a
        // loose ordered list at its end, as it did before) is a step of its own: typing on merges
        // into the next one, so a keystroke costs its block, not the blocks in between.
        QString filler;
        for (int i = 0; i < 300; ++i)
            filler += u"Akapit %1 zażółć\n\n"_s.arg(i);
        const QString content = u"Początek\n\n"_s + filler + u"1. jeden\n2. dwa\n\n1. trzy\n"_s;
        Fixture f(content, 100000, Fixture::NoModelTester{});
        f.page.startEditing(0, -1);
        f.page.setEditText(0, u"Początekx"_s, 9);
        const QString first = f.text();
        QVERIFY(first.endsWith(u"\n\n3. trzy\n"_s));
        f.page.setEditText(0, u"Początekxy"_s, 10);
        f.page.setEditText(0, u"Początekxyż"_s, 11);
        QVERIFY(f.page.undoHistorySize() < 100);
        f.page.undo();
        QCOMPARE(f.text(), first);
        f.page.undo();
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        f.page.redo();
        f.page.redo();
        QVERIFY(f.text().startsWith(u"Początekxyż\n"_s));
        QVERIFY(f.text().endsWith(u"\n\n3. trzy\n"_s));
    }

    void pasteIsASmallUndoStep()
    {
        // Markdown pasted into a long page (a whole-text edit): the step holds the pasted blocks,
        // the rows around them stay.
        QString content;
        for (int i = 0; i < 3000; ++i)
            content += u"- Blok numer %1 zażółć gęślą jaźń\n"_s.arg(i);
        Fixture f(content, 100000, Fixture::NoModelTester{});
        QSignalSpy removed(f.model(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy inserted(f.model(), &QAbstractItemModel::rowsInserted);
        const QVariant kept = f.role(2999, "uid");
        f.page.insertMarkdown(1500, u"- wklejony ą\n- drugi ę\n- trzeci ł\n"_s);
        QCOMPARE(f.count(), 3003);
        QCOMPARE(removed.size(), 0);
        QCOMPARE(inserted.size(), 1);
        QCOMPARE(f.role(3002, "uid"), kept);
        QVERIFY2(f.page.undoHistorySize() < 1000, qPrintable(QString::number(f.page.undoHistorySize())));
        f.page.undo();
        QCOMPARE(f.text(), content);
        QCOMPARE(f.role(2999, "uid"), kept);
        QCOMPARE(removed.size(), 1);
        f.page.redo();
        QCOMPARE(f.count(), 3003);
        QVERIFY(f.text().contains(u"- Blok numer 1500 zażółć gęślą jaźń\n- wklejony ą\n"_s));
    }

    void externalReloadKeepsUidsOfKeptBlocks()
    {
        // Another device changed one block: the others stay the same blocks (rows, folds, tracked).
        Fixture f(u"- a\n\t- a1\n- b ąę\n- c\n"_s, 10000);
        const QVariant a = f.role(0, "uid");
        const QVariant c = f.role(3, "uid");
        const int handle = f.page.trackBlock(3);
        f.page.setFolded(0, true);
        QSignalSpy removed(f.model(), &QAbstractItemModel::rowsRemoved);
        f.host.externalWrite(Page, u"- a\n\t- a1\n- b zmienione na telefonie\n- c\n"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n- b zmienione na telefonie\n- c\n"_s);
        QCOMPARE(f.role(0, "uid"), a);
        QCOMPARE(f.role(3, "uid"), c);
        QCOMPARE(f.page.trackedRow(handle), 3);
        QVERIFY(f.role(0, "collapsed").toBool());
        QCOMPARE(removed.size(), 1); // the changed block only
        QCOMPARE(removed.first().at(1).toInt(), 2);
    }

    // Last: runs the application's event loop to quit it.
    void savesWhenTheApplicationQuits()
    {
        // A page still open when the app quits is saved even before QML tears it down.
        auto *host = new FakeEditorHost;
        host->setFile(Page, u"- a\n"_s);
        auto *page = new PageController;
        page->setSaveDelay(100000);
        page->setHost(host);
        page->setPath(Page);
        page->setEditText(0, u"zapisz mnie"_s, 11);
        QCOMPARE(host->file(Page), u"- a\n"_s);
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        QCoreApplication::exec();
        QCOMPARE(host->file(Page), u"- zapisz mnie\n"_s);
        delete page;
        delete host;
    }
};

QTEST_MAIN(TestPageController)
#include "tst_editor_controller.moc"
