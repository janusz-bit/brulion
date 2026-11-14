#pragma once
// WorkspaceExtensions — the extensions of the open notebook (docs/EXTENSIONS.md, QML:
// Workspace.extensions): the Luau engine (core extensions::Engine) over a NotebookHost bound to the
// Workspace's vault and index; the built-in extensions the settings switch on (the gym) and the
// extensions written in the notebook (pages tagged #rozszerzenie, pageextension.h) — each page's
// code approved per device (a hash in the vault's LocalStore) and loaded again whenever its page
// changes; their palette commands, views and hooks (b.on "save" / "change"); the question a
// command or an action waits on (b.ask / b.choose / b.form, shown by ShellExtensionQuestion.qml);
// the Luau console (evaluate(), "Run Luau block"); messages (toasts) and the rest timer
// (b.timer: a countdown in the app and a notification when it ends).
//
//   extensions->runCommand(u"silownia"_s, u"start"_s);                 // the palette
//   extensions->runCommand(u"silownia"_s, u"add-set"_s, path, line);   // on the block under the caret
//   QML: Workspace.extensions.questionKind ("" | "ask" | "choose" | "form") → answerText / answerChoice /
//        setFieldValue + answerForm / cancelQuestion
//
// The Workspace tells it about the notebook: indexReady() (scan every page once), notesChanged()
// (the pages a change touched: an extension page loads again, "change" hooks), savedByEditor()
// (this device's editor saved a page: "save" hooks; an approved extension's new code stays
// approved — the code someone writes here is theirs, code that arrives by sync waits).
//
// Views (ExtensionViewModel) render through render() and act(); a view renders again after
// changed() (a command or an action finished, an extension was switched on or off).
//
// Single-threaded (the GUI thread, the Workspace's).

#include "extensionmodels.h"

#include "ai/ai.h"
#include "extensions/engine.h"
#include "extensions/pageextension.h"

#include <QAbstractListModel>
#include <QSet>
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion {
class Vault;
class VaultIndex;
namespace extensions {
class NotebookHost;
}
namespace sync {
class NetworkHttpTransport;
}
} // namespace brulion

namespace brulion::shell {

/// The fields of a b.form question (roles key, label, kind, value, step, unit).
class ExtensionFormModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KeyRole = Qt::UserRole + 1, LabelRole, KindRole, ValueRole, StepRole, UnitRole, TextRole, DisplayRole };
    using QAbstractListModel::QAbstractListModel;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reset(const QVariantList &fields);
    QVariantMap values() const;
    Q_INVOKABLE void setNumber(int row, double value);
    Q_INVOKABLE void setText(int row, const QString &text);
    /// A number typed in ("82,5", "82.5"); ignored when it is not one.
    Q_INVOKABLE void setNumberText(int row, const QString &text);
    Q_INVOKABLE void step(int row, int direction);
    void setComma(bool comma) { m_comma = comma; }

private:
    struct Field {
        QString key, label, kind, unit, text;
        double value = 0;
        double step = 1;
    };
    QList<Field> m_fields;
    bool m_comma = false;
};

