#include "bench.h"

#include "extensions/builtin.h"
#include "extensions/engine.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "text/text.h"
#include "vault/vault.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::assistantbench {

namespace {

// ---- reading tasks -------------------------------------------------------------------------------

/// Reads JSON values into a task, the first error kept (with where it is).
class Reader {
public:
    explicit Reader(QString where) : m_where(std::move(where)) {}

    bool ok() const { return m_error.isEmpty(); }
    QString error() const { return m_error; }
    void fail(const QString &what)
    {
        if (m_error.isEmpty())
            m_error = m_where + u": "_s + what;
    }

    /// Fails on a key of `object` that is not one of `known` (a misspelt key is no silent no-op).
    void onlyKeys(const QJsonObject &object, const QStringList &known, const QString &what)
    {
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!known.contains(it.key()))
                fail(u"%1: unknown key \"%2\""_s.arg(what, it.key()));
        }
    }

    QString string(const QJsonObject &object, const QString &key, const QString &what, bool required = false)
    {
        const QJsonValue value = object.value(key);
        if (value.isUndefined()) {
            if (required)
                fail(u"%1: \"%2\" is missing"_s.arg(what, key));
            return {};
        }
        if (!value.isString()) {
            fail(u"%1: \"%2\" must be a string"_s.arg(what, key));
            return {};
        }
        return value.toString();
    }

    QStringList strings(const QJsonObject &object, const QString &key, const QString &what)
    {
        const QJsonValue value = object.value(key);
        if (value.isUndefined())
            return {};
        QStringList out;
        if (value.isString())
            return {value.toString()};
        if (!value.isArray()) {
            fail(u"%1: \"%2\" must be a string or a list of strings"_s.arg(what, key));
            return {};
        }
        for (const QJsonValue item : value.toArray()) {
            if (!item.isString())
                fail(u"%1: \"%2\" must hold strings only"_s.arg(what, key));
            out << item.toString();
        }
        return out;
    }

    bool boolean(const QJsonObject &object, const QString &key, bool fallback, const QString &what)
    {
        const QJsonValue value = object.value(key);
        if (value.isUndefined())
            return fallback;
        if (!value.isBool())
            fail(u"%1: \"%2\" must be true or false"_s.arg(what, key));
        return value.toBool(fallback);
    }

    TextCheck text(const QJsonObject &object, const QString &what)
    {
        TextCheck check;
        check.contains = strings(object, u"contains"_s, what);
        check.notContains = strings(object, u"notContains"_s, what);
        check.matches = strings(object, u"matches"_s, what);
        check.notMatches = strings(object, u"notMatches"_s, what);
        const QJsonValue any = object.value(u"anyOf"_s);
        if (!any.isUndefined()) {
            if (!any.isArray())
                fail(u"%1: \"anyOf\" must be a list of lists of strings"_s.arg(what));
            for (const QJsonValue group : any.toArray()) {
                QStringList words;
                if (group.isString()) {
                    words << group.toString();
                } else {
                    for (const QJsonValue word : group.toArray())
                        words << word.toString();
                }
                if (words.isEmpty() || !(group.isString() || group.isArray()))
                    fail(u"%1: \"anyOf\" must be a list of lists of strings"_s.arg(what));
                check.anyOf << words;
            }
        }
        for (const QString &pattern : check.matches + check.notMatches) {
            const QRegularExpression re(pattern);
            if (!re.isValid())
                fail(u"%1: the expression /%2/ is malformed: %3"_s.arg(what, pattern, re.errorString()));
        }
        return check;
    }

    QDateTime when(const QString &text, const QString &what)
    {
        QDateTime t = QDateTime::fromString(text, Qt::ISODate);
        if (!t.isValid())
            fail(u"%1: \"%2\" is no date and time (2026-11-16T09:30)"_s.arg(what, text));
        t.setTimeZone(QTimeZone::LocalTime); // wall-clock time, never through UTC
        return t;
    }

    QList<Step> solution(const QJsonValue &value, const QString &what)
    {
        QList<Step> steps;
        if (value.isUndefined())
            return steps;
        if (!value.isArray()) {
            fail(u"%1: \"solution\" must be a list of steps"_s.arg(what));
            return steps;
        }
        int n = 0;
        for (const QJsonValue item : value.toArray()) {
            const QString at = u"%1: solution step %2"_s.arg(what).arg(++n);
            const QJsonObject o = item.toObject();
            onlyKeys(o, {u"thoughts"_s, u"answer"_s, u"calls"_s, u"call"_s, u"input"_s, u"unlessShown"_s, u"alternatives"_s},
                     at);
            Step step;
            step.thoughts = string(o, u"thoughts"_s, at);
            step.answer = string(o, u"answer"_s, at);
            step.unlessShown = string(o, u"unlessShown"_s, at);
            step.calls = callsOf(o, at);
            for (const QJsonValue other : o.value(u"alternatives"_s).toArray()) {
                const QJsonObject alternative = other.toObject();
                onlyKeys(alternative, {u"calls"_s, u"call"_s, u"input"_s}, at + u": an alternative"_s);
                step.alternatives << callsOf(alternative, at + u": an alternative"_s);
                if (step.alternatives.constLast().isEmpty())
                    fail(u"%1: an alternative is a call (\"call\", \"input\") or calls (\"calls\")"_s.arg(at));
            }
            if (o.contains(u"alternatives"_s) && !o.value(u"alternatives"_s).isArray())
                fail(u"%1: \"alternatives\" must be a list of calls"_s.arg(at));
            if (step.calls.isEmpty() == step.answer.isEmpty())
                fail(u"%1: a step either calls tools or answers"_s.arg(at));
            if ((!step.unlessShown.isEmpty() || !step.alternatives.isEmpty()) && step.calls.isEmpty())
                fail(u"%1: \"unlessShown\" and \"alternatives\" belong to a step that calls tools"_s.arg(at));
            steps << step;
        }
        if (!steps.isEmpty() && !steps.last().calls.isEmpty())
            fail(u"%1: the solution must end with an answer"_s.arg(what));
        return steps;
    }

private:
    /// A step's calls: `call` with its `input`, or `calls` ({name, input} each).
    QList<ai::ToolCall> callsOf(const QJsonObject &o, const QString &at)
    {
        QList<ai::ToolCall> out;
        QJsonArray calls = o.value(u"calls"_s).toArray();
        if (o.contains(u"call"_s))
            calls.append(QJsonObject{{u"name"_s, o.value(u"call"_s)}, {u"input"_s, o.value(u"input"_s)}});
        else if (o.contains(u"input"_s))
            fail(u"%1: \"input\" belongs to \"call\""_s.arg(at));
        for (const QJsonValue c : std::as_const(calls)) {
            const QJsonObject call = c.toObject();
            onlyKeys(call, {u"name"_s, u"input"_s}, at);
            const QString name = string(call, u"name"_s, at, true);
            const QJsonValue input = call.value(u"input"_s);
            if (!input.isUndefined() && !input.isObject())
                fail(u"%1: a call's \"input\" must be an object"_s.arg(at));
            out << ai::ToolCall{QString(), name, input.toObject().toVariantMap()};
        }
        return out;
    }

    QString m_where;
    QString m_error;
};

