#pragma once
// The AI assistant's bench (docs/FINETUNE.md): the built-in assistant
// (src/core/extensions/builtin/asystent.luau) at work on tasks whose outcome is known — a notebook
// of its own (finetune/notebooks/<name>/), the user's messages, what the notes and the answer must
// be afterwards (finetune/tasks/<name>.json). A task runs on a copy of its notebook, with the
// notebook's index and the extensions' host as in the app; every b.ai request of the assistant
// goes to a Responder — a model, or the task's own solution played back — and every change it
// asks for is approved (or declined, as the task says). check() then compares the notes and the
// answer with the task's expectations.
//
//   QString error;
//   const QList<assistantbench::Task> tasks = assistantbench::readTasks(u"finetune/tasks"_s, &error);
//   assistantbench::SolutionResponder solution;
//   const assistantbench::Outcome outcome = assistantbench::run(tasks[0], u"finetune/notebooks"_s, workDir, solution);
//   const QStringList failures = assistantbench::check(tasks[0], outcome);   // empty: the task is done
//
// Pure of the app: core modules only (extensions, ai, index, vault). The model on the device is
// brulion-assistantbench's (main.cpp), loaded as the app loads it.

#include "ai/ai.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace brulion::assistantbench {

/// What a text must hold. Words are compared folded (text::foldKey: letter case and diacritics
/// ignored — "Żółw" holds "zolw"); `matches` and `notMatches` are regular expressions over the
/// text as it is (case-insensitive; ^ and $ at every line). Either way every kind of space is a
/// space and every kind of hyphen or dash (U+2010–U+2015, U+2212) a hyphen: "2026‑12‑31" with
/// non-breaking hyphens is "2026-12-31".
struct TextCheck {
    QStringList contains;     ///< every one of them
    QList<QStringList> anyOf; ///< of each group, at least one
    QStringList notContains;  ///< none of them
    QStringList matches;      ///< every one matches somewhere
    QStringList notMatches;   ///< none matches anywhere (a word allowed in one sense only: "031 is paid")

    bool isEmpty() const
    {
        return contains.isEmpty() && anyOf.isEmpty() && notContains.isEmpty() && matches.isEmpty() && notMatches.isEmpty();
    }
};

/// A note the task leaves in a known state.
struct FileCheck {
    /// Its path in the notebook ("Zakupy.md", "journals/2026-11-16.md"), or the paths it may have
    /// — one of them must exist and pass (a reminder for tomorrow may go to today's journal or
    /// to tomorrow's).
    QStringList paths;
    TextCheck text; ///< of its text after the task
};

/// One turn of the model in a task's solution: tools called, or the answer.
struct Step {
    QString thoughts;          ///< what it thinks first ("": nothing written down)
    QString answer;            ///< its answer (a turn that calls no tool)
    QList<ai::ToolCall> calls; ///< the tools it calls (ids are made as it is played)
    /// Calls made only when no tool result before shows this text (compared folded): a page read
    /// for a phone number the search may already have shown with the page ("": always made).
    QString unlessShown;
    /// Other calls as right as `calls` at this point — a retry after a search that found nothing:
    /// another word, or the list of pages. Not played (the solution goes on from `calls`); for a
    /// reward that takes any of them (finetune/grpo.py).
    QList<QList<ai::ToolCall>> alternatives;
};

struct Task {
    QString id;       ///< unique: "dom-pl/zakupy-mleko"
    QString notebook; ///< the folder under the notebooks' folder
    QString language; ///< "pl" | "en": the notebook's language (b.lang) and the app's (b.uiLang)
    QString category; ///< "search", "query", "append", "journal", "update", "write", "no_tool", "decline"…
    QDateTime now;    ///< the clock of the run: local wall-clock time
    QString page;     ///< the page the conversation is about (ctx.page; "": none)
    QVariantList messages; ///< {role, content}: the conversation, the user's message last
    /// The answers to the changes the assistant asks to make, in order: "yes", "all" (yes to every
    /// change of the question) or "no". Past the end of the list: "yes".
    QStringList answers;

    QList<FileCheck> files; ///< the notes it must leave so
    /// No other note may change — none created, changed or removed — than those of `files`.
    bool onlyListedChanges = true;
    TextCheck answer;           ///< the assistant's answer (the model's words, not the app's notes under them)
    QStringList toolsCalled;    ///< each called at least once
    QStringList toolsNotCalled; ///< none of them called
    bool noTools = false;       ///< no tool called at all

    QList<Step> solution; ///< one way of doing it, played by SolutionResponder
};

/// Reads the tasks of a file (a JSON object: the notebook, the language and the clock all its tasks
/// share, and `tasks`), or of every *.json file of a folder, sorted by name. nullopt with `error`
/// (the file, the task and what is wrong) when one is malformed; task ids must be unique.
std::optional<QList<Task>> readTasks(const QString &fileOrFolder, QString *error);

/// One request of the assistant and its answer.
struct Round {
    QStringList calls;  ///< the tools the reply called, by name
    QString stopReason; ///< the reply's
    QString error;      ///< the reply was an error
    int promptTokens = 0;
    int reusedTokens = 0; ///< of the prompt: in the model's memory from the requests before
    int outputTokens = 0;
    qint64 promptMs = 0;
    qint64 outputMs = 0;
};

