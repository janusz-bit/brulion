// Ten extensions as people write them (tests/core/extensions/samples/extensions: pages of a
// notebook, Polish and English, each with its prose and several ```luau blocks) at work on a
// notebook of their own (samples/notebook): a vault, its index, the NotebookHost and one engine
// with all ten loaded, as the app runs them. The test plays the user (answers to b.ask / b.choose /
// b.form, taps in views, saves in the editor) and the model (b.ai):
//
//   Licznik słów      read-only commands, a block command, a table of every page
//   Przegląd zadań    a view of tasks with checkboxes, buttons and a stepper; stale lines
//   Nastrój           a form into the journal, block properties, a chart with gaps
//   Wydatki           amounts with a decimal comma, months through view params, both languages
//   Indeks projektów  hooks ("change", "save") that keep a page written
//   Pomodoro          the timer, a block command, the journal
//   Quick entries     an extension that writes extensions (b.checkExtension, b.write)
//   Fiszki            a review that moves through view params and b.replaceBlock
//   Link check        every page read in one call, the search, a time budget
//   Action items      b.ai with a tool, questions inside the conversation
//
// Every view's tree is held to what docs/EXTENSIONS.md "Views" says a node is (checkNode), so a
// sample that draws nothing in the app fails here.

#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "extensions/pageextension.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <climits>
#include <functional>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

const QString kSamples = QStringLiteral(BRULION_SOURCE_DIR) + u"/tests/core/extensions/samples"_s;
const QString kFolder = u"Rozszerzenia"_s;            // where the extension pages are in the test's notebook
const QString kJournal = u"journals/2026-11-14.md"_s; // today's note

using Model = std::function<QVariantMap(const QVariantMap &request)>;

struct Timer {
    QString extension;
    int seconds = 0;
    QString label;
};

struct HookRun {
    QString extension;
    QString event;
    Result result;
};

/// What a call came to, with every question it asked on the way (AI requests: kind "ai").
struct Outcome {
    Result result;
    QVariantList questions;
    QString message() const { return result.value.toString(); }
};

QString readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

bool writeFile(const QString &file, const QString &content)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(content.toUtf8()) >= 0;
}

/// The files under `dir`, relative to it.
QStringList filesUnder(const QString &dir)
{
    QStringList out;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        out << QDir(dir).relativeFilePath(it.next());
    out.sort();
    return out;
}

bool isText(const QVariant &v)
{
    return v.metaType().id() == QMetaType::QString;
}
bool isNumber(const QVariant &v)
{
    return v.metaType().id() == QMetaType::LongLong || v.metaType().id() == QMetaType::Double;
}
bool isRecord(const QVariant &v)
{
    return v.metaType().id() == QMetaType::QVariantMap;
}
/// A list from a script: a sequence, or the empty table (which crosses as an empty record).
bool isList(const QVariant &v)
{
    return v.metaType().id() == QMetaType::QVariantList || (isRecord(v) && v.toMap().isEmpty());
}

/// Every node of a view's tree in document order, the controls of rows too.
void collectNodes(const QVariant &node, QList<QVariantMap> *out)
{
    const QVariantMap map = node.toMap();
    if (map.isEmpty())
        return;
    out->append(map);
    for (const QVariant &child : map.value(u"children"_s).toList())
        collectNodes(child, out);
}

QList<QVariantMap> nodesOf(const QVariant &tree, const QString &type = QString())
{
    QList<QVariantMap> all, out;
    collectNodes(tree, &all);
    for (const QVariantMap &node : std::as_const(all))
        if (type.isEmpty() || node.value(u"type"_s).toString() == type)
            out << node;
    return out;
}

/// What the nodes of `type` say: a heading's or a paragraph's text, a button's or a link's label.
QStringList textsOf(const QVariant &tree, const QString &type)
{
    QStringList out;
    for (const QVariantMap &node : nodesOf(tree, type)) {
        if (type == u"link")
            out << (node.value(u"label"_s).toString().isEmpty() ? node.value(u"page"_s) : node.value(u"label"_s))
                            .toString();
        else if (type == u"button" || type == u"open" || type == u"run")
            out << node.value(u"label"_s).toString();
        else if (type == u"empty")
            out << node.value(u"title"_s).toString();
        else
            out << node.value(u"text"_s).toString();
    }
    return out;
}

/// A chart series' values as the app reads them: a value per label, an invalid QVariant for a gap
/// (a table with nil in it crosses as a record with the positions as keys).
QVariantList seriesValues(const QVariantMap &chart, int series)
{
    const int labels = int(chart.value(u"labels"_s).toList().size());
    const QVariant values = chart.value(u"series"_s).toList().value(series).toMap().value(u"values"_s);
    QVariantList out;
    if (values.metaType().id() == QMetaType::QVariantList) {
        out = values.toList();
    } else {
        const QVariantMap map = values.toMap();
        for (int i = 1; i <= labels; ++i)
            out << map.value(QString::number(i));
    }
    while (out.size() < labels)
        out << QVariant();
    return out;
}

/// Is the tree what docs/EXTENSIONS.md "Views" describes? The problems found, with their place.
void checkNode(const QVariant &value, const ViewInfo &view, const QStringList &views, const QStringList &commands,
               bool inRow, const QString &where, QStringList *problems)
{
    if (!isRecord(value) || value.toMap().isEmpty()) {
        *problems << where + u": not a node"_s;
        return;
    }
    const QVariantMap node = value.toMap();
    const QString type = node.value(u"type"_s).toString();
    const QString here = where + u'/' + type;
    const auto text = [&](const char *key) {
        if (!isText(node.value(QLatin1String(key))))
            *problems << u"%1: %2 is no text"_s.arg(here, QLatin1String(key));
    };
    const auto children = [&](bool row) {
        const QVariant list = node.value(u"children"_s);
        if (!isList(list)) {
            *problems << here + u": children is no list (a nil among them?)"_s;
            return;
        }
        const QVariantList items = list.toList();
        if (row && items.size() > 24)
            *problems << here + u": more than 24 controls"_s;
        for (qsizetype i = 0; i < items.size(); ++i)
            checkNode(items[i], view, views, commands, row, u"%1[%2]"_s.arg(here).arg(i + 1), problems);
    };
    const auto action = [&] {
        const QString name = node.value(u"action"_s).toString();
        if (!view.actions.contains(name))
            *problems << u"%1: the view has no action \"%2\""_s.arg(here, name);
        const QVariant args = node.value(u"args"_s);
        if (args.isValid() && !args.isNull() && !isRecord(args))
            *problems << here + u": args is no record"_s;
    };
    if (inRow && type != u"check" && type != u"stepper" && type != u"text" && type != u"link" && type != u"button"
        && type != u"open" && type != u"run") {
        *problems << here + u": not a control of a row"_s;
        return;
    }
    if (type == u"column") {
        children(false);
    } else if (type == u"row") {
        children(true);
    } else if (type == u"heading" || type == u"text") {
        text("text");
    } else if (type == u"link") {
        text("page");
        if (node.value(u"page"_s).toString().trimmed().isEmpty())
            *problems << here + u": no page"_s;
    } else if (type == u"button") {
        text("label");
        action();
    } else if (type == u"check" || type == u"stepper") {
        if (!inRow)
            *problems << here + u": only inside a row"_s;
        if (type == u"stepper" && !isNumber(node.value(u"value"_s)))
            *problems << here + u": value is no number"_s;
        action();
    } else if (type == u"table") {
        const QVariantList columns = node.value(u"columns"_s).toList();
        if (columns.isEmpty() || !isList(node.value(u"rows"_s)))
            *problems << here + u": columns or rows is no list"_s;
        for (const QVariant &column : columns)
            if (!isText(column))
                *problems << here + u": a column's heading is no text"_s;
        const QVariantList rows = node.value(u"rows"_s).toList();
        for (qsizetype r = 0; r < rows.size(); ++r) {
            const QVariantList cells = rows[r].toList();
            if (rows[r].metaType().id() != QMetaType::QVariantList || cells.size() != columns.size())
                *problems << u"%1: row %2 has not a cell per column"_s.arg(here).arg(r + 1);
            for (const QVariant &cell : cells)
                if (!isText(cell) && !isNumber(cell))
                    *problems << u"%1: row %2 has a cell that is no text"_s.arg(here).arg(r + 1);
        }
    } else if (type == u"chart") {
        const QVariantList labels = node.value(u"labels"_s).toList();
        const QVariantList series = node.value(u"series"_s).toList();
        const QString kind = node.value(u"kind"_s).toString();
        if (labels.isEmpty() || series.isEmpty())
            *problems << here + u": no labels or no series"_s;
        if (!kind.isEmpty() && kind != u"line" && kind != u"bar")
            *problems << here + u": kind "_s + kind;
        for (qsizetype s = 0; s < series.size(); ++s) {
            const QVariantMap one = series[s].toMap();
            if (!isText(one.value(u"name"_s)))
                *problems << u"%1: series %2 has no name"_s.arg(here).arg(s + 1);
            const QVariant values = one.value(u"values"_s);
            if (values.metaType().id() == QMetaType::QVariantList) {
                if (values.toList().size() > labels.size())
                    *problems << u"%1: series %2 has more values than labels"_s.arg(here).arg(s + 1);
            } else {
                const QVariantMap sparse = values.toMap(); // gaps: the positions as keys
                for (auto it = sparse.cbegin(); it != sparse.cend(); ++it) {
                    bool ok = false;
                    const int position = it.key().toInt(&ok);
                    if (!ok || position < 1 || position > labels.size() || !isNumber(it.value()))
                        *problems << u"%1: series %2 has the value \"%3\""_s.arg(here).arg(s + 1).arg(it.key());
                }
            }
        }
    } else if (type == u"empty") {
        text("title");
        text("text");
    } else if (type == u"open") {
        text("label");
        const QString target = node.value(u"view"_s).toString();
        if (!target.isEmpty() && !views.contains(target))
            *problems << u"%1: no view \"%2\""_s.arg(here, target);
        const QVariant params = node.value(u"params"_s);
        if (params.isValid() && !params.isNull() && !isRecord(params))
            *problems << here + u": params is no record"_s;
    } else if (type == u"run") {
        text("label");
        if (!commands.contains(node.value(u"command"_s).toString()))
            *problems << u"%1: no command \"%2\""_s.arg(here, node.value(u"command"_s).toString());
    } else {
        *problems << here + u": an unknown node"_s;
    }
}

/// What a model would answer to anything: a turn that calls every tool the request offers (its
/// arguments made from the tool's schema) until the conversation has their results, then a text.
QVariantMap anyModel(const QVariantMap &request)
{
    const QVariantList tools = request.value(u"tools"_s).toList();
    bool answered = false;
    for (const QVariant &m : request.value(u"messages"_s).toList())
        answered = answered || m.toMap().contains(u"results"_s);
    QVariantMap reply{
            {u"model"_s, u"test-model"_s}, {u"inputTokens"_s, 12}, {u"outputTokens"_s, 7}, {u"raw"_s, u"[]"_s}};
    if (tools.isEmpty() || answered) {
        reply.insert(u"text"_s, u"Nic więcej do zrobienia."_s);
        reply.insert(u"stopReason"_s, u"end_turn"_s);
        return reply;
    }
    QVariantList calls;
    for (const QVariant &t : tools) {
        const QVariantMap tool = t.toMap();
        QVariantMap input;
        const QVariantMap properties = tool.value(u"input"_s).toMap().value(u"properties"_s).toMap();
        for (auto it = properties.cbegin(); it != properties.cend(); ++it)
            input.insert(it.key(), it.key() == u"due" ? u"2026-11-20"_s : u"Kupić mleko"_s);
        calls << QVariantMap{
                {u"id"_s, u"call-%1"_s.arg(calls.size() + 1)}, {u"name"_s, tool.value(u"name"_s)}, {u"input"_s, input}};
    }
    reply.insert(u"text"_s, QString());
    reply.insert(u"stopReason"_s, u"tool_use"_s);
    reply.insert(u"toolCalls"_s, calls);
    return reply;
}

} // namespace

