// The NotebookHost's dry run (src/core/extensions/notebookhost.h, Options::dryRun — `brulion-cli
// extension check / run / eval`): an extension's writes are computed with the same edits as real
// ones — a page replaced or created, a line appended to a daily note (created from the journal
// template), a block's text and status replaced — and kept in dryWrites(); nothing reaches the
// disk, read() and later writes of the run see the computed texts, a stale block is still refused.
// And b.checkExtension (sandbox.h): a page text loaded in an engine of its own, never nested.

#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "notebook/itemref.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

class TestExtensionsHost : public QObject
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

    Result run(const QString &code)
    {
        return m_engine->evaluate(u"t"_s, code, u"test"_s);
    }

private Q_SLOTS:
    void init()
    {
        const QHash<QString, QString> files{
            {u"Zakupy.md"_s, u"- [ ] mleko\n- [ ] chleb\r\n"_s},
            {u"templates/Dziennik.md"_s, u"## Plan na {{date}}\n"_s},
            {u".brulion/config.json"_s, u"{\"journalTemplate\": \"templates/Dziennik.md\"}"_s},
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
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            if (it.key().endsWith(u".md"_s))
                m_index->update(it.key(), it.value());
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 14), QTime(9, 0)); };
        options.dryRun = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_engine = std::make_unique<Engine>();
        QVERIFY(m_engine->start(m_host.get(), Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}));
        Extension e;
        e.id = u"t"_s;
        e.permissions = {u"write"_s};
        QVERIFY(m_engine->load(e).ok());
    }

    void checkExtensionInASandbox()
    {
        // A page that loads: what it registers; its top-level write is reported, not made.
        Result r = run(u"return b.checkExtension(\"Licznik\", \"---\\ntags: [extension]\\npermissions: [write]\\n---\\n"
                       "```luau\\nb.write(\\\"Zakupy\\\", \\\"x\\\")\\nprint(\\\"wczytane\\\")\\n"
                       "b.command(\\\"ile\\\", function() return 1 end, { label = \\\"Ile\\\" })\\n"
                       "b.view(\\\"lista\\\", function() return b.ui.text(\\\"x\\\") end)\\n"
                       "b.on(\\\"save\\\", function() end)\\n```\\n\")"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVariantMap report = r.value.toMap();
        QVERIFY(report.value(u"ok"_s).toBool());
        QCOMPARE(report.value(u"id"_s).toString(), u"licznik"_s);
        QCOMPARE(report.value(u"path"_s).toString(), u"Licznik.md"_s);
        QCOMPARE(report.value(u"commands"_s).toList().value(0).toMap().value(u"label"_s).toString(), u"Ile"_s);
        QCOMPARE(report.value(u"views"_s).toList().size(), 1);
        QCOMPARE(report.value(u"hooks"_s).toStringList(), QStringList{u"save"_s});
        QCOMPARE(report.value(u"printed"_s).toStringList(), QStringList{u"wczytane"_s});
        QCOMPARE(report.value(u"writes"_s).toList().value(0).toMap().value(u"path"_s).toString(), u"Zakupy.md"_s);
        QCOMPARE(onDisk(u"Zakupy.md"_s), "- [ ] mleko\n- [ ] chleb\r\n"_ba);
        QVERIFY(m_engine->commands().isEmpty()); // nothing stays registered
        // An error with the page's line; a page that is no extension; a check inside a check.
        r = run(u"return b.checkExtension(\"Zły\", \"---\\ntags: [extension]\\n---\\nTekst.\\n\\n```luau\\nlocal = 1\\n```\")"_s);
        report = r.value.toMap();
        QVERIFY(!report.value(u"ok"_s).toBool());
        QCOMPARE(report.value(u"line"_s).toInt(), 7);
        QVERIFY2(report.value(u"error"_s).toString().startsWith(u"Zły:7:"_s), qPrintable(report.value(u"error"_s).toString()));
        r = run(u"return b.checkExtension(\"Zwykła\", \"```luau\\nreturn 1\\n```\")"_s);
        QVERIFY(!r.value.toMap().value(u"ok"_s).toBool());
        QVERIFY(!r.value.toMap().value(u"isExtension"_s).toBool());
        r = run(u"return b.checkExtension(\"Gniazdo\", \"tags:: extension\\n\\n```luau\\n"
                "print(b.checkExtension(\\\"Ptak\\\", \\\"tags:: extension\\\\n\\\\n```luau\\\\nreturn 1\\\\n```\\\").error)\\n```\")"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(r.value.toMap().value(u"printed"_s).toStringList().value(0).contains(u"while another check runs"_s),
                 qPrintable(r.value.toMap().value(u"printed"_s).toStringList().join(u'|')));
        // Top-level code that never ends: the check's own time limit.
        r = run(u"return b.checkExtension(\"Pętla\", \"tags:: extension\\n\\n```luau\\nwhile true do end\\n```\")"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toMap().value(u"status"_s).toString(), u"timeout"_s);
    }

    void writesAreComputedNotMade()
    {
        // b.write of an existing page and of a new one; later reads of the run see them.
        Result r = run(u"b.write(\"Zakupy\", \"- [x] mleko\\n\"); b.write(\"Nowa strona\", \"Żółw\\n\"); "
                       "return b.read(\"Zakupy\") .. b.read(\"Nowa strona\")"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"- [x] mleko\nŻółw\n"_s);
        QList<NotebookHost::DryWrite> writes = m_host->dryWrites();
        QCOMPARE(writes.size(), 2);
        QCOMPARE(writes[0].path, u"Zakupy.md"_s);
        QVERIFY(writes[0].existed);
        QCOMPARE(writes[0].before, u"- [ ] mleko\n- [ ] chleb\r\n"_s);
        QCOMPARE(writes[1].path, u"Nowa strona.md"_s);
        QVERIFY(!writes[1].existed);
        QCOMPARE(writes[1].after, u"Żółw\n"_s);
        // Written again: still one entry, its newest text.
        QVERIFY(run(u"b.write(\"Nowa strona\", \"Ćma\\n\")"_s).ok());
        writes = m_host->dryWrites();
        QCOMPARE(writes.size(), 2);
        QCOMPARE(writes[1].after, u"Ćma\n"_s);
        QCOMPARE(onDisk(u"Zakupy.md"_s), "- [ ] mleko\n- [ ] chleb\r\n"_ba);
        QCOMPARE(onDisk(u"Nowa strona.md"_s), "<missing>"_ba);
    }

    void journalFromItsTemplate()
    {
        Result r = run(u"b.appendToJournal(b.today(), \"- pierwsza\"); b.appendToJournal(b.today(), \"- druga\")"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QList<NotebookHost::DryWrite> writes = m_host->dryWrites();
        QCOMPARE(writes.size(), 1);
        QCOMPARE(writes[0].path, u"journals/2026-11-14.md"_s);
        QVERIFY(!writes[0].existed);
        QCOMPARE(writes[0].after, u"## Plan na 2026-11-14\n- pierwsza\n- druga\n"_s);
        QVERIFY(!QFile::exists(dir() + u"/journals/2026-11-14.md"_s));
    }

    void blocksReplacedAndStale()
    {
        Result r = run(u"local t = b.blocks({ page = \"Zakupy\" })[2]; b.replaceBlock(t.ref, t.text, \"x\"); "
                       "return t.ref"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        const QString ref = r.value.toString();
        QList<NotebookHost::DryWrite> writes = m_host->dryWrites();
        QCOMPARE(writes.size(), 1);
        QVERIFY2(writes[0].after.startsWith(u"- [ ] mleko\n- [x] chleb"_s), qPrintable(writes[0].after));
        QVERIFY(writes[0].after.endsWith(u"\r\n"_s)); // the line's own ending stays
        // The same ref after the computed change: the block changed, so it is stale.
        r = run(u"b.replaceBlock(\""_s + ref + u"\", \"bułki\")"_s);
        QCOMPARE(r.status, Status::Error);
        QVERIFY2(r.error.contains(u"stale"_s), qPrintable(r.error));
        QCOMPARE(onDisk(u"Zakupy.md"_s), "- [ ] mleko\n- [ ] chleb\r\n"_ba);
    }

    void refsNameNotesOnly()
    {
        // A block's ref names a note of the notebook. Whatever a script makes of one — the path of
        // another file in the notebook's folder (a git repository's configuration, the notebook's
        // own, a file among the attachments), with the right hash for its line or with none —
        // b.replaceBlock reads and writes nothing but notes. (An extension allowed to change notes
        // must not reach .git/config, where a line is a command git runs.)
        const QList<std::pair<QString, QString>> others{
                {u".git/config"_s, u"[core]\n\trepositoryformatversion = 0\n"_s},
                {u".brulion/config.json"_s, u"{\"journalTemplate\": \"templates/Dziennik.md\"}"_s},
                {u"assets/dane.csv"_s, u"a;b\n1;2\n"_s},
        };
        for (const auto &[path, content] : others) {
            QDir().mkpath(QFileInfo(dir() + u'/' + path).absolutePath());
            QFile f(dir() + u'/' + path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content.toUtf8());
        }
        // A host that really writes, as the app's, and the dry run of the command line.
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 14), QTime(9, 0)); };
        options.updateIndex = true;
        NotebookHost real(*m_vault, *m_index, options);
        Engine writing;
        QVERIFY(writing.start(&real, Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}));
        Extension e;
        e.id = u"t"_s;
        e.permissions = {u"write"_s};
        QVERIFY(writing.load(e).ok());
        for (const auto &[path, content] : others) {
            const QString hash = notebook::lineHash(content.section(u'\n', 0, 0));
            const QStringList refs{path + u"#L1"_s, path + u"#L1@"_s + hash,
                                   u"Zakupy.md/../"_s + path + u"#L1@"_s + hash, u"./"_s + path + u"#L2"_s};
            for (const QString &ref : refs) {
                const QString code = u"return b.replaceBlock(\"%1\", \"zmienione\")"_s.arg(ref);
                for (Engine *engine : {&writing, m_engine.get()}) {
                    const Result r = engine->evaluate(u"t"_s, code, u"test"_s);
                    QVERIFY2(r.status == Status::Error, qPrintable(ref + u" → "_s + r.value.toString()));
                    QVERIFY2(r.error.contains(u"b.replaceBlock: not a block reference: "_s + ref), qPrintable(r.error));
                }
                QCOMPARE(onDisk(path), content.toUtf8());
            }
        }
        QVERIFY(m_host->dryWrites().isEmpty());
        // A page that is not there (deleted since, or never there): stale, as before.
        Result r = writing.evaluate(u"t"_s, u"return b.replaceBlock(\"Nie ma.md#L1@00000000\", \"x\")"_s, u"test"_s);
        QVERIFY2(r.error.contains(u"b.replaceBlock: stale: the page is gone"_s), qPrintable(r.error));
        // A note's line without its hash is not a ref a block had: nothing is written over a line
        // the script did not see.
        r = writing.evaluate(u"t"_s, u"return b.replaceBlock(\"Zakupy.md#L1\", \"x\")"_s, u"test"_s);
        QVERIFY2(r.error.contains(u"b.replaceBlock: not a block reference: Zakupy.md#L1"_s), qPrintable(r.error));
        QCOMPARE(onDisk(u"Zakupy.md"_s), "- [ ] mleko\n- [ ] chleb\r\n"_ba);
        // And the ref a block has still works: the real host writes the file.
        r = writing.evaluate(u"t"_s,
                             u"local t = b.blocks({ page = \"Zakupy\" })[1]; return b.replaceBlock(t.ref, \"kefir\")"_s,
                             u"test"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(onDisk(u"Zakupy.md"_s), "- [ ] kefir\n- [ ] chleb\r\n"_ba);
        QCOMPARE(r.value.toString(), u"Zakupy.md#L1@"_s + notebook::lineHash(u"- [ ] kefir"_s));

        // The same for names: b.read and b.blocks know notes only, and b.write makes pages — a
        // name's leading dots and ".." are taken out, so the page lands beside those files, never
        // on them and never outside the notebook.
        r = writing.evaluate(u"t"_s,
                             u"return tostring(b.read(\".git/config\")) .. tostring(b.read(\".brulion/config.json\")) "
                             "..  tostring(b.read(\"assets/dane.csv\")) .. #b.blocks({ page = \".git/config\" })"_s,
                             u"test"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.value.toString(), u"nilnilnil0"_s);
        r = writing.evaluate(u"t"_s,
                             u"b.write(\".git/config\", \"x\") b.write(\".brulion/config.json\", \"x\") "
                             "b.write(\"../../Poza\", \"x\") return true"_s,
                             u"test"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        for (const auto &[path, content] : others)
            QCOMPARE(onDisk(path), content.toUtf8());
        QCOMPARE(onDisk(u"git/config.md"_s), "x"_ba);
        QCOMPARE(onDisk(u"brulion/config.json.md"_s), "x"_ba);
        QVERIFY(!QFile::exists(m_tmp.filePath(u"Poza.md"_s)) && !QFile::exists(m_tmp.filePath(u"../Poza.md"_s)));
        QStringList outside;
        for (const QString &entry : QDir(m_tmp.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot))
            outside << entry;
        QCOMPARE(outside, (QStringList{u"s"_s, u"v"_s})); // the device state and the notebook, nothing else
    }
};

QTEST_GUILESS_MAIN(TestExtensionsHost)
#include "tst_extensions_host.moc"