/// What answers the assistant's requests.
class Responder {
public:
    virtual ~Responder() = default;
    /// A task starts (its first request follows).
    virtual void begin(const Task &task) { (void)task; }
    /// The answer to `request` (b.ai's, as ai::requestFrom made it); `round` gets the counts and
    /// times there are (a model's).
    virtual ai::Reply answer(const ai::Request &request, Round *round) = 0;
    /// What b.aiModel() tells the assistant of the model that answers ({}: nil — the tools and
    /// instructions of a provider on the network, as before b.aiModel(): what every run that writes
    /// training data gets).
    virtual QVariantMap aiModel() const { return {}; }
};

/// Plays a task's solution: one step for each request, in order (a step `unlessShown` a text a tool
/// result already shows passed); a request past the last step is answered with an error. A call's
/// argument "{{ref Dom.md#L3}}" becomes the block's ref as a tool result of the conversation gave
/// it ("Dom.md#L3@1a2b3c4d").
class SolutionResponder : public Responder {
public:
    void begin(const Task &task) override;
    ai::Reply answer(const ai::Request &request, Round *round) override;
    /// The steps passed so far (unlessShown).
    int skipped() const { return m_skipped; }

private:
    QList<Step> m_steps;
    qsizetype m_next = 0;
    int m_skipped = 0;
};

/// A reply as the model on the device writes it (ai/local.h: an OpenAI-style assistant message as
/// its raw) of a step, its calls' ids unique in the conversation of `request`.
ai::Reply replyOf(const Step &step, const ai::Request &request);

/// What a task's run came to.
struct Outcome {
    QString said;    ///< the assistant's answer: the model's last words ("": none)
    QString message; ///< the command's value: the message the chat shows (the answer and the notes under it)
    QString error;   ///< the run failed: a script error, a question nobody answers ("" none)
    QList<Round> rounds;
    QStringList toolsCalled; ///< in order, repeated
    int toolErrors = 0;      ///< tool results that were errors (a declined change not counted)
    int unknownTools = 0;    ///< of them: a tool that does not exist
    QStringList toolFailures; ///< what those results said, in order
    int confirmations = 0;   ///< changes the assistant asked to make
    QHash<QString, QString> before; ///< the notebook's notes before (path → text)
    QHash<QString, QString> after;  ///< and after
    QVariantMap lastRequest; ///< the last b.ai request (b.ai's table), with the conversation so far
    QByteArray lastRaw;      ///< the reply to it (raw)
    qint64 ms = 0;           ///< the whole run
    /// The assistant stepped in: it ended the model's loop itself (`said` is then its message, not
    /// the model's words) or asked for an answer again (a request that does not go on from the one
    /// before) — the model's turns are no training data of what to do.
    bool assisted = false;

    /// The notes created, changed or removed (sorted).
    QStringList changed() const;
};

/// Runs `task` on a copy of `notebooksDir/<task.notebook>` made in `workDir` (an empty folder), by
/// the built-in assistant — or the assistant of the Luau source `assistant` (another version of
/// asystent.luau, to measure it).
Outcome run(const Task &task, const QString &notebooksDir, const QString &workDir, Responder &responder,
            const QString &assistant = QString());

/// The ways `outcome` misses the task's expectations, one line each; empty: the task is done.
/// Whatever the task, the answer must also be true to what the run changed: no change claimed when no
/// note changed ("Zapisałem w dzienniku…" for a draft), the run's own change never said to have been
/// there already without saying it is made ("was already marked done — nothing left to check off"),
/// no fact that only the system text holds (its example date) given as the notebook's.
QStringList check(const Task &task, const Outcome &outcome);

/// The ways `text` misses `check`, one line each, every line starting with `what`.
QStringList checkText(const TextCheck &check, const QString &text, const QString &what);

/// The facts of `answer` — numbers of three digits or more (a phone number, an amount, a code, a
/// year), dates, times ("09:00", "4 pm") — that `seen` does not hold, one line each. A fact counts
/// however `seen` writes it: spaces and hyphens of any kind, its digits grouped otherwise or in
/// part of a longer number ("4500" in "4 500 EUR", "5557 1203" in "+49 30 5557 1203"), a time with
/// or without its leading zero or on the other clock ("16:00" for "4 pm"), a full hour also as a
/// bare hour of `seen` ("14:00" for "między 12 a 14", "12 pm" for "between 12 and 2 pm"). A number
/// right after "=" that the arithmetic just before it gives ("8 × 60 = 480") is the answer's own
/// work. `workedOut`: checks of the values `answer` works out rather than reads (a sum, an amount
/// for more people) — a fact one of them asks for is no fact to find in `seen`.
QStringList ungroundedFacts(const QString &answer, const QString &seen, const TextCheck &workedOut = {});

/// What the model had read when it gave the last answer of `outcome`: the system text, the
/// conversation's messages (the open page), the tools' results and its own calls before — not its
/// thoughts. Of the system text its words only, not its digits: it is the app's, the same in every
/// conversation, and its date (the journal "named by its date (2026-11-12)") is an example, no fact.
/// Of the day the app writes first (its first message's first paragraph: today, and a calendar of
/// the days to come), only its first date, today: a day the answer works out from "pojutrze" is
/// grounded by the call that wrote it, not by the calendar.
QString seenBefore(const Outcome &outcome);

/// A solution's answer is grounded: it states no fact the run never showed the model (a phone
/// number of a line the search did not show — a model trained on it learns to make numbers up).
/// ungroundedFacts() of the run's answer and seenBefore(); a task that works its answer out
/// (`multi`, `no_tool`) may state the values its answer checks expect. Empty: grounded.
QStringList checkGrounding(const Task &task, const Outcome &outcome);

} // namespace brulion::assistantbench