const QStringList kCategories{u"search"_s, u"query"_s,  u"read"_s,    u"append"_s,  u"journal"_s, u"update"_s,
                              u"write"_s,  u"no_tool"_s, u"clarify"_s, u"decline"_s, u"multi"_s};

std::optional<QList<Task>> readFile(const QString &file, QString *error)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = u"%1: cannot be read"_s.arg(file);
        return std::nullopt;
    }
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parse);
    if (!doc.isObject()) {
        *error = u"%1: no JSON object (%2 at %3)"_s.arg(file, parse.errorString()).arg(parse.offset);
        return std::nullopt;
    }
    const QString name = QFileInfo(file).fileName();
    Reader top(name);
    const QJsonObject root = doc.object();
    top.onlyKeys(root, {u"notebook"_s, u"language"_s, u"now"_s, u"tasks"_s}, u"the file"_s);
    const QString notebook = top.string(root, u"notebook"_s, u"the file"_s);
    const QString language = top.string(root, u"language"_s, u"the file"_s);
    const QString now = top.string(root, u"now"_s, u"the file"_s);
    if (!top.ok()) {
        *error = top.error();
        return std::nullopt;
    }

    QList<Task> tasks;
    const QJsonArray list = root.value(u"tasks"_s).toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        const QJsonObject o = list.at(i).toObject();
        const QString id = o.value(u"id"_s).toString();
        Reader r(name);
        const QString what = id.isEmpty() ? u"task %1"_s.arg(i + 1) : id;
        r.onlyKeys(o,
                   {u"id"_s, u"category"_s, u"notebook"_s, u"language"_s, u"now"_s, u"page"_s, u"ask"_s, u"messages"_s,
                    u"answers"_s, u"expect"_s, u"solution"_s, u"note"_s},
                   what);
        Task t;
        t.id = r.string(o, u"id"_s, what, true);
        t.category = r.string(o, u"category"_s, what, true);
        if (!t.category.isEmpty() && !kCategories.contains(t.category))
            r.fail(u"%1: unknown category \"%2\" (%3)"_s.arg(what, t.category, kCategories.join(u", "_s)));
        t.notebook = o.contains(u"notebook"_s) ? r.string(o, u"notebook"_s, what) : notebook;
        t.language = o.contains(u"language"_s) ? r.string(o, u"language"_s, what) : language;
        if (t.notebook.isEmpty())
            r.fail(u"%1: no notebook (the file's or its own)"_s.arg(what));
        if (t.language != u"pl" && t.language != u"en")
            r.fail(u"%1: the language must be \"pl\" or \"en\""_s.arg(what));
        const QString when = o.contains(u"now"_s) ? r.string(o, u"now"_s, what) : now;
        if (when.isEmpty())
            r.fail(u"%1: no clock (\"now\": the file's or its own)"_s.arg(what));
        else
            t.now = r.when(when, what);
        t.page = r.string(o, u"page"_s, what);

        if (o.contains(u"ask"_s) == o.contains(u"messages"_s))
            r.fail(u"%1: either \"ask\" (the user's message) or \"messages\" (a conversation)"_s.arg(what));
        if (o.contains(u"ask"_s))
            t.messages << QVariantMap{{u"role"_s, u"user"_s}, {u"content"_s, r.string(o, u"ask"_s, what)}};
        for (const QJsonValue m : o.value(u"messages"_s).toArray()) {
            const QJsonObject message = m.toObject();
            r.onlyKeys(message, {u"role"_s, u"content"_s}, what);
            const QString role = r.string(message, u"role"_s, what, true);
            if (role != u"user" && role != u"assistant")
                r.fail(u"%1: a message's role is \"user\" or \"assistant\""_s.arg(what));
            t.messages << QVariantMap{{u"role"_s, role}, {u"content"_s, r.string(message, u"content"_s, what, true)}};
        }
        if (t.messages.isEmpty() || t.messages.last().toMap().value(u"role"_s).toString() != u"user")
            r.fail(u"%1: the conversation must end with the user's message"_s.arg(what));

        t.answers = r.strings(o, u"answers"_s, what);
        for (const QString &a : std::as_const(t.answers)) {
            if (a != u"yes" && a != u"all" && a != u"no")
                r.fail(u"%1: an answer is \"yes\", \"all\" or \"no\""_s.arg(what));
        }

        const QJsonObject expect = o.value(u"expect"_s).toObject();
        r.onlyKeys(expect,
                   {u"files"_s, u"onlyListedChanges"_s, u"answer"_s, u"toolsCalled"_s, u"toolsNotCalled"_s, u"noTools"_s},
                   what + u" expect"_s);
        for (const QJsonValue checked : expect.value(u"files"_s).toArray()) {
            const QJsonObject fo = checked.toObject();
            r.onlyKeys(fo, {u"path"_s, u"contains"_s, u"anyOf"_s, u"notContains"_s, u"matches"_s, u"notMatches"_s},
                       what + u" expect.files"_s);
            FileCheck fc;
            fc.paths = r.strings(fo, u"path"_s, what + u" expect.files"_s);
            if (fc.paths.isEmpty())
                r.fail(u"%1 expect.files: \"path\" is missing"_s.arg(what));
            fc.text = r.text(fo, what + u" " + fc.paths.join(u" | "_s));
            t.files << fc;
        }
        t.onlyListedChanges = r.boolean(expect, u"onlyListedChanges"_s, true, what);
        const QJsonObject answer = expect.value(u"answer"_s).toObject();
        r.onlyKeys(answer, {u"contains"_s, u"anyOf"_s, u"notContains"_s, u"matches"_s, u"notMatches"_s},
                   what + u" expect.answer"_s);
        t.answer = r.text(answer, what + u" expect.answer"_s);
        t.toolsCalled = r.strings(expect, u"toolsCalled"_s, what);
        t.toolsNotCalled = r.strings(expect, u"toolsNotCalled"_s, what);
        t.noTools = r.boolean(expect, u"noTools"_s, false, what);
        t.solution = r.solution(o.value(u"solution"_s), what);
        if (t.solution.isEmpty())
            r.fail(u"%1: no solution (every task shows one way of doing it)"_s.arg(what));
        if (!r.ok()) {
            *error = r.error();
            return std::nullopt;
        }
        tasks << t;
    }
    if (tasks.isEmpty()) {
        *error = u"%1: no tasks"_s.arg(name);
        return std::nullopt;
    }
    return tasks;
}

