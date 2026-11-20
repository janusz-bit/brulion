#pragma once
// WritingSession — one request of the editor's AI assistant (docs/UI.md "AI in the editor"; QML:
// `WritingSession { workspace: Workspace }`, BlockEditor's WritingDialog): the text the editor
// took — the selection, or the block or paragraph at the caret — rewritten after an instruction or
// a preset, continued or summed up by the model of Settings → Extensions → AI (core writing/
// writing.h; sent through WorkspaceExtensions::sendAi, the model on the device too). The answer
// shows as it is written (`result`, cleaned for the note as it comes), with the change word by
// word (changesHtml); nothing goes into the note but by the editor on Accept (edit(): where the
// result goes, one undo step there). Retry asks again; Stop keeps what came.
//
//   session.startInline("rewrite", "shorter", row, editText, from, to, page)   // a block's text
//   session.startBlocks("summarize", "", selectionMarkdown, lastRow, page)     // selected blocks
//   session.startSource("continue", "", sourceText, from, to, page)            // source mode
//   session.state: "" → "waiting" → "writing" → "done" | "stopped" | "failed"
//   session.change() → {from, to, text} of baseText  // then PageController.pasteText(row, …)
// A Rewrite without a preset waits for one (`choosing`) — rewriteWith(preset, words) asks then.
//
// Single-threaded (the GUI thread).