class WorkspaceExtensions : public QObject {
    Q_OBJECT
    /// The built-in extensions switched on ("silownia").
    Q_PROPERTY(QStringList enabled READ enabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(bool gymEnabled READ gymEnabled NOTIFY enabledChanged FINAL)
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
    /// The model b.ai asks on this device (AppSettings: provider, key, model, address).
    void setAi(const ai::Settings &settings);
    int aiPending() const { return m_aiPending; }
    /// Asks the model "Say OK." with the settings as they are: the answer or the error as a message.
    Q_INVOKABLE void testAi();

    QStringList enabled() const { return m_enabled; }
    QString language() const { return m_language; }
    bool gymEnabled() const { return m_enabled.contains(QStringLiteral("silownia")); }
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
    /// This device's editor is about to write `content` to `path` (its own save).
    void savedByEditor(const QString &path, const QString &content);

    // ---- extension pages (Settings → Extensions, the page's state line) -------------------------
    /// The state of the extension page `path` (ExtensionRow::state), "" when it is none.
    Q_INVOKABLE QString pageState(const QString &path) const;
    /// Its error or problem, one line.
    Q_INVOKABLE QString pageMessage(const QString &path) const;
    /// The 0-based line its error names, -1 none.
    Q_INVOKABLE int pageErrorLine(const QString &path) const;
    /// Approves the page's code as it is now on this device and runs it.
    Q_INVOKABLE void approve(const QString &path);
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

    // ---- the console ----------------------------------------------------------------------------
    /// Evaluates Luau typed in the console: an expression's value, statements' output.
    Q_INVOKABLE void evaluate(const QString &code);
    /// The ```luau block at `line` of `path` evaluated like the console's input ("Run Luau block").
    void runCodeAt(const QString &path, int line);

    /// Runs a command; `path`/`line` name the block under the caret (ctx.block) when given.
    Q_INVOKABLE void runCommand(const QString &extension, const QString &name, const QString &path = QString(), int line = -1);
    /// runCommand() of `command` "<extension>/<name>" on the block at `line` of `path` (the editor);
    /// "*/run" runs the ```luau block there in the console (runCodeAt()).
    Q_INVOKABLE void runBlockCommand(const QString &command, const QString &path, int line);
    /// A view's node tree ({} and `error` when it failed). Never while a question waits (the waiting
    /// call would be abandoned): {} then, and the view renders again after changed().
    QVariantMap render(const QString &extension, const QString &view, const QVariantMap &params, QString *error);
    /// One of a view's actions; false when it failed (the message was shown).
    bool act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args);
    bool isWaiting() const;

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
    Q_INVOKABLE void startRest(int seconds, const QString &label);
    Q_INVOKABLE void stopRest();

Q_SIGNALS:
    void enabledChanged();
    void commandsChanged();
    void questionChanged();
    void restChanged();
    void revisionChanged();
    void aiPendingChanged();
    /// A command's result or an error, for a toast (kind "info" | "error").
    void message(const QString &text, const QString &kind);
    /// Something changed what views show (a command or action finished, extensions on or off).
    void changed();

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
        int hookId = 0;
        QString hookEvent;
        QString extension;
    };
    /// A call's result: a question shown, an AI request sent, or its end (by its context).
    void handleResult(const extensions::Result &result);
    void handle(const extensions::Result &result, bool fromAction);
    void dispatchAi(int call, const QVariantMap &request);
    void finishAi(int call, const QVariantMap &reply, quint64 generation);
    void processPages();
    void setQuestion(const QVariantMap &question);
    void answer(const QVariant &value);
    void tickRest();
    void runHooks(const QString &event, const QVariantMap &arg);
    void flushChanged();
    bool ensureConsole();
    /// Evaluates `code` in the console's environment (`name` in errors); `toast`: the result as a
    /// message too ("Run Luau block": the console may not be on screen).
    void evaluateIn(const QString &code, const QString &name, const QString &echo, bool toast);
    void consoleResult(const extensions::Result &result, bool toast);
    void hookResult(const CallContext &ctx, const extensions::Result &result);

    Clock m_clock;
    Vault *m_vault = nullptr;
    VaultIndex *m_index = nullptr;
    QString m_language = QStringLiteral("pl");
    QStringList m_enabled;
    std::unique_ptr<extensions::NotebookHost> m_host;
    std::unique_ptr<extensions::Engine> m_engine;
    QString m_questionKind, m_questionTitle;
    QStringList m_questionOptions;
    int m_questionSerial = 0;
    ExtensionFormModel *m_fields = nullptr;
    QHash<QString, PageEntry> m_pages; // by path
    bool m_scanned = false;
    QSet<QString> m_approved; // hashes approved on this device (LocalStore)
    QStringList m_approvedOrder;
    QSet<QString> m_off; // page extensions switched off here (by id)
    QHash<QString, QByteArray> m_editorSaves; // path → SHA-1 of the text the editor wrote
    QHash<QString, QString> m_loadedKeys; // engine id → what was loaded (a key of its source)
    QHash<QString, extensions::Extension> m_lastGood; // engine id → the last version that loaded
    QHash<QString, QString> m_loadErrors; // engine id → why the last load failed
    QHash<int, int> m_hookFailures;
    QStringList m_changedPages;
    QTimer m_changeTimer;
    CallContext m_current; // the context of the call starting now
    QHash<int, CallContext> m_calls; // waiting calls by id
    int m_questionCall = 0; // the call the shown question belongs to
    bool m_reconcilePending = false; // pages changed while a question waited
    QStringList m_pendingPaths; // changed notes not looked at yet (processPages())
    QTimer m_pagesTimer;
    ai::Settings m_aiSettings;
    std::unique_ptr<sync::NetworkHttpTransport> m_transport;
    std::unique_ptr<ai::Client> m_aiClient;
    int m_aiPending = 0;
    quint64 m_generation = 0; // a new engine: answers for the old one are dropped
    int m_revision = 0;
    ExtensionListModel *m_list = nullptr;
    ExtensionConsoleModel *m_console = nullptr;
    QTimer m_restTimer;
    QDateTime m_restEnd;
    int m_restRemaining = 0;
    int m_restTotal = 0;
    QString m_restLabel;
    std::function<void(const QString &, const QString &)> m_notify;
};

} // namespace brulion::shell