// ---- running -------------------------------------------------------------------------------------

/// The notes (Markdown files) of a notebook folder, by their path in it; folders whose name starts
/// with a dot (the app's own, the trash) left out.
QHash<QString, QString> notesOf(const QString &dir)
{
    QHash<QString, QString> notes;
    const QDir root(QFileInfo(dir).absoluteFilePath());
    QDirIterator it(root.path(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString path = root.relativeFilePath(file);
        if (path.startsWith(u'.') || path.contains(u"/."_s) || !path.endsWith(u".md"_s, Qt::CaseInsensitive))
            continue;
        QFile f(file);
        if (f.open(QIODevice::ReadOnly))
            notes.insert(path, QString::fromUtf8(f.readAll()));
    }
    return notes;
}

bool copyFolder(const QString &from, const QString &to, QString *error)
{
    // (Absolute: a relative folder's files are named relative to the working directory.)
    const QDir source(QFileInfo(from).absoluteFilePath());
    if (!source.exists()) {
        *error = u"no notebook folder %1"_s.arg(from);
        return false;
    }
    QDirIterator it(source.path(), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + u'/' + source.relativeFilePath(file);
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(file, target)) {
            *error = u"cannot copy %1 to %2"_s.arg(file, target);
            return false;
        }
    }
    return true;
}

void countResults(const QVariantMap &request, Outcome *outcome)
{
    // The tool results the request carries in its last message: errors counted (a declined change
    // is the user's no, not the model's mistake).
    const QVariantList messages = request.value(u"messages"_s).toList();
    if (messages.isEmpty())
        return;
    for (const QVariant &v : messages.last().toMap().value(u"results"_s).toList()) {
        const QVariantMap result = v.toMap();
        const QString content = result.value(u"content"_s).toString();
        if (!result.value(u"isError"_s).toBool() || content == u"The user declined this change.")
            continue;
        ++outcome->toolErrors;
        outcome->toolFailures << content;
        if (content.startsWith(u"No tool named"_s))
            ++outcome->unknownTools;
    }
}

} // namespace

std::optional<QList<Task>> readTasks(const QString &fileOrFolder, QString *error)
{
    const QFileInfo info(fileOrFolder);
    QStringList files;
    if (info.isDir()) {
        const QDir dir(fileOrFolder);
        for (const QString &name : dir.entryList({u"*.json"_s}, QDir::Files, QDir::Name))
            files << dir.filePath(name);
    } else {
        files << fileOrFolder;
    }
    if (files.isEmpty()) {
        *error = u"%1: no task files"_s.arg(fileOrFolder);
        return std::nullopt;
    }
    QList<Task> all;
    QSet<QString> ids;
    for (const QString &file : std::as_const(files)) {
        const std::optional<QList<Task>> tasks = readFile(file, error);
        if (!tasks)
            return std::nullopt;
        for (const Task &t : *tasks) {
            if (ids.contains(t.id)) {
                *error = u"%1: the id \"%2\" is taken by another task"_s.arg(QFileInfo(file).fileName(), t.id);
                return std::nullopt;
            }
            ids.insert(t.id);
            all << t;
        }
    }
    return all;
}

ai::Reply replyOf(const Step &step, const ai::Request &request)
{
    ai::Reply reply;
    reply.ok = true;
    reply.model = u"solution"_s;
    reply.text = step.answer.trimmed();
    reply.stopReason = step.calls.isEmpty() ? u"stop"_s : u"tool_calls"_s;
    QJsonArray rawCalls;
    for (qsizetype c = 0; c < step.calls.size(); ++c) {
        ai::ToolCall call = step.calls.at(c);
        // As the model on the device names them: the turn's number and the call's.
        call.id = u"call_%1_%2"_s.arg(request.messages.size()).arg(c + 1);
        const QString arguments =
                QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(call.input)).toJson(QJsonDocument::Compact));
        rawCalls.append(QJsonObject{{u"id"_s, call.id},
                                    {u"type"_s, u"function"_s},
                                    {u"function"_s, QJsonObject{{u"name"_s, call.name}, {u"arguments"_s, arguments}}}});
        reply.toolCalls << call;
    }
    QJsonObject raw{{u"role"_s, u"assistant"_s}, {u"content"_s, step.answer}};
    if (!step.thoughts.isEmpty())
        raw.insert(u"reasoning_content"_s, step.thoughts);
    if (!rawCalls.isEmpty())
        raw.insert(u"tool_calls"_s, rawCalls);
    reply.raw = QJsonDocument(raw).toJson(QJsonDocument::Compact);
    return reply;
}

void SolutionResponder::begin(const Task &task)
{
    m_steps = task.solution;
    m_next = 0;
    m_skipped = 0;
}

QString plainText(const QString &text); // (below, with the checks)

ai::Reply SolutionResponder::answer(const ai::Request &request, Round *round)
{
    (void)round;
    if (m_next >= m_steps.size()) {
        ai::Reply reply;
        reply.error = u"the solution has no step left"_s;
        return reply;
    }
    // A step for what a tool's result already shows (a page the search showed whole) is passed.
    const auto shown = [&request](const QString &text) {
        const QString wanted = text::foldKey(plainText(text));
        for (const ai::Message &message : request.messages) {
            for (const ai::ToolResult &result : message.results) {
                if (text::foldKey(plainText(result.content)).contains(wanted))
                    return true;
            }
        }
        return false;
    };
    while (m_next < m_steps.size() - 1 && !m_steps.at(m_next).unlessShown.isEmpty() && shown(m_steps.at(m_next).unlessShown)) {
        ++m_next;
        ++m_skipped;
    }
    Step step = m_steps.at(m_next++);
    // A block's ref as a tool's result gave it ("{{ref Dom.md#L3}}" → "Dom.md#L3@1a2b3c4d"): a
    // solution, like the model, takes it from what it found; one it never found stays as written
    // (and the tool refuses it).
    static const QRegularExpression placeholder(u"^\\{\\{ref (.+)\\}\\}$"_s);
    for (ai::ToolCall &call : step.calls) {
        for (auto it = call.input.begin(); it != call.input.end(); ++it) {
            const QRegularExpressionMatch m = placeholder.match(it.value().toString());
            if (!m.hasMatch())
                continue;
            const QRegularExpression ref(QRegularExpression::escape(m.captured(1)) + u"@[0-9a-f]+"_s);
            for (const ai::Message &message : request.messages) {
                for (const ai::ToolResult &result : message.results) {
                    if (const QRegularExpressionMatch found = ref.match(result.content); found.hasMatch())
                        it.value() = found.captured(0);
                }
            }
        }
    }
    return replyOf(step, request);
}

