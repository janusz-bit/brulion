// brulion-assistantbench (docs/FINETUNE.md): runs the AI assistant's tasks (finetune/tasks) on their
// notebooks (finetune/notebooks) and says which it does — with the model on the device, opened as
// the app opens it (the plugin brulion_localai over llama.cpp, the model's own sampling), with the
// same model on a llama-server (--server: a GPU, many tasks at once, the training data), or with
// the tasks' own solutions (--solution: no model; every task must pass so, its answer stating no
// fact the run did not show — with --server and --training, the solutions as the model's own
// turns: the training data of what it cannot do).
//
//   brulion-assistantbench --solution
//   brulion-assistantbench --model ~/.cache/brulion-models/LFM3-1.7B-QAD-Q4_0.gguf --out base.jsonl
//   brulion-assistantbench --model … --filter '^dom-pl/' --traces traces.jsonl
//   brulion-assistantbench --server http://127.0.0.1:8091 --model … --jobs 8 --attempts 8
//       --temperature 0.8 --seed 1 --tasks train/tasks --notebooks train/notebooks --training training.jsonl
//   brulion-assistantbench --solution --server http://127.0.0.1:8091 --model … --tasks train/tasks
//       --notebooks train/notebooks --training taught.jsonl
//   brulion-assistantbench --solution --guided --server … --model … --attempts 4 --temperature 0.8
//       --tasks train/tasks --notebooks train/notebooks --training guided.jsonl
//   brulion-assistantbench --solution --correct 1 --server … --model … --effort low --tasks train/tasks
//       --notebooks train/notebooks --training corrected.jsonl
// (training data come from a generated set, finetune/generate.py: the hand-written tasks of
// finetune/tasks are for measuring only, and --training refuses them)
//   brulion-assistantbench --solution --assistant asystent-2026-11-17.luau   (another version of the assistant)

#include "bench.h"
#include "server.h"
#include "training.h"

#include "ai/localrunner.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThreadPool>
#include <QWaitCondition>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <optional>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

QTextStream &err()
{
    static QTextStream s(stderr);
    return s;
}

/// The model answers as in the app (ai::answerLocally: the request cut while it is too long, the
/// model's sampling) — on the device's runner or on a llama-server, whose tokens it keeps.
class ModelResponder : public assistantbench::Responder {
public:
    ModelResponder(ai::LocalRunner *runner, ai::LocalModel model, assistantbench::ServerRunner *server = nullptr,
                   QString effort = QString())
        : m_runner(runner), m_model(std::move(model)), m_server(server), m_effort(std::move(effort))
    {
    }

    void begin(const assistantbench::Task &task) override
    {
        (void)task;
        m_turns.clear();
    }

    ai::Reply answer(const ai::Request &request, assistantbench::Round *round) override
    {
        ai::LocalRunner::Result run;
        ai::Request asked = request;
        if (!m_effort.isEmpty())
            asked.effort = m_effort; // --effort: instead of the assistant's
        ai::Reply reply = ai::answerLocally(m_runner, m_model, asked, nullptr, &run);
        round->promptTokens = run.promptTokens;
        round->reusedTokens = run.reusedTokens + run.storedTokens;
        round->outputTokens = run.outputTokens;
        round->promptMs = run.promptMs;
        round->outputMs = run.outputMs;
        if (m_server && reply.error.isEmpty())
            m_turns.append(assistantbench::Turn{m_server->lastPrompt(), m_server->lastAnswer(), run.complete});
        return reply;
    }

    /// The requests of the task so far, as the model read and answered them (on a llama-server).
    const QList<assistantbench::Turn> &turns() const { return m_turns; }

private:
    ai::LocalRunner *m_runner;
    ai::LocalModel m_model;
    assistantbench::ServerRunner *m_server;
    QString m_effort;
    QList<assistantbench::Turn> m_turns;
};

