// EditorInput — text and input-method helpers for the block editor's QML (singleton
// `EditorInput` of the module Brulion, registered in editorforeign.h).
//
//   const t = EditorInput.plainText(edit.textDocument)   // exact text, no-break spaces kept
//   EditorInput.commitPreedit(edit)                       // before Enter/Backspace/Tab/… handling
//
// Why: a QML TextEdit's `text` is QTextDocument::toPlainText(), which turns no-break spaces
// (U+00A0 — Polish typography keeps one-letter words like "w", "z", "i" on the line with them)
// into spaces and U+2028 into line feeds; editing a block must not rewrite its other characters.
// And keys the editor handles itself arrive while an input method may still be composing a word
// (Android keyboards compose every word as preedit text, outside `text` and `cursorPosition`):
// splitting or merging blocks then would lose or misplace that word, so it is committed first.
//
// sendKey() / sendInputMethod() deliver real key and input-method events (the playground's
// self test `brulion --editor --editor-script selftest` drives the editor with them).
#pragma once

#include <QObject>
#include <QString>

class QQuickItem;
class QQuickTextDocument;

namespace brulion::editor {

class EditorInput : public QObject {
    Q_OBJECT

public:
    explicit EditorInput(QObject *parent = nullptr);

    /// The exact plain text of a TextEdit's document: paragraph separators become "\n", every
    /// other character (no-break spaces, U+2028) is kept. "" for null.
    Q_INVOKABLE QString plainText(QQuickTextDocument *document) const;

    /// Commit the input method's pending composition (preedit) of `item` (a TextEdit with the
    /// keyboard) into its text: asks the platform input context, and commits the preedit itself
    /// when no input context took it (some keyboards, headless runs). Returns true when there
    /// was a composition.
    Q_INVOKABLE bool commitPreedit(QQuickItem *item) const;

    /// The keyboard modifiers (Qt.KeyboardModifiers) of the input event being handled — a click's
    /// Shift for handlers whose signal does not carry it (a frame's, a bullet's tap).
    Q_INVOKABLE int keyboardModifiers() const;

    // --- Drivers for tests -------------------------------------------------------------------
    /// Press and release `key` (Qt::Key) with `modifiers` (Qt::KeyboardModifiers) and `text` in
    /// the window of `item`, delivered like a real key to the window's focus item.
    Q_INVOKABLE void sendKey(QQuickItem *item, int key, int modifiers = 0, const QString &text = QString()) const;
    /// An input-method event to `item`: composing text `preedit` (caret at its end) and/or the
    /// committed text `commit`.
    Q_INVOKABLE void sendInputMethod(QQuickItem *item, const QString &preedit, const QString &commit = QString()) const;
};

} // namespace brulion::editor