QStringList Outcome::changed() const
{
    QStringList paths;
    for (auto it = after.cbegin(); it != after.cend(); ++it) {
        if (before.value(it.key()) != it.value() || !before.contains(it.key()))
            paths << it.key();
    }
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!after.contains(it.key()))
            paths << it.key();
    }
    paths.sort();
    return paths;
}

Outcome run(const Task &task, const QString &notebooksDir, const QString &workDir, Responder &responder,
            const QString &assistant)
{
    Outcome outcome;
    QElapsedTimer clock;
    clock.start();
    const QString dir = workDir + u"/notebook"_s;
    if (!copyFolder(notebooksDir + u'/' + task.notebook, dir, &outcome.error))
        return outcome;
    outcome.before = notesOf(dir);

    Vault vault(dir, workDir + u"/state"_s);
    QString error;
    if (!vault.open(&error)) {
        outcome.error = u"the notebook does not open: "_s + error;
        return outcome;
    }
    VaultIndex index(vault.config());
    for (auto it = outcome.before.cbegin(); it != outcome.before.cend(); ++it)
        index.update(it.key(), it.value());
    extensions::NotebookHost::Options options;
    const QDateTime now = task.now;
    options.now = [now] { return now; };
    options.language = task.language;
    options.uiLanguage = task.language;
    options.updateIndex = true;
    extensions::NotebookHost host(vault, index, options);
    const QVariantMap model = responder.aiModel();
    host.onAiModel = [model] { return model; };
    extensions::Engine engine;
    // (Generous: the script's own work between two requests is small, but a sanitizer build is slow.)
    if (!engine.start(&host, extensions::Limits{20000, 256ll << 20}, &error)) {
        outcome.error = u"the Luau engine does not start: "_s + error;
        return outcome;
    }
    extensions::Extension script = extensions::builtinExtension(u"asystent"_s);
    if (!assistant.isEmpty())
        script.source = assistant;
    const extensions::Result loaded = engine.load(script);
    if (!loaded.ok()) {
        outcome.error = u"the assistant does not load: "_s + loaded.error;
        return outcome;
    }

    responder.begin(task);
    qsizetype answered = 0;
    QString lastText;
    bool lastCalled = false;
    qsizetype lastMessages = 0; // of the last request with tools
    extensions::Result r = engine.runCommand(
            u"asystent"_s, u"chat"_s,
            {{u"page"_s, task.page}, {u"messages"_s, task.messages}, {u"conversation"_s, u"AI/Rozmowa testowa"_s}});
    while (r.status == extensions::Status::Suspended) {
        const QVariantMap question = r.value.toMap();
        const QString kind = question.value(u"kind"_s).toString();
        if (kind == u"ai") {
            const QVariantMap table = question.value(u"request"_s).toMap();
            countResults(table, &outcome);
            Round round;
            ai::Reply reply;
            QString why;
            if (const std::optional<ai::Request> request = ai::requestFrom(table, &why)) {
                reply = responder.answer(*request, &round);
                // (Only the assistant's requests with tools are its loop; a look at a picture is one of
                // its own, whose answer is a tool's result.)
                if (!request->tools.isEmpty()) {
                    // A request with no more messages than the one before: the assistant asked again
                    // for an answer it did not take (one that said a change was made, none made).
                    if (request->messages.size() <= lastMessages)
                        outcome.assisted = true;
                    lastMessages = request->messages.size();
                    outcome.lastRequest = table;
                    outcome.lastRaw = reply.raw;
                    lastText = reply.text;
                    lastCalled = !reply.toolCalls.isEmpty();
                }
            } else {
                reply.error = why;
            }
            round.stopReason = reply.stopReason;
            round.error = reply.ok ? QString() : reply.error;
            for (const ai::ToolCall &call : std::as_const(reply.toolCalls)) {
                round.calls << call.name;
                outcome.toolsCalled << call.name;
            }
            outcome.rounds << round;
            r = engine.resume(r.call, reply.toVariant());
        } else if (kind == u"choose") {
            // A change to approve: yes, yes to all of the question's, no (the assistant's order).
            ++outcome.confirmations;
            const QStringList choices = question.value(u"options"_s).toStringList();
            const QString wanted = task.answers.value(answered++, u"yes"_s);
            const qsizetype which = wanted == u"no" ? 2 : wanted == u"all" ? 1 : 0;
            if (choices.size() != 3) {
                outcome.error = u"the assistant asked a question that is no change to approve: "_s
                                + question.value(u"prompt"_s).toString();
                engine.cancel(r.call);
                break;
            }
            r = engine.resume(r.call, choices.at(which));
        } else {
            outcome.error = u"the assistant asked a question no task answers (%1): %2"_s.arg(
                    kind, question.value(u"prompt"_s).toString());
            engine.cancel(r.call);
            break;
        }
    }
    // The model's failure first: the script then fails with it (b.ai raises the error).
    for (const Round &round : std::as_const(outcome.rounds)) {
        if (!round.error.isEmpty() && outcome.error.isEmpty())
            outcome.error = u"the model failed: "_s + round.error;
    }
    if (r.status != extensions::Status::Suspended && !r.ok() && outcome.error.isEmpty())
        outcome.error = u"the assistant failed: "_s + r.error;
    if (r.ok())
        outcome.message = r.value.toString();
    if (!lastCalled) {
        outcome.said = lastText.trimmed();
    } else if (r.ok() && !outcome.message.startsWith(u"_("_s)) {
        // The model still calling, yet the assistant answered: it ended the loop itself (the model
        // only repeated a change it had made). Its answer is the message's, without the notes
        // under it (the pages changed, a page cut short); not when the steps ran out — that
        // message is the assistant's note "_(… did not finish …)_".
        outcome.assisted = true;
        QString said = outcome.message;
        for (const QString &note : {u"\n\n✏️"_s, u"\n\n_("_s}) {
            if (const qsizetype at = said.indexOf(note); at >= 0)
                said.truncate(at);
        }
        outcome.said = said.trimmed();
    }
    outcome.after = notesOf(dir);
    outcome.ms = clock.elapsed();
    return outcome;
}

