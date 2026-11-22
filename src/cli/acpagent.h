// `brulion-cli acp agent`: Brulion's AI assistant as an agent of the Agent Client Protocol, for an
// editor that talks to agents — Zed, JetBrains' IDEs, any ACP client (docs/CLI.md "The assistant
// in Zed and JetBrains"). The editor starts the program and speaks JSON-RPC 2.0 with it, one
// message per line on its standard input and output (the side of acp/acp.h's Client, reusing its
// Connection). Each session is a conversation with the built-in assistant (asystent.luau's
// `chat`), run without the app in an extension engine over the notebook:
//
// * its b.ai asks the model the app's Settings → Extensions → AI name — ai/provider, ai/model,
//   ai/baseUrl of the app's settings file, the key from its secrets (aiSettingsOfApp()) —, and
//   nothing is asked without one (a prompt then fails with what is missing);
// * a change of a note it wants (b.choose: Allow, Allow every change of this question, No) goes to
//   the editor as session/request_permission (the options' kinds allow_once, allow_always,
//   reject_once), the change shown as a tool call of kind "edit";
// * what it does meanwhile (b.status: "Searching the notes: …") is a tool call of its own, the
//   model's thoughts agent_thought_chunk updates as they are written, its answer one
//   agent_message_chunk at the turn's end ("✏️ Changed: [[…]]" with it);
// * session/cancel stops the model's request, drops the engine's call and ends the turn with
//   stopReason "cancelled"; b.ask and b.form, which an editor cannot show, are answered "nothing".
//
// The conversation is kept in memory per session (no session/load): the user's prompts — text,
// embedded files as text, links to files as [[page]] links when they are the notebook's — and the
// assistant's answers, sent from a later turn on when they no longer fit (chat::firstSent). The
// assistant's writes are the command line's ("Replaced from the command line (agent)" in a page's
// history). Pictures are not sent (no picture leaves the notebook unconverted: the app's loader
// needs Qt GUI).
#pragma once

#include "acp/acp.h"
#include "ai/ai.h"
#include "chat/chat.h"
#include "extensions/engine.h"

#include <QCoreApplication> // Q_DECLARE_TR_FUNCTIONS
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace brulion {
namespace notebook {
class Notebook;
}
namespace extensions {
class NotebookHost;
}
namespace sync {
class HttpTransport;
}
} // namespace brulion

namespace brulion::cli {

class Context;

/// The model the app on this device asks (Settings → Extensions → AI): its settings file's
/// ai/provider, ai/model, ai/baseUrl (ai/localModel for "local") and the key in
/// `<app data>/secrets/ai-key`.
ai::Settings aiSettingsOfApp(const Context &context);
/// Why `settings` cannot be asked from the command line ("" they can), for the user: no key, no
/// model, the model on the device (only the app runs it).
QString aiProblemForCli(const ai::Settings &settings);

struct AcpAgentOptions {
    ai::Settings ai;
    /// "" the model can be asked; else why not (every prompt fails with it).
    QString aiProblem;
    /// The assistant's language: what it writes into notes and what it asks ("pl" | "en").
    QString language = QStringLiteral("en");
    /// Local wall-clock time (the engine's b.today, b.now); unset: the system's.
    std::function<QDateTime()> now;
    QString version; ///< agentInfo's
    extensions::Limits limits;
    /// The conversation sent to the model, in characters (chat::firstSent).
    int historyBudget = 120000;
};

class AcpAgent : public QObject {
    Q_DECLARE_TR_FUNCTIONS(brulion::cli)
public:
    /// `notebook` nullptr: none could be opened (`notebookProblem` says why; sessions are refused).
    /// `transport` carries b.ai's requests (not owned); `send` writes a line to the editor.
    AcpAgent(notebook::Notebook *notebook, const QString &notebookProblem, AcpAgentOptions options,
             sync::HttpTransport *transport, acp::Connection::Send send, QObject *parent = nullptr);
    ~AcpAgent() override;

    /// Bytes from the editor.
    void received(const QByteArray &bytes);
    /// The editor is gone (its end of standard input): every turn stops.
    void closed();

    /// The sessions there are and whether a turn runs in one (tests).
    QStringList sessions() const { return m_sessions.keys(); }
    bool running(const QString &session) const;

    /// A line for the log (standard error), never a note's text or a key.
    std::function<void(const QString &line)> log;

private:
    struct Turn {
        QJsonValue promptId;  // the session/prompt to answer
        quint64 serial = 0;   // its own number: an answer of another turn's is stale
        int call = 0;         // the engine's call while it waits
        std::optional<ai::Pending> ai;
        QString statusId;     // the tool call that shows b.status now ("" none)
        QString status;
        QString requestThoughts; // what the model's current request thought, as sent
        bool newRequest = false;
        bool anyThoughts = false;
    };
    struct Session {
        QString id;
        chat::Conversation conversation;
        std::optional<Turn> turn;
    };

    void onRequest(const QJsonValue &id, const QString &method, const QJsonObject &params);
    void onNotification(const QString &method, const QJsonObject &params);
    void initialize(const QJsonValue &id, const QJsonObject &params);
    void newSession(const QJsonValue &id, const QJsonObject &params);
    void prompt(const QJsonValue &id, const QJsonObject &params);
    void cancel(const QString &session);
    bool ensureEngine(QString *error);
    /// The prompt's blocks as the text of the user's message.
    QString promptText(const QJsonArray &blocks) const;

    /// Runs `step` (a call or a resume of the engine) for `session`'s turn and goes on with its
    /// result.
    void run(const QString &session, const std::function<extensions::Result()> &step);
    void handle(const QString &session, const extensions::Result &r);
    void askAi(const QString &session, const QVariantMap &request);
    void aiAnswered(const QString &session, quint64 serial, const QVariantMap &reply, const QString &thoughts);
    void aiPartial(const QString &session, quint64 serial, const ai::Partial &partial);
    void askPermission(const QString &session, const QVariantMap &question);
    void permissionAnswered(const QString &session, quint64 serial, const QString &toolCall, const QStringList &options,
                            const QJsonValue &result, const acp::Connection::Error &error);
    void showStatus(const QString &session, const QString &text);
    void closeStatus(const QString &session, Turn &turn, const QString &state);
    void finish(const QString &session, const extensions::Result &r);
    void update(const QString &session, const QJsonObject &update);
    void thoughts(const QString &session, Turn &turn, const QString &text);
    Turn *turnOf(const QString &session, quint64 serial);
    void say(const QString &line) const;

    notebook::Notebook *m_notebook;
    QString m_notebookProblem;
    AcpAgentOptions m_options;
    acp::Connection m_connection;
    ai::Client m_ai;
    std::unique_ptr<extensions::NotebookHost> m_host;
    std::unique_ptr<extensions::Engine> m_engine;
    QHash<QString, Session> m_sessions;
    QString m_running; // the session whose engine call executes now
    quint64 m_serial = 0;
    int m_toolCalls = 0;
    bool m_initialized = false;
};

} // namespace brulion::cli
