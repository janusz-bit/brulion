// What a brulion:// address does in a notebook (docs/UI.md "Automation"): plan() resolves a
// Request against the notebook — the page a name, path, alias or date names, the daily note, the
// template, the task line — into a Plan without writing anything; execute() carries out a Plan's
// write through a Writer: the app's Workspace (the open notebook) or a headless notebook::Notebook
// (brulion-cli uri run, Android's background run, a notebook other than the open one). Part of the
// automation module — see automation/automation.h.
//
// Error codes (Plan::error, Outcome::error; docs/UI.md "Automation" lists them for callers):
//   unknown_parameter, missing_parameter, missing_text, missing_title, page_not_found,
//   not_a_page, heading_not_found, block_not_found, exists, template_not_found,
//   date_not_understood, recurrence_not_understood, bad_priority, bad_args, unknown_view,
//   not_utf8, conflict, failed — and from elsewhere: the address's own (uri.h), the policy's
//   (policy.h), unknown_notebook / ambiguous_notebook (matchNotebook()), no_notebook,
//   not_approved (an extension's command), cancelled (the user's "Cancel"), busy.
#pragma once

#include "automation/edits.h"
#include "automation/policy.h"
#include "automation/uri.h"
#include "notebook/session.h"
#include "types.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace brulion {
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::automation {

/// The clock and the language dates are read in.
struct Context {
    QDateTime now;
    dates::UiLocale locale = dates::UiLocale::English;
};

/// A request worked out for one notebook.
struct Plan {
    Action action = Action::Open;
    Access access = Access::Open;
    QString error;  ///< "" when it can be carried out
    QString detail; ///< what the error is about (a parameter, a name), English

    /// What to show (Open; after a write with `open`): {view: "page", path, heading?, blockId?,
    /// line?} | {view: "journal", date} | {view: "search", query} | {view: <a view>, …} |
    /// {view: "capture"} (the quick capture dialog).
    QJsonObject route;

    // ---- Write ------------------------------------------------------------------------------
    VaultPath path;       ///< the note written
    bool exists = false;  ///< it is there now (else it is created, from `initialText`)
    QString initialText;  ///< a missing note's first text: a daily note's template, a new page's text
    bool newPage = false; ///< action new: a page that must not exist yet
    bool journal = false; ///< `path` is a daily note (created from the journal template when missing)
    QDate day;            ///< its day
    InsertSpec insert;    ///< the block added (new: none — the page is `initialText`)
    QString kind;         ///< what is written: "note", "task", "reminder", "alarm", "due", "page"
    DateSpec reminder;    ///< a reminder the block has (invalid: none)
    bool open = false;    ///< show the note after writing (`open=yes`; new pages by default)

    // ---- Run --------------------------------------------------------------------------------
    QString extension;  ///< the extension's id
    QString command;    ///< its command
    QJsonObject args;   ///< ctx.args
    VaultPath page;     ///< ctx's page (`page`), "" none

    bool ok() const { return error.isEmpty(); }
};

/// Work out `request` in the notebook of `vault` and `index` (complete) at `context`. Never
/// writes.
Plan plan(const Request &request, Vault &vault, const VaultIndex &index, const Context &context);

/// The result of a request, as callers get it (brulion-cli's JSON, Android's result broadcast,
/// the x-callback parameters).
struct Outcome {
    bool ok = false;
    QString action;  ///< actionName()
    QString error;   ///< a code ("" when ok)
    QString message; ///< English, for logs and callers
    VaultPath path;  ///< the note opened or written
    int line = -1;   ///< the written block's line (0-based)
    bool created = false;
    QString block;      ///< what was written
    QString kind;       ///< Plan::kind
    DateSpec reminder;  ///< a reminder written (Android arms it at once)
    bool alarm = false;
    QJsonObject route;  ///< what the app shows
    QString id;         ///< the request's id

    /// {ok, action, error?, message?, path?, line? (1-based), created?, block?, kind?, reminder?
    /// ("YYYY-MM-DD HH:mm"), alarm?, route?, id?}
    QJsonObject toJson() const;
    /// The x-success / x-error parameters: path, line (1-based), id — errorCode and errorMessage
    /// for an error.
    QList<std::pair<QString, QString>> callbackParams() const;
};

/// An outcome that is only an error.
Outcome failure(const QString &error, const QString &message, Action action = Action::Open);

/// What execute() writes through.
class Writer {
public:
    virtual ~Writer() = default;
    /// Apply `edit` to `path` as notebook::Notebook::edit() does (compare-and-swap, History).
    virtual notebook::Notebook::WriteResult edit(const VaultPath &path, const notebook::Notebook::Edit &edit,
                                                 const notebook::Notebook::WriteOptions &options) = 0;
};

/// A headless notebook as the Writer (brulion-cli, Android's background run).
class NotebookWriter final : public Writer {
public:
    explicit NotebookWriter(notebook::Notebook &notebook) : m_notebook(notebook) {}
    notebook::Notebook::WriteResult edit(const VaultPath &path, const notebook::Notebook::Edit &edit,
                                         const notebook::Notebook::WriteOptions &options) override
    {
        return m_notebook.edit(path, edit, options);
    }

private:
    notebook::Notebook &m_notebook;
};

/// Carry out `plan`'s write (Access::Write); an Open plan's outcome is its route, a Run plan's
/// an error (the app runs commands). A failed plan is its error.
Outcome execute(const Plan &plan, Writer &writer, const QString &requestId = {});

/// The text `plan` would leave in its note, given the note's `current` text (`exists` false: a
/// missing note) — what execute() writes, for a preview; nullopt and `error` when it cannot.
std::optional<QString> planned(const Plan &plan, const QString &current, bool exists, QString *error = nullptr,
                               int *line = nullptr);

/// A notebook the device knows (the open one, the recent ones).
struct KnownNotebook {
    QString root; ///< absolute folder
    QString name; ///< what it is called (its folder's name unless it has another)
};

struct NotebookMatch {
    int index = -1;    ///< into the list; -1 with `error`
    QString error;     ///< "unknown_notebook", "ambiguous_notebook"
    QStringList names; ///< the candidates of an ambiguous name
};

/// The notebook `wanted` names among `known`: an absolute folder (as given, then ignoring letter
/// case), else a name — the notebook's name or its folder's, case and diacritics aside.
NotebookMatch matchNotebook(const QString &wanted, const QList<KnownNotebook> &known);

/// The parameters each action reads (beside the common ones: uri.h), for the unknown_parameter
/// check and the documentation.
QStringList knownParameters(Action action);

} // namespace brulion::automation