QString plainText(const QString &text)
{
    // Models write "601 234 567" with narrow no-break spaces, "2026‑12‑31" with non-breaking
    // hyphens and "didn’t" with a typographic apostrophe: what a reader sees as spaces, hyphens
    // and apostrophes counts as such.
    QString out = text;
    for (QChar &c : out) {
        if (c.category() == QChar::Separator_Space || c == u'\t')
            c = u' ';
        else if ((c.unicode() >= 0x2010 && c.unicode() <= 0x2015) || c.unicode() == 0x2212)
            c = u'-';
        else if (c.unicode() == 0x2018 || c.unicode() == 0x2019 || c.unicode() == 0x02BC)
            c = u'\'';
    }
    return out;
}

QStringList checkText(const TextCheck &check, const QString &original, const QString &what)
{
    QStringList failures;
    const QString text = plainText(original);
    const QString folded = text::foldKey(text);
    for (const QString &word : check.contains) {
        if (!folded.contains(text::foldKey(plainText(word))))
            failures << u"%1: lacks “%2”"_s.arg(what, word);
    }
    for (const QStringList &group : check.anyOf) {
        const bool found = std::any_of(group.cbegin(), group.cend(),
                                       [&folded](const QString &word) { return folded.contains(text::foldKey(plainText(word))); });
        if (!found)
            failures << u"%1: lacks any of “%2”"_s.arg(what, group.join(u"” | “"_s));
    }
    for (const QString &word : check.notContains) {
        if (folded.contains(text::foldKey(plainText(word))))
            failures << u"%1: holds “%2”"_s.arg(what, word);
    }
    const auto expression = [](const QString &pattern) {
        return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption
                                                   | QRegularExpression::UseUnicodePropertiesOption);
    };
    for (const QString &pattern : check.matches) {
        if (!expression(pattern).match(text).hasMatch())
            failures << u"%1: does not match /%2/"_s.arg(what, pattern);
    }
    for (const QString &pattern : check.notMatches) {
        if (const QRegularExpressionMatch m = expression(pattern).match(text); m.hasMatch())
            failures << u"%1: matches /%2/ (“%3”)"_s.arg(what, pattern, m.captured(0).trimmed());
    }
    return failures;
}

// ---- grounding -----------------------------------------------------------------------------------

