#pragma once
// WorkspaceExtensions — the extensions of the open notebook (docs/EXTENSIONS.md, QML:
// Workspace.extensions): the Luau engine (core extensions::Engine) over a NotebookHost bound to the
// Workspace's vault and index; the built-in extensions the settings switch on (the gym) and the
// extensions written in the notebook (pages tagged #rozszerzenie, pageextension.h) — each page's
// code approved per device (a hash in the vault's LocalStore) and loaded again whenever its page
// changes; their palette commands, views and hooks (b.on "save" / "change"); the question a
// command or an action waits on (b.ask / b.choose / b.form, shown by ShellExtensionQuestion.qml);
// the Luau console (evaluate(), "Run Luau block"); messages (toasts) and the rest timer
// (b.timer: a countdown in the app and a notification when it ends — armed on the platform when it
// starts, so it comes with the app closed where the platform can: Android's alarms).
//
//   extensions->runCommand(u"silownia"_s, u"start"_s);                 // the palette
//   extensions->runCommand(u"silownia"_s, u"add-set"_s, path, line);   // on the block under the caret
//   QML: Workspace.extensions.questionKind ("" | "ask" | "choose" | "form") → answerText / answerChoice /
//        setFieldValue + answerForm / cancelQuestion
//   extensions->runChat(u"AI/2026-11-15 Pytanie.md"_s, messages, u"Dom"_s);   // a turn of the AI chat
//
// The AI chat (WorkspaceChats): runChat() runs the assistant's command "chat" with a
// conversation's messages; its value arrives as chatAnswered(), an error or a missing assistant as
// chatFailed(), what it says it does (b.status) as chatStatus(). Its questions (a change to
// confirm) are the usual question, with questionChat naming the conversation, so the chat shows it
// in the conversation instead of the dialog.
//
// The Workspace tells it about the notebook: indexReady() (scan every page once), notesChanged()
// (the pages a change touched: an extension page loads again, "change" hooks), savedByEditor()
// (this device's editor saved a page: "save" hooks; an approved extension's new code stays
// approved — the code someone writes here is theirs, code that arrives by sync waits, and so
// does a save made over a text that arrived: what the editor merged in is not its own).
//
// Views (ExtensionViewModel) render through render() and act(); a view renders again after
// changed() (a command or an action finished, an extension was switched on or off).
//
// Single-threaded (the GUI thread, the Workspace's).

#include "extensionmodels.h"

#include "ai/ai.h"
#include "extensions/engine.h"
#include "extensions/events.h"
#include "extensions/host.h"
#include "extensions/macros.h"
#include "extensions/pageextension.h"
#include "extensions/typecheck.h"
#include "reminders/notifications.h"
#include "types.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace brulion {
class Vault;
class VaultIndex;
namespace extensions {
class NotebookHost;
}
namespace sync {
class NetworkHttpTransport;
}
namespace io {
struct FileEntry;
}
} // namespace brulion

namespace brulion::shell {

/// The fields of a b.form question (roles key, label, kind — "number" | "text" —, value, step, unit,
/// text — a text field's —, display — a number as shown —, placeholder — a text field's example).
class ExtensionFormModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        LabelRole,
        KindRole,
        ValueRole,
        StepRole,
        UnitRole,
        TextRole,
        DisplayRole,
        PlaceholderRole,
    };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QVariantList &fields);
    QVariantMap values() const;
    Q_INVOKABLE void setNumber(int row, double value);
    Q_INVOKABLE void setText(int row, const QString &text);
    /// A number typed in ("82,5", "82.5", "82,5 kg" — parseExtensionNumber()); ignored when it is
    /// not one.
    Q_INVOKABLE void setNumberText(int row, const QString &text);
    /// One step up (direction 1) or down (-1) onto the step's grid (stepExtensionNumber()).
    Q_INVOKABLE void step(int row, int direction);
    /// The first field of kind "text" (-1: none): the dialog's focus.
    Q_INVOKABLE int firstTextField() const;
    void setComma(bool comma) { m_comma = comma; }

private:
    struct Field {
        QString key, label, kind, unit, text, placeholder;
        double value = 0;
        double step = 1;
    };
    QList<Field> m_fields;
    bool m_comma = false;
};

