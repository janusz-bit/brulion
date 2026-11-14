// The extensions as the app runs them (src/app/shell/workspaceextensions.h), on the sample
// extensions and their notebook (tests/core/extensions/samples): pages that arrive wait for
// approval and the approved ones run; commands ask through the question the app shows; views draw
// and their actions write; "save" and "change" hooks as the Workspace delivers them — what an
// extension writes never comes back to the hooks, what the user changes always does; code an
// extension writes waits for approval; the editor's own save of an approved page keeps it
// approved, a save that does not load keeps the version before it running.
//
// The Workspace's part is played as Workspace::onVaultChanged and Workspace::write play it: the
// vault reports a change while it is written (the index first, then notesChanged()), and the
// editor's save says savedByEditor() first.

#include "workspaceextensions.h"

#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::shell;

namespace {

const QString kSamples = QStringLiteral(BRULION_SOURCE_DIR) + u"/tests/core/extensions/samples"_s;
const QString kFolder = u"Rozszerzenia"_s;
const QString kJournal = u"journals/2026-11-14.md"_s;

/// An extension that counts the "change" events it is told about in a page it writes itself, and
/// has a command that writes another page.
const QString kCounter = u"---\ntags: [extension]\npermissions: [write]\n---\n```luau\n"
                         "local n = 0\n"
                         "b.on(\"change\", function(e)\n"
                         "    n += 1\n"
                         "    b.write(\"Licznik\", `zmiany: {n} ({table.concat(e.pages, \", \")})\\n`)\n"
                         "end)\n"
                         "b.command(\"pisz\", function(ctx)\n"
                         "    b.write(\"Z polecenia\", \"pisze polecenie\\n\")\n"
                         "    b.appendToJournal(b.today(), \"- wpis polecenia\")\n"
                         "    return \"napisane\"\n"
                         "end)\n```\n"_s;

QString readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QStringList filesUnder(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(dir).relativeFilePath(it.next());
    out.sort();
    return out;
}

/// The nodes of a view's tree of `type`, in document order.
void collect(const QVariant &node, const QString &type, QList<QVariantMap> *out)
{
    const QVariantMap map = node.toMap();
    if (map.value(u"type"_s).toString() == type)
        out->append(map);
    for (const QVariant &child : map.value(u"children"_s).toList())
        collect(child, type, out);
}

QStringList textsOf(const QVariantMap &tree, const QString &type)
{
    QList<QVariantMap> nodes;
    collect(tree, type, &nodes);
    QStringList out;
    for (const QVariantMap &node : std::as_const(nodes))
        out << node.value(u"text"_s).toString();
    return out;
}

} // namespace

