// SourceInputRules — extensions' input rules (b.app.inputRule, docs/UI.md "Input rules") in source
// mode's TextEdit (SourceEditor.qml; QML type `SourceInputRules` of the module Brulion, registered
// in editorforeign.h).
//
//   SourceInputRules { textDocument: source.textDocument; target: source; host: root.controller.host }
//
// The block editor applies them in PageController::setEditText(); source mode's TextEdit keeps its
// own text and undo, so the rules watch it here:
// * typing only: a change of the text while a key's event (or an input method's commit) is
//   delivered to `target` — an insertion ending at the caret; an edit made by code (find and
//   replace, a spelling correction, the toolbar's inserts, Vim's commands) never fires one;
// * cheap per keystroke: the match on the caret's line alone (inputRuleMatch); the whole text is
//   read — for fences, code spans, formulas and the frontmatter (sourceInputRuleProtected) — only
//   when a rule matched;
// * a step of its own: the replacement, made after the key's event (the document must still be
//   what the rule saw), is one edit block of QTextDocument's undo — one undo brings the typed text
//   back, and what is typed after it is another step.
#pragma once

#include "editorhost.h"

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>

#include <optional>

namespace brulion::editor {

class SourceInputRules : public QObject {
    Q_OBJECT
    Q_PROPERTY(
            QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged FINAL)
    Q_PROPERTY(QObject *target READ target WRITE setTarget NOTIFY targetChanged FINAL)
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)

public:
    explicit SourceInputRules(QObject *parent = nullptr);

    QQuickTextDocument *textDocument() const { return m_quickDocument; }
    void setTextDocument(QQuickTextDocument *document);
    /// The document itself (the QML property's, or one a test makes).
    void setDocument(QTextDocument *document);
    /// What receives the keys: its key and input-method events are the typing that fires a rule.
    QObject *target() const { return m_target; }
    void setTarget(QObject *target);
    /// Whose rules (EditorHost::inputRules(), every extension's).
    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);

Q_SIGNALS:
    void textDocumentChanged();
    void targetChanged();
    void hostChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// The replacement to make once the key's event is over: [from, to) holding `match`, in a
    /// document of `length` characters (what the rule saw).
    struct Pending {
        int from = 0;
        int to = 0;
        QString match;
        QString replace;
        int length = 0;
    };
    void changed(int position, int charsRemoved, int charsAdded);
    void applyPending();

    QPointer<QQuickTextDocument> m_quickDocument;
    QPointer<QTextDocument> m_document;
    QPointer<QObject> m_target;
    QPointer<EditorHost> m_host;
    std::optional<Pending> m_pending;
    int m_length = 0; ///< the document's characterCount() after the last change
    int m_typing = 0; ///< key or input-method events being delivered to the target
};

} // namespace brulion::editor