/// Plays a task's solution as the model's own turns (--solution with --training): each step the
/// turn the model would have written (assistantbench::writtenAnswer), in the tokens of the prompt
/// the app writes — the training data of a task the model does not do by itself. With `answers`
/// (--guided) only the solution's tool calls are played so: where the solution answers, the model
/// answers itself (its own thoughts on what the tools gave). With `student` (--correct N) the model
/// takes the first N steps itself — read, not learnt — and the solution goes on from where it led:
/// the teacher's corrections in the states the model gets itself into (the solution's steps it
/// already did, the same calls, are not made again). A turn that cannot be written so, a prompt too
/// long for the model, a tool the model calls where the solution answers, the model answering
/// before the teacher takes over is the attempt's failure (error()).
class TeacherResponder : public assistantbench::Responder {
public:
    TeacherResponder(assistantbench::ServerRunner *server, const ai::LocalModel &model,
                     ModelResponder *answers = nullptr, QString effort = QString(), ModelResponder *student = nullptr,
                     int studentSteps = 0)
        : m_server(server), m_model(model), m_answers(answers), m_effort(std::move(effort)), m_student(student),
          m_studentSteps(studentSteps)
    {
    }

    void begin(const assistantbench::Task &task) override
    {
        m_solution.begin(task);
        m_turns.clear();
        m_error.clear();
        m_asked = 0;
        if (m_answers)
            m_answers->begin(task);
        if (m_student)
            m_student->begin(task);
    }

    ai::Reply answer(const ai::Request &request, assistantbench::Round *round) override
    {
        if (m_student && m_asked < m_studentSteps && m_error.isEmpty()) {
            ++m_asked;
            ai::Reply own = m_student->answer(request, round);
            if (!own.error.isEmpty()) {
                m_error = own.error;
            } else if (own.toolCalls.isEmpty()) {
                m_error = u"the model answered before the teacher took over"_s;
            } else if (!m_student->turns().isEmpty()) {
                assistantbench::Turn turn = m_student->turns().constLast();
                turn.learnt = false;
                m_turns.append(turn);
            }
            return own;
        }
        ++m_asked;
        // The solution's next step; one whose calls the model already made, the same, is passed.
        bool answering = false;
        ai::Reply reply;
        for (;;) {
            reply = m_solution.answer(request, round);
            if (!reply.ok || !m_error.isEmpty())
                return reply;
            answering = reply.toolCalls.isEmpty();
            if (answering || !m_student || !madeBefore(request, reply))
                break;
        }
        ai::Request asked = request;
        if (!m_effort.isEmpty())
            asked.effort = m_effort; // --effort: instead of the assistant's
        if (!ai::local::thinks(asked)) {
            // A model that does not think writes none of the solution's thoughts.
            QJsonObject raw = QJsonDocument::fromJson(reply.raw).object();
            raw.remove(u"reasoning_content"_s);
            reply.raw = QJsonDocument(raw).toJson(QJsonDocument::Compact);
        }
        if (m_answers && answering) {
            ai::Reply own = m_answers->answer(request, round);
            if (!own.error.isEmpty()) {
                m_error = own.error;
            } else if (!own.toolCalls.isEmpty()) {
                m_error = u"the model calls %1 where the solution answers"_s.arg(own.toolCalls.constFirst().name);
            } else if (!m_answers->turns().isEmpty()) {
                m_turns.append(m_answers->turns().constLast());
            }
            return own;
        }
        QString error;
        const std::optional<QList<ai::local::Piece>> written =
                assistantbench::writtenAnswer(m_model.format, asked, reply, u"<|im_end|>"_s, &error);
        std::optional<assistantbench::Tokenized> prompt;
        std::optional<assistantbench::Tokenized> answer;
        if (written)
            prompt = m_server->tokenize(ai::local::prompt(m_model.format, asked).pieces, &error);
        if (prompt)
            answer = m_server->tokenize(*written, &error);
        if (!answer) {
            m_error = error;
            return reply;
        }
        // As ai::answerLocally leaves room for the answer: the app would cut this prompt.
        const int context = m_server->contextTokens();
        const int reserve = std::clamp(request.maxTokens > 0 ? request.maxTokens : 1024, 256, std::max(256, context / 4));
        const int limit = context - std::clamp(reserve, 16, context / 2);
        round->promptTokens = int(prompt->all().size());
        round->outputTokens = int(answer->all().size());
        if (round->promptTokens > limit) {
            m_error = u"the request is too long for the model (%1 tokens; at most %2)"_s.arg(round->promptTokens).arg(limit);
            return reply;
        }
        m_turns.append(assistantbench::Turn{*prompt, *answer, true});
        return reply;
    }

