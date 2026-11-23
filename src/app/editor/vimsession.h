// VimSession — Vim keys for one block editor (docs/UI.md "Vim mode"; QML type VimSession of the
// module Brulion, editorforeign.h): the engine of the core module vim over the editor's page
// (VimPageBuffer) or, in source mode, its TextEdit (VimSourceBuffer). BlockEditor gives it the keys
// of its inline editors, its block selection and its source TextEdit before handling them itself;
// what it shows (the mode, the command being typed, the command line, a message) are properties.
//
//   VimSession { id: vim; enabled: AppSettings.vimMode; controller: page }
//   Keys.onPressed: (event) => event.accepted = vim.key(event.key, event.modifiers, event.text)
//
// Registers, the last search and the last change are the app's (one vim::Shared): yank in one
// page, put in another. "+ and "* are the system clipboard.
#pragma once

#include "vimpagebuffer.h"
#include "vimsourcebuffer.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <memory>

class QQuickItem;

namespace brulion::editor {

class PageController;

class VimSession : public QObject {
    Q_OBJECT
    /// Vim keys on (the device setting); off: nothing is taken, nothing changes.
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    Q_PROPERTY(brulion::editor::PageController *controller READ controller WRITE setController NOTIFY controllerChanged
                       FINAL)
    /// Source mode's TextEdit (SourceEditor sets it while it exists).
    Q_PROPERTY(QQuickItem *sourceEdit READ sourceEdit WRITE setSourceEdit NOTIFY sourceEditChanged FINAL)
    /// Lines one screen shows (Ctrl+D / Ctrl+U: half of it).
    Q_PROPERTY(int pageLines READ pageLines WRITE setPageLines NOTIFY pageLinesChanged FINAL)
    /// "normal", "insert", "replace", "visual", "visualLine" ("" while off).
    Q_PROPERTY(QString mode READ mode NOTIFY stateChanged FINAL)
    /// The mode in the UI's language ("NORMAL", "INSERT"…; "" while off).
    Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY stateChanged FINAL)
    /// Keys are commands (normal, visual, replace): the editor does not take typing.
    Q_PROPERTY(bool commands READ commands NOTIFY stateChanged FINAL)
    /// Insert mode: the editor takes typing (only Esc is Vim's).
    Q_PROPERTY(bool typing READ typing NOTIFY stateChanged FINAL)
    /// The keys of a command being typed ("2d", "\"a"), Vim's showcmd.
    Q_PROPERTY(QString pending READ pending NOTIFY stateChanged FINAL)
    /// The command line being typed (":s/a/b", "/żółw"), "" when none.
    Q_PROPERTY(QString commandLine READ commandLine NOTIFY stateChanged FINAL)
    /// What the last command reported ("Not found: …"), "" when nothing; `messageError`: a failure.
    Q_PROPERTY(QString message READ message NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool messageError READ messageError NOTIFY stateChanged FINAL)
    /// "recording @a" while a macro is being recorded (q), "" else.
    Q_PROPERTY(QString recording READ recording NOTIFY stateChanged FINAL)
    /// All of it in one line for a status bar: the command line, else the mode with the pending
    /// keys or the message ("" while off).
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)

public:
    explicit VimSession(QObject *parent = nullptr);
    ~VimSession() override;

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    PageController *controller() const { return m_controller; }
    void setController(PageController *controller);
    QQuickItem *sourceEdit() const { return m_sourceEdit; }
    void setSourceEdit(QQuickItem *edit);
    int pageLines() const { return m_pageLines; }
    void setPageLines(int lines);

    QString mode() const;
    QString modeLabel() const;
    bool commands() const;
    bool typing() const;
    QString pending() const;
    QString commandLine() const;
    QString recording() const;
    QString message() const { return m_message; }
    bool messageError() const { return m_messageError; }
    QString status() const;

    /// A key of the editor (Qt::Key, Qt::KeyboardModifiers, its text): true when Vim took it.
    Q_INVOKABLE bool key(int key, int modifiers, const QString &text);
    /// Keys in Vim's notation ("dw", "ciwnowe<Esc>", ":s/a/b/<CR>"), the self-tests' way in; what
    /// insert mode leaves to the editor is typed into the edited block (or the source text).
    Q_INVOKABLE void keys(const QString &notation);
    /// The answer to viewRequested(): the view's first and last row (positions of the text in source
    /// mode), -1: not known.
    Q_INVOKABLE void setView(int first, int last);
    /// Back to normal mode (a page changed, the setting went off, the editor gave up the keys).
    Q_INVOKABLE void reset();
    /// Insert mode where the caret is (a click into the text while the mode was normal does not
    /// need it: the caret just moves).
    Q_INVOKABLE void startInsert();
    /// Keys came while blocks are selected (the mouse, Shift+↑↓): visual line mode over them,
    /// unless it is Vim's own selection.
    Q_INVOKABLE void adoptBlockSelection();
    /// A text selection made with the mouse in `row` [from, to) (edit text): visual mode over it.
    Q_INVOKABLE void adoptTextSelection(int row, int from, int to);
    /// The same in source mode (positions in the text).
    Q_INVOKABLE void adoptSourceSelection(int from, int to);
    /// The blocks shown as selected are Vim's own: a visual selection's band, or the caret on a
    /// table (which selects it) — not a selection to take over as visual line mode.
    Q_INVOKABLE bool ownsBlockSelection() const;

    // Tests.
    vim::Engine *engine() { return m_engine.get(); }
    VimPageBuffer *pageBuffer() { return &m_page; }
    VimSourceBuffer *sourceBuffer() { return &m_source; }
    /// The app's registers and searches (one for all editors).
    static vim::Shared *shared();
    /// The message for `m` with its argument and counts, in the UI's language.
    static QString messageText(vim::Message m, const QString &arg, int count, int count2);

Q_SIGNALS:
    void enabledChanged();
    void controllerChanged();
    void sourceEditChanged();
    void pageLinesChanged();
    void stateChanged();
    /// Select [anchor → cursor) of `row`'s edit text in its inline editor (row -1: deselect).
    void textSelectionRequested(int row, int anchor, int cursor);
    /// zt zz zb: `row` (blocks) or text `position` (source mode, row -1) to the view's top (-1),
    /// middle (0) or bottom (1).
    void scrollRequested(int row, int position, int where);
    /// H M L ask where the view is: answer with setView() (a direct connection: before this returns).
    void viewRequested();
    /// :q / ZZ — close the page (go back, or close its tab or window).
    void quitRequested(bool force);

private:
    void updateBuffer();
    void publish();
    bool requestView(int &first, int &last);
    vim::Buffer *activeBuffer();

    bool m_enabled = false;
    QPointer<PageController> m_controller;
    QPointer<QQuickItem> m_sourceEdit;
    int m_pageLines = 20;
    VimPageBuffer m_page;
    VimSourceBuffer m_source;
    std::unique_ptr<vim::Engine> m_engine;
    QMetaObject::Connection m_sourceModeConnection;
    QMetaObject::Connection m_pathConnection;
    QString m_message;
    bool m_messageError = false;
    int m_viewFirst = -1;
    int m_viewLast = -1;
    // What publish() last told.
    QString m_lastState;
};

} // namespace brulion::editor