class TestShellExtensions : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_dir;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<WorkspaceExtensions> m_ext;
    QStringList m_messages; ///< the toasts, "kind: text"

    QString disk(const QString &path) const { return readFile(m_dir + u'/' + path); }
    QString page(const QString &title) const { return kFolder + u'/' + title + u".md"_s; }

    QString lineWith(const QString &path, const QString &needle) const
    {
        for (const QString &line : disk(path).split(u'\n'))
            if (line.contains(needle))
                return line;
        return {};
    }

    int lineOf(const QString &path, const QString &needle) const
    {
        const QStringList lines = disk(path).split(u'\n');
        for (int i = 0; i < lines.size(); ++i)
            if (lines[i].contains(needle))
                return i;
        return -1;
    }

    /// The console's lines that hold `text`.
    int consoleLines(const QString &text) const
    {
        return int(m_ext->console()->text().split(u'\n').filter(text).size());
    }

    /// The sample notebook with the ten sample extensions and the counter, opened as the app opens
    /// a notebook: every extension page is there, none was approved on this device.
    void open()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
        m_messages.clear();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QHash<QString, QString> files{{page(u"Licznik zmian"_s), kCounter}};
        for (const QString &file : filesUnder(kSamples + u"/notebook"_s))
            files.insert(file, readFile(kSamples + u"/notebook/"_s + file));
        for (const QString &file : filesUnder(kSamples + u"/extensions"_s))
            files.insert(kFolder + u'/' + file, readFile(kSamples + u"/extensions/"_s + file));
        QVERIFY(files.size() > 20);
        for (auto it = files.cbegin(); it != files.cend(); ++it) {
            const QString file = m_dir + u'/' + it.key();
            QDir().mkpath(QFileInfo(file).absolutePath());
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(it.value().toUtf8());
        }
        m_vault = std::make_unique<Vault>(m_dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        QVERIFY(m_vault->open());
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        m_ext = std::make_unique<WorkspaceExtensions>([] { return QDateTime(QDate(2026, 11, 14), QTime(10, 30)); });
        connect(m_ext.get(), &WorkspaceExtensions::message, this,
                [this](const QString &text, const QString &kind) { m_messages << kind + u": "_s + text; });
        // Workspace::onVaultChanged: the index first, then the extensions are told — inside the
        // vault's write.
        connect(m_vault.get(), &Vault::changed, this, [this](const QList<VaultChange> &changes) {
            QStringList changed;
            for (const VaultChange &c : changes) {
                if (c.type == ChangeType::Deleted) {
                    m_index->remove(c.path);
                } else if (paths::isMarkdown(c.path)) {
                    bool ok = false;
                    const QString content = m_vault->readText(c.path, &ok);
                    if (ok)
                        m_index->update(c.path, content);
                }
                changed << c.path;
            }
            m_ext->notesChanged(changed);
        });
        m_ext->attach(m_vault.get(), m_index.get());
        m_ext->indexReady();
    }

    void approveAll()
    {
        for (const QString &file : filesUnder(m_dir + u'/' + kFolder))
            m_ext->approve(kFolder + u'/' + file);
    }

    /// Workspace::write: this device's editor saves a page.
    bool editorSaves(const QString &path, const QString &content)
    {
        m_ext->savedByEditor(path, content);
        return m_vault->writeText(path, content, ChangeSource::User);
    }

    /// Another program or a sync changes a page.
    bool syncBrings(const QString &path, const QString &content)
    {
        return m_vault->writeText(path, content, ChangeSource::External);
    }