    const QList<assistantbench::Turn> &turns() const { return m_turns; }
    const QString &error() const { return m_error; }

private:
    /// Did the conversation make every call of `reply` already, with the same input?
    static bool madeBefore(const ai::Request &request, const ai::Reply &reply)
    {
        QSet<QString> made;
        for (const ai::Message &m : request.messages) {
            for (const QJsonValue call : QJsonDocument::fromJson(m.raw).object().value(u"tool_calls"_s).toArray()) {
                const QJsonObject function = call.toObject().value(u"function"_s).toObject();
                const QJsonValue arguments = function.value(u"arguments"_s);
                const QJsonObject input = arguments.isObject()
                                                  ? arguments.toObject()
                                                  : QJsonDocument::fromJson(arguments.toString().toUtf8()).object();
                made.insert(function.value(u"name"_s).toString() + u' '
                            + QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact)));
            }
        }
        return !reply.toolCalls.isEmpty() && std::ranges::all_of(reply.toolCalls, [&made](const ai::ToolCall &c) {
            return made.contains(c.name + u' '
                                 + QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(c.input))
                                                             .toJson(QJsonDocument::Compact)));
        });
    }

    assistantbench::SolutionResponder m_solution;
    assistantbench::ServerRunner *m_server;
    ai::LocalModel m_model;
    ModelResponder *m_answers;
    QString m_effort;
    ModelResponder *m_student;
    int m_studentSteps = 0;
    int m_asked = 0;
    QList<assistantbench::Turn> m_turns;
    QString m_error;
};

/// A task's attempt: which, and with what seed.
struct Work {
    qsizetype task = 0;
    int attempt = 0;
    quint32 seed = 0;
};

/// What an attempt came to.
struct Done {
    assistantbench::Outcome outcome;
    QStringList failures;
    /// A model's answer: the facts it states that the run never showed (checkGrounding) — shown, not
    /// counted: a date it works out ("the next collection is Sunday, 2027-01-24"), a range ("12–14") or
    /// a date written otherwise is one too, as is a number made up.
    QStringList ungrounded;
    QList<assistantbench::Turn> turns;
    bool finished = false;
};

QJsonObject resultJson(const assistantbench::Task &task, const Work &work, const Done &done, const QString &model,
                       double temperature)
{
    const assistantbench::Outcome &outcome = done.outcome;
    QJsonArray rounds;
    for (const assistantbench::Round &r : outcome.rounds) {
        rounds.append(QJsonObject{{u"calls"_s, QJsonArray::fromStringList(r.calls)},
                                  {u"stopReason"_s, r.stopReason},
                                  {u"error"_s, r.error},
                                  {u"promptTokens"_s, r.promptTokens},
                                  {u"reusedTokens"_s, r.reusedTokens},
                                  {u"outputTokens"_s, r.outputTokens},
                                  {u"promptMs"_s, r.promptMs},
                                  {u"outputMs"_s, r.outputMs}});
    }
    QJsonObject changed;
    for (const QString &path : outcome.changed())
        changed.insert(path, outcome.after.contains(path) ? QJsonValue(outcome.after.value(path)) : QJsonValue());
    return QJsonObject{{u"id"_s, task.id},
                       {u"category"_s, task.category},
                       {u"language"_s, task.language},
                       {u"model"_s, model},
                       {u"attempt"_s, work.attempt},
                       {u"seed"_s, qint64(work.seed)},
                       {u"temperature"_s, temperature},
                       {u"passed"_s, done.failures.isEmpty()},
                       {u"failures"_s, QJsonArray::fromStringList(done.failures)},
                       {u"ungrounded"_s, QJsonArray::fromStringList(done.ungrounded)},
                       {u"said"_s, outcome.said},
                       {u"error"_s, outcome.error},
                       {u"tools"_s, QJsonArray::fromStringList(outcome.toolsCalled)},
                       {u"toolErrors"_s, outcome.toolErrors},
                       {u"unknownTools"_s, outcome.unknownTools},
                       {u"toolFailures"_s, QJsonArray::fromStringList(outcome.toolFailures)},
                       {u"confirmations"_s, outcome.confirmations},
                       {u"changed"_s, changed},
                       {u"rounds"_s, rounds},
                       {u"ms"_s, outcome.ms}};
}

/// Sums of runs, for the summary.
struct Tally {
    int runs = 0;
    int passed = 0;
    int rounds = 0;
    int calls = 0;
    int toolErrors = 0;
    int unknownTools = 0;
    int failedReplies = 0;
    qint64 lastPromptTokens = 0; ///< of each run's last request: how far its context grew
    qint64 outputTokens = 0;
    qint64 ms = 0;
    QSet<QString> tasks;     ///< the tasks run
    QSet<QString> doneTasks; ///< of them, those passed at least once