class WorkspaceExtensions : public QObject {
    Q_OBJECT
    /// The built-in extensions switched on ("silownia", "nawyki").
    Q_PROPERTY(QStringList enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool gymEnabled READ gymEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool habitsEnabled READ habitsEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool flashcardsEnabled READ flashcardsEnabled NOTIFY enabledChanged FINAL)
    /// The flashcards to study now (the built-in `fiszki`'s command `count`: cards due and new
    /// ones left today), counted again after notes change and every few minutes; 0 while off.
    Q_PROPERTY(int flashcardsDue READ flashcardsDue NOTIFY flashcardsDueChanged FINAL)
    /// The language of the extensions' words: "pl" | "en".
    Q_PROPERTY(QString language READ language NOTIFY commandsChanged FINAL)
    /// The question waiting for an answer: "" (none), "ask", "choose", "form".
    Q_PROPERTY(QString questionKind READ questionKind NOTIFY questionChanged FINAL)
    /// Counts the questions asked: a dialog closing late cancels only the question it showed.
    Q_PROPERTY(int questionSerial READ questionSerial NOTIFY questionChanged FINAL)
    Q_PROPERTY(QString questionTitle READ questionTitle NOTIFY questionChanged FINAL)
    Q_PROPERTY(QStringList questionOptions READ questionOptions NOTIFY questionChanged FINAL)
    Q_PROPERTY(brulion::shell::ExtensionFormModel *questionFields READ questionFields CONSTANT FINAL)
    /// The rest timer: seconds left (0 = none), its length and label.
    Q_PROPERTY(int restRemaining READ restRemaining NOTIFY restChanged FINAL)
    Q_PROPERTY(int restTotal READ restTotal NOTIFY restChanged FINAL)
    Q_PROPERTY(QString restLabel READ restLabel NOTIFY restChanged FINAL)
    /// The time left as "1:47".
    Q_PROPERTY(QString restText READ restText NOTIFY restChanged FINAL)
    /// Every extension with its state (Settings → Extensions).
    Q_PROPERTY(brulion::shell::ExtensionListModel *list READ list CONSTANT FINAL)
    /// The Luau console's lines.
    Q_PROPERTY(brulion::shell::ExtensionConsoleModel *console READ console CONSTANT FINAL)
    /// Counts the changes of states, commands and views: bindings that call pageState() & co.
    /// read it to follow them.
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
    /// Extension pages waiting for approval on this device.
    Q_PROPERTY(int waitingApproval READ waitingApproval NOTIFY revisionChanged FINAL)
    /// b.ai requests on their way (the console shows that it waits).
    Q_PROPERTY(int aiPending READ aiPending NOTIFY aiPendingChanged FINAL)
    /// The conversation (its path) whose turn asks the waiting question, "" for any other's.
    Q_PROPERTY(QString questionChat READ questionChat NOTIFY questionChanged FINAL)
    /// The extensions' timers running (b.timer): [{extension, title, label, remaining, total,
    /// text}], the one ending first first — the app's banner shows them (the gym's in its view).
    Q_PROPERTY(QVariantList timers READ timers NOTIFY timersChanged FINAL)

public:
    using Clock = std::function<QDateTime()>;
    explicit WorkspaceExtensions(Clock clock, QObject *parent = nullptr);
    ~WorkspaceExtensions() override;

    void attach(Vault *vault, VaultIndex *index);
    void detach();
    void setLanguage(const QString &language);
    void setEnabled(const QStringList &ids);
    /// Shows a notification now (the rest timer's end): Workspace connects its reminders' sink.
    void setNotifier(std::function<void(const QString &title, const QString &body)> notify);
    /// Arms a timer's end on the platform when it starts (notify::Notifier::armTimer(): true when
    /// the platform shows it by itself, also while the app is closed) and cancels it when the timer
    /// stops or is replaced (cancelTimer()). The shell's integration connects the notifier; without
    /// them the app shows the end itself while it runs (setNotifier()).
    void setTimerAlarms(std::function<bool(const ScheduledNotification &)> arm,
                        std::function<void(const QString &id)> cancel);
    /// The id of the notification of the timer of `extension` in this notebook
    /// ("brulion-timer:<notebook>:<extension>").
    QString timerNotificationId(const QString &extension) const;
    /// The model b.ai asks on this device (AppSettings: provider, key, model, address).
    void setAi(const ai::Settings &settings);
    /// What answers the provider "local" (the app's LocalAi; not owned, nullptr: none).
    void setLocalAi(ai::LocalBackend *backend);
    /// Loads the pictures a request names (b.ai's `images`): Workspace's, from the notebook.
    void setAiPictures(ai::ImageLoader loader);
    /// The engine's limits: extensions::Limits{} (the app's), unless a test whose subject is not the
    /// time limit gives its calls more time (a wall clock, which a stalled CI runner runs out).
    void setLimits(const extensions::Limits &limits);
    int aiPending() const { return m_aiPending; }
    /// Asks the model "Say OK." with the settings as they are: the answer or the error as a message.
    Q_INVOKABLE void testAi();