private Q_SLOTS:
    void init() { open(); }

    void cleanup()
    {
        m_ext.reset();
        m_index.reset();
        m_vault.reset();
    }

    void pagesWaitForApproval()
    {
        // Code that is in the notebook when it opens (a sync brought it, an import) runs nowhere
        // until it is approved on this device.
        const QStringList pages = filesUnder(m_dir + u'/' + kFolder);
        QCOMPARE(pages.size(), 11);
        QCOMPARE(m_ext->waitingApproval(), 11);
        for (const QString &file : pages)
            QCOMPARE(m_ext->pageState(kFolder + u'/' + file), u"approval"_s);
        QVERIFY(m_ext->commands().isEmpty());
        QVERIFY(m_ext->views().isEmpty());
        // One approved: it runs, the others still wait.
        m_ext->approve(page(u"Licznik słów"_s));
        QCOMPARE(m_ext->pageState(page(u"Licznik słów"_s)), u"running"_s);
        QCOMPARE(m_ext->commands().size(), 3);
        QCOMPARE(m_ext->waitingApproval(), 10);
        // All of them: every sample loads in the app's engine, in a Polish interface.
        approveAll();
        for (const QString &file : pages)
            QVERIFY2(m_ext->pageState(kFolder + u'/' + file) == u"running",
                     qPrintable(file + u": "_s + m_ext->pageMessage(kFolder + u'/' + file)));
        QCOMPARE(m_ext->waitingApproval(), 0);
        QCOMPARE(m_ext->commands().size(), 16);
        QCOMPARE(m_ext->views().size(), 8);
        QCOMPARE(m_ext->title(u"licznik-slow"_s), u"Licznik słów"_s);
        QCOMPARE(m_ext->viewTitle(u"wydatki"_s, u"miesiac"_s), u"Wydatki"_s);
        // An English interface loads them again: the bilingual one speaks English.
        m_ext->setLanguage(u"en"_s);
        QCOMPARE(m_ext->viewTitle(u"wydatki"_s, u"miesiac"_s), u"Expenses"_s);
        QCOMPARE(m_ext->commands().size(), 16);
    }

    void commandsAndTheirQuestions()
    {
        approveAll();
        // A command's text is a message.
        m_ext->runCommand(u"licznik-slow"_s, u"policz"_s, u"Lalka.md"_s);
        QCOMPARE(m_messages, QStringList{u"info: „Lalka”: 19 słów, 133 znaki, ok. 1 min czytania"_s});
        // A command on the block under the caret asks for a line of text.
        m_messages.clear();
        m_ext->runCommand(u"przeglad-zadan"_s, u"termin"_s, u"Zakupy.md"_s, lineOf(u"Zakupy.md"_s, u"mleko"_s));
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        QVERIFY(m_ext->isWaiting());
        m_ext->answerText(u"jutro"_s);
        QVERIFY(!m_ext->isWaiting());
        QCOMPARE(m_messages, QStringList{u"info: Termin: 2026-11-15"_s});
        QCOMPARE(lineWith(u"Zakupy.md"_s, u"mleko"_s), u"- [ ] mleko 📅 2026-11-15"_s);
        // A form, then a question that is cancelled: the command goes on without the note.
        m_messages.clear();
        m_ext->runCommand(u"nastroj"_s, u"zapisz"_s);
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        QCOMPARE(m_ext->questionTitle(), u"Jak się dziś czujesz?"_s);
        QCOMPARE(m_ext->questionFields()->rowCount(), 2);
        m_ext->questionFields()->setNumber(0, 5);
        m_ext->answerForm();
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        m_ext->cancelQuestion();
        QCOMPARE(m_messages, QStringList{u"info: Zapisano: nastrój 5/5, energia 3/5"_s});
        QVERIFY(disk(kJournal).endsWith(u"- Samopoczucie #nastrój\n  nastroj:: 5\n  energia:: 3\n"_s));
        // A form, a choice among the categories, a text.
        m_messages.clear();
        m_ext->runCommand(u"wydatki"_s, u"dodaj"_s);
        QCOMPARE(m_ext->questionKind(), u"form"_s);
        m_ext->questionFields()->setNumber(0, 12.5);
        m_ext->answerForm();
        QCOMPARE(m_ext->questionKind(), u"choose"_s);
        QCOMPARE(m_ext->questionOptions().first(), u"jedzenie"_s);
        m_ext->answerChoice(u"jedzenie"_s);
        m_ext->answerText(u"kebab"_s);
        QCOMPARE(m_messages, QStringList{u"info: Dopisano: 12,5 zł, jedzenie"_s});
        QVERIFY(disk(kJournal).endsWith(u"- 12,5 zł jedzenie: kebab #wydatek\n"_s));
        // The app's timer.
        m_ext->runCommand(u"pomodoro"_s, u"break"_s);
        QCOMPARE(m_ext->restLabel(), u"Break"_s);
        QVERIFY(m_ext->restRemaining() > 290 && m_ext->restRemaining() <= 300);
        m_ext->stopRest();
        // A model without a key (nothing leaves the device): the command says so.
        m_messages.clear();
        m_ext->runCommand(u"action-items"_s, u"extract"_s, u"Podróże/Peru.md"_s);
        QTRY_VERIFY2(m_messages.join(u'|').contains(u"AI is not available: "_s), qPrintable(m_messages.join(u'|')));
        QCOMPARE(m_ext->aiPending(), 0);
    }

    void viewsAndTheirActions()
    {
        approveAll();
        QString error;
        QVariantMap tree = m_ext->render(u"przeglad-zadan"_s, u"przeglad"_s, {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(tree.value(u"type"_s).toString(), u"column"_s);
        QCOMPARE(textsOf(tree, u"heading"_s),
                 (QStringList{u"Zaległe (2)"_s, u"Na dziś (1)"_s, u"Najbliższe dni: 7 (2)"_s}));
        QList<QVariantMap> checks;
        collect(tree, u"check"_s, &checks);
        QCOMPARE(checks.size(), 5);
        // A tap: the action writes the note, a text it returns is a message.
        QVERIFY(m_ext->act(u"przeglad-zadan"_s, u"przeglad"_s, u"zrobione"_s, checks[0].value(u"args"_s).toMap()));
        QCOMPARE(lineWith(u"Dom.md"_s, u"czynsz"_s), u"- [x] Zapłacić czynsz 📅 2026-11-10 ⏫ ✅ 2026-11-14"_s);
        // The same control tapped again (the view not drawn in between): the line changed, nothing
        // is written, and the app says so.
        m_messages.clear();
        QVERIFY(!m_ext->act(u"przeglad-zadan"_s, u"przeglad"_s, u"zrobione"_s, checks[0].value(u"args"_s).toMap()));
        QCOMPARE(m_messages.size(), 1);
        QVERIFY2(m_messages[0].startsWith(u"info: "_s) && !m_messages[0].contains(u"stale"_s),
                 qPrintable(m_messages[0]));
        tree = m_ext->render(u"przeglad-zadan"_s, u"przeglad"_s, {}, &error);
        QCOMPARE(textsOf(tree, u"heading"_s).first(), u"Zaległe (1)"_s);
        // A view with params, as a b.ui.open node gives them.
        tree = m_ext->render(u"wydatki"_s, u"miesiac"_s, {{u"month"_s, u"2026-10"_s}}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(textsOf(tree, u"heading"_s), QStringList{u"Wydatki: październik 2026"_s});
        QCOMPARE(textsOf(tree, u"text"_s), QStringList{u"Razem: 155,5 zł"_s});
    }

    void ownWritesAreNoChange()
    {
        // "change" hooks hear what changed in the notebook — but never what an extension wrote
        // itself (no echoes, no loops), and always what the user changed, also in a page an
        // extension wrote before.
        m_ext->approve(page(u"Licznik zmian"_s));
        QCOMPARE(m_ext->pageState(page(u"Licznik zmian"_s)), u"running"_s);
        // The user edits a page: one event, with that page. The hook's own write of its page is
        // none (it would be the second event).
        QVERIFY(editorSaves(u"Dom.md"_s, disk(u"Dom.md"_s) + u"- [ ] Podlać kwiaty\n"_s));
        QTRY_COMPARE(disk(u"Licznik.md"_s), u"zmiany: 1 (Dom)\n"_s);
        QTest::qWait(1500);
        QCOMPARE(disk(u"Licznik.md"_s), u"zmiany: 1 (Dom)\n"_s);
        // The user edits the page the extension wrote: an event like any other.
        QVERIFY(editorSaves(u"Licznik.md"_s, u"ręcznie\n"_s));
        QTRY_COMPARE(disk(u"Licznik.md"_s), u"zmiany: 2 (Licznik)\n"_s);
        // A command of the extension writes a page and today's note: no event.
        m_ext->runCommand(u"licznik-zmian"_s, u"pisz"_s);
        QCOMPARE(m_messages, QStringList{u"info: napisane"_s});
        QCOMPARE(disk(u"Z polecenia.md"_s), u"pisze polecenie\n"_s);
        QTest::qWait(1500);
        QCOMPARE(disk(u"Licznik.md"_s), u"zmiany: 2 (Licznik)\n"_s);
        // The user edits what the command wrote, a sync changes another page: one event each
        // burst, with the pages of the burst.
        QVERIFY(editorSaves(u"Z polecenia.md"_s, u"ręcznie\n"_s));
        QVERIFY(syncBrings(u"Zakupy.md"_s, disk(u"Zakupy.md"_s) + u"- [ ] jajka\n"_s));
        QTRY_COMPARE(disk(u"Licznik.md"_s), u"zmiany: 3 (Z polecenia, Zakupy)\n"_s);
        QTest::qWait(1500);
        QCOMPARE(disk(u"Licznik.md"_s), u"zmiany: 3 (Z polecenia, Zakupy)\n"_s);
        QCOMPARE(consoleLines(u"error"_s), 0);
    }

    void aPageKeptWrittenByAHook()
    {
        // The project index: its "change" hook writes the page "Projekty" when a project changes —
        // once, with nothing to do for its own write —, its "save" hook notes the save.
        m_ext->approve(page(u"Indeks projektów"_s));
        QString dom = disk(u"Dom.md"_s);
        dom.replace(u"- [ ] Zapłacić czynsz"_s, u"- [x] Zapłacić czynsz"_s);
        QVERIFY(editorSaves(u"Dom.md"_s, dom));
        QTRY_VERIFY(
                disk(u"Projekty.md"_s).contains(u"- [[Dom]] — otwarte zadania: 2, najbliższy termin: 2026-11-14\n"_s));
        QTRY_COMPARE(consoleLines(u"Zapisano projekt: Dom (Dom.md)"_s), 1);
        QTest::qWait(1500);
        QCOMPARE(consoleLines(u"Indeks projektów przepisany"_s), 1);
        // Somebody edits the written page: the next event puts it back.
        const QString written = disk(u"Projekty.md"_s);
        QVERIFY(editorSaves(u"Projekty.md"_s, written + u"- dopisek\n"_s));
        QTRY_COMPARE(disk(u"Projekty.md"_s), written);
        QTRY_COMPARE(consoleLines(u"Indeks projektów przepisany"_s), 2);
    }

    void codeAnExtensionWritesWaits()
    {
        // "Quick entries" writes an extension page. It is code that did not come from this
        // device's editor: it waits for approval like code from a sync, then runs.
        m_ext->approve(page(u"Quick entries"_s));
        m_ext->runCommand(u"quick-entries"_s, u"new"_s);
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        m_ext->answerText(u"Kawa"_s);
        QCOMPARE(m_ext->questionKind(), u"ask"_s);
        m_ext->answerText(u"☕ {time} Kawa #kawa"_s);
        QVERIFY2(m_messages.value(0).startsWith(u"info: Written: \"Quick entry Kawa\""_s),
                 qPrintable(m_messages.join(u'|')));
        const QString made = page(u"Quick entry Kawa"_s);
        QVERIFY(disk(made).contains(u"local LINE = \"☕ {time} Kawa #kawa\"\n"_s));
        QTRY_COMPARE(m_ext->pageState(made), u"approval"_s);
        QVERIFY(m_ext->commands().size() == 1); // the new page's command is not there
        m_ext->approve(made);
        QCOMPARE(m_ext->pageState(made), u"running"_s);
        m_messages.clear();
        m_ext->runCommand(u"quick-entry-kawa"_s, u"add"_s);
        QVERIFY2(m_messages.value(0).startsWith(u"info: Added: ☕ "_s), qPrintable(m_messages.join(u'|')));
        static const QRegularExpression line(u"^- ☕ \\d\\d:\\d\\d Kawa #kawa$"_s);
        QVERIFY2(line.match(disk(kJournal).trimmed().section(u'\n', -1)).hasMatch(), qPrintable(disk(kJournal)));
    }

    void editingAnApprovedPage()
    {
        const QString path = page(u"Link check"_s);
        m_ext->approve(path);
        QString error;
        QCOMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                 QStringList{u"Broken links: 4"_s});
        // This device's editor changes the code: it stays approved and runs at once.
        QString code = disk(path);
        QVERIFY(code.contains(u"b.ui.heading(`Broken links: "_s));
        code.replace(u"b.ui.heading(`Broken links: "_s, u"b.ui.heading(`Missing pages: "_s);
        QVERIFY(editorSaves(path, code));
        QTRY_COMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                     QStringList{u"Missing pages: 4"_s});
        QCOMPARE(m_ext->pageState(path), u"running"_s);
        // A save that does not load: the page says where, and the version before it keeps running.
        QString broken = code;
        broken.replace(u"local MAX_SHOWN = 50"_s, u"local MAX_SHOWN = = 50"_s);
        QVERIFY(broken != code);
        QVERIFY(editorSaves(path, broken));
        QTRY_COMPARE(m_ext->pageState(path), u"error"_s);
        QCOMPARE(m_ext->pageErrorLine(path), int(broken.left(broken.indexOf(u"local MAX_SHOWN"_s)).count(u'\n')));
        QVERIFY2(m_ext->pageMessage(path).startsWith(u"Link check:"_s), qPrintable(m_ext->pageMessage(path)));
        QCOMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                 QStringList{u"Missing pages: 4"_s});
        // The same change arriving from elsewhere (a sync): it waits, and nothing of the page runs.
        code.replace(u"Missing pages: "_s, u"Gone: "_s);
        QVERIFY(syncBrings(path, code));
        QTRY_COMPARE(m_ext->pageState(path), u"approval"_s);
        QVERIFY(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error).isEmpty());
        m_ext->approve(path);
        QCOMPARE(textsOf(m_ext->render(u"link-check"_s, u"broken"_s, {}, &error), u"heading"_s),
                 QStringList{u"Gone: 4"_s});
    }
};

QTEST_GUILESS_MAIN(TestShellExtensions)
#include "tst_extensions.moc"