    void add(const QString &task, const assistantbench::Outcome &o, bool ok)
    {
        ++runs;
        passed += ok ? 1 : 0;
        tasks.insert(task);
        if (ok)
            doneTasks.insert(task);
        rounds += int(o.rounds.size());
        calls += int(o.toolsCalled.size());
        toolErrors += o.toolErrors;
        unknownTools += o.unknownTools;
        for (const assistantbench::Round &r : o.rounds) {
            outputTokens += r.outputTokens;
            failedReplies += r.error.isEmpty() ? 0 : 1;
        }
        if (!o.rounds.isEmpty())
            lastPromptTokens += o.rounds.last().promptTokens + o.rounds.last().outputTokens;
        ms += o.ms;
    }
};

QString percent(qsizetype part, qsizetype whole)
{
    return whole > 0 ? u"%1%"_s.arg(100.0 * double(part) / double(whole), 0, 'f', 0) : u"–"_s;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"brulion-assistantbench"_s);

    QCommandLineParser parser;
    parser.setApplicationDescription(
            u"Runs the AI assistant's tasks on their notebooks and says which it does (docs/FINETUNE.md)."_s);
    parser.addHelpOption();
    const QCommandLineOption tasksOpt(u"tasks"_s, u"A task file, or a folder of them (default: finetune/tasks)."_s,
                                      u"path"_s, u"finetune/tasks"_s);
    const QCommandLineOption notebooksOpt(u"notebooks"_s, u"The notebooks' folder (default: finetune/notebooks)."_s,
                                          u"dir"_s, u"finetune/notebooks"_s);
    const QCommandLineOption filterOpt(u"filter"_s, u"Only the tasks whose id matches this expression."_s, u"regex"_s);
    const QCommandLineOption solutionOpt(u"solution"_s, u"Play the tasks' own solutions (no model)."_s);
    const QCommandLineOption modelOpt(u"model"_s,
                                      u"The model's file (GGUF): opened on this device, or the vocabulary of the model "
                                      "the --server runs."_s,
                                      u"file"_s);
    const QCommandLineOption serverOpt(
            u"server"_s,
            u"A llama-server running the model of --model (with --special, a context of 16384 tokens per slot)."_s,
            u"url"_s);
    const QCommandLineOption modelIdOpt(u"model-id"_s,
                                        u"The app's model whose chat format and sampling it has (default: lfm3-1.7b)."_s,
                                        u"id"_s, u"lfm3-1.7b"_s);
    const QCommandLineOption nameOpt(u"name"_s, u"The model's name in the results (default: the file's name)."_s,
                                     u"name"_s);
    const QCommandLineOption contextOpt(u"context"_s, u"Tokens the model holds at once (default: 16384, as the app)."_s,
                                        u"tokens"_s, u"16384"_s);
    const QCommandLineOption threadsOpt(u"threads"_s, u"Threads (default: as the app chooses)."_s, u"n"_s, u"0"_s);
    const QCommandLineOption attemptsOpt(u"attempts"_s, u"Attempts at each task (default: 1)."_s, u"n"_s, u"1"_s);
    const QCommandLineOption seedOpt(u"seed"_s,
                                     u"The first attempt's seed, the next one's that plus one… (default: the app's)."_s,
                                     u"n"_s);
    const QCommandLineOption temperatureOpt(u"temperature"_s, u"The sampling's temperature (default: the app's)."_s,
                                            u"t"_s);
    const QCommandLineOption jobsOpt(u"jobs"_s, u"Attempts run at once (--server, --solution; default: 1)."_s, u"n"_s,
                                     u"1"_s);
#ifdef BRULION_LOCAL_AI_OUTPUT_DIR
    const QString pluginDefault = QStringLiteral(BRULION_LOCAL_AI_OUTPUT_DIR);
#else
    const QString pluginDefault;