    /// The open notebook's index (nullptr: none) and today's date by the app's clock: what an
    /// extension view's Markdown is rendered with (ExtensionViewModel).
    VaultIndex *index() const { return m_index; }
    QDate today() const;
    QStringList enabled() const { return m_enabled; }
    QString language() const { return m_language; }
    bool gymEnabled() const { return m_enabled.contains(QStringLiteral("silownia")); }
    bool habitsEnabled() const { return m_enabled.contains(QStringLiteral("nawyki")); }
    bool flashcardsEnabled() const { return m_enabled.contains(QStringLiteral("fiszki")); }
    int flashcardsDue() const { return m_flashcardsDue; }
    /// The flashcards' settings on this device ({tags, newPerDay, retention}: AppSettings): given
    /// to `fiszki` with every view's params, action's args and command's ctx.args (theirs win).
    void setFlashcardSettings(const QVariantMap &settings);
    /// Counts the flashcards to study now again (flashcardsDue), at once.
    Q_INVOKABLE void refreshFlashcardsDue();
    QList<extensions::CommandInfo> commands() const;
    QList<extensions::ViewInfo> views() const;
    /// An extension's title: a built-in's in the UI language ("Siłownia"), a page's title, the
    /// console's ("Luau"), else its id.
    Q_INVOKABLE QString title(const QString &extension) const;
    ExtensionListModel *list() const { return m_list; }
    ExtensionConsoleModel *console() const { return m_console; }
    int revision() const { return m_revision; }
    int waitingApproval() const;

    // ---- the notebook (called by the Workspace) -------------------------------------------------
    /// The index is complete: every extension page is read and the approved ones run.
    void indexReady();
    /// Notes changed (created, modified, deleted, renamed: both paths), already in the index.
    void notesChanged(const QStringList &paths);
    /// This device's editor is about to write `content` to `path` (its own save). `base`: the
    /// text it writes over — what it last read or wrote there (EditorHost::writeIfUnchanged's
    /// `expected`). Trust follows only a save made over the code that was approved: a save
    /// without a base, or over other code that arrived meanwhile and was merged in, waits like
    /// any code from elsewhere.
    void savedByEditor(const QString &path, const QString &content, const std::optional<QString> &base = std::nullopt);
    /// The vault is about to tell the index of `changes` (Workspace::onVaultChanged, before the
    /// index follows): where each comes from — this device's app, an extension's call running now
    /// (its write), a sync, another program — and, while hooks listen for "task", the tasks of the
    /// notes as they were. notesChanged() follows with the same paths.
    void notesChanging(const QList<VaultChange> &changes);

