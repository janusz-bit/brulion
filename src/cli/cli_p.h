// Internals of brulion-cli (src/cli): the command registry, the call context, results and the
// helpers the commands share. One registry describes every command once; the argv parser, the
// generated help, `brulion-cli schema` (JSON Schema) and the MCP server's tools/list come from it.
#pragma once

#include "dates/names.h"
#include "notebook/itemref.h"
#include "notebook/session.h"
#include "types.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

class QLockFile;

namespace brulion {
class VaultIndex;
}

namespace brulion::cli {

/// Exit codes (docs/CLI.md "Exit codes"); stable.
enum Exit : int {
    ExitOk = 0,
    ExitInternal = 1,    ///< a bug or an I/O error
    ExitUsage = 2,       ///< unknown command or option, missing or invalid argument, path outside the notebook
    ExitNotFound = 3,    ///< page, block, line or history version not found
    ExitConflict = 4,    ///< stale ref, hash mismatch, ambiguous name, target exists, file keeps changing
    ExitUnavailable = 5, ///< no notebook, not a folder, storage gone, lock timeout, no running app
    ExitParse = 6,       ///< a date phrase, query or calculation not understood
    ExitRefused = 7,     ///< refused by policy: --read-only, destructive without --allow-destructive
};

/// Human messages are English (translatable later): QCoreApplication::translate("brulion::cli").
QString tr(const char *text);

struct Error {
    Error() = default;
    Error(int exitCode, QString errorCode, QString text, QJsonObject more = {})
        : exit(exitCode), code(std::move(errorCode)), message(std::move(text)), details(std::move(more))
    { }

    int exit = ExitInternal;
    QString code;    ///< stable, snake_case ("not_found", "stale_ref", …)
    QString message; ///< for people
    QJsonObject details;
};

struct Result {
    QJsonValue value;           ///< the envelope's "result"
    QJsonArray warnings;        ///< [{code, message, details?}]
    std::optional<Error> error; ///< failed (the envelope's "error")
    QString text;               ///< --format text; "" = rendered from `value`
    bool raw = false;           ///< print `text` as it is, whatever the format (completion scripts)
};

enum class ArgType {
    String, ///< a word or a name
    Text,   ///< note text (may come from --stdin)
    Int,
    Bool, ///< a flag
    Enum, ///< one of `values`
    Ref,  ///< a page or item reference (docs/CLI.md "References")
    Date, ///< a date phrase ("jutro", "w piątek 18:00", "2026-10-15")
    Path, ///< a vault path (folders)
};

struct Arg {
    QString name; ///< the JSON key (camelCase); on the command line --kebab-case
    ArgType type = ArgType::String;
    QString help;
    bool required = false;
    bool positional = false;
    bool rest = false;      ///< takes the remaining positional words, joined by spaces
    bool fromStdin = false; ///< `--stdin` provides it
    QStringList values;     ///< Enum
};

class Context;
class Call;
using Handler = std::function<Result(Call &)>;

struct Command {
    QString id;      ///< "page.read" — `page read` on the command line, brulion_page_read in MCP
    QString summary; ///< one line
    QString help;    ///< more, for `help <command>` and the MCP tool description
    QList<Arg> args;
    bool writes = false;      ///< changes notes (refused with --read-only)
    bool destructive = false; ///< needs --allow-destructive
    bool needsVault = true;
    bool mcp = true; ///< offered as an MCP tool
    QString aliasOf; ///< "task.set" for `task done`
    Handler run;

    QStringList words() const { return id.split(u'.'); }
    QString usage(const QString &program) const;
};

/// Every command, in help order.
const QList<Command> &commands();
const Command *findCommand(const QString &id);
/// The MCP tool name of a command: "brulion_" + id with '.' as '_' ("brulion_task_add").
QString toolName(const Command &command);
/// "expectHash" -> "--expect-hash".
QString optionName(const QString &argName);
/// JSON Schema (draft 2020-12) of a command's arguments; for MCP also `dryRun` on writes.
QJsonObject inputSchema(const Command &command, bool forMcp);
/// The schema document of `brulion-cli schema`: {$schema, commands: {id: {…}}}.
QJsonObject schemaDocument();

// Registry parts (registry.cpp collects them).
void addPageCommands(QList<Command> &out);
void addJournalCommands(QList<Command> &out);
void addTaskCommands(QList<Command> &out);
void addIndexCommands(QList<Command> &out);
void addToolCommands(QList<Command> &out);
void addMetaCommands(QList<Command> &out);

/// Options for the whole run (docs/CLI.md "Global options").
struct Globals {
    QString vault;
    QString stateDir;
    bool noHistory = false;
    QString format = QStringLiteral("json");
    bool compact = false;
    bool dryRun = false;
    bool diff = false;
    bool allowDestructive = false;
    bool readOnly = false;
    bool verbose = false;
    bool stdinText = false;
    QString now;
    QString lang;
    bool help = false;
};

/// The parsed command line.
struct Invocation {
    Globals globals;
    const Command *command = nullptr;
    QJsonObject args;
    std::optional<Error> error;
    QString helpTopic; ///< `--help` / `help` without a command: "" = everything
};

/// Parse argv (without the program name).
Invocation parseArguments(const QStringList &arguments);
/// Validate and complete JSON arguments (MCP tools/call, and the parsed command line): types,
/// required arguments, enum values; unknown keys refused. `error` on problems.
QJsonObject checkArguments(const Command &command, const QJsonObject &args, std::optional<Error> *error);

/// The state of a run (one process; the MCP server keeps it for all its calls).
class Context {
public:
    Context();
    ~Context();

    Globals globals;
    bool serving = false; ///< the MCP server: the notebook stays open and watched
    QString program = QStringLiteral("brulion-cli");

