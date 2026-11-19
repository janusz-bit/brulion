// The functions of `b` that read and change notes beyond a block's first line, and the helpers for
// dates, words and recurrence rules (docs/EXTENSIONS.md "Reading", "Writing", "Helpers"), on a
// real notebook (NotebookHost writing to disk, the index following each write): blocks and pages by
// a property, a query's pages, attachments, the revision, a page appended to, blocks inserted,
// removed, their checkbox and properties set — each refusing a stale or forged reference, every
// other byte of the note kept (line endings, a missing final line break) —, and the pure helpers.

#include "extensions/blockedit.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

class TestExtensionsApi : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tmp;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;

    QString dir() const { return m_tmp.filePath(u"v"_s); }
    QByteArray onDisk(const QString &path) const
    {
        QFile f(dir() + u'/' + path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
    }
    Result run(const QString &code) { return m_engine->evaluate(u"t"_s, code, u"test"_s); }
    QVariant value(const QString &code)
    {
        const Result r = run(code);
        if (!r.ok())
            qWarning("%s", qPrintable(r.error));
        return r.ok() ? r.value : QVariant(u"<error: %1>"_s.arg(r.error));
    }
    QString error(const QString &code)
    {
        const Result r = run(code);
        return r.ok() ? u"<no error>"_s : r.error;
    }

private Q_SLOTS:
    void init()
    {
        const QHash<QString, QString> files{
            {u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Bieganie #zdrowie\n  nawyk:: pn, śr, pt\n  start:: 2026-10-05\n- Zwykły punkt\n"_s},
            {u"Czytanie.md"_s, u"---\nhabit: 3 razy w tygodniu\n---\n# Czytanie\n"_s},
            {u"Lista.md"_s, u"- [ ] mleko\r\n  - pełne\r\n- [x] chleb ✅ 2026-11-01\r\n- DONE masło\r\n- ostatni"_s},
            {u"journals/2026_11_14.md"_s, u"- [x] Medytacja #nawyk ✅ 2026-11-14\n- [[Strony/Czytanie|Książka]] #habit #zdrowie 📅 2026-11-20\n"_s},
            {u"assets/zdjęcie.png"_s, u"png"_s},
        };
        QDir(dir()).removeRecursively();
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString file = dir() + u'/' + it.key();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(it.value().toUtf8());
        }
        m_vault = std::make_unique<Vault>(dir(), m_tmp.filePath(u"s"_s));
        QVERIFY(m_vault->open());
        VaultConfig config = m_vault->config();
        config.weekStartsOn = Qt::Sunday;
        m_index = std::make_unique<VaultIndex>(config);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 14), QTime(9, 5)); };
        options.updateIndex = true;
        options.uiLanguage = u"pl"_s;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_engine = std::make_unique<Engine>();
        QVERIFY(m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}));
        Extension e;
        e.id = u"t"_s;
        e.permissions = {u"write"_s};
        QVERIFY(m_engine->load(e).ok());
    }

    void blocksAndPagesByProperty()
    {
        // Blocks with one of the properties (frontmatter is the page's, not a block's).
        QVariantList blocks = value(u"return b.blocks({ prop = { \"habit\", \"NAWYK\" } })"_s).toList();
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0].toMap().value(u"plain"_s).toString(), u"Medytacja"_s);
        QCOMPARE(blocks[1].toMap().value(u"plain"_s).toString(), u"Bieganie"_s);
        QCOMPARE(blocks[1].toMap().value(u"props"_s).toMap().value(u"start"_s).toString(), u"2026-10-05"_s);
        // With a tag: both; with a page: that page's at any depth.
        QCOMPARE(value(u"return #b.blocks({ prop = \"nawyk\", tag = \"zdrowie\" })"_s).toInt(), 1);
        QCOMPARE(value(u"return #b.blocks({ prop = \"habit\", page = \"Lista\" })"_s).toInt(), 0);
        QCOMPARE(value(u"return #b.blocks({ page = \"Lista\" })"_s).toInt(), 4); // top level, as before
        QCOMPARE(value(u"return #b.blocks({ tag = \"zdrowie\", page = \"2026_11_14\" })"_s).toInt(), 1);
        // A task's plain words: no dates, no tags, links as their labels.
        QCOMPARE(value(u"return b.blocks({ tag = \"habit\" })[1].plain"_s).toString(), u"Książka"_s);
        QCOMPARE(value(u"return b.blocks({ tag = \"nawyk\" })[1].plain"_s).toString(), u"Medytacja"_s);
        QVERIFY(error(u"return b.blocks({ prop = 3 })"_s).contains(u"prop must be a text"_s));
        QVERIFY(error(u"return b.blocks({})"_s).contains(u"a page, a tag or a prop"_s));
        // Pages by a page property; a page's path.
        QCOMPARE(value(u"return b.pages({ prop = \"habit\" })"_s).toStringList(), QStringList{u"Czytanie"_s});
        QCOMPARE(value(u"return b.pages({ prop = \"habit\", tag = \"x\" })"_s).toStringList(), QStringList());
        QCOMPARE(value(u"return b.page(\"Czytanie\").path"_s).toString(), u"Czytanie.md"_s);
        QCOMPARE(value(u"return b.page(\"2026_11_14\").path"_s).toString(), u"journals/2026_11_14.md"_s);
    }

    void queryPagesAttachmentsRevision()
    {
        QCOMPARE(value(u"return b.query(\"type:tasks\", 2).total"_s).toInt(), 4);
        const QString first = value(u"return b.query(\"type:tasks sort:text\", 2)[\"items\"][2].text"_s).toString();
        QCOMPARE(value(u"return b.query(\"type:tasks sort:text\", 1, 1).items[1].text"_s).toString(), first);
        QCOMPARE(value(u"return #b.query(\"type:tasks\", 10, 3).items"_s).toInt(), 1);
        QCOMPARE(value(u"return #b.query(\"type:tasks\", 10, 40).items"_s).toInt(), 0);
        QCOMPARE(value(u"return b.query(\"type:tasks\", 10, 40).total"_s).toInt(), 4);
        QCOMPARE(value(u"return b.attachments()"_s).toStringList(), QStringList{u"assets/zdjęcie.png"_s});
        // The revision follows every write.
        const double before = value(u"return b.revision()"_s).toDouble();
        QCOMPARE(value(u"return b.revision()"_s).toDouble(), before);
        QVERIFY(run(u"b.appendToPage(\"Lista\", \"- nowy\")"_s).ok());
        QVERIFY(value(u"return b.revision()"_s).toDouble() != before);
    }

    void helpers()
    {
        QCOMPARE(value(u"return b.now()"_s).toString(), u"2026-11-14 09:05"_s);
        QCOMPARE(value(u"return b.addDays(\"2026-12-31\", 1)"_s).toString(), u"2027-01-01"_s);
        QCOMPARE(value(u"return b.addDays(\"2026-03-01\", -1)"_s).toString(), u"2026-02-28"_s);
        QCOMPARE(value(u"return b.addMonths(\"2026-01-31\", 1)"_s).toString(), u"2026-02-28"_s);
        QCOMPARE(value(u"return b.addMonths(\"2026-11-14\", -11)"_s).toString(), u"2025-12-14"_s);
        QVERIFY(error(u"return b.addDays(\"2026-02-30\", 1)"_s).contains(u"invalid date"_s));
        QVERIFY(error(u"return b.addDays(\"2026-02-03\", 1.5)"_s).contains(u"whole number"_s));
        QCOMPARE(value(u"return b.weekday(\"2026-11-14\")"_s).toInt(), 6);
        QCOMPARE(value(u"return b.weekStart()"_s).toInt(), 7);
        QCOMPARE(value(u"return b.weekdayName(3)"_s).toString(), u"środa"_s);
        QCOMPARE(value(u"return b.weekdayName(1, \"short\")"_s).toString(), u"pn"_s);
        QCOMPARE(value(u"return b.monthName(11)"_s).toString(), u"listopad"_s);
        QCOMPARE(value(u"return b.monthName(11, \"of\")"_s).toString(), u"listopada"_s);
        QVERIFY(error(u"return b.weekdayName(8)"_s).contains(u"from 1 to 7"_s));
        QCOMPARE(value(u"return b.lower(\"ŻÓŁW Łódź\")"_s).toString(), u"żółw łódź"_s);
        QCOMPARE(value(u"return b.plain(\"Czytać [[Książki/Diuna]] i [[A|bajki]] #wieczór\\ndruga\")"_s).toString(),
                 u"Czytać Diuna i bajki"_s);
        // Recurrence rules: read, written in either language, a rule that is none.
        const QVariantMap rule = value(u"return b.parseRecurrence(\"co 2 tygodnie w poniedziałek i piątek\")"_s).toMap();
        QCOMPARE(rule.value(u"every"_s).toString(), u"week"_s);
        QCOMPARE(rule.value(u"interval"_s).toInt(), 2);
        QCOMPARE(rule.value(u"weekdays"_s).toList(), (QVariantList{1, 5}));
        QVERIFY(value(u"return b.parseRecurrence(\"co miesiąc ostatniego\").lastDay"_s).toBool());
        QVERIFY(value(u"return b.parseRecurrence(\"w dni robocze\").workdays"_s).toBool());
        QVERIFY(value(u"return b.parseRecurrence(\"co drugi wtorek\") == nil"_s).toBool());
        QCOMPARE(value(u"return b.formatRecurrence({ every = \"week\", weekdays = { 5, 1 } }, \"en\")"_s).toString(),
                 u"every week on Monday, Friday"_s);
        QCOMPARE(value(u"return b.formatRecurrence(b.parseRecurrence(\"every 3 days\"))"_s).toString(), u"co 3 dni"_s);
        QVERIFY(error(u"return b.formatRecurrence({ every = \"hour\" })"_s).contains(u"every must be"_s));
        QCOMPARE(value(u"return b.parseWeekdays(\"pn, śr i pt\")"_s).toList(), (QVariantList{1, 3, 5}));
        QVERIFY(value(u"return b.parseWeekdays(\"pn, jutro\") == nil"_s).toBool());
    }

    void appendToPage()
    {
        QVERIFY(run(u"b.appendToPage(\"Nawyki\", \"- Pływanie\\n  habit:: sob\")"_s).ok());
        QCOMPARE(onDisk(u"Nawyki.md"_s),
                 "- Medytacja\n  habit:: codziennie\n- Bieganie #zdrowie\n  nawyk:: pn, śr, pt\n  start:: 2026-10-05\n"
                 "- Zwykły punkt\n- Pływanie\n  habit:: sob\n"_ba);
        // A page that does not exist is made where new pages go.
        QVERIFY(run(u"b.appendToPage(\"Nowa\", \"- pierwszy\")"_s).ok());
        QVERIFY(QString::fromUtf8(onDisk(u"Nowa.md"_s)).contains(u"- pierwszy"_s));
        QVERIFY(error(u"b.appendToPage(\"\", \"- x\")"_s).contains(u"the page's name is empty"_s));
    }

    void insertAndRemoveBlocks()
    {
        // After a block and its children, with the note's own line ending.
        QString ref = value(u"return b.blocks({ page = \"Lista\" })[1].ref"_s).toString();
        QString added = value(u"return b.insertBlock(\"%1\", \"- [ ] ser\")"_s.arg(ref)).toString();
        QCOMPARE(onDisk(u"Lista.md"_s),
                 "- [ ] mleko\r\n  - pełne\r\n- [ ] ser\r\n- [x] chleb ✅ 2026-11-01\r\n- DONE masło\r\n- ostatni"_ba);
        QVERIFY2(added.startsWith(u"Lista.md#L3@"_s), qPrintable(added));
        // As a child (the children's indentation) and before; the last line keeps having no break.
        ref = value(u"return b.blocks({ page = \"Lista\" })[1].ref"_s).toString();
        QVERIFY(run(u"b.insertBlock(\"%1\", \"- chude\\n  ilość:: 2\", \"child\")"_s.arg(ref)).ok());
        ref = value(u"return b.blocks({ page = \"Lista\" })[5].ref"_s).toString();
        QVERIFY(run(u"b.insertBlock(\"%1\", \"- przed\", \"before\")"_s.arg(ref)).ok());
        QVERIFY(run(u"b.insertBlock(b.blocks({ page = \"Lista\" })[6].ref, \"- po\")"_s).ok());
        QCOMPARE(onDisk(u"Lista.md"_s),
                 "- [ ] mleko\r\n  - pełne\r\n  - chude\r\n    ilość:: 2\r\n- [ ] ser\r\n- [x] chleb ✅ 2026-11-01\r\n"
                 "- DONE masło\r\n- przed\r\n- ostatni\r\n- po"_ba);
        // A child of a block without children: one level, two spaces.
        QVERIFY(run(u"b.insertBlock(b.blocks({ page = \"Lista\" })[2].ref, \"- dziecko\", \"child\")"_s).ok());
        QVERIFY(onDisk(u"Lista.md"_s).contains("- [ ] ser\r\n  - dziecko\r\n"));
        // Removing a block takes its children; the end of the note stays without a break.
        ref = value(u"return b.blocks({ page = \"Lista\" })[1].ref"_s).toString();
        QVERIFY(run(u"b.removeBlock(\"%1\")"_s.arg(ref)).ok());
        QVERIFY(run(u"local all = b.blocks({ page = \"Lista\" }) b.removeBlock(all[#all].ref)"_s).ok());
        QCOMPARE(onDisk(u"Lista.md"_s),
                 "- [ ] ser\r\n  - dziecko\r\n- [x] chleb ✅ 2026-11-01\r\n- DONE masło\r\n- przed\r\n- ostatni"_ba);
        // Stale and forged references, a wrong placement, nothing to insert.
        QVERIFY(error(u"b.removeBlock(\"%1\")"_s.arg(ref)).contains(u"stale"_s));
        QVERIFY(error(u"b.insertBlock(\"Lista.md#L1\", \"- x\")"_s).contains(u"not a block reference"_s));
        QVERIFY(error(u"b.removeBlock(\".brulion/config.json#L1@00000000\")"_s).contains(u"not a block reference"_s));
        QVERIFY(error(u"b.insertBlock(b.blocks({ page = \"Lista\" })[1].ref, \"- x\", \"inside\")"_s).contains(u"\"after\""_s));
        QVERIFY(error(u"b.insertBlock(b.blocks({ page = \"Lista\" })[1].ref, \"\\n\")"_s).contains(u"nothing to insert"_s));
    }

    void taskStatusAndProperties()
    {
        // The checkbox alone: ✅ / ❌ dates as the editor writes them, a Logseq keyword stays one.
        QVERIFY(run(u"b.setTask(b.blocks({ page = \"Lista\" })[1].ref, \"x\")"_s).ok());
        QVERIFY(run(u"b.setTask(b.blocks({ page = \"Lista\" })[2].ref, \" \")"_s).ok());
        QVERIFY(run(u"b.setTask(b.blocks({ page = \"Lista\" })[3].ref, \"-\")"_s).ok());
        QCOMPARE(onDisk(u"Lista.md"_s),
                 "- [x] mleko ✅ 2026-11-14\r\n  - pełne\r\n- [ ] chleb\r\n- CANCELED masło ❌ 2026-11-14\r\n- ostatni"_ba);
        QVERIFY(error(u"b.setTask(b.blocks({ page = \"Lista\" })[1].ref, \"xx\")"_s).contains(u"one character"_s));
        // A recurring task: its next occurrence goes above, the ref returned is the task done.
        QVERIFY(run(u"b.appendToPage(\"Powtarzane\", \"- [ ] podlać kwiaty 🔁 every week 📅 2026-11-14\")"_s).ok());
        const QString done = value(u"return b.setTask(b.blocks({ page = \"Powtarzane\" })[1].ref, \"x\")"_s).toString();
        QVERIFY2(done.startsWith(u"Powtarzane.md#L2@"_s), qPrintable(done));
        QCOMPARE(value(u"return b.blocks({ page = \"Powtarzane\" })[2].task"_s).toString(), u"x"_s);
        // Properties: a new one under the text, one changed, one removed; a number as a value.
        QVERIFY(run(u"b.setProperty(b.blocks({ page = \"Lista\" })[4].ref, \"ilość\", 3)"_s).ok());
        QVERIFY(onDisk(u"Lista.md"_s).endsWith("- ostatni\r\n  ilość:: 3"));
        QVERIFY(run(u"b.setProperty(b.blocks({ prop = \"nawyk\" })[1].ref, \"Nawyk\", \"codziennie\")"_s).ok());
        QVERIFY(run(u"b.setProperty(b.blocks({ prop = \"habit\" })[1].ref, \"notatka\", \"rano\")"_s).ok());
        QVERIFY(run(u"b.setProperty(b.blocks({ prop = \"nawyk\" })[1].ref, \"start\", nil)"_s).ok());
        QCOMPARE(onDisk(u"Nawyki.md"_s),
                 "- Medytacja\n  habit:: codziennie\n  notatka:: rano\n- Bieganie #zdrowie\n  nawyk:: codziennie\n"
                 "- Zwykły punkt\n"_ba);
        QVERIFY(error(u"b.setProperty(b.blocks({ prop = \"habit\" })[1].ref, \"zły klucz\", \"x\")"_s).contains(u"cannot set"_s));
        QVERIFY(error(u"b.setProperty(b.blocks({ prop = \"habit\" })[1].ref, \"a\", \"x\\ny\")"_s).contains(u"one line"_s));
    }

    void writingNeedsThePermission()
    {
        Extension e;
        e.id = u"czyta"_s;
        QVERIFY(m_engine->load(e).ok());
        for (const QString &call : {u"b.appendToPage(\"Lista\", \"- x\")"_s, u"b.removeBlock(\"Lista.md#L1@0\")"_s,
                                    u"b.insertBlock(\"Lista.md#L1@0\", \"- x\")"_s, u"b.setTask(\"Lista.md#L1@0\", \"x\")"_s,
                                    u"b.setProperty(\"Lista.md#L1@0\", \"a\", \"b\")"_s}) {
            const Result r = m_engine->evaluate(u"czyta"_s, call, u"test"_s);
            QVERIFY2(r.error.contains(u"permission denied: write"_s), qPrintable(call + u": "_s + r.error));
        }
        QCOMPARE(onDisk(u"Lista.md"_s), "- [ ] mleko\r\n  - pełne\r\n- [x] chleb ✅ 2026-11-01\r\n- DONE masło\r\n- ostatni"_ba);
    }

    void pureEdits_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("expected");
        QTest::newRow("property after the text") << u"- a\n- b\n"_s << u"- a\n  k:: v\n- b\n"_s;
        QTest::newRow("numbered item") << u"12. a\n"_s << u"12. a\n    k:: v\n"_s;
        QTest::newRow("after other properties") << u"- a\n  x:: 1\n  - dziecko\n"_s << u"- a\n  x:: 1\n  k:: v\n  - dziecko\n"_s;
        QTest::newRow("tabs") << u"\t- a\n"_s << u"\t- a\n\t  k:: v\n"_s;
        QTest::newRow("no final break") << u"- a"_s << u"- a\n  k:: v"_s;
        QTest::newRow("value replaced") << u"- a\n  K:: stare\n"_s << u"- a\n  K:: v\n"_s;
    }
    void pureEdits()
    {
        QFETCH(QString, content);
        QFETCH(QString, expected);
        const QStringList lines = content.split(u'\n');
        QCOMPARE(setBlockProperty(content, 0, int(lines.size()) - 1, u"k"_s, u"v"_s).value_or(u"<none>"_s), expected);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsApi)
#include "tst_extensions_api.moc"