    // ---- extension pages (Settings → Extensions, the page's state line) -------------------------
    /// The state of the extension page `path` (ExtensionRow::state), "" when it is none.
    Q_INVOKABLE QString pageState(const QString &path) const;
    /// Its error or problem, one line.
    Q_INVOKABLE QString pageMessage(const QString &path) const;
    /// The 0-based line its error names, -1 none.
    Q_INVOKABLE int pageErrorLine(const QString &path) const;
    /// What Luau's type checker says of its code (docs/EXTENSIONS.md "Types"): how many warnings,
    /// the first one ("line 14: Unknown global 'x'") and its 0-based line (-1: none).
    Q_INVOKABLE int pageTypeCount(const QString &path) const;
    Q_INVOKABLE QString pageTypeMessage(const QString &path) const;
    Q_INVOKABLE int pageTypeLine(const QString &path) const;
    /// Approves the page's code as it is now on this device and runs it (tests, the self-test: the
    /// app's "Run" approves the version it showed, approveVersion()).
    void approve(const QString &path);
    /// What "Run" shows before it approves the page `path` (ShellExtensionApproval.qml): {path,
    /// title, hash, permissions (texts), newPermissions (texts not approved before), before (an
    /// approved version is kept to compare with), lines [{kind ("same" | "added" | "removed" |
    /// "skipped"), text, line, count}], added, removed}; {} when it is no extension page.
    Q_INVOKABLE QVariantMap approvalReview(const QString &path) const;
    /// Approves the page's code if it is still the version `hash` that was shown, and runs it:
    /// "" when done, "changed" when the code is another one now (a sync replaced it meanwhile:
    /// show it again), "gone" when the page is no extension page any more.
    Q_INVOKABLE QString approveVersion(const QString &path, const QString &hash);
    /// The hash of the page's code now ("" for no extension page): the review sees a new version.
    Q_INVOKABLE QString pageHash(const QString &path) const;
    /// Switches an extension page on or off on this device.
    Q_INVOKABLE void setPageOn(const QString &path, bool on);
    /// A new extension page (an example to start from, approved here); its path, "" on failure.
    Q_INVOKABLE QString createExtension();
    /// The source of a built-in extension as a page of the notebook, which then runs instead of the
    /// built-in code (approved here); its path ("" on failure). An existing one is returned.
    Q_INVOKABLE QString editBuiltinSource(const QString &builtin);
    /// The page of the notebook that overrides a built-in extension ("" = none).
    Q_INVOKABLE QString overridePage(const QString &builtin) const;
    /// Moves the pages that override a built-in to the trash: the built-in code runs again.
    Q_INVOKABLE bool restoreBuiltin(const QString &builtin);
    /// A fresh VM, every extension loaded again ("Reload extensions").
    Q_INVOKABLE void reloadAll();
    /// The label of a view (b.view's meta.label), else its name.
    Q_INVOKABLE QString viewTitle(const QString &extension, const QString &view) const;

    // ---- an extension's own settings (b.settings; Settings → Extensions → its Settings) ----------
    /// The settings `extension` declared, each with every key (key, label, kind, description, step,
    /// unit, options, placeholder) and its value on this device as currentText, currentNumber,
    /// currentOn and display (a number as shown).
    Q_INVOKABLE QVariantList settingsOf(const QString &extension) const;
    /// How many settings `extension` declared (0: none, or it does not run).
    Q_INVOKABLE int settingsCount(const QString &extension) const;
    /// The engine's id of the extension page `path` (its own, or the built-in's it overrides), "".
    Q_INVOKABLE QString pageExtension(const QString &path) const;
    /// Sets one (a number, a text, true / false, one of a choice's options); false when it does not
    /// fit the setting. Views draw again.
    Q_INVOKABLE bool setSetting(const QString &extension, const QString &key, const QVariant &value);
    /// Every setting of `extension` back to its default.
    Q_INVOKABLE void resetSettings(const QString &extension);

    // ---- sharing between notebooks ----------------------------------------------------------------
    /// Where each notebook keeps this device's state (AppController: notebook::deviceStatePath of
    /// the app's data folder); unset, nothing can be copied.
    void setDeviceStates(std::function<QString(const QString &root)> stateOf);
    /// Copies the extension page `path` (with the extension pages its code requires) into the
    /// notebook at `root`, at the same path, approved there on this device: "" when done (a
    /// message says so), "conflict" when a page there has other text (call again with `replace`),
    /// else the error.
    Q_INVOKABLE QString copyToNotebook(const QString &path, const QString &root, bool replace = false);

    // ---- the console ----------------------------------------------------------------------------
    /// Evaluates Luau typed in the console: an expression's value, statements' output.
    Q_INVOKABLE void evaluate(const QString &code);
    /// The ```luau block at `line` of `path` evaluated like the console's input ("Run Luau block").
    void runCodeAt(const QString &path, int line);

