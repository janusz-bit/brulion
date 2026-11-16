#include "sourcetextsync.h"

#include "blocktext.h"
#include "editorinput.h"

#include <QQuickTextDocument>
#include <QScopedValueRollback>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace brulion::editor {

SourceTextSync::SourceTextSync(QObject *parent) : QObject(parent)
{
    m_verifyTimer.setSingleShot(true);
    m_verifyTimer.setInterval(VerifyDelayMs);
    connect(&m_verifyTimer, &QTimer::timeout, this, &SourceTextSync::verify);
}

SourceTextSync::~SourceTextSync()
{
    // (No comparison here: the editor is being torn down, and the controller asked for its text
    // before leaving source mode or saving — sourceTextWanted.)
    disconnect(m_connection);
}

void SourceTextSync::setTextDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    attach(document ? document->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

void SourceTextSync::setDocument(QTextDocument *document)
{
    attach(document);
}

void SourceTextSync::attach(QTextDocument *document)
{
    if (m_document == document)
        return;
    disconnect(m_connection);
    m_document = document;
    if (document) {
        // QTextDocument reports edits only once it has a layout (a TextEdit's always has one).
        (void)document->documentLayout();
        m_connection = connect(document, &QTextDocument::contentsChange, this, &SourceTextSync::onContentsChange);
    }
    restart();
}

void SourceTextSync::setController(PageController *controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (controller) {
        connect(controller, &PageController::sourceTextChanged, this, [this] {
            if (m_pushing)
                ++m_echoes;
        });
        connect(controller, &PageController::sourceTextWanted, this, &SourceTextSync::verify);
    }
    restart();
    Q_EMIT controllerChanged();
}

void SourceTextSync::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    // Switched off while the TextEdit is filled from the controller, on once it holds that text:
    // nothing to compare until the next edit.
    restart();
    Q_EMIT activeChanged();
}

void SourceTextSync::restart()
{
    m_verifyTimer.stop();
    m_length = documentLength();
    m_whole = false;
    m_unverified = false;
}

qsizetype SourceTextSync::documentLength() const
{
    // characterCount() includes the last paragraph separator, which is no text.
    return m_document ? std::max(0, m_document->characterCount() - 1) : 0;
}

QString SourceTextSync::documentText() const
{
    return documentPlainText(m_document);
}

void SourceTextSync::onContentsChange(int position, int /*charsRemoved*/, int charsAdded)
{
    const qsizetype length = documentLength();
    const qsizetype before = std::exchange(m_length, length);
    if (!m_active || !m_controller)
        return;
    if (m_whole) {
        pushWhole(documentText());
        return;
    }
    // Qt counts the document's last paragraph separator (no text) in a change that reaches the
    // end, in both counts: the added count is clamped to the text, the removed one follows from
    // the lengths (Qt's own removed count is not used).
    const qsizetype added = std::min<qsizetype>(charsAdded, length - position);
    const qsizetype removed = before - length + added;
    if (position < 0 || added < 0 || removed < 0) {
        m_whole = true;
        pushWhole(documentText());
        return;
    }
    QString inserted;
    if (added > 0) {
        QTextCursor cursor(m_document);
        cursor.setPosition(position);
        cursor.setPosition(int(position + added), QTextCursor::KeepAnchor);
        inserted = cursor.selectedText();
        mapDocumentSeparators(inserted);
    }
    ++m_edits;
    m_echoes = 0;
    PageController::SourceEditResult result;
    {
        const QScopedValueRollback pushing(m_pushing, true);
        result = m_controller->editSourceText(position, removed, inserted);
    }
    using Result = PageController::SourceEditResult;
    // An edit is echoed once (its own sourceTextChanged); another change of the controller's text
    // while it was handed over (a file changed on disk merged in by the autosave it started) was
    // not loaded into the TextEdit: from now on the TextEdit's whole text, as before.
    const int expected = result == Result::Applied ? 1 : 0;
    if (m_echoes != expected && m_controller) {
        m_whole = true;
        return;
    }
    switch (result) {
    case Result::Applied:
        m_unverified = true;
        m_verifyTimer.start();
        return;
    case Result::Unchanged: return;
    case Result::Refused:
        // The TextEdit keeps the refused edit (as with setSourceText): later edits hand over the
        // whole text, refused again while the page stays read-only.
        m_whole = true;
        return;
    case Result::Unfit:
        m_whole = true;
        pushWhole(documentText());
        return;
    }
}

void SourceTextSync::pushWhole(const QString &text)
{
    if (!m_controller)
        return;
    ++m_wholeTexts;
    const QScopedValueRollback pushing(m_pushing, true);
    m_controller->setSourceText(text);
}

void SourceTextSync::verify()
{
    m_verifyTimer.stop();
    if (!std::exchange(m_unverified, false) || m_whole || !m_active || !m_document || !m_controller
        || !m_controller->sourceMode())
        return;
    const QString shown = documentText();
    if (shownText(m_controller->sourceText()) == shown)
        return;
    ++m_differences;
    qWarning("SourceTextSync: the page's source text differs from the editor's after an edit; "
             "the editor's text is taken");
    m_whole = true;
    pushWhole(shown);
}

} // namespace brulion::editor