namespace {

/// A fact of an answer: as written, and what it is.
struct Fact {
    QString text;
    QString digits;   ///< a number's or a date's digits ("4 500" → "4500"); "" for a time
    int minutes = -1; ///< a time's, from midnight
    qsizetype start = 0; ///< where it is in the answer
};

/// A time of the day in a text: where, and its minutes from midnight.
struct TimeAt {
    qsizetype start = 0;
    qsizetype length = 0;
    int minutes = 0;
};

/// The times of a text, on either clock: "16:00", "4 pm", "4:30 p.m." — with `dotted` also
/// "16.00", as a note may write one (not an answer's: there it is as likely an amount).
QList<TimeAt> timesOf(const QString &plain, bool dotted)
{
    static const QRegularExpression clock(u"(?<![0-9])([0-9]{1,2}):([0-9]{2})(?![0-9])"_s);
    static const QRegularExpression dottedClock(u"(?<![0-9])([0-9]{1,2})[:.]([0-9]{2})(?![0-9])"_s);
    static const QRegularExpression twelve(u"(?<![0-9])([0-9]{1,2})(?::([0-9]{2}))? ?([ap])\\.?m\\b\\.?"_s,
                                           QRegularExpression::CaseInsensitiveOption);
    QList<TimeAt> out;
    for (QRegularExpressionMatchIterator it = twelve.globalMatch(plain); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const int hour = m.captured(1).toInt();
        const int minute = m.captured(2).toInt();
        if (hour >= 1 && hour <= 12 && minute <= 59) {
            const int pm = m.captured(3).compare(u"p"_s, Qt::CaseInsensitive) == 0 ? 12 : 0;
            out << TimeAt{m.capturedStart(0), m.capturedLength(0), (hour % 12 + pm) * 60 + minute};
        }
    }
    const qsizetype twelves = out.size();
    for (QRegularExpressionMatchIterator it = (dotted ? dottedClock : clock).globalMatch(plain); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const int hour = m.captured(1).toInt();
        const int minute = m.captured(2).toInt();
        // ("4:30 pm" is one time, the afternoon's.)
        const bool inTwelve = std::any_of(out.cbegin(), out.cbegin() + twelves, [&m](const TimeAt &t) {
            return m.capturedStart(0) >= t.start && m.capturedStart(0) < t.start + t.length;
        });
        if (!inTwelve && hour <= 23 && minute <= 59)
            out << TimeAt{m.capturedStart(0), m.capturedLength(0), hour * 60 + minute};
    }
    return out;
}

QString digitsOf(const QString &text)
{
    QString out;
    for (const QChar c : text) {
        if (c >= u'0' && c <= u'9')
            out += c;
    }
    return out;
}

QString withoutLeadingZeros(const QString &digits)
{
    qsizetype i = 0;
    while (i + 1 < digits.size() && digits.at(i) == u'0')
        ++i;
    return digits.mid(i);
}

/// The numbers of a text as their digit groups: "tel. 601 234 567, 2026-11-20 09:00" → {601, 234,
/// 567}, {2026, 11, 20, 09, 00} (groups joined by one space, dot, comma, colon, slash or hyphen).
QList<QStringList> numbersOf(const QString &plain)
{
    static const QRegularExpression number(u"[0-9]+(?:[ .,:/-][0-9]+)*"_s);
    static const QRegularExpression group(u"[0-9]+"_s);
    QList<QStringList> out;
    for (QRegularExpressionMatchIterator it = number.globalMatch(plain); it.hasNext();) {
        QStringList groups;
        for (QRegularExpressionMatchIterator g = group.globalMatch(it.next().captured(0)); g.hasNext();)
            groups << g.next().captured(0);
        out << groups;
    }
    return out;
}

/// The facts of an answer: its dates, its times, then its other numbers of three digits or more.
QList<Fact> factsOf(const QString &answer)
{
    QList<Fact> facts;
    QString rest = plainText(answer);
    const auto takeOut = [&rest](qsizetype start, qsizetype length) { rest.replace(start, length, QString(length, u'|')); };
    static const QRegularExpression date(u"(?<![0-9])[0-9]{4}-[0-9]{2}-[0-9]{2}(?![0-9])"_s);
    for (QRegularExpressionMatchIterator it = date.globalMatch(rest); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        facts << Fact{m.captured(0), digitsOf(m.captured(0)), -1, m.capturedStart(0)};
        takeOut(m.capturedStart(0), m.capturedLength(0));
    }
    for (const TimeAt &t : timesOf(rest, false)) {
        facts << Fact{rest.mid(t.start, t.length).trimmed(), QString(), t.minutes, t.start};
        takeOut(t.start, t.length);
    }
    static const QRegularExpression number(u"[0-9]+(?:[ .,-][0-9]+)*"_s);
    for (QRegularExpressionMatchIterator it = number.globalMatch(rest); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        if (const QString digits = digitsOf(m.captured(0)); digits.size() >= 3)
            facts << Fact{m.captured(0), digits, -1, m.capturedStart(0)};
    }
    return facts;
}

/// The full hours a text gives bare, as hours: "między 12 a 14", "between 12 and 2 pm", "od 9 do
/// 17", "po 16", "o 10", "after 4", "at 10" — each as that hour and twelve hours later (a bare hour
/// says neither morning nor afternoon), in minutes from midnight.
QSet<int> bareHoursOf(const QString &plain)
{
    static const QRegularExpression range(
            u"\\b(?:między|miedzy|between|od|from)\\s+([0-9]{1,2})\\s+(?:a|and|do|to|i)\\s+([0-9]{1,2})(?![0-9.,:])"_s,
            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression single(
            u"\\b(?:po|przed|o|około|okolo|ok\\.|godz\\.|after|before|at|around|until|till|by)\\s+([0-9]{1,2})(?![0-9.,:])"_s,
            QRegularExpression::CaseInsensitiveOption);
    QSet<int> out;
    const auto add = [&out](const QString &number) {
        const int hour = number.toInt();
        if (hour > 23)
            return;
        out.insert(hour * 60);
        if (hour < 12)
            out.insert((hour + 12) * 60);
        else if (hour == 12)
            out.insert(0);
    };
    for (QRegularExpressionMatchIterator it = range.globalMatch(plain); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        add(m.captured(1));
        add(m.captured(2));
    }
    for (QRegularExpressionMatchIterator it = single.globalMatch(plain); it.hasNext();)
        add(it.next().captured(1));
    return out;
}

/// A number as written: "4 500", "245,67", "6,750", "1.5" (a last dot or comma before one or two
/// digits is the decimal point, before three a thousands' separator).
double valueOf(QString number)
{
    number.remove(u' ');
    const qsizetype point = std::max(number.lastIndexOf(u'.'), number.lastIndexOf(u','));
    QString whole = number;
    QString fraction;
    if (point >= 0 && number.size() - point - 1 <= 2) {
        whole = number.left(point);
        fraction = number.mid(point + 1);
    }
    whole.remove(u'.');
    whole.remove(u',');
    return QString(whole + u'.' + (fraction.isEmpty() ? u"0"_s : fraction)).toDouble();
}

/// Where the answer works a number out itself: the numbers right after an "=" that the arithmetic
/// just before it gives — numbers joined by + - × x * · / ÷ (or plus, minus, razy, times), with
/// short words (units, "to") and , £ € $ % between them: "8 × 60 = 480 minut, plus 30 = 510",
/// "£245.67 + £207.46 = £453.13". By where each starts in the answer.
QSet<qsizetype> workedOutIn(const QString &plain)
{
    enum Kind { Number, Operator, Equals, Skip, Stop };
    struct Token {
        Kind kind;
        QString text;
        qsizetype start;
    };
    static const QRegularExpression token(
            u"(?<n>[0-9]+(?:[ .,][0-9]+)*)|(?<op>[-+×*·/÷]|\\b(?:x|plus|minus|razy|times)\\b)|(?<eq>=)"
            "|(?<w>[^\\W\\d_]+)|(?<o>\\S)"_s,
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    QList<Token> tokens;
    for (QRegularExpressionMatchIterator it = token.globalMatch(plain); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        Kind kind = Stop;
        if (m.capturedLength(u"n"_s) > 0)
            kind = Number;
        else if (m.capturedLength(u"op"_s) > 0)
            kind = Operator;
        else if (m.capturedLength(u"eq"_s) > 0)
            kind = Equals;
        else if (m.capturedLength(u"w"_s) > 0)
            kind = m.capturedLength(u"w"_s) <= 6 ? Skip : Stop;
        else
            kind = u",£€$%"_s.contains(m.captured(0)) ? Skip : Stop;
        tokens << Token{kind, m.captured(0).trimmed(), m.capturedStart(0)};
    }
    QSet<qsizetype> out;
    for (qsizetype e = 0; e < tokens.size(); ++e) {
        if (tokens.at(e).kind != Equals)
            continue;
        qsizetype r = e + 1;
        while (r < tokens.size() && tokens.at(r).kind == Skip && tokens.at(r).text != u",")
            ++r; // ("= £453.13")
        if (r >= tokens.size() || tokens.at(r).kind != Number)
            continue;
        // The expression before it, from its end: a number, then an operator and a number, again…
        QList<double> numbers;
        QStringList operators;
        bool wantNumber = true;
        for (qsizetype i = e - 1; i >= 0; --i) {
            const Token &t = tokens.at(i);
            if (t.kind == Skip)
                continue;
            if (wantNumber && t.kind == Number) {
                numbers.prepend(valueOf(t.text));
            } else if (!wantNumber && t.kind == Operator) {
                operators.prepend(t.text.toLower());
            } else {
                break;
            }
            wantNumber = !wantNumber;
        }
        if (numbers.size() < 2)
            continue;
        if (operators.size() == numbers.size()) // (it began with an operator: no number to it)
            operators.removeFirst();
        // × and / first, then + and -.
        QList<double> terms{numbers.first()};
        QList<int> signs{1};
        for (qsizetype k = 0; k < operators.size(); ++k) {
            const QString &op = operators.at(k);
            const double next = numbers.at(k + 1);
            if (op == u"+" || op == u"plus") {
                terms << next;
                signs << 1;
            } else if (op == u"-" || op == u"minus") {
                terms << next;
                signs << -1;
            } else if (op == u"/" || op == u"÷") {
                terms.last() = next == 0 ? qQNaN() : terms.last() / next;
            } else {
                terms.last() *= next;
            }
        }
        double value = 0;
        for (qsizetype k = 0; k < terms.size(); ++k)
            value += signs.at(k) * terms.at(k);
        const double result = valueOf(tokens.at(r).text);
        if (std::abs(value - result) < 0.005 + 1e-9 * std::abs(result))
            out.insert(tokens.at(r).start);
    }
    return out;
}

/// Is a number of these digits one of `numbers`, or a run of a number's groups?
bool holdsDigits(const QList<QStringList> &numbers, const QString &digits)
{
    const QString wanted = withoutLeadingZeros(digits);
    for (const QStringList &groups : numbers) {
        for (qsizetype from = 0; from < groups.size(); ++from) {
            QString joined;
            for (qsizetype to = from; to < groups.size() && joined.size() < digits.size(); ++to) {
                joined += groups.at(to);
                if (withoutLeadingZeros(joined) == wanted)
                    return true;
            }
        }
    }
    return false;
}

/// Does a check of `workedOut` ask for this fact (a word of it the same number or time, or an
/// expression of it matching the fact)?
bool askedFor(const TextCheck &workedOut, const Fact &fact)
{
    QStringList words = workedOut.contains;
    for (const QStringList &group : workedOut.anyOf)
        words += group;
    for (const QString &word : std::as_const(words)) {
        const QString plain = plainText(word).trimmed();
        if (plain == fact.text)
            return true;
        if (const QString digits = digitsOf(plain);
            !fact.digits.isEmpty() && !digits.isEmpty() && withoutLeadingZeros(digits) == withoutLeadingZeros(fact.digits))
            return true;
    }
    return std::ranges::any_of(workedOut.matches, [&fact](const QString &pattern) {
        return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(fact.text).hasMatch();
    });
}

/// The facts of `answer` that `seen` does not hold (ungroundedFacts()).
QList<Fact> unreadFacts(const QString &answer, const QString &seen, const TextCheck &workedOut)
{
    const QString plain = plainText(seen);
    const QList<QStringList> numbers = numbersOf(plain);
    QSet<int> times;
    for (const TimeAt &t : timesOf(plain, true))
        times.insert(t.minutes);
    const QSet<int> hours = bareHoursOf(plain);
    const QSet<qsizetype> worked = workedOutIn(plainText(answer));
    QList<Fact> unread;
    for (const Fact &fact : factsOf(answer)) {
        const bool held = fact.minutes >= 0 ? times.contains(fact.minutes) || hours.contains(fact.minutes)
                                            : holdsDigits(numbers, fact.digits) || worked.contains(fact.start);
        if (!held && !askedFor(workedOut, fact))
            unread << fact;
    }
    return unread;
}

} // namespace

QStringList ungroundedFacts(const QString &answer, const QString &seen, const TextCheck &workedOut)
{
    QStringList failures;
    for (const Fact &fact : unreadFacts(answer, seen, workedOut))
        failures << u"answer: “%1” is in nothing the model read — no tool result, message or system text of the run holds it"_s
                            .arg(fact.text);
    return failures;
}

QString seenBefore(const Outcome &outcome)
{
    // The system text is the app's, the same in every conversation: what it names is an example (the
    // journal "named by its date (2026-11-12)"), never a fact of the notebook — its words count, its
    // digits do not.
    QString system = outcome.lastRequest.value(u"system"_s).toString();
    for (QChar &c : system) {
        if (c >= u'0' && c <= u'9')
            c = u'#';
    }
    QStringList parts{system};
    bool first = true;
    for (const QVariant &v : outcome.lastRequest.value(u"messages"_s).toList()) {
        const QVariantMap message = v.toMap();
        QString content = message.value(u"content"_s).toString();
        if (first && message.value(u"role"_s).toString() == u"user") {
            // The app's day: today counts, the calendar after it does not.
            first = false;
            static const QRegularExpression date(u"(?<![0-9])[0-9]{4}-[0-9]{2}-[0-9]{2}(?![0-9])"_s);
            if (const qsizetype end = content.indexOf(u"\n\n"_s); end >= 0)
                content = date.match(content.left(end)).captured(0) + content.mid(end);
        }
        parts << content;
        for (const QVariant &result : message.value(u"results"_s).toList())
            parts << result.toMap().value(u"content"_s).toString();
        // The model's own turns: what it said and the calls it made (their inputs' texts), not its thoughts.
        const QJsonObject raw = QJsonDocument::fromJson(message.value(u"raw"_s).toByteArray()).object();
        parts << raw.value(u"content"_s).toString();
        for (const QJsonValue call : raw.value(u"tool_calls"_s).toArray()) {
            const QJsonValue arguments = call.toObject().value(u"function"_s).toObject().value(u"arguments"_s);
            const QJsonObject input = arguments.isObject()
                                              ? arguments.toObject()
                                              : QJsonDocument::fromJson(arguments.toString().toUtf8()).object();
            for (auto it = input.begin(); it != input.end(); ++it)
                parts << (it.value().isString() ? it.value().toString()
                                                : QString::fromUtf8(QJsonDocument(QJsonArray{it.value()}).toJson(QJsonDocument::Compact)));
        }
    }
    return parts.join(u'\n');
}

QStringList checkGrounding(const Task &task, const Outcome &outcome)
{
    if (outcome.said.isEmpty())
        return {};
    const bool worksItOut = task.category == u"multi" || task.category == u"no_tool";
    return ungroundedFacts(outcome.said, seenBefore(outcome), worksItOut ? task.answer : TextCheck());
}

// ---- what the answer says of the changes ---------------------------------------------------------

namespace {

QRegularExpression answerExpression(const QString &pattern)
{
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption
                                               | QRegularExpression::UseUnicodePropertiesOption);
}

/// A change the answer says it made: the first person of a verb of changing ("Dodałam", "zapisałem",
/// "przesunęłam", "I've added", "I marked") or a line opening with one ("Added … to [[Shopping]]",
/// "Moved …") — not "nie dodałam", "zapisałam się", "I've added nothing".
const QRegularExpression &claimOfChange()
{
    static const QRegularExpression e = answerExpression(
            u"(?<!\\bnie\\s)\\b(?:dodał|dopisał|zapisał|wpisał|zanotował|odhaczył|oznaczył|zaznaczył|ustawił|zmienił"
            u"|utworzył|stworzył|założył|przełożył|przeniosł|anulował)(?:em|am)\\b(?!\\s+si[ęe]\\b)"
            u"|(?<!\\bnie\\s)\\b(?:przesun|usun)(?:ąłem|ęłam)\\b|\\bI(?:'ve|\\s+have)?\\s+(?:just\\s+|now\\s+"
            u"|also\\s+)?(?:added|saved|noted|written down|logged|marked|ticked|checked off|moved|set|created"
            u"|updated|put|changed|recorded|scheduled|appended|rescheduled|cancell?ed|removed"
            u"|deleted)\\b(?!\\s+nothing)|(?:^|(?<=[.!?]\\s)|(?<=\\n))\\s*(?:[-*]\\s+)?(?:\\*\\*)?(?:added|saved|noted in"
            u"|noted down|logged|ticked off|moved|created|reminder set|appended)\\b"_s);
    return e;
}

/// That what was asked for was so before ("was already marked done", "the line already reads", "no
/// change needed", "Nie trzeba nic zmieniać", "było już").
const QRegularExpression &soAlready()
{
    static const QRegularExpression e = answerExpression(
            u"\\b(?:was|were|had)\\s+already\\b|\\balready\\s+(?:reads|read|had)\\b|\\b(?:it's|it\\s+is|is"
            u"|are)\\s+already\\s+(?:been\\s+)?(?:marked|ticked|checked|done|completed|recorded|set|due|on)\\b"
            u"|\\bno\\s+(?:further\\s+)?changes?\\s+(?:is\\s+|was\\s+|were\\s+)?(?:needed|necessary|required)"
            u"|\\bnothing\\s+(?:left\\s+|more\\s+)?to\\s+(?:change|check off|tick off|tick|update|move|mark)"
            u"|\\b(?:był[aoy]?|były)\\s+już\\b|\\bjuż\\s+(?:był[aoy]?|były)\\b|\\bnie\\s+trzeba\\s+(?:było\\s+)?(?:nic\\s+"
            u"|niczego\\s+)?(?:zmienia|zmieni|odhacza|robi|przesuwa)|\\bnic\\s+nie\\s+trzeba\\b|\\bnie\\s+(?:ma"
            u"|było)\\s+potrzeby\\b"_s);
    return e;
}

/// That the change is there now, in other words than a claim: "now", "teraz", "Done", "Marked …",
/// "zaktualizowałem", "dodano", "została zmieniona", "has been moved".
const QRegularExpression &soNow()
{
    static const QRegularExpression e = answerExpression(
            u"\\b(?:now|teraz|obecnie)\\b|\\bha(?:s|ve)\\s+been\\s+(?:marked|moved|added|ticked|set|updated|changed"
            u"|created|saved|checked)|(?:^|(?<=[.!?]\\s)|(?<=\\n))\\s*(?:[-*]\\s+)?(?:\\*\\*)?(?:marked|cancell?ed|done"
            u"|gotowe|zapisane|dodane|dopisane)\\b|zaktualizował|zastosował|przygotował|zrobił(?:em|am)\\s+to"
            u"|\\b(?:dodano|dopisano|zapisano|wpisano|odhaczono|oznaczono|zaznaczono|zmieniono|ustawiono"
            u"|przesunięto|utworzono|zaktualizowano|zanotowano)\\b|\\bzostał[aoy]?\\s+(?:zmienion|dodan|zapisan"
            u"|odhaczon|oznaczon|zaznaczon|przesunięt|zaktualizowan|ustawion|dopisan|utworzon|wpisan)"_s);
    return e;
}

/// The answer against what the run changed, whatever the task: a change claimed and no note changed
/// (the user believes it is there: "Zapisałem w dzienniku…" for a draft asked for); the run's own
/// change said to have been there already, and never said to be made (the user believes nothing was
/// done: "was already marked done — nothing left to check off"); a fact only the system text holds —
/// its example, the journal "named by its date (2026-11-12)" — given as the notebook's.
QStringList checkSaid(const Task &task, const Outcome &outcome)
{
    QStringList failures;
    if (outcome.said.isEmpty())
        return failures;
    const QString said = plainText(outcome.said);
    const QStringList changed = outcome.changed();
    const QRegularExpressionMatch claim = claimOfChange().match(said);
    if (changed.isEmpty() && claim.hasMatch())
        failures << u"answer: claims a change (“%1”), and no note changed"_s.arg(claim.captured(0).trimmed());
    if (const QRegularExpressionMatch already = soAlready().match(said);
        !changed.isEmpty() && already.hasMatch() && !claim.hasMatch() && !soNow().match(said).hasMatch())
        failures << u"answer: says it was so already (“%1”), and the run changed %2"_s.arg(already.captured(0).trimmed(),
                                                                                         changed.join(u", "_s));
    if (const QString system = outcome.lastRequest.value(u"system"_s).toString(); !system.isEmpty()) {
        const bool worksItOut = task.category == u"multi" || task.category == u"no_tool";
        const TextCheck workedOut = worksItOut ? task.answer : TextCheck();
        const QString seen = seenBefore(outcome);
        const QList<Fact> unread = unreadFacts(outcome.said, seen, workedOut);
        if (!unread.isEmpty()) {
            const QList<Fact> anyway = unreadFacts(outcome.said, seen + u'\n' + system, workedOut);
            for (const Fact &fact : unread) {
                const QString failure = u"answer: “%1” is only the system text's example, no fact of the notebook"_s.arg(fact.text);
                if (std::ranges::none_of(anyway, [&fact](const Fact &f) { return f.start == fact.start; })
                    && !failures.contains(failure))
                    failures << failure;
            }
        }
    }
    return failures;
}

} // namespace

QStringList check(const Task &task, const Outcome &outcome)
{
    QStringList failures;
    if (!outcome.error.isEmpty())
        failures << u"run: "_s + outcome.error;
    if (outcome.said.isEmpty())
        failures << u"answer: none (the assistant did not finish)"_s;
    QSet<QString> listed;
    for (const FileCheck &file : task.files) {
        // One of its paths passes, else what the first one there lacks.
        QStringList first;
        bool passed = false;
        bool found = false;
        for (const QString &path : file.paths) {
            listed.insert(path);
            if (passed || !outcome.after.contains(path))
                continue;
            const QStringList missed = checkText(file.text, outcome.after.value(path), path);
            passed = missed.isEmpty();
            if (!found)
                first = missed;
            found = true;
        }
        if (!found)
            failures << u"%1: no such note"_s.arg(file.paths.join(u" | "_s));
        else if (!passed)
            failures << first;
    }
    if (task.onlyListedChanges) {
        for (const QString &path : outcome.changed()) {
            if (!listed.contains(path))
                failures << u"%1: changed, and the task changes no such note"_s.arg(path);
        }
    }
    // The same line added twice: a change made again (a small model goes on adding what it has).
    for (const QString &path : outcome.changed()) {
        QHash<QString, int> lines;
        for (const QString &line : outcome.before.value(path).split(u'\n'))
            --lines[line.trimmed()];
        for (const QString &line : outcome.after.value(path).split(u'\n')) {
            const QString l = line.trimmed();
            if (!l.isEmpty() && ++lines[l] == 2) {
                failures << u"%1: “%2” added twice"_s.arg(path, l);
                break;
            }
        }
    }
    failures << checkText(task.answer, outcome.said, u"answer"_s);
    failures << checkSaid(task, outcome);
    for (const QString &tool : task.toolsCalled) {
        if (!outcome.toolsCalled.contains(tool))
            failures << u"tools: %1 not called"_s.arg(tool);
    }
    for (const QString &tool : task.toolsNotCalled) {
        if (outcome.toolsCalled.contains(tool))
            failures << u"tools: %1 called"_s.arg(tool);
    }
    if (task.noTools && !outcome.toolsCalled.isEmpty())
        failures << u"tools: %1 called, the task needs none"_s.arg(outcome.toolsCalled.join(u", "_s));
    return failures;
}

} // namespace brulion::assistantbench