    /// Runs a command; `path`/`line` name the block under the caret (ctx.block) when given; `args`
    /// is its ctx.args (`{}` when none: what the app or a toast's button passes).
    Q_INVOKABLE void runCommand(const QString &extension, const QString &name, const QString &path = QString(), int line = -1,
                                const QVariantMap &args = QVariantMap());
    /// runCommand() of `command` "<extension>/<name>" on the block at `line` of `path` (the editor);
    /// "*/run" runs the ```luau block there in the console (runCodeAt()). `selection`: the
    /// editor's selection (ctx.selection).
    Q_INVOKABLE void runBlockCommand(const QString &command, const QString &path, int line,
                                     const QString &selection = QString());
    /// The editor on screen answers the uiAction "offerSelection" with its selection (the text
    /// selected in a block, else the blocks selected, as Markdown): the command run next from the
    /// palette gets it as ctx.selection — when it runs on the page `path`.
    Q_INVOKABLE void offerSelection(const QString &path, const QString &text);
    /// Forgets an offered selection (the palette asks again for each command).
    void clearSelection();
    /// A view's node tree ({} and `error` when it failed). Never while a question waits (the waiting
    /// call would be abandoned): {} then, and the view renders again after changed().
    QVariantMap render(const QString &extension, const QString &view, const QVariantMap &params, QString *error);
    /// One of a view's actions; false when it failed (the message was shown).
    bool act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args);
    bool isWaiting() const;

    // ---- the buttons of messages (a result { message, tone, button }) -----------------------------
    /// The button `id` of messageWithButton() was pressed: its view action runs (of the view whose
    /// action returned it) with its args, or its command (ctx.args), or its page opens
    /// (pageRequested()). Each button works once.
    Q_INVOKABLE void pressButton(int id);
    /// The toast of button `id` went: it is forgotten.
    Q_INVOKABLE void forgetButton(int id);

    // ---- the AI chat (WorkspaceChats) --------------------------------------------------------
    /// Runs the assistant's turn in the conversation `path` (asystent's "chat": ctx.messages,
    /// ctx.page): its answer comes as chatAnswered(path, …), a failure as chatFailed(path, …).
    /// False (and chatFailed) when the assistant is not loaded.
    bool runChat(const QString &path, const QVariantList &messages, const QString &page);
    /// Drops the waiting turn of the conversation `path` (its question too): no answer comes.
    void stopChat(const QString &path);
    QString questionChat() const { return m_questionChat; }

    QString questionKind() const { return m_questionKind; }
    int questionSerial() const { return m_questionSerial; }
    QString questionTitle() const { return m_questionTitle; }
    QStringList questionOptions() const { return m_questionOptions; }
    ExtensionFormModel *questionFields() const { return m_fields; }
    Q_INVOKABLE void answerText(const QString &text);
    Q_INVOKABLE void answerChoice(const QString &option);
    Q_INVOKABLE void answerForm();
    Q_INVOKABLE void cancelQuestion();

    int restRemaining() const { return m_restRemaining; }
    int restTotal() const { return m_restTotal; }
    QString restLabel() const { return m_restLabel; }
    QString restText() const;
    /// The gym's timer (silownia's b.timer: GymView's banner).
    Q_INVOKABLE void startRest(int seconds, const QString &label);
    Q_INVOKABLE void stopRest();
    QVariantList timers() const;
    /// Ends the timer of `extension` without a word (the banner's ×).
    Q_INVOKABLE void stopTimer(const QString &extension);

    // ---- macros, template variables, panels (workspacemacros.cpp; docs/EXTENSIONS.md "Macros") ---
    /// What the macro `macro` written in the note `path` shows now: its last value (computed again
    /// in the background when what it read changed: macrosChanged() then), "…" while it is computed
    /// first, its extension's state (waiting for approval, off, unknown) or its error. Never runs
    /// Luau itself: rendering never waits for an extension.
    markdown::MacroView macroView(const markdown::MacroSpan &macro, const QString &path);
    /// The macros the extensions offer that match `filter` (the `{{` completion): rows {title
    /// "ext/name", subtitle (its description · the extension's title), insertText "ext:ext/name
    /// example", hint (its arguments' names), icon}.
    QVariantList macroSuggestions(const QString &filter, int limit) const;
    /// A template's variable `{{extension/name arguments}}` filled now for `context` ({page, path,
    /// date, template}): its text, or nullopt — left as written (no such variable; a failure goes
    /// to the console).
    std::optional<QString> templateVariable(const QString &extension, const QString &name, const QString &arguments,
                                            const QVariantMap &context);
    /// A macro's placeholder was clicked: what can be done about its extension — {state ("approval"
    /// | "off" | "error" | "unknown" | "running"), title, path (its page, "" for a built-in), line
    /// (an error's, 0-based; -1), message, builtin}.
    Q_INVOKABLE QVariantMap macroExtension(const QString &extension) const;
    /// The page of `extension` when its code waits for approval on this device (a macro's
    /// placeholder, a view block: the approval with its code), else "".
    Q_INVOKABLE QString approvalPageOf(const QString &extension) const;
    /// What the macros of `files` show, for the documents of an export (io::ExportInput::
    /// macroTexts): the values computed, those not computed yet computed now — a few seconds for
    /// the whole export at most.
    QHash<QString, QHash<QString, QString>> macroTexts(const QList<io::FileEntry> &files);
    /// A macro's placeholder was clicked in the editor: macroHelpRequested() for the dialog that
    /// offers to run, switch on or open its extension.
    Q_INVOKABLE void showMacro(const QString &extension, const QString &name, const QString &arguments);
    /// The same for a ```view block's view whose extension does not run.
    Q_INVOKABLE void showView(const QString &extension, const QString &view);
    /// What a view drawn beside or in the note `path` gets of it (a ```view block's, a panel's):
    /// {page, path, date (a daily note's)}.
    Q_INVOKABLE QVariantMap noteParams(const QString &path) const;
    /// The views the extensions offer as sections of the side panel (b.view's meta.panel) beside
    /// a `kind` of note ("page" | "journal"), or of the pages panel ("sidebar"): rows {extension,
    /// view, title, icon}, by title.
    Q_INVOKABLE QVariantList panels(const QString &kind) const;
    /// The views an extension offers by `extension/name` for a ```view block (the editor's slash
    /// command): rows {extension, view, title, icon, source}, labelled views only, by title.
    Q_INVOKABLE QVariantList embeddableViews() const;