#endif
    const QCommandLineOption pluginOpt(u"plugin-dir"_s, u"Where the model runner's plugin is."_s, u"dir"_s,
                                       pluginDefault);
    const QCommandLineOption outOpt(u"out"_s, u"Write each attempt's result as a line of JSON to this file."_s,
                                    u"file"_s);
    const QCommandLineOption tracesOpt(
            u"traces"_s, u"Write each attempt's conversation (its last request and the reply) as a line of JSON."_s,
            u"file"_s);
    const QCommandLineOption trainingOpt(
            u"training"_s,
            u"Write the passed attempts as training sequences, a line of JSON each (--server only; with "
            "--solution, the solutions as the model's turns)."_s,
            u"file"_s);
    const QCommandLineOption effortOpt(
            u"effort"_s,
            u"The requests' effort instead of the assistant's (\"low\": the model does not think first)."_s,
            u"effort"_s);
    const QCommandLineOption correctOpt(
            u"correct"_s,
            u"With --solution and --training: the model takes the first N steps itself, the solution goes on from "
            "there; only the solution's steps are learnt."_s,
            u"n"_s);
    const QCommandLineOption guidedOpt(
            u"guided"_s,
            u"With --solution and --training: the solutions' tool calls, the model's own answers (sampled as "
            "--temperature, --seed say)."_s);
    const QCommandLineOption verboseOpt(u"verbose"_s, u"Show the answer and the tools of every attempt."_s);
    const QCommandLineOption assistantOpt(
            u"assistant"_s, u"The assistant's Luau script instead of the built-in one (another asystent.luau)."_s,
            u"file"_s);
    parser.addOptions({tasksOpt, notebooksOpt, filterOpt, solutionOpt, modelOpt, serverOpt, modelIdOpt, nameOpt,
                       contextOpt, threadsOpt, attemptsOpt, seedOpt, temperatureOpt, jobsOpt, pluginOpt, outOpt,
                       tracesOpt, trainingOpt, effortOpt, correctOpt, guidedOpt, verboseOpt, assistantOpt});
    parser.process(app);

    QString assistant;
    if (parser.isSet(assistantOpt)) {
        QFile script(parser.value(assistantOpt));
        if (!script.open(QIODevice::ReadOnly)) {
            err() << "--assistant: cannot read " << script.fileName() << Qt::endl;
            return 2;
        }
        assistant = QString::fromUtf8(script.readAll());
    }

    QString error;
    const std::optional<QList<assistantbench::Task>> all = assistantbench::readTasks(parser.value(tasksOpt), &error);
    if (!all) {
        err() << error << Qt::endl;
        return 2;
    }
    QList<assistantbench::Task> tasks;
    const QRegularExpression filter(parser.value(filterOpt));
    if (!filter.isValid()) {
        err() << "--filter: " << filter.errorString() << Qt::endl;
        return 2;
    }
    for (const assistantbench::Task &t : *all) {
        if (filter.match(t.id).hasMatch())
            tasks << t;
    }

    const int attempts = parser.value(attemptsOpt).toInt();
    const int jobs = parser.value(jobsOpt).toInt();
    if (attempts < 1 || jobs < 1) {
        err() << "--attempts and --jobs: 1 or more" << Qt::endl;
        return 2;
    }
    const bool solution = parser.isSet(solutionOpt);
    const bool server = parser.isSet(serverOpt);
    if (jobs > 1 && !solution && !server) {
        err() << "--jobs: the model on this device answers one request at a time (use --server)" << Qt::endl;
        return 2;
    }
    if (parser.isSet(trainingOpt) && !server) {
        err() << "--training: the tokens come from a --server" << Qt::endl;
        return 2;
    }
    // The hand-written tasks measure the assistant and are never learnt: their folder has a file
    // MEASURING-ONLY (docs/FINETUNE.md "Training data").
    if (parser.isSet(trainingOpt)) {
        const QFileInfo given(parser.value(tasksOpt));
        const QDir folder = given.isDir() ? QDir(given.filePath()) : given.dir();
        if (folder.exists(u"MEASURING-ONLY"_s)) {
            err() << "--training: the tasks of " << folder.path()
                  << " are for measuring only (give --tasks and --notebooks of a generated set)" << Qt::endl;
            return 2;
        }
    }
    if (solution && server && !parser.isSet(trainingOpt)) {
        err() << "--solution with --server writes training data: give --training" << Qt::endl;
        return 2;
    }
    const bool guided = parser.isSet(guidedOpt);
    const int corrected = parser.value(correctOpt).toInt();
    if ((guided || corrected > 0) && !(solution && server)) {
        err() << "--guided, --correct: with --solution, --server and --training" << Qt::endl;
        return 2;
    }

    // The model: its format and sampling as the app has them, the temperature and seed as asked.
    ai::LocalModel known;
    std::unique_ptr<ai::LocalRunner> runner;
    std::shared_ptr<const assistantbench::Vocabulary> vocabulary;
    QString model = u"solution"_s;
    if (!solution || server) {
        if (!parser.isSet(modelOpt)) {
            err() << (solution ? "--solution with --server: give --model <file.gguf>, the vocabulary of the model the "
                                 "server runs."
                               : "Give --model <file.gguf> (with or without --server), or --solution to play the "
                                 "tasks' own solutions.")
                  << Qt::endl;
            return 2;
        }
        const ai::LocalModel *ofApp = ai::localModel(parser.value(modelIdOpt));
        if (!ofApp) {
            err() << "--model-id: no such model of the app: " << parser.value(modelIdOpt) << Qt::endl;
            return 2;
        }
        known = *ofApp;
        if (parser.isSet(temperatureOpt))
            known.temperature = parser.value(temperatureOpt).toFloat();
        if (!solution)
            model = parser.isSet(nameOpt) ? parser.value(nameOpt) : QFileInfo(parser.value(modelOpt)).completeBaseName();
        else if (corrected > 0)
            model = u"corrected"_s;
        else if (guided)
            model = u"guided"_s;
        if (server) {
            std::optional<assistantbench::Vocabulary> read = assistantbench::readVocabulary(parser.value(modelOpt), &error);
            if (!read) {
                err() << "--model: " << error << Qt::endl;
                return 2;
            }
            vocabulary = std::make_shared<const assistantbench::Vocabulary>(std::move(*read));
            if (known.format != u"lfm3" || !vocabulary->controls.contains(u"<|im_end|>"_s)) {
                err() << "--server: the training data knows the end of a turn of the format lfm3 only" << Qt::endl;
                return 2;
            }
            err() << model << ": " << parser.value(contextOpt) << " tokens at once, llama-server at "
                  << parser.value(serverOpt) << ", " << jobs << " at once" << Qt::endl;
        } else {
            ai::LocalRunner::Options options;
            options.modelPath = parser.value(modelOpt);
            options.contextTokens = parser.value(contextOpt).toInt();
            options.threads = parser.value(threadsOpt).toInt();
            runner = ai::loadLocalRunner({parser.value(pluginOpt)}, options, &error);
            if (!runner) {
                err() << "The model does not open: " << error << Qt::endl;
                return 2;
            }
            err() << model << ": " << runner->contextTokens() << " tokens at once, " << runner->device() << Qt::endl;
        }
    }
    const quint32 firstSeed = parser.isSet(seedOpt) ? parser.value(seedOpt).toUInt() : known.seed;

    QFile results(parser.value(outOpt));
    QFile traces(parser.value(tracesOpt));
    QFile training(parser.value(trainingOpt));
    const std::pair<const QCommandLineOption *, QFile *> files[] = {
            {&outOpt, &results}, {&tracesOpt, &traces}, {&trainingOpt, &training}};
    for (const auto &[option, file] : files) {
        if (parser.isSet(*option) && !file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            err() << "--" << option->names().constFirst() << ": cannot write " << file->fileName() << Qt::endl;
            return 2;
        }
    }

    QList<Work> work;
    for (qsizetype t = 0; t < tasks.size(); ++t) {
        for (int a = 0; a < attempts; ++a)
            work.append(Work{t, a, firstSeed + quint32(a)});
    }

    const QString notebooks = parser.value(notebooksOpt);
    const QString effort = parser.value(effortOpt);
    const QUrl serverUrl(parser.value(serverOpt));
    const int contextTokens = parser.value(contextOpt).toInt();
    // One attempt, on the thread that runs it: a responder of its own (on a server, a connection of
    // its own).
    const auto attempt = [&](const Work &w) {
        Done done;
        const assistantbench::Task &task = tasks.at(w.task);
        QTemporaryDir dir;
        QString taught; // the teacher's failure
        if (solution && server) {
            assistantbench::ServerRunner serverRunner(serverUrl, vocabulary, contextTokens);
            ai::LocalModel sampled = known;
            sampled.seed = w.seed;
            ModelResponder answers(&serverRunner, sampled, &serverRunner, effort);
            ModelResponder student(&serverRunner, sampled, &serverRunner, effort);
            TeacherResponder responder(&serverRunner, known, guided ? &answers : nullptr, effort,
                                       corrected > 0 ? &student : nullptr, corrected);
            done.outcome = assistantbench::run(task, notebooks, dir.path(), responder, assistant);
            done.turns = responder.turns();
            taught = responder.error();
        } else if (solution) {
            assistantbench::SolutionResponder responder;
            done.outcome = assistantbench::run(task, notebooks, dir.path(), responder, assistant);
        } else {
            ai::LocalModel sampled = known;
            sampled.seed = w.seed;
            if (server) {
                assistantbench::ServerRunner serverRunner(serverUrl, vocabulary, contextTokens);
                ModelResponder responder(&serverRunner, sampled, &serverRunner, effort);
                done.outcome = assistantbench::run(task, notebooks, dir.path(), responder, assistant);
                done.turns = responder.turns();
            } else {
                ModelResponder responder(runner.get(), sampled, nullptr, effort);
                done.outcome = assistantbench::run(task, notebooks, dir.path(), responder, assistant);
            }
        }
        done.failures = assistantbench::check(task, done.outcome);
        // The answer states only what the run showed — the solution's, or with --guided the model's
        // own: an attempt that makes a number up is no training data.
        if (solution)
            done.failures << assistantbench::checkGrounding(task, done.outcome);
        else
            done.ungrounded = assistantbench::checkGrounding(task, done.outcome);
        if (!taught.isEmpty())
            done.failures.prepend(u"taught: "_s + taught);
        done.finished = true;
        return done;
    };

    QList<Done> finished(work.size());
    QMutex mutex;
    QWaitCondition ready;
    QThreadPool pool;
    pool.setMaxThreadCount(jobs);
    if (jobs > 1) {
        for (qsizetype i = 0; i < work.size(); ++i) {
            pool.start([&, i] {
                Done done = attempt(work.at(i));
                const QMutexLocker lock(&mutex);
                finished[i] = std::move(done);
                ready.wakeAll();
            });
        }
    }

    Tally total;
    QMap<QString, Tally> byCategory;
    QMap<QString, Tally> byLanguage;
    QHash<QString, QSet<QByteArray>> learnt; // per task: the sequences written (no two the same)
    int sequences = 0;
    int ungrounded = 0;       // a model's answers with a fact the run never showed (not counted)
    int passedUngrounded = 0; // of them, passed
    const int endToken = vocabulary ? vocabulary->controls.value(u"<|im_end|>"_s) : -1;
    const double temperature = solution && !guided && corrected == 0 ? 0.0 : double(known.temperature);
    for (qsizetype i = 0; i < work.size(); ++i) {
        // The attempts in order, whichever ends first.
        Done done;
        if (jobs > 1) {
            QMutexLocker lock(&mutex);
            while (!finished.at(i).finished)
                ready.wait(&mutex);
            done = std::move(finished[i]);
            finished[i] = Done{};
        } else {
            done = attempt(work.at(i));
        }
        const Work &w = work.at(i);
        const assistantbench::Task &task = tasks.at(w.task);
        const assistantbench::Outcome &outcome = done.outcome;
        const bool ok = done.failures.isEmpty();
        total.add(task.id, outcome, ok);
        byCategory[task.category].add(task.id, outcome, ok);
        byLanguage[task.language].add(task.id, outcome, ok);

        out() << (ok ? "PASS " : "FAIL ") << task.id;
        if (attempts > 1)
            out() << " #" << w.attempt + 1;
        out() << "  (" << outcome.rounds.size() << " requests, " << outcome.toolsCalled.join(u' ')
              << (outcome.toolsCalled.isEmpty() ? "" : ", ") << QString::number(outcome.ms / 1000.0, 'f', 1) << " s)"
              << Qt::endl;
        for (const QString &f : std::as_const(done.failures))
            out() << "     " << f << Qt::endl;
        for (const QString &f : std::as_const(done.ungrounded))
            out() << "     (not counted) " << f << Qt::endl;
        if (!done.ungrounded.isEmpty()) {
            ++ungrounded;
            passedUngrounded += ok ? 1 : 0;
        }
        if (parser.isSet(verboseOpt) || !ok) {
            out() << "     said: " << QString(outcome.said).replace(u'\n', u"⏎"_s).left(400) << Qt::endl;
            for (const QString &failed : outcome.toolFailures)
                out() << "     failed call: " << QString(failed).replace(u'\n', u"⏎"_s).left(200) << Qt::endl;
        }
        if (results.isOpen())
            results.write(QJsonDocument(resultJson(task, w, done, model, temperature)).toJson(QJsonDocument::Compact)
                          + '\n');
        if (traces.isOpen() && !outcome.lastRequest.isEmpty()) {
            const QJsonObject trace{{u"id"_s, task.id},
                                    {u"model"_s, model},
                                    {u"attempt"_s, w.attempt},
                                    {u"seed"_s, qint64(w.seed)},
                                    {u"passed"_s, ok},
                                    {u"request"_s, QJsonObject::fromVariantMap(outcome.lastRequest)},
                                    {u"reply"_s, QJsonDocument::fromJson(outcome.lastRaw).object()}};
            traces.write(QJsonDocument(trace).toJson(QJsonDocument::Compact) + '\n');
        }
        // (Not an attempt the assistant stepped in: its turns hold the repeated calls it cut short,
        // an answer it did not take.)
        if (training.isOpen() && ok && !outcome.assisted) {
            const QList<QJsonArray> learnable = assistantbench::trainingSequences(done.turns, u"<|im_end|>"_s, endToken);
            for (qsizetype s = 0; s < learnable.size(); ++s) {
                const QByteArray segments = QJsonDocument(learnable.at(s)).toJson(QJsonDocument::Compact);
                const QByteArray hash = QCryptographicHash::hash(segments, QCryptographicHash::Sha256);
                if (learnt[task.id].contains(hash))
                    continue; // another attempt wrote the same
                learnt[task.id].insert(hash);
                const QJsonObject line{{u"id"_s, task.id},
                                       {u"category"_s, task.category},
                                       {u"language"_s, task.language},
                                       {u"model"_s, model},
                                       {u"attempt"_s, w.attempt},
                                       {u"seed"_s, qint64(w.seed)},
                                       {u"temperature"_s, temperature},
                                       {u"sequence"_s, s},
                                       {u"segments"_s, learnable.at(s)}};
                training.write(QJsonDocument(line).toJson(QJsonDocument::Compact) + '\n');
                ++sequences;
            }
        }
    }
    pool.waitForDone();

    auto line = [attempts](const QString &name, const Tally &t) {
        out() << "  " << name.leftJustified(10) << " " << QString::number(t.passed).rightJustified(4) << "/"
              << QString::number(t.runs).leftJustified(4) << " " << percent(t.passed, t.runs).rightJustified(4);
        if (attempts > 1)
            out() << "   tasks done at least once " << t.doneTasks.size() << "/" << t.tasks.size() << " "
                  << percent(t.doneTasks.size(), t.tasks.size());
        out() << Qt::endl;
    };
    out() << Qt::endl << model << ": " << total.passed << " of " << total.runs
          << (attempts > 1 ? " attempts passed (" : " tasks done (") << percent(total.passed, total.runs) << ")";
    if (attempts > 1)
        out() << ", " << total.doneTasks.size() << " of " << total.tasks.size() << " tasks done at least once ("
              << percent(total.doneTasks.size(), total.tasks.size()) << ")";
    out() << Qt::endl;
    for (auto it = byLanguage.cbegin(); it != byLanguage.cend(); ++it)
        line(it.key(), it.value());
    for (auto it = byCategory.cbegin(); it != byCategory.cend(); ++it)
        line(it.key(), it.value());
    if (total.runs > 0) {
        out() << "  requests per attempt " << QString::number(double(total.rounds) / total.runs, 'f', 1)
              << ", tool calls " << total.calls << ", failed calls " << total.toolErrors << " (unknown tools "
              << total.unknownTools << "), failed replies " << total.failedReplies << Qt::endl;
        out() << "  context at the end " << total.lastPromptTokens / total.runs << " tokens on average, written "
              << total.outputTokens << " tokens, " << QString::number(total.ms / 1000.0, 'f', 0)
              << " s of attempts" << Qt::endl;
        if (!solution)
            out() << "  answers stating a fact the run never showed " << ungrounded << " (" << passedUngrounded
                  << " of them passed; not counted)" << Qt::endl;
    }
    if (training.isOpen())
        out() << "  training: " << sequences << " sequences to " << training.fileName() << Qt::endl;
    return total.passed == total.runs ? 0 : 1;
}
