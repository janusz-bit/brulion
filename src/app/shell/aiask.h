#pragma once
// AiAsk — one question to the AI of the user's settings (b.ai's model: Settings → Extensions → AI),
// answered as text: what the app asks the AI for by itself outside a conversation — Code mode's
// commit message written from the changes (CodeView hands the question of GitChangesPanel's commit
// dialog to it; docs/CODE-GIT.md "The panel"). QML: `AiAsk { workspace: Workspace }`.
//
//   ask(system, prompt) → true when it was sent; answered(ok, text) once (the text trimmed, a
//   fenced block around all of it taken off); a new ask() stops the one on its way; stop().
//
// It goes through WorkspaceExtensions::sendAi (the same settings, client and count of requests on
// their way as the editor's writing assistant).
#include "ai/ai.h"

#include <QObject>
#include <QPointer>
#include <QString>

namespace brulion::shell {

class Workspace;

class AiAsk : public QObject {
    Q_OBJECT
    Q_MOC_INCLUDE("workspace.h")
    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    /// The AI is set up (its settings usable): asking makes sense.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    /// A question is on its way.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    explicit AiAsk(QObject *parent = nullptr);
    ~AiAsk() override;

    Workspace *workspace() const { return m_workspace; }
    void setWorkspace(Workspace *workspace);
    bool available() const;
    bool busy() const { return m_busy; }

    /// Sends `prompt` (the user's message) with `system` (its instructions). False when the AI is
    /// not set up or the prompt is blank: answered() does not come then.
    Q_INVOKABLE bool ask(const QString &system, const QString &prompt);
    Q_INVOKABLE void stop();

    /// The answer as text: trimmed, one fenced block around all of it taken off (pure; tested).
    static QString cleaned(const QString &text);

Q_SIGNALS:
    void workspaceChanged();
    void availableChanged();
    void busyChanged();
    /// The answer (ok), or why there is none (`text` the error).
    void answered(bool ok, const QString &text);

private:
    void setBusy(bool busy);

    QPointer<Workspace> m_workspace;
    ai::Pending m_pending;
    quint64 m_request = 0;
    bool m_busy = false;
};

} // namespace brulion::shell
