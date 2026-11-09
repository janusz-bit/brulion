#include "editorinput.h"

#include "blocktext.h"

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
#include <QVariantMap>

namespace brulion::editor {

EditorInput::EditorInput(QObject *parent) : QObject(parent) {}

void EditorInput::replaceRanges(QQuickTextDocument *document, const QVariantList &ranges,
                                const QString &replacement) const
{
    QTextDocument *text = document ? document->textDocument() : nullptr;
    if (!text || ranges.isEmpty())
        return;
    // A stale range reaching past the text is skipped: the cursor would not move to it, and the
    // replacement would take the text from wherever the cursor is instead.
    const auto inside = [text](int position) { return position >= 0 && position < text->characterCount(); };
    QTextCursor cursor(text);
    cursor.beginEditBlock();
    // From the end: the positions before stay right.
    for (qsizetype i = ranges.size() - 1; i >= 0; --i) {
        const QVariantMap range = ranges.at(i).toMap();
        const int from = range.value(QStringLiteral("from")).toInt();
        const int to = range.value(QStringLiteral("to")).toInt();
        if (!inside(from) || !inside(to))
            continue;
        cursor.setPosition(from);
        cursor.setPosition(to, QTextCursor::KeepAnchor);
        cursor.insertText(replacement);
    }
    cursor.endEditBlock();
}

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
    QWindow *window = item && item->window() ? item->window() : QGuiApplication::focusWindow();
    if (!window)
        return;
    const auto mods = Qt::KeyboardModifiers(modifiers);
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QCoreApplication::sendEvent(window, &release);
}

int EditorInput::keyboardModifiers() const
{
    return int(QGuiApplication::keyboardModifiers());
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

QList<int> EditorInput::changedRange(const QString &before, const QString &after) const
{
    const TextDelta d = TextDelta::between(before, after);
    return {int(d.from), int(d.from + d.inserted.size()), int(d.from + d.removed.size())};
}

int EditorInput::childCount(QQuickItem *item) const
{
    return item ? int(item->childItems().size()) : 0;
}

QQuickItem *EditorInput::childAt(QQuickItem *item, int index) const
{
    return item ? item->childItems().value(index) : nullptr; // value(): null out of range
}

} // namespace brulion::editor
