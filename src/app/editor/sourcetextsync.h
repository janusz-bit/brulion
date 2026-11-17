// SourceTextSync — source mode's TextEdit (SourceEditor.qml) and PageController::sourceText kept
// in step one edit at a time (QML type `SourceTextSync` of the module Brulion, registered in
// editorforeign.h).
//
//   SourceTextSync { id: sync; textDocument: source.textDocument; controller: root.controller }
//   sync.active = false; source.text = root.controller.sourceText; sync.active = true
//
// Why: every keystroke used to copy the TextEdit's whole text (EditorInput.plainText), compare it
// with the previous copy and map its line breaks back onto the file's (restoreLineEndings) — and
// so did every highlighting pass, which a TextEdit reports as a text change too: O(page) per
// keystroke, 18 ms for a 500 KB page on a Raspberry Pi 4 before a frame was drawn
// (docs/PERFORMANCE.md "Source mode: one edit, not the page"). QTextDocument::contentsChange says
// where the text changed, so only that edit is handed over (PageController::editSourceText,
// sourceEditOf() in blocktext.h): O(edit) — what QOwnNotes does for its Markdown language server.
//
// Of a change only its position and its added count are taken from Qt: the removed count follows
// from the document's lengths before and after (a change that reaches the end counts the
// document's last paragraph separator, which is no text, in both). A change of formats only
// replaces a range with the same text, which changes nothing.
//
// Safety net: an edit the controller refuses or cannot place switches the rest of the session back
// to handing over the whole text (setSourceText, as before), and so does a change of the
// controller's text while an edit was being handed over. After a pause in typing (VerifyDelayMs)
// and before the controller saves the text or leaves source mode with it (sourceTextWanted), the
// controller's text is compared with the TextEdit's once: a difference (none is known) is logged,
// the TextEdit's text taken and the session hands over whole texts from then on.
#pragma once

#include "editorhost.h"
#include "pagecontroller.h"

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QTimer>

namespace brulion::editor {

class SourceTextSync : public QObject {
    Q_OBJECT
    Q_PROPERTY(
            QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged FINAL)
    Q_PROPERTY(brulion::editor::PageController *controller READ controller WRITE setController NOTIFY controllerChanged
                       FINAL)
    /// Edits are handed over only while active: off while the TextEdit is filled from the controller.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    /// True while an edit is being handed to the controller: its sourceTextChanged is that edit's
    /// echo, not a text to load into the TextEdit.
    Q_PROPERTY(bool pushing READ pushing FINAL)

public:
    /// The controller's text is compared with the TextEdit's this long after the last edit —
    /// before the autosave (PageController's default delay is 400 ms) writes it.
    static constexpr int VerifyDelayMs = 250;

    explicit SourceTextSync(QObject *parent = nullptr);
    ~SourceTextSync() override;

    QQuickTextDocument *textDocument() const { return m_quickDocument; }
    void setTextDocument(QQuickTextDocument *document);
    /// A QTextDocument directly (tests, C++ users) instead of a TextEdit's.
    void setDocument(QTextDocument *document);
    QTextDocument *document() const { return m_document; }
    PageController *controller() const { return m_controller; }
    void setController(PageController *controller);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool pushing() const { return m_pushing; }

    /// If an edit came since the last comparison: compare the controller's text with the
    /// TextEdit's now (the timer and stopping do this; a difference is logged and repaired).
    Q_INVOKABLE void verify();

    /// For tests: edits handed over one by one, whole texts handed over, differences found.
    int edits() const { return m_edits; }
    int wholeTexts() const { return m_wholeTexts; }
    int differences() const { return m_differences; }
    /// True once the session hands over whole texts (after a refused or misplaced edit).
    bool handsOverWholeText() const { return m_whole; }

Q_SIGNALS:
    void textDocumentChanged();
    void controllerChanged();
    void activeChanged();

private:
    void attach(QTextDocument *document);
    void restart();
    void onContentsChange(int position, int charsRemoved, int charsAdded);
    void pushWhole(const QString &text);
    QString documentText() const;
    qsizetype documentLength() const;

    QPointer<QQuickTextDocument> m_quickDocument;
    QPointer<QTextDocument> m_document;
    QPointer<PageController> m_controller;
    QMetaObject::Connection m_connection;
    QTimer m_verifyTimer;
    qsizetype m_length = 0; ///< the document's text length after the last change seen
    bool m_active = false;
    bool m_pushing = false;
    bool m_whole = false;      ///< the rest of this session hands over whole texts
    bool m_unverified = false; ///< an edit came since the last comparison
    int m_echoes = 0;          ///< the controller's sourceTextChanged while an edit is handed over
    int m_edits = 0;
    int m_wholeTexts = 0;
    int m_differences = 0;
};

} // namespace brulion::editor