    QDateTime now() const;
    QDate today() const { return now().date(); }
    /// The language of note content: --lang, else the app's setting ui/language, else (never
    /// chosen) the system's, as the app (notebook::appLanguage()).
    QString language() const;
    dates::UiLocale locale() const;
    Qt::DayOfWeek weekStart() const;

    /// The app's settings, read-only (never AppSettings, which writes when constructed).
    QString settingsValue(const QString &key) const;
    QString appDataDir() const;

    /// Find the notebook folder (--vault, BRULION_VAULT, the app's current notebook).
    std::optional<Error> resolveVault();
    QString vaultRoot() const { return m_root; }
    QString vaultSource() const { return m_source; }
    /// The opened notebook (opened on first use); nullptr with `error`.
    notebook::Notebook *notebook(std::optional<Error> *error);
    QString stateDir() const;

    /// Standard input as text, read once. Invalid UTF-8 is replaced (stdinInvalidUtf8).
    QString readStdin();
    bool stdinInvalidUtf8 = false;
    /// Tests and the MCP server: a fixed text instead of the real standard input.
    void setStdin(const QString &text) { m_stdin = text; }
    void closeNotebook();

private:
    std::unique_ptr<notebook::Notebook> m_notebook;
    QString m_root;
    QString m_source;
    std::optional<QString> m_stdin;
};

/// One command being run: its arguments, its warnings, the helpers commands share.
class Call {
public:
    Call(Context &context, const Command &command, const QJsonObject &args);
    ~Call();

    Context &ctx;
    const Command &cmd;
    QJsonObject args;
    QJsonArray warnings;
    /// Set by a helper that failed (it returned nothing); `failed()` turns it into the result.
    std::optional<Error> error;

    // ---- arguments
    QString str(const QString &name) const;
    bool has(const QString &name) const;
    bool flag(const QString &name) const;
    int integer(const QString &name, int fallback) const;
    /// A Text argument, or standard input with --stdin; "" when neither.
    QString text(const QString &name);
    bool dryRun() const;

    // ---- results
    Result ok(const QJsonValue &value, const QString &text = QString());
    Result fail(int exit, const QString &code, const QString &message, const QJsonObject &details = {});
    Result failed();
    /// Record an error for failed() and return nullopt / nullptr from a helper.
    void setError(int exit, const QString &code, const QString &message, const QJsonObject &details = {});
    void warn(const QString &code, const QString &message, const QJsonObject &details = {});

    // ---- the notebook
    notebook::Notebook *nb();
    VaultIndex *index();
    Vault *vault();

    /// A page reference resolved (docs/CLI.md "References").
    struct PageRef {
        VaultPath path;
        bool exists = false;
        std::optional<QDate> journal;          ///< the day, for a daily note
        std::optional<notebook::ItemRef> item; ///< the ref had a line or block part
    };
    /// Resolve a page ref: a vault path, "today" or an ISO date (the daily note), a page name
    /// (like [[links]]: a title, a path suffix, an alias; several pages of that name are a
    /// conflict), another date phrase when no page has that name ("wczoraj", "w piątek"), or an
    /// item ref. With `mustExist` a missing page is not found; else the path it would get.
    std::optional<PageRef> page(const QString &ref, bool mustExist);
    /// A vault path given by the user: refused when outside the notebook, in .brulion/ or .trash/.
    std::optional<VaultPath> vaultPath(const QString &raw);
    /// A day: "" = today, an ISO date or a date phrase (Polish or English).
    std::optional<QDate> date(const QString &phrase);
    /// A date with an optional time ("jutro 9:00", "2026-10-15 18:30"); a time-only phrase is today
    /// or tomorrow. Words that are no date make a warning.
    std::optional<DateSpec> dateSpec(const QString &phrase, const QString &argName);

    // ---- writing
    /// Take the device's write lock for the rest of the call (5: lock_timeout).
    bool lock();
    /// Notebook::edit with the lock, the run's dry-run flag; failures become errors.
    std::optional<notebook::Notebook::WriteResult> edit(const VaultPath &path, const notebook::Notebook::Edit &edit,
                                                        notebook::Notebook::WriteOptions options = {});
    /// {path, changed, created, hashBefore, hashAfter, dryRun?, diff?}.
    QJsonObject writeJson(const notebook::Notebook::WriteResult &result) const;
    /// "path#L13@hash" of a line as it is in `content`.
    static QString lineRef(const VaultPath &path, const QString &content, int line);

private:
    std::unique_ptr<QLockFile> m_lock;
};

/// The command's result as the JSON envelope: {ok, command, vault, result, warnings} or
/// {ok: false, command, vault, error: {code, message, details}, warnings}.
QJsonObject envelope(const Context &context, const Command *command, const Result &result);
/// The text of --format text.
QString renderText(const Command *command, const Result &result);
/// A unified diff of two texts ("--- a/path", "+++ b/path", "@@ …"), 3 lines of context.
QString unifiedDiff(const QString &before, const QString &after, const QString &path);

/// Run one command (policy checks, the handler, warnings): the result for the envelope.
Result execute(Context &context, const Command &command, const QJsonObject &args);
/// Exit code of a result.
int exitCode(const Result &result);

/// The MCP server on standard input / output (`brulion-cli mcp`).
int serveMcp(Context &context, bool readOnly, bool allowDestructive);
/// The MCP protocol version this server speaks (initialize).
QString mcpProtocolVersion();

/// Shell completion script for bash, fish or zsh ("" for another shell).
QString completionScript(const QString &shell, const QString &program);
/// The help text (all commands, or one).
QString helpText(const QString &program, const Command *command);

} // namespace brulion::cli