#include "ai/ai.h"
#include "writing/writing.h"

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace brulion::shell {

class Workspace;

class WritingSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    /// "" (nothing asked) | "waiting" (asked, nothing written yet) | "writing" | "done" | "stopped"
    /// (Stop: what came is kept) | "failed" (`error` says why).
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    /// The request's action ("rewrite" | "continue" | "summarize"), preset ("" none) and
    /// instruction (the user's words, or the preset's label).
    Q_PROPERTY(QString action READ action NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString preset READ preset NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString instruction READ instruction NOTIFY stateChanged FINAL)
    /// The text sent (the selection, or the block or paragraph).
    Q_PROPERTY(QString original READ original NOTIFY stateChanged FINAL)
    /// The answer as it goes into the note (writing::clean), as it comes.
    Q_PROPERTY(QString result READ result NOTIFY resultChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    /// The model is the one on the device ("waiting" may be a wait for another answer of it).
    Q_PROPERTY(bool onDevice READ onDevice NOTIFY stateChanged FINAL)
    /// Rewrite's presets, in the menu's order (writing::presets()).
    Q_PROPERTY(QStringList presets READ presets CONSTANT FINAL)
    /// What the result goes into: "inline" (`row`'s edit text), "blocks" (the selected blocks, the
    /// last of them `row`), "source" (source mode's text); "" not started by the editor.
    Q_PROPERTY(QString target READ target NOTIFY stateChanged FINAL)
    Q_PROPERTY(int row READ row NOTIFY stateChanged FINAL)
    /// "blocks": the first selected block (the depth a summary or a continuation goes at).
    Q_PROPERTY(int firstRow READ firstRow NOTIFY stateChanged FINAL)
    /// The text the change goes into as it was taken (the editor checks it is still so).
    Q_PROPERTY(QString baseText READ baseText NOTIFY stateChanged FINAL)
    /// A Rewrite waits for its preset or the user's words (nothing asked yet).
    Q_PROPERTY(bool choosing READ choosing NOTIFY stateChanged FINAL)

public:
    explicit WritingSession(QObject *parent = nullptr);
    ~WritingSession() override;

    Workspace *workspace() const { return m_workspace; }
    void setWorkspace(Workspace *workspace);
    QString state() const { return m_state; }
    QString action() const { return writing::actionName(m_input.action); }
    QString preset() const { return m_preset; }
    QString instruction() const { return m_instruction; }
    QString original() const { return m_input.text; }
    QString result() const { return m_result; }
    QString error() const { return m_error; }
    bool onDevice() const;
    QStringList presets() const { return writing::presets(); }
    QString target() const { return m_target; }
    int row() const { return m_row; }
    int firstRow() const { return m_firstRow; }
    QString baseText() const { return m_base; }
    bool choosing() const { return m_choosing; }

    /// The editor's requests: `action` on `row`'s edit text `editText` — its [from, to), the whole
    /// of it when they meet, up to the caret for a continuation …
    Q_INVOKABLE bool startInline(const QString &action, const QString &preset, int row, const QString &editText,
                                 int from, int to, const QString &page);
    /// … the selected blocks (`markdown`, from `firstRow` to `lastRow`) …
    Q_INVOKABLE bool startBlocks(const QString &action, const QString &preset, const QString &markdown, int firstRow,
                                 int lastRow, const QString &page);
    /// … source mode's text: [from, to), or the paragraph at the caret when they meet. A Rewrite
    /// without a preset asks nothing yet (`choosing`).
    Q_INVOKABLE bool startSource(const QString &action, const QString &preset, const QString &source, int from, int to,
                                 const QString &page);
    /// Where the result goes in baseText: {from, to, text} (writing::edit), and its parts typed.
    Q_INVOKABLE QVariantMap change() const;
    Q_INVOKABLE int changeFrom() const;
    Q_INVOKABLE int changeTo() const;
    Q_INVOKABLE QString changeText() const;

    /// Asks the model: `action` on `text` (`before` / `after` the text around it in the note,
    /// `page` its name; `inlineText`: a part of one block's text); a Rewrite after `preset` (one of
    /// presets()) or else `instruction`. False, with `error`, when it cannot be asked (no text, no
    /// instruction, no model set up, no notebook).
    Q_INVOKABLE bool start(const QString &action, const QString &preset, const QString &instruction,
                           const QString &text, const QString &before, const QString &after, const QString &page,
                           bool inlineText);
    /// The same request again (another answer).
    Q_INVOKABLE bool retry();
    /// A Rewrite of the same text after another preset or instruction (the one `choosing` waits for).
    Q_INVOKABLE bool rewriteWith(const QString &preset, const QString &instruction);
    /// Ends the request on its way: what came stays (state "stopped"), nothing came: "".
    Q_INVOKABLE void stop();
    /// Forgets everything (the dialog closed).
    Q_INVOKABLE void clear();
    /// The result can go into the note (done, or stopped with something written).
    Q_INVOKABLE bool canAccept() const;
    /// A preset's name in the UI ("Shorter", "Translate to English").
    Q_INVOKABLE QString presetLabel(const QString &preset) const;
    /// The change from `original` to `result` as rich text, escaped: removed words struck through in
    /// `removedColor`, added ones on `addedBackground`, line breaks kept.
    Q_INVOKABLE QString changesHtml(const QColor &removedColor, const QColor &addedBackground) const;
    /// Where the result goes in `text` whose [from, to) was sent (writing::edit): {from, to, text}.
    Q_INVOKABLE QVariantMap edit(const QString &text, int from, int to) const;

Q_SIGNALS:
    void workspaceChanged();
    void stateChanged();
    void resultChanged();

private:
    bool send();
    /// The editor's request set up: asked now, or (a Rewrite without a preset) when chosen.
    bool begin(const QString &action, const QString &preset, const QString &text, const QString &before,
               const QString &after, const QString &page, bool inlineText);
    void setState(const QString &state, const QString &error = QString());
    void setResult(const QString &result);

    QPointer<Workspace> m_workspace;
    writing::Input m_input;
    QString m_preset;
    QString m_instruction;
    QString m_state;
    QString m_result;
    QString m_error;
    QString m_raw; // the answer so far as the model wrote it
    QString m_target;
    int m_row = -1;
    int m_firstRow = -1;
    QString m_base;
    qsizetype m_from = 0;
    qsizetype m_to = 0;
    bool m_choosing = false;
    ai::Pending m_pending;
    quint64 m_request = 0; // the request on its way (an answer to an older one is dropped)
    QTimer m_partialTimer;
};

} // namespace brulion::shell