class TestExtensionsSamples : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    QString m_dir;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<VaultIndex> m_index;
    std::unique_ptr<NotebookHost> m_host;
    std::unique_ptr<Engine> m_engine;
    QHash<QString, PageExtension> m_pages; ///< the extension pages, by id
    QStringList m_printed;                 ///< print / b.log, "extension: text"
    QList<Timer> m_timers;
    QStringList m_written;      ///< the paths extensions wrote, in order
    QList<HookRun> m_hooks;     ///< every hook the test fired, with its result
    QStringList m_viewProblems; ///< of the last view() call

    QString disk(const QString &path) const { return readFile(m_dir + u'/' + path); }

    /// The 0-based line of `path` that holds `needle`, -1 when none does.
    int lineOf(const QString &path, const QString &needle) const
    {
        const QStringList lines = disk(path).split(u'\n');
        for (int i = 0; i < lines.size(); ++i)
            if (lines[i].contains(needle))
                return i;
        return -1;
    }

    /// The line of `path` that holds `needle`, "" when none does.
    QString lineWith(const QString &path, const QString &needle) const
    {
        const int line = lineOf(path, needle);
        return line < 0 ? QString() : disk(path).split(u'\n').value(line);
    }

    /// Loads (or loads again) the extension page at `path` as the app does once it is approved.
    Result loadPage(const QString &path)
    {
        const ParsedNote *note = m_index->note(path);
        if (!note || !isExtensionPage(*note))
            return {Status::Error, {}, path + u" is no extension page"_s};
        const PageExtension page = readPageExtension(path, *note, m_index->content(path));
        if (!page.problems.isEmpty())
            return {Status::Error, {}, path + u": "_s + page.problems.join(u"; "_s)};
        m_pages.insert(page.id, page);
        return m_engine->load(page.extension());
    }

    /// A fresh notebook — the sample notes and the ten extension pages — and an engine with every
    /// extension page loaded; "" when all of them loaded.
    QString open(const QString &language = u"pl"_s,
                 const Limits &limits = Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20},
                 const QHash<QString, QString> &extraNotes = {})
    {
        m_engine.reset();
        m_host.reset();
        m_index.reset();
        m_vault.reset();
        m_pages.clear();
        m_printed.clear();
        m_timers.clear();
        m_written.clear();
        m_hooks.clear();
        m_dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QHash<QString, QString> files = extraNotes;
        for (const QString &file : filesUnder(kSamples + u"/notebook"_s))
            files.insert(file, readFile(kSamples + u"/notebook/"_s + file));
        for (const QString &file : filesUnder(kSamples + u"/extensions"_s))
            files.insert(kFolder + u'/' + file, readFile(kSamples + u"/extensions/"_s + file));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            if (!writeFile(m_dir + u'/' + it.key(), it.value()))
                return u"cannot write "_s + it.key();
        m_vault = std::make_unique<Vault>(m_dir, m_tmp.filePath(u"s%1"_s.arg(m_n)));
        if (!m_vault->open())
            return u"cannot open the notebook"_s;
        m_index = std::make_unique<VaultIndex>(m_vault->config());
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            m_index->update(it.key(), it.value());
        NotebookHost::Options options;
        options.now = [] { return QDateTime(QDate(2026, 11, 14), QTime(10, 30)); };
        options.language = language;
        options.uiLanguage = language;
        options.updateIndex = true;
        m_host = std::make_unique<NotebookHost>(*m_vault, *m_index, options);
        m_host->onLog = [this](const QString &extension, const QString &text) {
            m_printed << extension + u": "_s + text;
        };
        m_host->onTimer = [this](const QString &extension, int seconds, const QString &label) {
            m_timers.append({extension, seconds, label});
        };
        m_host->onWritten = [this](const QString &, const QString &path) { m_written << path; };
        m_engine = std::make_unique<Engine>();
        if (!m_engine->start(m_host.get(), limits))
            return u"cannot start the engine"_s;
        QStringList pages, errors;
        m_index->forEachNote([&](const VaultPath &path, const ParsedNote &note) {
            if (isExtensionPage(note))
                pages << path;
        });
        pages.sort();
        for (const QString &path : std::as_const(pages)) {
            const Result r = loadPage(path);
            if (!r.ok())
                errors << path + u": "_s + r.error;
        }
        return errors.join(u'\n');
    }

    /// Answers what a call waits for until it ends: the questions with `answers` in order (a
    /// QString for b.ask and b.choose, a QVariantMap for b.form, an invalid QVariant to cancel; none
    /// left: cancelled), b.ai with `model` (none: the provider's error).
    Outcome drive(Result r, QVariantList answers, const Model &model)
    {
        Outcome out;
        for (int turns = 0; r.status == Status::Suspended && turns < 200; ++turns) {
            const QVariantMap question = r.value.toMap();
            out.questions << question;
            const QString kind = question.value(u"kind"_s).toString();
            QVariant answer;
            if (kind == u"ai")
                answer = model ? model(question.value(u"request"_s).toMap())
                               : QVariantMap{{u"error"_s, u"no API key is set (Settings → Extensions → AI)"_s}};
            else if (!answers.isEmpty())
                answer = answers.takeFirst();
            else if (kind == u"ask")
                answer = QString(); // cancelled
            r = m_engine->resume(r.call, answer);
        }
        out.result = r;
        return out;
    }

    /// A command from the palette: `page` is the page on screen ("" = none), `line` the caret's
    /// line there (a command with `block = true`).
    Outcome command(const QString &extension, const QString &name, const QString &page = QString(), int line = -1,
                    const QVariantList &answers = {}, const Model &model = {})
    {
        const VaultPath path = page.isEmpty() ? VaultPath() : m_host->pathOf(page);
        QVariantMap ctx{{u"page"_s, path.isEmpty() ? QString() : paths::pageTitle(path)}, {u"selection"_s, QString()}};
        if (!path.isEmpty() && line >= 0) {
            const QVariantMap block = m_host->blockAt(path, line);
            if (!block.isEmpty())
                ctx.insert(u"block"_s, block);
        }
        return drive(m_engine->runCommand(extension, name, ctx), answers, model);
    }

    /// A view drawn, as the app draws it: a view never waits, and its tree is checked against the
    /// documentation (m_viewProblems).
    Result view(const QString &extension, const QString &name, const QVariantMap &params = {})
    {
        m_viewProblems.clear();
        Result r = m_engine->render(extension, name, params);
        if (r.status == Status::Suspended) {
            m_engine->cancel(r.call);
            return {Status::Error, {}, u"the view waits for a question or for AI"_s};
        }
        if (!r.ok())
            return r;
        QStringList views, commands;
        ViewInfo info;
        for (const ViewInfo &v : m_engine->views()) {
            if (v.extension != extension)
                continue;
            views << v.name;
            if (v.name == name)
                info = v;
        }
        for (const CommandInfo &c : m_engine->commands())
            if (c.extension == extension)
                commands << c.name;
        checkNode(r.value, info, views, commands, false, name, &m_viewProblems);
        return r;
    }

    Outcome act(const QString &extension, const QString &name, const QString &action, const QVariantMap &args,
                const QVariantList &answers = {})
    {
        return drive(m_engine->act(extension, name, action, args), answers, {});
    }

    /// Taps the control `node` of a drawn view (a button, a checkbox; a stepper with its new value).
    Outcome tap(const QString &extension, const QString &name, const QVariantMap &node, double value = 0)
    {
        QVariantMap args = node.value(u"args"_s).toMap();
        if (node.value(u"type"_s).toString() == u"stepper")
            args.insert(u"value"_s, value);
        return act(extension, name, node.value(u"action"_s).toString(), args);
    }

    void fire(const QString &event, const QVariantMap &arg)
    {
        for (const HookInfo &h : m_engine->hooks(event)) {
            const Outcome out = drive(m_engine->runHook(h.id, arg), {}, anyModel);
            m_hooks.append({h.extension, event, out.result});
        }
    }

    /// The user's editor saves `path`: the file, the index, then the hooks as the app runs them —
    /// "save" for the page and "change" with the pages changed. What extensions write in their
    /// hooks does not come back as another "change".
    bool userSaves(const QString &path, const QString &content)
    {
        if (!m_vault->writeText(path, content))
            return false;
        m_index->update(path, content);
        const QString title = paths::pageTitle(path);
        fire(u"save"_s, {{u"page"_s, title}, {u"path"_s, path}});
        fire(u"change"_s, {{u"pages"_s, QStringList{title}}});
        return true;
    }

    /// Notes change behind the app (a sync): the file and the index, "change" hooks only.
    bool syncBrings(const QString &path, const QString &content)
    {
        if (!m_vault->writeText(path, content, ChangeSource::External))
            return false;
        m_index->update(path, content);
        fire(u"change"_s, {{u"pages"_s, QStringList{paths::pageTitle(path)}}});
        return true;
    }

    /// The first control of `tree` of `type` whose label (or action) is `what`.
    static QVariantMap control(const QVariant &tree, const QString &type, const QString &what)
    {
        for (const QVariantMap &node : nodesOf(tree, type))
            if (node.value(u"label"_s).toString() == what || node.value(u"action"_s).toString() == what)
                return node;
        return {};
    }

    /// The rows of the view (b.ui.row) that have a text or a link saying `what`.
    static QList<QVariantMap> rowsWith(const QVariant &tree, const QString &what)
    {
        QList<QVariantMap> out;
        for (const QVariantMap &row : nodesOf(tree, u"row"_s)) {
            const QVariant value = row;
            if (textsOf(value, u"text"_s).join(u'\n').contains(what) || textsOf(value, u"link"_s).contains(what))
                out << row;
        }
        return out;
    }