Q_SIGNALS:
    void enabledChanged();
    void commandsChanged();
    void questionChanged();
    void restChanged();
    void timersChanged();
    void revisionChanged();
    void aiPendingChanged();
    void flashcardsDueChanged();
    /// A command's result or an error, for a toast (kind "info" | "success" | "error").
    void message(const QString &text, const QString &kind);
    /// A result { message, tone, button = { label, … } }: a toast with a button; its press is
    /// pressButton(`button`).
    void messageWithButton(const QString &text, const QString &kind, const QString &label, int button);
    /// A button of a message opens a page (a page name, made when missing).
    void pageRequested(const QString &page);
    /// Something changed what views show (a command or action finished, extensions on or off).
    void changed();
    /// A turn of runChat() ended with the AI's message.
    void chatAnswered(const QString &path, const QString &text);
    /// A turn of runChat() failed: why, for the user.
    void chatFailed(const QString &path, const QString &error);
    /// What the conversation's turn does now (b.status; "" = nothing to say).
    void chatStatus(const QString &path, const QString &text);
    /// What the model has written so far of its answer and its thoughts in the conversation's
    /// turn (each of its requests starts with ""), as it writes; `tokens` written in `writingMs`
    /// (0: not known).
    void chatPartial(const QString &path, const QString &text, const QString &thoughts, int tokens, qint64 writingMs);
    /// What macros show changed (values computed, extensions loaded, approved, switched off).
    void macrosChanged();
    /// showMacro(), showView(): the extension and what of it was clicked (the macro as written,
    /// "```view extension/view"), for ShellMacroDialog.
    void macroHelpRequested(const QString &extension, const QString &macro);

