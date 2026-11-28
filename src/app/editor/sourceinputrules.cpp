#include "sourceinputrules.h"

#include "editorinput.h"
#include "inputrules.h"

#include <QEvent>
#include <QTextBlock>
#include <QTextCursor>

namespace brulion::editor {

SourceInputRules::SourceInputRules(QObject *parent) : QObject(parent) { }

void SourceInputRules::setTextDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    setDocument(document ? document->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

void SourceInputRules::setDocument(QTextDocument *document)
{
    if (m_document == document)
        return;
    if (m_document)
        disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    m_pending.reset();
    if (document) {
        m_length = document->characterCount();
        // contentsChange comes with a layout only (a TextEdit's has one; a bare document not yet).
        (void)document->documentLayout();
        connect(document, &QTextDocument::contentsChange, this, &SourceInputRules::changed);
    }
}

void SourceInputRules::setTarget(QObject *target)
{
    if (m_target == target)
        return;
    if (m_target)
        m_target->removeEventFilter(this);
    m_target = target;
    if (target)
        target->installEventFilter(this);
    Q_EMIT targetChanged();
}

void SourceInputRules::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    m_host = host;
    Q_EMIT hostChanged();
}

bool SourceInputRules::eventFilter(QObject *watched, QEvent *event)
{
    // A key's event or an input method's commit on its way to the TextEdit: what it changes in the
    // text is typing — until the event loop's next turn, after the TextEdit has handled it.
    if (watched == m_target && (event->type() == QEvent::KeyPress || event->type() == QEvent::InputMethod)) {
        ++m_typing;
        QMetaObject::invokeMethod(this, [this] { m_typing = std::max(0, m_typing - 1); }, Qt::QueuedConnection);
    }
    return QObject::eventFilter(watched, event);
}

void SourceInputRules::changed(int position, int /*charsRemoved*/, int charsAdded)
{
    // The removed count from the lengths (SourceTextSync: Qt's counts a change reaching the end
    // with the document's last paragraph separator); a change of formats alone replaces nothing.
    const int length = m_document->characterCount();
    const int removed = m_length + charsAdded - length;
    m_length = length;
    if (m_typing == 0 || removed != 0 || !m_host)
        return;
    const QList<InputRule> &rules = m_host->inputRules();
    if (rules.isEmpty())
        return;
    // The match on the caret's line alone: what every keystroke costs.
    const int cursor = position + charsAdded;
    const QTextBlock block = m_document->findBlock(position);
    const int column = cursor - block.position(); // past the line's text: a line break typed, no match
    const std::optional<InputRuleHit> hit = inputRuleMatch(block.text().left(column), column, charsAdded, rules);
    if (!hit)
        return;
    // A rule matched: is the place code, math, a comment, a link's target or the frontmatter?
    // Only now the whole text.
    const int from = block.position() + hit->from;
    if (sourceInputRuleProtected(documentPlainText(m_document), from))
        return;
    m_pending = Pending{from, block.position() + hit->to, block.text().mid(hit->from, hit->to - hit->from),
                        hit->replace, length};
    // After the key's event: no edit of the document while it reports one.
    QMetaObject::invokeMethod(this, &SourceInputRules::applyPending, Qt::QueuedConnection);
}

void SourceInputRules::applyPending()
{
    const std::optional<Pending> pending = std::exchange(m_pending, std::nullopt);
    if (!pending || !m_document || m_document->characterCount() != pending->length)
        return;
    QTextCursor cursor(m_document);
    cursor.setPosition(pending->from);
    cursor.setPosition(pending->to, QTextCursor::KeepAnchor);
    if (cursor.selectedText() != pending->match)
        return; // the text is no longer what the rule saw
    // One edit block: a step of the text's own undo, apart from the typing before and after it.
    // The replacement first, then the typed text out: QTextDocument merges an insertion typed next
    // into a block that ended with an insertion (QTextDocumentPrivate::appendUndoItem), so one undo
    // would take the replacement and what was typed after it; a block that ends with a removal stays
    // a step of its own. (The caret, after the typed text, follows both edits.)
    const int added = int(pending->replace.size());
    cursor.beginEditBlock();
    cursor.setPosition(pending->from);
    cursor.insertText(pending->replace);
    cursor.setPosition(pending->to + added, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
}

} // namespace brulion::editor