private Q_SLOTS:
    void init()
    {
        const QString errors = open();
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
    }

    void cleanup()
    {
        m_engine.reset();
        m_host.reset();
    }

    // ---- all of them ------------------------------------------------------------------------------

    void theyLoadAsPages_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QStringList>("permissions");
        QTest::addColumn<QStringList>("commands");
        QTest::addColumn<QStringList>("views");
        QTest::addColumn<QStringList>("actions");
        QTest::addColumn<QStringList>("hooks");
        const QStringList none, write{u"write"_s};
        QTest::newRow("licznik-slow") << u"licznik-slow"_s << u"Licznik słów"_s << none
                                      << QStringList{u"policz"_s, u"policz-blok"_s, u"ranking"_s}
                                      << QStringList{u"statystyki"_s} << none << none;
        QTest::newRow("przeglad-zadan") << u"przeglad-zadan"_s << u"Przegląd zadań"_s << write
                                        << QStringList{u"na-dzis"_s, u"termin"_s} << QStringList{u"przeglad"_s}
                                        << QStringList{u"horyzont"_s, u"przesun"_s, u"zrobione"_s} << none;
        QTest::newRow("nastroj") << u"nastroj"_s << u"Nastrój"_s << write << QStringList{u"zapisz"_s}
                                 << QStringList{u"wykres"_s} << none << none;
        QTest::newRow("wydatki") << u"wydatki"_s << u"Wydatki"_s << write << QStringList{u"dodaj"_s}
                                 << QStringList{u"miesiac"_s} << none << none;
        QTest::newRow("indeks-projektow")
                << u"indeks-projektow"_s << u"Indeks projektów"_s << write << QStringList{u"odswiez"_s} << none << none
                << QStringList{u"change"_s, u"save"_s};
        QTest::newRow("pomodoro") << u"pomodoro"_s << u"Pomodoro"_s << write
                                  << QStringList{u"break"_s, u"start"_s, u"start-here"_s} << QStringList{u"today"_s}
                                  << none << none;
        QTest::newRow("quick-entries") << u"quick-entries"_s << u"Quick entries"_s << write << QStringList{u"new"_s}
                                       << QStringList{u"list"_s} << none << none;
        QTest::newRow("fiszki") << u"fiszki"_s << u"Fiszki"_s << write << QStringList{u"dodaj"_s}
                                << QStringList{u"powtorka"_s} << QStringList{u"ocen"_s} << none;
        QTest::newRow("link-check") << u"link-check"_s << u"Link check"_s << none << QStringList{u"report"_s}
                                    << QStringList{u"broken"_s} << none << none;
        QTest::newRow("action-items") << u"action-items"_s << u"Action items"_s << QStringList{u"ai"_s, u"write"_s}
                                      << QStringList{u"extract"_s} << none << none << none;
    }
    void theyLoadAsPages()
    {
        // The page's properties, in Polish or English, give its id and permissions; its ```luau
        // blocks register what the palette offers.
        QFETCH(QString, id);
        QFETCH(QString, title);
        QFETCH(QStringList, permissions);
        QFETCH(QStringList, commands);
        QFETCH(QStringList, views);
        QFETCH(QStringList, actions);
        QFETCH(QStringList, hooks);
        QVERIFY2(m_pages.contains(id), qPrintable(m_pages.keys().join(u' ')));
        const PageExtension page = m_pages.value(id);
        QCOMPARE(page.title, title);
        QCOMPARE(page.path, kFolder + u'/' + title + u".md"_s);
        QCOMPARE(page.permissions, permissions);
        QVERIFY(!page.description.isEmpty());
        QVERIFY(page.blocks >= 2); // prose between the blocks: literate code
        QVERIFY(m_engine->isLoaded(id));
        QStringList foundCommands, foundViews, foundActions, foundHooks;
        for (const CommandInfo &c : m_engine->commands()) {
            if (c.extension != id)
                continue;
            foundCommands << c.name;
            QVERIFY2(c.label != c.name, qPrintable(c.name)); // the palette shows a label
        }
        for (const ViewInfo &v : m_engine->views()) {
            if (v.extension != id)
                continue;
            foundViews << v.name;
            foundActions << v.actions;
            QVERIFY2(!v.label.isEmpty() && !v.icon.isEmpty(), qPrintable(v.name));
        }
        for (const HookInfo &h : m_engine->hooks())
            if (h.extension == id)
                foundHooks << h.event;
        foundHooks.sort();
        QCOMPARE(foundCommands, commands);
        QCOMPARE(foundViews, views);
        QCOMPARE(foundActions, actions);
        QCOMPARE(foundHooks, hooks);
        QVERIFY2(m_printed.isEmpty(), qPrintable(m_printed.join(u'\n'))); // loading is silent
        QVERIFY2(m_written.isEmpty(), qPrintable(m_written.join(u'\n'))); // and writes nothing
    }

    void everythingRunsInBothLanguages_data()
    {
        QTest::addColumn<QString>("language");
        QTest::newRow("pl") << u"pl"_s;
        QTest::newRow("en") << u"en"_s;
    }
    void everythingRunsInBothLanguages()
    {
        // Whatever the palette offers runs to its end with a user who answers every question, in a
        // Polish and in an English app: every command (on a page; with a task under the caret),
        // every view and every control it shows, every hook.
        QFETCH(QString, language);
        const QString errors = open(language);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        int commands = 0, views = 0, taps = 0;
        for (const CommandInfo &c : m_engine->commands()) {
            const int line = c.needsBlock ? lineOf(u"Dom.md"_s, u"Zapłacić czynsz"_s) : -1;
            // The answers a user may give: a text; the first option; a form as shown, a step up.
            Result r = m_engine->runCommand(c.extension, c.name, [&] {
                QVariantMap ctx{{u"page"_s, u"Dom"_s}, {u"selection"_s, QString()}};
                if (line >= 0)
                    ctx.insert(u"block"_s, m_host->blockAt(u"Dom.md"_s, line));
                return ctx;
            }());
            for (int turns = 0; r.status == Status::Suspended && turns < 60; ++turns) {
                const QVariantMap question = r.value.toMap();
                const QString kind = question.value(u"kind"_s).toString();
                QVariant answer;
                if (kind == u"ai") {
                    answer = anyModel(question.value(u"request"_s).toMap());
                } else if (kind == u"ask") {
                    answer = u"Kawa i ciastko"_s;
                } else if (kind == u"choose") {
                    answer = question.value(u"options"_s).toStringList().value(0);
                } else if (kind == u"form") {
                    QVariantMap values;
                    for (const QVariant &f : question.value(u"fields"_s).toList()) {
                        const QVariantMap field = f.toMap();
                        values.insert(field.value(u"key"_s).toString(),
                                      field.value(u"value"_s).toDouble() + field.value(u"step"_s, 1).toDouble());
                    }
                    answer = values;
                }
                r = m_engine->resume(r.call, answer);
            }
            QVERIFY2(r.ok(), qPrintable(u"%1/%2: %3"_s.arg(c.extension, c.name, r.error)));
            ++commands;
        }
        // A view as the app shows it: drawn, then one tap on each kind of control it has (an action
        // by its name), drawn again after each tap — a control of the tree before may be stale.
        // "" when all of it worked.
        const auto exercise = [&](const ViewInfo &v, const QVariantMap &params) -> QString {
            Result r = view(v.extension, v.name, params);
            QStringList tapped;
            for (;;) {
                if (!r.ok())
                    return r.error;
                if (!m_viewProblems.isEmpty())
                    return m_viewProblems.join(u"; "_s);
                QVariantMap node;
                for (const QVariantMap &candidate : nodesOf(r.value)) {
                    const QString action = candidate.value(u"action"_s).toString();
                    if (!action.isEmpty() && !tapped.contains(action)) {
                        node = candidate;
                        break;
                    }
                }
                if (node.isEmpty())
                    return {};
                const QString action = node.value(u"action"_s).toString();
                tapped << action;
                const Outcome out = tap(v.extension, v.name, node, node.value(u"value"_s).toDouble() + 1);
                if (!out.result.ok())
                    return action + u": "_s + out.result.error;
                ++taps;
                r = view(v.extension, v.name, params);
            }
        };
        for (const ViewInfo &v : m_engine->views()) {
            QString failure = exercise(v, {});
            QVERIFY2(failure.isEmpty(), qPrintable(u"%1/%2: %3"_s.arg(v.extension, v.name, failure)));
            // And where its b.ui.open nodes lead: this view (or another) with their params.
            const Result r = view(v.extension, v.name);
            for (const QVariantMap &node : nodesOf(r.value, u"open"_s)) {
                const QString target = node.value(u"view"_s).toString();
                ViewInfo opened = v;
                for (const ViewInfo &other : m_engine->views())
                    if (other.extension == v.extension && other.name == target)
                        opened = other;
                failure = exercise(opened, node.value(u"params"_s).toMap());
                QVERIFY2(failure.isEmpty(), qPrintable(u"%1/%2 opened: %3"_s.arg(v.extension, opened.name, failure)));
            }
            ++views;
        }
        QVERIFY(userSaves(u"Dom.md"_s, disk(u"Dom.md"_s) + u"- [ ] Podlać kwiaty\n"_s));
        for (const HookRun &h : std::as_const(m_hooks))
            QVERIFY2(h.result.ok(), qPrintable(u"%1, %2: %3"_s.arg(h.extension, h.event, h.result.error)));
        QCOMPARE(commands, 15);
        QCOMPARE(views, 8);
        QCOMPARE(taps, 4); // three actions of the task review, a card's grade behind "Pokaż odpowiedź"
        QCOMPARE(m_hooks.size(), 2);
    }

    // ---- Licznik słów: only reads ---------------------------------------------------------------------

    void wordCount()
    {
        const QString id = u"licznik-slow"_s;
        // A page: its text without the properties, Polish letters counted as characters, the
        // message in Polish plural forms.
        Outcome out = command(id, u"policz"_s, u"Lalka"_s);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        const QString body = disk(u"Lalka.md"_s).section(u"---\n"_s, 2);
        QCOMPARE(body.toUcs4().size(), 133);
        QCOMPARE(out.message(), u"„Lalka”: 19 słów, 133 znaki, ok. 1 min czytania"_s);
        // No page on screen (the journal's stream, a view).
        QCOMPARE(command(id, u"policz"_s).message(), u"Otwórz najpierw stronę."_s);

        // The block under the caret, alone and with what is under it.
        out = command(id, u"policz-blok"_s, u"Dom"_s, lineOf(u"Dom.md"_s, u"Żółw je sałatę"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Blok: 6 słów, 33 znaki, ok. 1 min czytania; z podpunktami: 13 słów"_s);
        out = command(id, u"policz-blok"_s, u"Dom"_s, lineOf(u"Dom.md"_s, u"szczegół pierwszy"_s));
        QCOMPARE(out.message(), u"Blok: 2 słowa, 17 znaków, ok. 1 min czytania"_s);
        // The caret on a line that is no block (the frontmatter's closing line).
        QCOMPARE(command(id, u"policz-blok"_s, u"Dom"_s, 3).message(), u"Ustaw kursor w bloku."_s);

        // The ranking: ten lines in the console, the longest page first; the message counts all.
        m_printed.clear();
        out = command(id, u"ranking"_s);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(m_printed.size(), 10);
        static const QRegularExpression line(u"^licznik-slow: (\\d+)\\. (.+): (\\d+) (słowo|słowa|słów)$"_s);
        int previous = INT_MAX;
        for (int i = 0; i < m_printed.size(); ++i) {
            const QRegularExpressionMatch m = line.match(m_printed[i]);
            QVERIFY2(m.hasMatch(), qPrintable(m_printed[i]));
            QCOMPARE(m.captured(1).toInt(), i + 1);
            QVERIFY(m.captured(3).toInt() <= previous);
            previous = m.captured(3).toInt();
        }
        // (The words of every page are counted, these extensions' own pages too: whatever the sum.)
        static const QRegularExpression summary(
                u"^27 stron, razem \\d+ (słowo|słowa|słów)\\. Ranking jest w konsoli Luau\\.$"_s);
        QVERIFY2(summary.match(out.message()).hasMatch(), qPrintable(out.message()));

        // The view: the same ranking as a table of four columns, a link to the longest page.
        const Result r = view(id, u"statystyki"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        const QVariantMap table = nodesOf(r.value, u"table"_s).value(0);
        QCOMPARE(table.value(u"columns"_s).toStringList(), (QStringList{u"Nr"_s, u"Strona"_s, u"Słowa"_s, u"Znaki"_s}));
        const QVariantList rows = table.value(u"rows"_s).toList();
        QCOMPARE(rows.size(), 15);
        QCOMPARE(rows[0].toStringList().value(0), u"1."_s);
        QCOMPARE(textsOf(r.value, u"link"_s), QStringList{rows[0].toStringList().value(1)});
        QCOMPARE(nodesOf(r.value, u"run"_s).value(0).value(u"command"_s).toString(), u"ranking"_s);

        // It has no permission: its `b` cannot write, whatever the code.
        const Result denied = m_engine->evaluate(id, u"b.write(\"Lalka\", \"\")"_s, u"proba"_s);
        QCOMPARE(denied.status, Status::Error);
        QVERIFY2(denied.error.contains(u"b.write: permission denied: write"_s), qPrintable(denied.error));
        QVERIFY(m_written.isEmpty());
    }

    // ---- Przegląd zadań: a view that changes tasks ----------------------------------------------------

    void taskReview()
    {
        const QString id = u"przeglad-zadan"_s, name = u"przeglad"_s;
        Result r = view(id, name);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"heading"_s),
                 (QStringList{u"Zaległe (2)"_s, u"Na dziś (1)"_s, u"Najbliższe dni: 7 (2)"_s}));
        // The overdue ones oldest first, each a row: checkbox, text without its dates, date, page.
        const QList<QVariantMap> rows = nodesOf(r.value, u"row"_s);
        QCOMPARE(rows.size(), 6);
        QCOMPARE(textsOf(rows[0], u"text"_s), (QStringList{u"Zapłacić czynsz ⏫"_s, u"2026-11-10"_s}));
        QCOMPARE(textsOf(rows[0], u"link"_s), QStringList{u"Dom"_s});
        QCOMPARE(textsOf(rows[1], u"text"_s), (QStringList{u"Odpisać na maile"_s, u"2026-11-12"_s}));
        QCOMPARE(textsOf(rows[2], u"text"_s).value(0), u"Wymienić żarówkę w łazience"_s);
        QCOMPARE(textsOf(rows[3], u"text"_s).value(0), u"Pomalować pokój"_s);
        QCOMPARE(textsOf(rows[4], u"text"_s).value(0), u"Raport kwartalny 🔼"_s);

        // The checkbox: the task is done in its note, with today's date.
        Outcome out = tap(id, name, control(rows[0], u"check"_s, u"zrobione"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(lineWith(u"Dom.md"_s, u"czynsz"_s), u"- [x] Zapłacić czynsz 📅 2026-11-10 ⏫ ✅ 2026-11-14"_s);
        // "+7 dni": a week from today, the rest of the line as it was.
        out = tap(id, name, control(rows[1], u"button"_s, u"+7 dni"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Nowy termin: 2026-11-21"_s);
        QCOMPARE(lineWith(u"Praca.md"_s, u"maile"_s), u"- [ ] Odpisać na maile 📅 2026-11-21"_s);
        r = view(id, name);
        QCOMPARE(textsOf(r.value, u"heading"_s), (QStringList{u"Na dziś (1)"_s, u"Najbliższe dni: 7 (3)"_s}));

        // A line that changed since the view was drawn (the editor, a sync): the tap is refused,
        // nothing is overwritten.
        const QVariantMap staleCheck =
                control(rowsWith(r.value, u"Wymienić żarówkę"_s).value(0), u"check"_s, u"zrobione"_s);
        QVERIFY(!staleCheck.isEmpty());
        QString dom = disk(u"Dom.md"_s);
        dom.replace(u"Wymienić żarówkę w łazience"_s, u"Wymienić żarówkę w kuchni"_s);
        QVERIFY(userSaves(u"Dom.md"_s, dom));
        out = tap(id, name, staleCheck);
        QCOMPARE(out.result.status, Status::Error);
        QVERIFY2(out.result.error.contains(u"b.replaceBlock: stale:"_s), qPrintable(out.result.error));
        QVERIFY2(out.result.error.startsWith(u"Przegląd zadań:"_s), qPrintable(out.result.error));
        QCOMPARE(disk(u"Dom.md"_s), dom);

        // The stepper keeps how far ahead the view looks (a variable of the script), within 1 … 60.
        const QVariantMap stepper = nodesOf(r.value, u"stepper"_s).value(0);
        QCOMPARE(stepper.value(u"value"_s).toInt(), 7);
        QVERIFY(tap(id, name, stepper, 3).result.ok());
        r = view(id, name);
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Na dziś (1)"_s});
        QCOMPARE(nodesOf(r.value, u"stepper"_s).value(0).value(u"value"_s).toInt(), 3);
        QVERIFY(tap(id, name, stepper, 100).result.ok());
        r = view(id, name);
        QCOMPARE(textsOf(r.value, u"heading"_s), (QStringList{u"Na dziś (1)"_s, u"Najbliższe dni: 60 (4)"_s}));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
    }

    void overdueTasksMovedToToday()
    {
        const QString id = u"przeglad-zadan"_s;
        // "Zostaw" (and a closed dialog) changes nothing.
        Outcome out = command(id, u"na-dzis"_s, {}, -1, {u"Zostaw"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QVERIFY(out.result.value.isNull());
        QCOMPARE(out.questions.size(), 1);
        QCOMPARE(out.questions[0].toMap().value(u"prompt"_s).toString(), u"Przesunąć zaległe zadania (2) na dziś?"_s);
        QVERIFY(m_written.isEmpty());
        out = command(id, u"na-dzis"_s, {}, -1, {QVariant()});
        QVERIFY(out.result.ok() && m_written.isEmpty());

        // "Przesuń": every overdue task is due today, the other fields of its line untouched.
        out = command(id, u"na-dzis"_s, {}, -1, {u"Przesuń"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Przesunięte na dziś: 2"_s);
        QCOMPARE(lineWith(u"Dom.md"_s, u"czynsz"_s), u"- [ ] Zapłacić czynsz 📅 2026-11-14 ⏫"_s);
        QCOMPARE(lineWith(u"Praca.md"_s, u"maile"_s), u"- [ ] Odpisać na maile 📅 2026-11-14"_s);
        QCOMPARE(m_written, (QStringList{u"Dom.md"_s, u"Praca.md"_s}));
        // Nothing is overdue now.
        QCOMPARE(command(id, u"na-dzis"_s).message(), u"Nie ma zaległych zadań."_s);
    }

    void dueDateOfTheTaskUnderTheCaret_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("needle");
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QString>("message");
        QTest::addColumn<QString>("line");
        QTest::newRow("tomorrow") << u"Zakupy"_s << u"mleko"_s << u"Jutro"_s << u"Termin: 2026-11-15"_s
                                  << u"- [ ] mleko 📅 2026-11-15"_s;
        QTest::newRow("today, with diacritics")
                << u"Zakupy"_s << u"mleko"_s << u"dziś"_s << u"Termin: 2026-11-14"_s << u"- [ ] mleko 📅 2026-11-14"_s;
        QTest::newRow("days") << u"Zakupy"_s << u"chleb"_s << u"+3"_s << u"Termin: 2026-11-17"_s
                              << u"- [ ] chleb żytni 📅 2026-11-17"_s;
        QTest::newRow("a date replaces the date")
                << u"Dom"_s << u"czynsz"_s << u"2026-12-24"_s << u"Termin: 2026-12-24"_s
                << u"- [ ] Zapłacić czynsz 📅 2026-12-24 ⏫"_s;
        QTest::newRow("over the year's end") << u"Dom"_s << u"czynsz"_s << u"60"_s << u"Termin: 2027-01-13"_s
                                             << u"- [ ] Zapłacić czynsz 📅 2027-01-13 ⏫"_s;
        QTest::newRow("no such day") << u"Dom"_s << u"czynsz"_s << u"2026-02-30"_s
                                     << u"Nie rozumiem terminu „2026-02-30”."_s
                                     << u"- [ ] Zapłacić czynsz 📅 2026-11-10 ⏫"_s;
        QTest::newRow("words") << u"Dom"_s << u"czynsz"_s << u"kiedyś"_s << u"Nie rozumiem terminu „kiedyś”."_s
                               << u"- [ ] Zapłacić czynsz 📅 2026-11-10 ⏫"_s;
        QTest::newRow("cancelled") << u"Dom"_s << u"czynsz"_s << QString() << QString()
                                   << u"- [ ] Zapłacić czynsz 📅 2026-11-10 ⏫"_s;
        QTest::newRow("not a task") << u"Dom"_s << u"Żółw je sałatę"_s << u"jutro"_s << u"Ustaw kursor w zadaniu."_s
                                    << u"- Żółw je sałatę w ogrodzie #pomysł"_s;
    }
    void dueDateOfTheTaskUnderTheCaret()
    {
        QFETCH(QString, page);
        QFETCH(QString, needle);
        QFETCH(QString, answer);
        QFETCH(QString, message);
        QFETCH(QString, line);
        const QString path = page + u".md"_s;
        const Outcome out = command(u"przeglad-zadan"_s, u"termin"_s, page, lineOf(path, needle), {answer});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), message);
        QCOMPARE(lineWith(path, needle), line);
    }

    // ---- Nastrój: a form, properties, a chart with gaps ---------------------------------------------------

    void mood()
    {
        const QString id = u"nastroj"_s;
        Result r = view(id, u"wykres"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"text"_s),
                 QStringList{u"Ostatnie 14 dni: nastrój 3,5, energia 3,5 (dni z wpisem: 6)."_s});
        // Two weeks of days; a day without an entry is a gap; of two entries of a day the last counts.
        QVariantMap chart = nodesOf(r.value, u"chart"_s).value(0);
        const QStringList labels = chart.value(u"labels"_s).toStringList();
        QCOMPARE(labels.size(), 14);
        QCOMPARE(labels.first(), u"11-01"_s);
        QCOMPARE(labels.last(), u"11-14"_s);
        const QVariant gap;
        QCOMPARE(seriesValues(chart, 0), (QVariantList{gap, gap, 2, gap, 3, gap, gap, 5, gap, 4, gap, 4, 3, gap}));
        QCOMPARE(seriesValues(chart, 1), (QVariantList{gap, gap, 2, gap, 4, gap, gap, 4, gap, 3, gap, 5, 3, gap}));
        // Averages by weekday, Monday first, numbers as a Polish notebook writes them.
        const QVariantList rows = nodesOf(r.value, u"table"_s).value(0).value(u"rows"_s).toList();
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].toStringList(), (QStringList{u"2"_s, u"wtorek"_s, u"3"_s, u"2,5"_s}));
        QCOMPARE(rows[1].toStringList(), (QStringList{u"2"_s, u"czwartek"_s, u"3,5"_s, u"4,5"_s}));
        QCOMPARE(rows[2].toStringList(), (QStringList{u"1"_s, u"piątek"_s, u"3"_s, u"3"_s}));
        QCOMPARE(rows[3].toStringList(), (QStringList{u"1"_s, u"niedziela"_s, u"5"_s, u"4"_s}));

        // The command: the form's numbers are rounded into 1 … 5 and written as the block's
        // properties, the note in its text.
        const QString before = disk(kJournal);
        Outcome out = command(id, u"zapisz"_s, {}, -1,
                              {QVariantMap{{u"nastroj"_s, 4.4}, {u"energia"_s, 9}}, u"po bieganiu"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Zapisano: nastrój 4/5, energia 5/5"_s);
        QCOMPARE(out.questions.size(), 2);
        QCOMPARE(out.questions[0].toMap().value(u"kind"_s).toString(), u"form"_s);
        QCOMPARE(out.questions[0].toMap().value(u"fields"_s).toList().size(), 2);
        QCOMPARE(disk(kJournal), before + u"- Samopoczucie: po bieganiu #nastrój\n  nastroj:: 4\n  energia:: 5\n"_s);
        // The view reads it back from the note.
        r = view(id, u"wykres"_s);
        chart = nodesOf(r.value, u"chart"_s).value(0);
        QCOMPARE(seriesValues(chart, 0).last(), QVariant(4));
        QCOMPARE(seriesValues(chart, 1).last(), QVariant(5));
        QVERIFY(textsOf(r.value, u"text"_s).value(0).contains(u"(dni z wpisem: 7)"_s));

        // Without a note (the question cancelled); and the form cancelled: nothing at all.
        out = command(id, u"zapisz"_s, {}, -1, {QVariantMap{{u"nastroj"_s, 1}, {u"energia"_s, 2}}});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QVERIFY(disk(kJournal).endsWith(u"- Samopoczucie #nastrój\n  nastroj:: 1\n  energia:: 2\n"_s));
        const QString after = disk(kJournal);
        out = command(id, u"zapisz"_s, {}, -1, {QVariant()});
        QVERIFY(out.result.ok() && out.result.value.isNull());
        QCOMPARE(disk(kJournal), after);
    }

    // ---- Wydatki: numbers, months, two languages ------------------------------------------------------

    void expenses()
    {
        const QString id = u"wydatki"_s, name = u"miesiac"_s;
        Result r = view(id, name);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Wydatki: listopad 2026"_s});
        // "12,50", "1 250", "8.90", either tag; a line without an amount is no expense.
        QCOMPARE(textsOf(r.value, u"text"_s), QStringList{u"Razem: 1361,4 zł"_s});
        QVariantList rows = nodesOf(r.value, u"table"_s).value(0).value(u"rows"_s).toList();
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows[0].toStringList(), (QStringList{u"92%"_s, u"mieszkanie"_s, u"1250 zł"_s, u"1"_s}));
        QCOMPARE(rows[1].toStringList(), (QStringList{u"5%"_s, u"jedzenie"_s, u"66,4 zł"_s, u"3"_s}));
        QCOMPARE(rows[2].toStringList(), (QStringList{u"2%"_s, u"Transport"_s, u"30 zł"_s, u"1"_s}));
        QCOMPARE(rows[3].toStringList(), (QStringList{u"1%"_s, u"książki"_s, u"15 zł"_s, u"1"_s}));
        const QVariantMap chart = nodesOf(r.value, u"chart"_s).value(0);
        QCOMPARE(chart.value(u"kind"_s).toString(), u"bar"_s);
        QCOMPARE(chart.value(u"labels"_s).toStringList(),
                 (QStringList{u"2026-06"_s, u"2026-07"_s, u"2026-08"_s, u"2026-09"_s, u"2026-10"_s, u"2026-11"_s}));
        const QVariantList totals = seriesValues(chart, 0);
        QCOMPARE(totals.mid(0, 4), (QVariantList{0, 0, 0, 0}));
        QVERIFY(qAbs(totals.value(4).toDouble() - 155.5) < 1e-9 && qAbs(totals.value(5).toDouble() - 1361.4) < 1e-9);

        // The arrows are b.ui.open nodes of this view with another month.
        const QList<QVariantMap> arrows = nodesOf(r.value, u"open"_s);
        QCOMPARE(arrows.size(), 2);
        QCOMPARE(arrows[0].value(u"label"_s).toString(), u"← październik 2026"_s);
        QCOMPARE(arrows[0].value(u"view"_s).toString(), QString());
        QCOMPARE(arrows[0].value(u"params"_s).toMap(), (QVariantMap{{u"month"_s, u"2026-10"_s}}));
        QCOMPARE(arrows[1].value(u"label"_s).toString(), u"grudzień 2026 →"_s);
        r = view(id, name, arrows[0].value(u"params"_s).toMap());
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Wydatki: październik 2026"_s});
        QCOMPARE(textsOf(r.value, u"text"_s), QStringList{u"Razem: 155,5 zł"_s});
        rows = nodesOf(r.value, u"table"_s).value(0).value(u"rows"_s).toList();
        QCOMPARE(rows[0].toStringList(), (QStringList{u"77%"_s, u"transport"_s, u"120 zł"_s, u"1"_s}));
        QCOMPARE(nodesOf(r.value, u"open"_s).value(0).value(u"label"_s).toString(), u"← wrzesień 2026"_s);
        // January's neighbours are in other years; a month without expenses says so.
        r = view(id, name, {{u"month"_s, u"2026-01"_s}});
        QCOMPARE(textsOf(r.value, u"open"_s), (QStringList{u"← grudzień 2025"_s, u"luty 2026 →"_s}));
        QCOMPARE(textsOf(r.value, u"empty"_s), QStringList{u"Brak wydatków"_s});
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        // Params come from a route: anything that is no month means this month.
        const QVariantList notMonths{u"listopad"_s, 7, u"2026-13-01"_s, u"2026-13"_s, u"2026-00"_s, u"0000-01"_s,
                                     QVariantMap{}};
        for (const QVariant &bad : notMonths) {
            r = view(id, name, {{u"month"_s, bad}});
            QVERIFY2(r.ok(), qPrintable(r.error));
            QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Wydatki: listopad 2026"_s});
        }

        // Adding: the amount from the form, a category of the last months (the most used first),
        // a note; written as the notebook writes numbers.
        const QString before = disk(kJournal);
        Outcome out = command(id, u"dodaj"_s, {}, -1, {QVariantMap{{u"amount"_s, 12.5}}, u"jedzenie"_s, u"kebab"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Dopisano: 12,5 zł, jedzenie"_s);
        QCOMPARE(out.questions[0].toMap().value(u"fields"_s).toList().value(0).toMap().value(u"unit"_s).toString(),
                 u"zł"_s);
        QCOMPARE(out.questions[1].toMap().value(u"options"_s).toStringList(),
                 (QStringList{u"jedzenie"_s, u"transport"_s, u"książki"_s, u"mieszkanie"_s, u"Inna…"_s}));
        QCOMPARE(disk(kJournal), before + u"- 12,5 zł jedzenie: kebab #wydatek\n"_s);
        r = view(id, name);
        QCOMPARE(textsOf(r.value, u"text"_s), QStringList{u"Razem: 1373,9 zł"_s});
        // A new category (a colon would end it: it is taken out), no note.
        out = command(id, u"dodaj"_s, {}, -1, {QVariantMap{{u"amount"_s, 40}}, u"Inna…"_s, u"zdrowie: leki"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QVERIFY2(disk(kJournal).endsWith(u"- 40 zł zdrowie leki #wydatek\n"_s), qPrintable(disk(kJournal)));
        // Nothing for an amount of zero or a closed dialog.
        const QString after = disk(kJournal);
        QCOMPARE(command(id, u"dodaj"_s, {}, -1, {QVariantMap{{u"amount"_s, 0}}}).message(),
                 u"Kwota musi być większa od zera."_s);
        QVERIFY(command(id, u"dodaj"_s, {}, -1, {QVariant()}).result.value.isNull());
        QVERIFY(command(id, u"dodaj"_s, {}, -1, {QVariantMap{{u"amount"_s, 5}}, QVariant()}).result.value.isNull());
        QCOMPARE(disk(kJournal), after);
    }

    void expensesInEnglish()
    {
        // The same page in an English app and notebook: its labels, the decimal point, the English
        // tag for what it writes; the currency is still the page's own property.
        const QString errors = open(u"en"_s);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        const QString id = u"wydatki"_s;
        for (const CommandInfo &c : m_engine->commands())
            if (c.extension == id)
                QCOMPARE(c.label, u"Expenses: add"_s);
        const Result r = view(id, u"miesiac"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Expenses: November 2026"_s});
        QCOMPARE(textsOf(r.value, u"text"_s), QStringList{u"Total: 1361.4 zł"_s});
        QCOMPARE(textsOf(r.value, u"open"_s), (QStringList{u"← October 2026"_s, u"December 2026 →"_s}));
        QCOMPARE(nodesOf(r.value, u"table"_s).value(0).value(u"columns"_s).toStringList(),
                 (QStringList{u"Share"_s, u"Category"_s, u"Amount"_s, u"Entries"_s}));
        const Outcome out =
                command(id, u"dodaj"_s, {}, -1, {QVariantMap{{u"amount"_s, 12.5}}, u"jedzenie"_s, u"kebab"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Added: 12.5 zł, jedzenie"_s);
        QVERIFY2(disk(kJournal).endsWith(u"- 12.5 zł jedzenie: kebab #expense\n"_s), qPrintable(disk(kJournal)));
    }

    // ---- Indeks projektów: hooks that keep a page written ----------------------------------------------

    void projectIndex()
    {
        const QString id = u"indeks-projektow"_s, page = u"Projekty.md"_s;
        QVERIFY(!QFile::exists(m_dir + u'/' + page));
        // The command writes the page: projects by status, their open tasks, the nearest due date.
        Outcome out = command(id, u"odswiez"_s);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Strona „Projekty” przepisana, projektów: 4"_s);
        const QString head =
                u"---\ntags: [spis]\n---\n"
                "Tę stronę pisze rozszerzenie [[Indeks projektów]]: zmiany wpisane tutaj zostaną zastąpione.\n"_s;
        QCOMPARE(disk(page), head
                                     + u"\n## Aktywny\n\n"
                                       "- [[Dom]] — otwarte zadania: 3, najbliższy termin: 2026-11-10\n"
                                       "- [[Praca]] — otwarte zadania: 3, najbliższy termin: 2026-11-12\n"
                                       "\n## Wstrzymany\n\n"
                                       "- [[Ogród]] — bez otwartych zadań\n"
                                       "\n## Bez statusu\n\n"
                                       "- [[Peru]] — otwarte zadania: 2, najbliższy termin: 2026-12-01\n"_s);
        // Again: nothing differs, nothing is written (nothing for a sync to send).
        out = command(id, u"odswiez"_s);
        QCOMPARE(out.message(), u"Strona „Projekty” jest aktualna, projektów: 4"_s);
        QCOMPARE(m_written, QStringList{page});

        // The editor saves a project with a task done: "save" notes it in the console, "change"
        // writes the page again.
        m_printed.clear();
        QString dom = disk(u"Dom.md"_s);
        dom.replace(u"- [ ] Zapłacić czynsz 📅 2026-11-10 ⏫"_s,
                    u"- [x] Zapłacić czynsz 📅 2026-11-10 ⏫ ✅ 2026-11-14"_s);
        QVERIFY(userSaves(u"Dom.md"_s, dom));
        QCOMPARE(m_hooks.size(), 2);
        QCOMPARE(m_hooks[0].event, u"save"_s);
        QVERIFY2(m_hooks[0].result.ok(), qPrintable(m_hooks[0].result.error));
        QCOMPARE(m_printed, QStringList{u"indeks-projektow: Zapisano projekt: Dom (Dom.md)"_s});
        QCOMPARE(m_hooks[1].event, u"change"_s);
        QVERIFY2(m_hooks[1].result.ok(), qPrintable(m_hooks[1].result.error));
        QCOMPARE(m_hooks[1].result.value.toString(), u"Indeks projektów przepisany (4)"_s);
        QVERIFY2(disk(page).contains(u"- [[Dom]] — otwarte zadania: 2, najbliższy termin: 2026-11-14\n"_s),
                 qPrintable(disk(page)));
        QCOMPARE(m_written, (QStringList{page, page}));

        // A page that is no project: "save" is silent, "change" finds nothing to write.
        m_printed.clear();
        m_hooks.clear();
        QVERIFY(userSaves(u"Zakupy.md"_s, disk(u"Zakupy.md"_s) + u"- [ ] jajka\n"_s));
        QVERIFY(m_printed.isEmpty());
        QCOMPARE(m_hooks.size(), 2);
        QVERIFY(m_hooks[1].result.ok() && m_hooks[1].result.value.isNull());
        QCOMPARE(m_written.size(), 2);

        // A sync brings projects with other statuses: the known ones in their order (whatever the
        // case), then the others, the pages without a status last.
        m_hooks.clear();
        QVERIFY(syncBrings(u"Remont.md"_s,
                           u"---\ntags: [projekt]\nstatus: Zakończony\n---\n- [x] Gotowe ✅ 2026-10-01\n"_s));
        QVERIFY(syncBrings(u"Żagle.md"_s,
                           u"---\ntags: [projekt]\nstatus: w planach\n---\n- [ ] Kurs \"na patent\"\n"_s));
        for (const HookRun &h : std::as_const(m_hooks))
            QVERIFY2(h.result.ok(), qPrintable(h.result.error));
        const QString text = disk(page);
        const QStringList headings = text.split(u'\n').filter(QRegularExpression(u"^## "_s));
        QCOMPARE(headings, (QStringList{u"## Aktywny"_s, u"## Wstrzymany"_s, u"## Zakończony"_s, u"## W planach"_s,
                                        u"## Bez statusu"_s}));
        QVERIFY2(text.contains(u"## Zakończony\n\n- [[Remont]] — bez otwartych zadań\n"_s), qPrintable(text));
        QVERIFY2(text.contains(u"## W planach\n\n- [[Żagle]] — otwarte zadania: 1\n"_s), qPrintable(text));

        // Somebody edits the page itself: the next change puts it back.
        m_hooks.clear();
        QVERIFY(userSaves(page, text + u"- dopisek\n"_s));
        QCOMPARE(disk(page), text);
        // And a project that is deleted leaves it.
        QVERIFY(m_vault->remove(u"Remont.md"_s));
        m_index->remove(u"Remont.md"_s);
        fire(u"change"_s, {{u"pages"_s, QStringList{u"Remont"_s}}});
        QVERIFY2(!disk(page).contains(u"Remont"_s), qPrintable(disk(page)));
    }

    // ---- Pomodoro: the timer and the journal -------------------------------------------------------------

    void pomodoro()
    {
        const QString id = u"pomodoro"_s;
        Result r = view(id, u"today"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Pomodoro: 1 today"_s});
        QCOMPARE(textsOf(r.value, u"text"_s),
                 (QStringList{u"Focused for 25 min."_s, u"09:05"_s, u"Raport kwartalny ([[Praca]])"_s}));
        QVariantMap chart = nodesOf(r.value, u"chart"_s).value(0);
        QCOMPARE(chart.value(u"labels"_s).toStringList().first(), u"11-08"_s);
        QCOMPARE(seriesValues(chart, 0), (QVariantList{0, 0, 0, 0, 2, 3, 1}));

        // "Start": the session's name (tags and extra spaces gone) in today's note with the time
        // and the page on screen, and the app's timer for 25 minutes.
        static const QRegularExpression logged(
                u"^- 🍅 \\d\\d:\\d\\d Raport kwartalny \\(\\[\\[Praca\\]\\]\\) #pomodoro$"_s);
        Outcome out = command(id, u"start"_s, u"Praca"_s, -1, {u"  Raport   kwartalny #praca "_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Focus for 25 minutes: Raport kwartalny"_s);
        QString last = disk(kJournal).trimmed().section(u'\n', -1);
        QVERIFY2(logged.match(last).hasMatch(), qPrintable(last));
        QCOMPARE(m_timers.size(), 1);
        QCOMPARE(m_timers[0].extension, id);
        QCOMPARE(m_timers[0].seconds, 25 * 60);
        QCOMPARE(m_timers[0].label, u"Raport kwartalny"_s);
        // On the line under the caret: a task without its dates.
        out = command(id, u"start-here"_s, u"Dom"_s, lineOf(u"Dom.md"_s, u"Pomalować pokój"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Focus for 25 minutes: Pomalować pokój"_s);
        last = disk(kJournal).trimmed().section(u'\n', -1);
        QVERIFY2(last.endsWith(u" Pomalować pokój ([[Dom]]) #pomodoro"_s), qPrintable(last));
        QCOMPARE(m_timers.size(), 2);
        // A break only runs the timer; a cancelled question does nothing.
        const QString journal = disk(kJournal);
        QCOMPARE(command(id, u"break"_s).message(), u"A break of 5 minutes"_s);
        QCOMPARE(m_timers.size(), 3);
        QCOMPARE(m_timers[2].seconds, 5 * 60);
        QVERIFY(command(id, u"start"_s, u"Praca"_s).result.value.isNull());
        QCOMPARE(m_timers.size(), 3);
        QCOMPARE(disk(kJournal), journal);

        r = view(id, u"today"_s);
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Pomodoro: 3 today"_s});
        QCOMPARE(textsOf(r.value, u"text"_s).value(0), u"Focused for 1 h 15 min."_s);
        chart = nodesOf(r.value, u"chart"_s).value(0);
        QCOMPARE(seriesValues(chart, 0).last(), QVariant(3));
        QCOMPARE(textsOf(r.value, u"run"_s), (QStringList{u"Start"_s, u"Break"_s}));
    }

    // ---- Quick entries: an extension that writes extensions ---------------------------------------------

    void quickEntries()
    {
        const QString id = u"quick-entries"_s;
        const QString title = u"Quick entry Kawa mała"_s, path = kFolder + u"/Quick entry Kawa mała.md"_s;
        // The name becomes a title (what a file name cannot hold is taken out); the line is data.
        const QString line = u"☕ {time} Kawa \"mała\" \\ #kawa"_s;
        Outcome out = command(id, u"new"_s, {}, -1, {u" Kawa / mała "_s, line});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(
                out.message(),
                u"Written: \"Quick entry Kawa mała\" with the command \"Quick entry: Kawa mała\". Approve it on its page."_s);
        QCOMPARE(m_written, QStringList{path});
        // What it wrote is an extension page that asks to write, and loads once approved …
        const QString text = disk(path);
        QVERIFY2(text.startsWith(u"---\ntags: [extension]\npermissions: [write]\n"_s), qPrintable(text));
        const Result loaded = loadPage(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        const PageExtension made = m_pages.value(u"quick-entry-kawa-mala"_s);
        QCOMPARE(made.title, title);
        QCOMPARE(made.permissions, QStringList{u"write"_s});
        // … and its command adds the line, the time in it, quotes and backslash as typed.
        out = command(made.id, u"add"_s);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        static const QRegularExpression added(u"^- ☕ \\d\\d:\\d\\d Kawa \"mała\" \\\\ #kawa$"_s);
        const QString last = disk(kJournal).trimmed().section(u'\n', -1);
        QVERIFY2(added.match(last).hasMatch(), qPrintable(last));
        QVERIFY(out.message().startsWith(u"Added: ☕ "_s));
        QCOMPARE(m_written, (QStringList{path, kJournal}));
        m_written.clear();

        // The view lists it by its name.
        const Result r = view(id, u"list"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"link"_s), QStringList{u"Kawa mała"_s});
        QCOMPARE(nodesOf(r.value, u"link"_s).value(0).value(u"page"_s).toString(), title);

        // The same name again: asked first; "Keep" leaves the page, "Replace" writes that page.
        out = command(id, u"new"_s, {}, -1, {u"Kawa mała"_s, u"herbata"_s, u"Keep"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.questions.size(), 3);
        QCOMPARE(disk(path), text);
        out = command(id, u"new"_s, {}, -1, {u"Kawa mała"_s, u"herbata"_s, u"Replace"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QVERIFY2(disk(path).contains(u"local LINE = \"herbata\"\n"_s), qPrintable(disk(path)));
        QCOMPARE(m_written, QStringList{path});
        QCOMPARE(filesUnder(m_dir + u'/' + kFolder).filter(u"Quick entry"_s).size(), 1);

        // Nothing for a closed dialog; a line that is not one line is refused.
        QVERIFY(command(id, u"new"_s).result.value.isNull());
        QVERIFY(command(id, u"new"_s, {}, -1, {u"Bieg"_s}).result.value.isNull());
        QCOMPARE(command(id, u"new"_s, {}, -1, {u"Bieg"_s, u"pierwszy\ndrugi"_s}).message(),
                 u"The line must be one line of text."_s);
        QCOMPARE(m_written.size(), 1);
    }

    void quickEntryLineIsNeverCode()
    {
        // Text that looks like code — a quote that would end the string, a call, a fence — stays
        // the text of the line in the page the extension generates.
        const QString id = u"quick-entries"_s;
        const QString hostile = u"\"; b.write(\"Dom\", \"\") -- ``` ]] \\\" {time}"_s;
        Outcome out = command(id, u"new"_s, {}, -1, {u"Zły"_s, hostile});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        const QString path = kFolder + u"/Quick entry Zły.md"_s;
        QCOMPARE(m_written, QStringList{path});
        const Result loaded = loadPage(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        const QString dom = disk(u"Dom.md"_s);
        out = command(u"quick-entry-zly"_s, u"add"_s);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(disk(u"Dom.md"_s), dom);
        const QString last = disk(kJournal).trimmed().section(u'\n', -1);
        static const QRegularExpression time(u"\\d\\d:\\d\\d$"_s);
        QVERIFY2(last.startsWith(u"- \"; b.write(\"Dom\", \"\") -- ``` ]] \\\" "_s) && time.match(last).hasMatch(),
                 qPrintable(last));
    }

    // ---- Fiszki: a review through view params -------------------------------------------------------------

    void flashcards()
    {
        const QString id = u"fiszki"_s, name = u"powtorka"_s, page = u"Geografia.md"_s;
        Result r = view(id, name);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        // Four cards (a line that only mentions the tag is none), three to repeat: the new one
        // first, then the longest overdue. Only the question shows.
        QCOMPARE(textsOf(r.value, u"heading"_s), (QStringList{u"Fiszki: 3 do powtórki"_s, u"Stolica Peru"_s}));
        QCOMPARE(textsOf(r.value, u"text"_s), (QStringList{u"Wszystkich: 4, opanowanych: 0."_s, u"Ze strony"_s}));
        QVERIFY(nodesOf(r.value, u"button"_s).isEmpty());
        QVariantMap show = nodesOf(r.value, u"open"_s).value(0);
        QCOMPARE(show.value(u"label"_s).toString(), u"Pokaż odpowiedź"_s);
        // "Pokaż odpowiedź" opens the same view with the card's ref: the answer and the grades.
        QVariantMap params = show.value(u"params"_s).toMap();
        r = view(id, name, params);
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"text"_s).value(1), u"Lima"_s);
        QCOMPARE(textsOf(r.value, u"button"_s), (QStringList{u"Jeszcze raz"_s, u"Umiem"_s, u"Łatwe"_s}));

        // "Umiem" on a new card: again in two days, written at the end of its line.
        Outcome out = tap(id, name, control(r.value, u"button"_s, u"Umiem"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"Następna powtórka: 2026-11-16"_s);
        QCOMPARE(lineWith(page, u"Peru"_s), u"- Stolica Peru >> Lima #fiszka (powtórka 2026-11-16, co 2 dni)"_s);
        // The view drawn again with the same params: the next card, its answer hidden.
        r = view(id, name, params);
        QCOMPARE(textsOf(r.value, u"heading"_s), (QStringList{u"Fiszki: 2 do powtórki"_s, u"Stolica Polski"_s}));
        QVERIFY(nodesOf(r.value, u"button"_s).isEmpty());
        // "Łatwe" triples the interval (2 → 6), "Jeszcze raz" goes back to a day.
        params = nodesOf(r.value, u"open"_s).value(0).value(u"params"_s).toMap();
        r = view(id, name, params);
        out = tap(id, name, control(r.value, u"button"_s, u"Łatwe"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(lineWith(page, u"Warszawa"_s),
                 u"- Stolica Polski >> Warszawa #fiszka (powtórka 2026-11-20, co 6 dni)"_s);
        r = view(id, name, params);
        QCOMPARE(textsOf(r.value, u"heading"_s).value(1), u"Najwyższy szczyt Polski"_s);
        r = view(id, name, nodesOf(r.value, u"open"_s).value(0).value(u"params"_s).toMap());
        out = tap(id, name, control(r.value, u"button"_s, u"Jeszcze raz"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(lineWith(page, u"Rysy"_s),
                 u"- Najwyższy szczyt Polski >> Rysy #fiszka (powtórka 2026-11-15, co 1 dni)"_s);
        // Nothing is left for today; the other lines of the page never changed.
        r = view(id, name);
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Fiszki: 0 do powtórki"_s});
        QCOMPARE(textsOf(r.value, u"empty"_s), QStringList{u"Na dziś wszystko"_s});
        QCOMPARE(lineWith(page, u"Wisła"_s),
                 u"- Najdłuższa rzeka Polski >> Wisła #fiszka (powtórka 2026-11-20, co 8 dni)"_s);
        QCOMPARE(lineWith(page, u"wzmianka"_s), u"- To nie jest fiszka, tylko wzmianka o tagu #fiszka"_s);

        // A grade tapped twice (the view not drawn in between): the second tap is stale.
        r = view(id, name);
        out = command(id, u"dodaj"_s, {}, -1, {u" Stolica Czech "_s, u"Praga"_s});
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QVERIFY(disk(kJournal).endsWith(u"- Stolica Czech >> Praga #fiszka\n"_s));
        r = view(id, name);
        QCOMPARE(textsOf(r.value, u"heading"_s), (QStringList{u"Fiszki: 1 do powtórki"_s, u"Stolica Czech"_s}));
        r = view(id, name, nodesOf(r.value, u"open"_s).value(0).value(u"params"_s).toMap());
        const QVariantMap good = control(r.value, u"button"_s, u"Umiem"_s);
        QVERIFY(tap(id, name, good).result.ok());
        out = tap(id, name, good);
        QCOMPARE(out.result.status, Status::Error);
        QVERIFY2(out.result.error.contains(u"stale:"_s), qPrintable(out.result.error));
        QVERIFY(disk(kJournal).endsWith(u"- Stolica Czech >> Praga #fiszka (powtórka 2026-11-16, co 2 dni)\n"_s));
        // An unknown grade is the extension's own error, without a line number.
        out = act(id, name, u"ocen"_s,
                  {{u"ref"_s, good.value(u"args"_s).toMap().value(u"ref"_s)},
                   {u"text"_s, u"x"_s},
                   {u"grade"_s, u"super"_s}});
        QCOMPARE(out.result.status, Status::Error);
        QVERIFY2(out.result.error.startsWith(u"Nieznana ocena: super"_s), qPrintable(out.result.error));
    }

    // ---- Link check: every page in one call ----------------------------------------------------------------

    void linkCheck()
    {
        const QString id = u"link-check"_s;
        Result r = view(id, u"broken"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        // Aliases and headings name their page; code, files and dates are no broken links; the
        // targets in alphabetical order (Ż last), each with the pages that link to it.
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Broken links: 4"_s});
        const QList<QVariantMap> rows = nodesOf(r.value, u"row"_s);
        QCOMPARE(rows.size(), 4);
        const auto links = [&](int row) { return textsOf(rows[row], u"link"_s).mid(0, 2); };
        QCOMPARE(links(0), (QStringList{u"Lima"_s, u"Peru"_s}));
        QCOMPARE(links(1), (QStringList{u"Projekty"_s, u"Indeks projektów"_s}));
        QCOMPARE(links(2), (QStringList{u"Solaris"_s, u"Książki"_s}));
        QCOMPARE(links(3), (QStringList{u"Żółw"_s, u"Ogród"_s}));
        static const QRegularExpression counted(u"^\\d+ links on 27 pages\\.$"_s);
        QVERIFY2(counted.match(textsOf(r.value, u"text"_s).value(0)).hasMatch(),
                 qPrintable(textsOf(r.value, u"text"_s).join(u'|')));

        // The command prints them all.
        m_printed.clear();
        Outcome out = command(id, u"report"_s);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"4 broken links on 27 pages; the list is in the Luau console"_s);
        QCOMPARE(m_printed,
                 (QStringList{u"link-check: [[Lima]] ← Peru"_s, u"link-check: [[Projekty]] ← Indeks projektów"_s,
                              u"link-check: [[Solaris]] ← Książki"_s, u"link-check: [[Żółw]] ← Ogród"_s}));

        // The page appears (a sync); it links to a missing one twice, in other letters. And the
        // sync was a change for the other extensions too: the project index wrote its page, so the
        // link to "Projekty" leads somewhere now.
        QVERIFY(syncBrings(u"Solaris.md"_s, u"Stanisław Lem, 1961. Zobacz też [[żółw]] i [[ŻÓŁW|tego żółwia]].\n"_s));
        r = view(id, u"broken"_s);
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Broken links: 2"_s});
        const QList<QVariantMap> after = nodesOf(r.value, u"row"_s);
        QCOMPARE(after.size(), 2);
        QCOMPARE(textsOf(after[0], u"link"_s).mid(0, 2), (QStringList{u"Lima"_s, u"Peru"_s}));
        QCOMPARE(textsOf(after[1], u"link"_s).mid(0, 3), (QStringList{u"Żółw"_s, u"Ogród"_s, u"Solaris"_s}));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
    }

    void linkCheckStopsInTime()
    {
        // The scan has a budget of its own: with none left it reports how far it got instead of
        // running into the engine's time limit. (The user edits the page's code; the editor's save
        // loads it again.)
        const QString path = kFolder + u"/Link check.md"_s;
        QString code = disk(path);
        QVERIFY(code.contains(u"local BUDGET_SECONDS = 1\n"_s));
        code.replace(u"local BUDGET_SECONDS = 1\n"_s, u"local BUDGET_SECONDS = -1\n"_s);
        QVERIFY(userSaves(path, code));
        const Result loaded = loadPage(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        const Result r = view(u"link-check"_s, u"broken"_s);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY2(m_viewProblems.isEmpty(), qPrintable(m_viewProblems.join(u"; "_s)));
        QCOMPARE(textsOf(r.value, u"heading"_s), QStringList{u"Broken links: 0"_s});
        // (28 pages: the save was a change, and the project index wrote its page.)
        QCOMPARE(textsOf(r.value, u"text"_s),
                 QStringList{u"Checked 0 of 28 pages: the notebook is too big for one pass."_s});
        QCOMPARE(command(u"link-check"_s, u"report"_s).message(), u"0 broken links on the first 0 of 28 pages"_s);
    }

    // ---- Action items: a model with a tool ----------------------------------------------------------------

    void actionItems()
    {
        const QString id = u"action-items"_s;
        QList<QVariantMap> requests;
        // The model proposes two tasks (one with a list marker, a line break and a date that is
        // none), sends an empty one and calls a tool that does not exist; then it sums up.
        const Model model = [&requests](const QVariantMap &request) {
            requests << request;
            QVariantMap reply{{u"model"_s, u"test-model"_s}, {u"raw"_s, u"[{\"turn\":%1}]"_s.arg(requests.size())}};
            if (requests.size() > 1) {
                reply.insert(u"text"_s, u"Dwie rzeczy do zrobienia."_s);
                return reply;
            }
            reply.insert(u"text"_s, QString());
            reply.insert(u"toolCalls"_s,
                         QVariantList{
                                 QVariantMap{{u"id"_s, u"c1"_s},
                                             {u"name"_s, u"add_task"_s},
                                             {u"input"_s, QVariantMap{{u"text"_s, u"Kupić bilety do Limy"_s},
                                                                      {u"due"_s, u"2026-12-01"_s}}}},
                                 QVariantMap{{u"id"_s, u"c2"_s},
                                             {u"name"_s, u"add_task"_s},
                                             {u"input"_s,
                                              QVariantMap{{u"text"_s, u"- [ ] Zaszczepić się\n  przed wyjazdem"_s},
                                                          {u"due"_s, u"wkrótce"_s}}}},
                                 QVariantMap{{u"id"_s, u"c3"_s},
                                             {u"name"_s, u"add_task"_s},
                                             {u"input"_s, QVariantMap{{u"text"_s, u"   "_s}}}},
                                 QVariantMap{{u"id"_s, u"c4"_s},
                                             {u"name"_s, u"delete_everything"_s},
                                             {u"input"_s, QVariantMap{}}},
                         });
            return reply;
        };
        const QString before = disk(kJournal);
        Outcome out = command(id, u"extract"_s, u"Peru"_s, -1, {u"Add"_s, u"Add"_s}, model);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"2 tasks added to today's note"_s);
        // Each proposal was shown as it would be written and waited for its yes.
        QStringList kinds, prompts;
        for (const QVariant &q : std::as_const(out.questions)) {
            kinds << q.toMap().value(u"kind"_s).toString();
            if (kinds.last() == u"choose")
                prompts << q.toMap().value(u"prompt"_s).toString();
        }
        QCOMPARE(kinds, (QStringList{u"ai"_s, u"choose"_s, u"choose"_s, u"ai"_s}));
        QCOMPARE(prompts, (QStringList{u"Add this task?\nKupić bilety do Limy 📅 2026-12-01"_s,
                                       u"Add this task?\nZaszczepić się przed wyjazdem"_s}));
        QCOMPARE(disk(kJournal), before
                                         + u"- Action items of [[Peru]]\n"
                                           "  - [ ] Kupić bilety do Limy 📅 2026-12-01\n"
                                           "  - [ ] Zaszczepić się przed wyjazdem\n"_s);
        // The first request: the note, the tool; the second: the model's turn verbatim and a result
        // for every call, the refused ones as errors.
        QCOMPARE(requests.size(), 2);
        QCOMPARE(requests[0].value(u"tools"_s).toList().size(), 1);
        QCOMPARE(requests[0].value(u"maxTokens"_s).toInt(), 2000);
        const QVariantList first = requests[0].value(u"messages"_s).toList();
        QCOMPARE(first.size(), 1);
        QVERIFY(first[0].toMap()
                        .value(u"content"_s)
                        .toString()
                        .startsWith(u"Today is 2026-11-14. The note \"Peru\":\n\n---\ntags: [projekt]"_s));
        const QVariantList second = requests[1].value(u"messages"_s).toList();
        QCOMPARE(second.size(), 3);
        QCOMPARE(second[1].toMap(), (QVariantMap{{u"role"_s, u"assistant"_s}, {u"raw"_s, u"[{\"turn\":1}]"_s}}));
        const QVariantList results = second[2].toMap().value(u"results"_s).toList();
        QCOMPARE(results.size(), 4);
        QCOMPARE(results[0].toMap(),
                 (QVariantMap{{u"id"_s, u"c1"_s}, {u"content"_s, u"Added."_s}, {u"isError"_s, false}}));
        QCOMPARE(results[1].toMap().value(u"content"_s).toString(), u"Added."_s);
        QCOMPARE(results[2].toMap(), (QVariantMap{{u"id"_s, u"c3"_s},
                                                  {u"content"_s, u"A task needs a text of one line."_s},
                                                  {u"isError"_s, true}}));
        QCOMPARE(results[3].toMap(),
                 (QVariantMap{{u"id"_s, u"c4"_s}, {u"content"_s, u"No such tool."_s}, {u"isError"_s, true}}));

        // "Add all" accepts the rest without asking; "Skip" (or a closed dialog) adds nothing.
        requests.clear();
        out = command(id, u"extract"_s, u"Peru"_s, -1, {u"Add all"_s}, model);
        QCOMPARE(out.message(), u"2 tasks added to today's note"_s);
        QCOMPARE(out.questions.size(), 3);
        requests.clear();
        const QString journal = disk(kJournal);
        out = command(id, u"extract"_s, u"Peru"_s, -1, {u"Skip"_s, QVariant()}, model);
        QCOMPARE(out.message(), u"No task added."_s);
        QCOMPARE(requests[1]
                         .value(u"messages"_s)
                         .toList()
                         .value(2)
                         .toMap()
                         .value(u"results"_s)
                         .toList()
                         .value(0)
                         .toMap()
                         .value(u"content"_s)
                         .toString(),
                 u"The user skipped it."_s);
        QCOMPARE(disk(kJournal), journal);
        requests.clear();
        out = command(id, u"extract"_s, u"Peru"_s, -1, {u"Skip"_s, u"Add"_s}, model);
        QCOMPARE(out.message(), u"1 task added to today's note"_s);

        // A model that calls no tool: its words are the message. One that fails: the provider's
        // error without the script's line. One that never stops calling: four turns, then the end.
        const QString unchanged = disk(kJournal);
        out = command(id, u"extract"_s, u"Peru"_s, -1, {}, [](const QVariantMap &) {
            return QVariantMap{{u"text"_s, u"Nie ma tu nic do zrobienia."_s}, {u"raw"_s, u"[]"_s}};
        });
        QCOMPARE(out.message(), u"Nie ma tu nic do zrobienia."_s);
        out = command(id, u"extract"_s, u"Peru"_s);
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(out.message(), u"AI is not available: no API key is set (Settings → Extensions → AI)"_s);
        int turns = 0;
        out = command(id, u"extract"_s, u"Peru"_s, -1, {}, [&turns](const QVariantMap &) {
            ++turns;
            return QVariantMap{{u"text"_s, QString()},
                               {u"raw"_s, u"[]"_s},
                               {u"toolCalls"_s,
                                QVariantList{QVariantMap{
                                        {u"id"_s, u"c"_s}, {u"name"_s, u"nothing"_s}, {u"input"_s, QVariantMap{}}}}}};
        });
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QCOMPARE(turns, 4);
        QCOMPARE(out.message(), u"No action items found."_s);
        // No note on screen, an empty one.
        QCOMPARE(command(id, u"extract"_s).message(), u"Open a note first."_s);
        QVERIFY(syncBrings(u"Pusta.md"_s, u"\n \n"_s));
        QCOMPARE(command(id, u"extract"_s, u"Pusta"_s).message(), u"This note is empty."_s);
        QCOMPARE(disk(kJournal), unchanged);
    }

    // ---- at scale, and on notes from anywhere --------------------------------------------------------------

    void aBigNotebook()
    {
        // Two thousand pages and a year of daily notes: every view still draws and the commands
        // that read all of it still end, with what the documentation calls for — a quick render —
        // reported in the test's output.
        QHash<QString, QString> notes;
        for (int i = 1; i <= 2000; ++i) {
            QString text;
            if (i % 50 == 0)
                text += u"---\ntags: [projekt]\nstatus: %1\n---\n"_s.arg(i % 100 == 0 ? u"aktywny"_s : u"wstrzymany"_s);
            text += u"# Strona %1\n\nZażółć gęślą jaźń: akapit strony %1 z linkiem do [[Strona %2]]"_s.arg(i).arg(
                    i % 2000 + 1);
            if (i % 10 == 0)
                text += u" i do [[Brak %1]]"_s.arg(i);
            text += u".\n\n- [ ] Zadanie %1 📅 2026-%2-%3\n"_s.arg(i)
                            .arg(1 + i % 12, 2, 10, QLatin1Char('0'))
                            .arg(1 + i % 28, 2, 10, QLatin1Char('0'));
            if (i % 4 == 0)
                text += u"- Pytanie %1 >> Odpowiedź %1 #fiszka\n"_s.arg(i);
            notes.insert(u"Duże/Strona %1.md"_s.arg(i), text);
        }
        for (QDate day(2025, 11, 15); day < QDate(2026, 11, 14); day = day.addDays(1)) {
            const int n = day.dayOfYear();
            notes.insert(u"journals/%1.md"_s.arg(day.toString(Qt::ISODate)),
                         u"- Samopoczucie #nastrój\n  nastroj:: %1\n  energia:: %2\n- %3,50 zł %4: wpis #wydatek\n"
                         "- 🍅 10:00 Praca #pomodoro\n"_s.arg(1 + n % 5)
                                 .arg(1 + n % 4)
                                 .arg(10 + n % 90)
                                 .arg(n % 3 == 0 ? u"jedzenie"_s : u"transport"_s));
        }
        QElapsedTimer clock;
        clock.start();
        const QString errors = open(u"pl"_s, Limits{20000 * BRULION_TEST_SLOWDOWN, 64ll << 20}, notes);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        int pages = 0;
        m_index->forEachNote([&](const VaultPath &, const ParsedNote &) { ++pages; });
        QStringList report{u"%1 pages, opened in %2 ms"_s.arg(pages).arg(clock.elapsed())};
        for (const ViewInfo &v : m_engine->views()) {
            clock.restart();
            const Result r = view(v.extension, v.name);
            const qint64 ms = clock.elapsed();
            QVERIFY2(r.ok(), qPrintable(u"%1/%2: %3"_s.arg(v.extension, v.name, r.error)));
            QVERIFY2(m_viewProblems.isEmpty(), qPrintable(v.extension + u": "_s + m_viewProblems.join(u"; "_s)));
            report << u"view %1/%2: %3 ms, %4 nodes"_s.arg(v.extension, v.name).arg(ms).arg(nodesOf(r.value).size());
        }
        const QList<std::pair<QString, QString>> commands{{u"licznik-slow"_s, u"ranking"_s},
                                                          {u"indeks-projektow"_s, u"odswiez"_s},
                                                          {u"link-check"_s, u"report"_s}};
        for (const auto &[extension, name] : commands) {
            clock.restart();
            const Outcome out = command(extension, name);
            QVERIFY2(out.result.ok(), qPrintable(u"%1/%2: %3"_s.arg(extension, name, out.result.error)));
            report << u"command %1/%2: %3 ms — %4"_s.arg(extension, name).arg(clock.elapsed()).arg(out.message());
        }
        report << u"memory in use: %1 MB"_s.arg(m_engine->memoryUsed() / (1 << 20));
        qInfo().noquote() << report.join(u"\n    "_s);
        // The index page lists the forty projects of the big folder and the four of the sample.
        QCOMPARE(disk(u"Projekty.md"_s).count(u"\n- [["_s), 44);
    }

    void notesFromAnywhere()
    {
        // Notes come from imports and syncs: lines of tens of thousands of characters, numbers that
        // are none, dates that are no day. Every view still draws, in the usual time, and what is
        // no entry is left out.
        const QString digits(20000, u'7');
        const QHash<QString, QString> notes{
                {u"journals/2026-11-09.md"_s,
                 u"- "_s + digits + u" zł jedzenie: za dużo cyfr #wydatek\n"_s + u"- 9"_s + QString(250, u'9')
                         + u" zł jedzenie #wydatek\n- ,5 zł x #wydatek\n- 5 #wydatek\n- 7 zł #wydatek\n"
                           "- Samopoczucie #nastrój\n  nastroj:: 1e308\n  energia:: -1e308\n"
                           "- Samopoczucie #nastrój\n  nastroj:: nan\n  energia:: inf\n"
                           "- 🍅 25:99 "_s
                         + QString(50000, u'x') + u" #pomodoro\n"_s},
                {u"Wrogie.md"_s, u"- "_s + QString(60000, u'a')
                                         + u" >> b #fiszka\n"
                                           "- a >> b >> c #fiszka (powtórka 9999-99-99, co 99999999999999999999 dni)\n"
                                           "- >> #fiszka\n- >> x #fiszka\n- x >> #fiszka\n"_s
                                         + QString(30000, u'[')
                                         + u"\n[[a|b|c#d]] [[ ]] [[|x]] [[#y]] [[a]]]] [[[[zagnieżdżony]]]]\n- [ ] "_s
                                         + QString(100000, u'z') + u" 📅 2026-11-01\n- [ ] 📅 2026-11-02\n"_s},
        };
        const QString errors = open(u"pl"_s, Limits{2000 * BRULION_TEST_SLOWDOWN, 64ll << 20}, notes);
        QVERIFY2(errors.isEmpty(), qPrintable(errors));
        QHash<QString, QVariant> trees;
        for (const ViewInfo &v : m_engine->views()) {
            const Result r = view(v.extension, v.name);
            QVERIFY2(r.ok(), qPrintable(u"%1/%2: %3"_s.arg(v.extension, v.name, r.error)));
            QVERIFY2(m_viewProblems.isEmpty(), qPrintable(v.extension + u": "_s + m_viewProblems.join(u"; "_s)));
            trees.insert(v.extension, r.value);
        }
        // Expenses: thousands of digits are no amount (nor is infinity), a line without one is none;
        // "7 zł" alone is an expense without a category.
        QCOMPARE(textsOf(trees[u"wydatki"_s], u"text"_s), QStringList{u"Razem: 1368,4 zł"_s});
        QStringList expenseRows;
        for (const QVariant &row : nodesOf(trees[u"wydatki"_s], u"table"_s).value(0).value(u"rows"_s).toList())
            expenseRows << row.toStringList().join(u'|');
        QVERIFY2(expenseRows.contains(u"1%|inne|7 zł|1"_s), qPrintable(expenseRows.join(u'\n')));
        // Mood: 1e308 is a number (clamped to the scale), "nan" and "inf" are not.
        const QVariantMap chart = nodesOf(trees[u"nastroj"_s], u"chart"_s).value(0);
        QCOMPARE(seriesValues(chart, 0).value(8), QVariant(5));
        QCOMPARE(seriesValues(chart, 1).value(8), QVariant(1));
        // Cards: a line of 60 000 characters is none, an interval of twenty digits is ten years.
        QCOMPARE(textsOf(trees[u"fiszki"_s], u"text"_s).value(0), u"Wszystkich: 5, opanowanych: 1."_s);
        QCOMPARE(textsOf(trees[u"fiszki"_s], u"heading"_s).value(0), u"Fiszki: 3 do powtórki"_s);
        // Links: brackets by the thousand are no link; an alias with more bars, empty targets.
        QCOMPARE(textsOf(trees[u"link-check"_s], u"heading"_s), QStringList{u"Broken links: 6"_s});
        const QList<QVariantMap> rows = nodesOf(trees[u"link-check"_s], u"row"_s);
        QCOMPARE(textsOf(rows.value(0), u"link"_s).mid(0, 2), (QStringList{u"a"_s, u"Wrogie"_s}));
        QCOMPARE(textsOf(rows.value(4), u"link"_s).mid(0, 2), (QStringList{u"zagnieżdżony"_s, u"Wrogie"_s}));
        // Tasks: a task of 100 000 characters and one that is only a date are rows like any other,
        // and can be ticked.
        const QList<QVariantMap> taskRows = nodesOf(trees[u"przeglad-zadan"_s], u"row"_s);
        QCOMPARE(textsOf(taskRows.value(0), u"text"_s).value(0).size(), 100000);
        QCOMPARE(textsOf(taskRows.value(1), u"text"_s), (QStringList{QString(), u"2026-11-02"_s}));
        const Outcome out =
                tap(u"przeglad-zadan"_s, u"przeglad"_s, control(taskRows.value(1), u"check"_s, u"zrobione"_s));
        QVERIFY2(out.result.ok(), qPrintable(out.result.error));
        QVERIFY(disk(u"Wrogie.md"_s).endsWith(u"- [x] 📅 2026-11-02 ✅ 2026-11-14\n"_s));
        // The session with a time that is none still counts for its day.
        QCOMPARE(seriesValues(nodesOf(trees[u"pomodoro"_s], u"chart"_s).value(0), 0),
                 (QVariantList{0, 1, 0, 0, 2, 3, 1}));
    }
};

QTEST_GUILESS_MAIN(TestExtensionsSamples)
#include "tst_extensions_samples.moc"