private:
    /// An extension page of the notebook as read from the index.
    struct PageEntry
    {
        extensions::PageExtension page;
        QString runId; ///< the engine's id: page.id, or the built-in's it overrides
        QString builtin; ///< the built-in it overrides, ""
        QString state;
        QString message;
        int errorLine = -1;
        /// What Luau's type checker found in its code (typecheck.h), of the code `typedHash`.
        QList<extensions::TypeProblem> types;
        QString typedHash;
    };
    /// What the editor is writing to a page: the key of its text, and the extension's hash in
    /// the text it writes over ("": unknown).
    struct EditorSave {
        QByteArray text;
        QString baseHash;
    };

    void reload();
    bool ensureEngine();
    /// Loads what should run and unloads the rest (only what changed), then updates the states.
    void reconcile();
    void refreshList();
    void bumpRevision();
    bool updatePage(const QString &path);
    void scanPages();
    void loadTrust();
    void storeTrust();
    bool isApproved(const QString &hash) const { return m_approved.contains(hash); }
    void addApproved(const QString &hash);
    const PageEntry *pageAt(const QString &path) const;
    QString pageStateOf(const PageEntry &entry) const;
    void recordError(const QString &runId, const QString &error);
    /// Where a call came from: what its result means and where it goes.
    struct CallContext
    {
        bool console = false; // the console's: the value or the error in the console
        bool toast = false; // also as a message ("Run Luau block")
        bool fromAction = false; // a view's action
        bool hook = false; // a hook: errors to the console, never a question
        bool askedAi = false; // "Asking AI…" said once per call (an agent's loop asks many times)
        QString chat; // a turn of the AI chat: the conversation's path
        int depth = 0; // hooks answering hooks (events.h): what this call writes is one deeper
        int hookId = 0;
        QString hookEvent;
        QString extension;
        QString view; // a view's action: the view (a result's button acts in it)
    };
    /// What a message's button does (a result's `button`).
    struct Button
    {
        QString extension, view, action, command, page;
        QVariantMap args;
    };
    /// A call's result: a question shown, an AI request sent, or its end (by its context).
    void handleResult(const extensions::Result &result);
    void handle(const extensions::Result &result, const CallContext &ctx);
    /// A command's or an action's value: a text, or a table { message, tone, button } — a message.
    void showResult(const QVariant &value, const CallContext &ctx);
    void chatResult(const QString &path, const extensions::Result &result);
    /// resume() of `call` with the conversation it belongs to known to b.status.
    extensions::Result resumeCall(int call, const QVariant &value);
    void dispatchAi(int call, const QVariantMap &request);
    void finishAi(int call, const QVariantMap &reply, quint64 generation);
    void processPages();
    void setQuestion(const QVariantMap &question);
    void answer(const QVariant &value);
    void tickRest();
    void runHooks(const QString &event, const QVariantMap &arg, const QString &onlyExtension = QString());
    void runHook(const extensions::HookInfo &hook, const QVariantMap &arg, int depth);
    void flushChanged();
    void checkDay();
    /// The modules `id` required (engine's modulesOf) as they are now: loading again when it changes.
    QString moduleKey(const QString &id) const;
    /// Keeps the code of `entry` as approved (approvedCode: the next approval's diff).
    void rememberApproved(const PageEntry &entry);
    void startTimerFor(const QString &extension, const extensions::TimerSpec &spec);
    /// Luau's type checker over a page's code, on a worker (never in the browser: no checker).
    void checkTypes(const QString &path);
    void timerEnded(const QString &extension);
    bool ensureConsole();
    /// Evaluates `code` in the console's environment (`name` in errors); `toast`: the result as a
    /// message too ("Run Luau block": the console may not be on screen).
    void evaluateIn(const QString &code, const QString &name, const QString &echo, bool toast);
    void consoleResult(const extensions::Result &result, bool toast);
    void hookResult(const CallContext &ctx, const extensions::Result &result);

    // Macros (workspacemacros.cpp).
    /// Computes the stale macros asked for lately, a slice of time at a time.
    void runMacros();
    void scheduleMacros();
    /// Extensions loaded, unloaded, approved or switched: every macro asks again.
    void macrosReset();
    void noteMacrosChanged();
    /// Computes one macro now and keeps its value (runMacros(), macroTexts()).
    markdown::MacroView computeMacro(const QString &key, const extensions::MacroRequest &request, qint64 now);
    /// The ctx a macro or a variable of the note `path` gets.
    QVariantMap macroContext(const QString &arguments, const QString &path) const;

    Clock m_clock;
    Vault *m_vault = nullptr;
    VaultIndex *m_index = nullptr;
    QString m_language = QStringLiteral("pl");
    QStringList m_enabled;
    std::unique_ptr<extensions::NotebookHost> m_host;
    std::unique_ptr<extensions::Engine> m_engine;
    extensions::Limits m_limits;
    QString m_questionKind, m_questionTitle;
    QStringList m_questionOptions;
    int m_questionSerial = 0;
    ExtensionFormModel *m_fields = nullptr;
    QHash<QString, PageEntry> m_pages; // by path
    bool m_scanned = false;
    QSet<QString> m_approved; // hashes approved on this device (LocalStore)
    QStringList m_approvedOrder;
    QSet<QString> m_off; // page extensions switched off here (by id)
    QHash<QString, EditorSave> m_editorSaves; // by path: the save the editor made last
    QHash<QString, QString> m_loadedKeys; // engine id → what was loaded (a key of its source)
    QHash<QString, extensions::Extension> m_lastGood; // engine id → the last version that loaded
    QHash<QString, QString> m_loadErrors; // engine id → why the last load failed
    QHash<int, int> m_hookFailures;
    QTimer m_changeTimer;
    /// The flashcards' due count: after notes change (coalesced) and every five minutes.
    QTimer m_dueTimer;
    QTimer m_dueClock;
    int m_flashcardsDue = 0;
    /// Settings the app gives a built-in with its calls (setFlashcardSettings), by its id.
    QHash<QString, QVariantMap> m_settingsFor;
    /// `values` under the settings of `extension` (the values win).
    QVariantMap withSettings(const QString &extension, const QVariantMap &values) const;
    CallContext m_current; // the context of the call starting now
    QHash<int, Button> m_buttons; // the buttons of messages shown, by id
    int m_lastButton = 0;
    QHash<int, CallContext> m_calls; // waiting calls by id
    int m_questionCall = 0; // the call the shown question belongs to
    QString m_questionChat; // its conversation, when a chat's turn asks
    QString m_statusChat; // the conversation of the call running now (b.status goes there)
    bool m_reconcilePending = false; // pages changed while a question waited
    QStringList m_pendingPaths; // changed notes not looked at yet (processPages())
    QTimer m_pagesTimer;
    ai::Settings m_aiSettings;
    ai::LocalBackend *m_localAi = nullptr;
    ai::ImageLoader m_aiPictures;
    ai::Client *aiClient();
    std::unique_ptr<sync::NetworkHttpTransport> m_transport;
    std::unique_ptr<ai::Client> m_aiClient;
    QHash<int, ai::Pending> m_aiRequests; // by call: the requests on their way (stopped with it)
    /// Stops every request on its way (their engine is gone).
    void stopAiRequests();
    int m_aiPending = 0;
    quint64 m_generation = 0; // a new engine: answers for the old one are dropped
    int m_revision = 0;
    ExtensionListModel *m_list = nullptr;
    ExtensionConsoleModel *m_console = nullptr;
    QTimer m_restTimer; // ticks every timer of m_timers
    struct RunningTimer {
        extensions::TimerSpec spec;
        QDateTime end; // UTC
        int remaining = 0;
        bool native = false; // the platform shows its end by itself (setTimerAlarms())
    };
    /// The notification of a timer's end (the platform arms it): its words, its moment.
    ScheduledNotification timerNotification(const QString &extension, const RunningTimer &timer) const;
    /// The title and the body of a timer's end: the extension's words, else the app's.
    std::pair<QString, QString> timerWords(const QString &extension, const extensions::TimerSpec &spec) const;
    /// The running timers in the notebook's device state ("extensions/timers"): a restart of the
    /// app — Android ends a process in the background — finds them again (loadTimers()).
    void storeTimers();
    /// The stored timers: those still running count on, those that ended while the app was closed
    /// (within a day) run their "timer" hooks with `late` once the notebook is open.
    void loadTimers();
    std::function<bool(const ScheduledNotification &)> m_timerArm;
    std::function<void(const QString &)> m_timerCancel;
    /// Timers that ended while the app was closed: their hooks wait for "open".
    QList<std::pair<QString, extensions::TimerSpec>> m_lateTimers;
    QHash<QString, RunningTimer> m_timers; // by extension (one each)
    int m_restRemaining = 0;
    int m_restTotal = 0;
    QString m_restLabel;
    std::function<void(const QString &, const QString &)> m_notify;
    extensions::MacroCache m_macros;
    QTimer m_macroTimer;                   // runMacros()
    QTimer m_macrosChangedTimer;           // macrosChanged(), coalesced
    QElapsedTimer m_macroClock;            // the cache's time
    QHash<QString, qint64> m_macroSpentMs; // per note, in the current round of changes
    QString m_macroSignature;              // what ran and the pages' states when macros were last reset
    QSet<QString> m_macroNames;            // "extension/name" of the macros registered
    // Events (events.h): the burst of changes the hooks hear after 0.6 s, where each change came
    // from (notesChanging()), the tasks of the notes as they were, the depth of the call running.
    extensions::ChangeBatch m_batch;
    struct Changing {
        extensions::Origin origin = extensions::Origin::App;
        QString by;
        int depth = 0;
        bool existed = false;
        bool tasksKnown = false;
        QList<extensions::TaskState> tasks;
    };
    QHash<QString, Changing> m_changing; // by path, until notesChanged()
    int m_runningDepth = 0;
    bool m_opened = false; // "open" hooks ran for this notebook
    QDate m_day;           // "day" hooks: the day they last heard of
    QTimer m_dayTimer;
    QString m_selectionPath, m_selection;              // offerSelection()
    std::function<QString(const QString &)> m_stateOf; // setDeviceStates()
};

} // namespace brulion::shell
