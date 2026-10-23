#include "editorinput.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QQuickWindow>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace brulion::editor {

EditorInput::EditorInput(QObject *parent) : QObject(parent) {}

QString EditorInput::plainText(QQuickTextDocument *document) const
{
    if (!document || !document->textDocument())
        return {};
    QString text = document->textDocument()->toRawText();
    for (QChar &c : text) {
        // Block separators and frame markers of QTextDocument (what toPlainText() maps too).
        if (c == QChar::ParagraphSeparator || c.unicode() == 0xfdd0 || c.unicode() == 0xfdd1)
            c = u'\n';
    }
    return text;
}

QString EditorInput::textRange(QQuickTextDocument *document, int from, int to) const
{
    if (!document || !document->textDocument())
        return {};
    QTextDocument *doc = document->textDocument();
    const int end = std::max(0, doc->characterCount() - 1);
    from = std::clamp(from, 0, end);
    to = std::clamp(to, 0, end);
    if (to < from)
        std::swap(from, to);
    QTextCursor cursor(doc);
    cursor.setPosition(from);
    cursor.setPosition(to, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText();
    for (QChar &c : text) {
        // Paragraph and line separators, frame markers (a table's cells).
        if (c == QChar::ParagraphSeparator || c == QChar::LineSeparator || c.unicode() == 0xfdd0
            || c.unicode() == 0xfdd1)
            c = u'\n';
    }
    return text;
}

bool EditorInput::commitPreedit(QQuickItem *item) const
{
    if (!item)
        return false;
    const QString preedit = item->property("preeditText").toString();
    if (preedit.isEmpty())
        return false;
    if (QGuiApplication::focusObject() == item)
        QGuiApplication::inputMethod()->commit();
    if (!item->property("preeditText").toString().isEmpty()) {
        // No input context took it: commit the composition ourselves, then let the input method
        // forget it (it would otherwise keep composing a word that is now text).
        QInputMethodEvent event;
        event.setCommitString(preedit);
        QCoreApplication::sendEvent(item, &event);
        QGuiApplication::inputMethod()->reset();
    }
    return true;
}

void EditorInput::sendKey(QQuickItem *item, int key, int modifiers, const QString &text) const
{
    QWindow *window = item && item->window() ? static_cast<QWindow *>(item->window()) : QGuiApplication::focusWindow();
    if (!window)
        return;
    const auto mods = Qt::KeyboardModifiers(modifiers);
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QCoreApplication::sendEvent(window, &release);
}

void EditorInput::sendInputMethod(QQuickItem *item, const QString &preedit, const QString &commit) const
{
    if (!item)
        return;
    QList<QInputMethodEvent::Attribute> attributes;
    if (!preedit.isEmpty())
        attributes.append({QInputMethodEvent::Cursor, int(preedit.size()), 1, QVariant()});
    QInputMethodEvent event(preedit, attributes);
    if (!commit.isEmpty())
        event.setCommitString(commit);
    QCoreApplication::sendEvent(item, &event);
}

} // namespace brulion::editor
