// SourceInputRules: extensions' input rules (b.app.inputRule, docs/UI.md "Input rules") in source
// mode's TextEdit — the text typed before the caret replaced as a step of the text's own undo (one
// undo brings the typed text back), never on a paste, an edit made by code, in code, math, a comment,
// a link's target, a URL or the frontmatter. The TextEdit here is a QObject that edits its
// QTextDocument as a TextEdit does: a key's text inserted at its cursor while the key event is
// delivered (the helper watches the events, as on SourceEditor.qml's TextEdit).
#include "fakeeditorhost.h"
#include "sourceinputrules.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QSignalSpy>
#include <QTest>
#include <QTextCursor>
#include <QTextDocument>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

/// A TextEdit's typing: a key's text (or an input method's commit) goes in at the cursor.
class FakeTextEdit : public QObject {
public:
    explicit FakeTextEdit(QTextDocument *doc) : cursor(doc) { }
    /// The caret, as a TextEdit keeps it: a QTextCursor of the document, moved by others' edits.
    QTextCursor cursor;

protected:
    bool event(QEvent *e) override
    {
        QString text;
        if (e->type() == QEvent::KeyPress)
            text = static_cast<QKeyEvent *>(e)->text();
        else if (e->type() == QEvent::InputMethod)
            text = static_cast<QInputMethodEvent *>(e)->commitString();
        else
            return QObject::event(e);
        cursor.insertText(text);
        return true;
    }
};

struct Fixture {
    FakeEditorHost host;
    QTextDocument doc;
    FakeTextEdit edit{&doc};
    SourceInputRules rules;

    explicit Fixture(const QString &text)
    {
        doc.setPlainText(text);
        edit.cursor.movePosition(QTextCursor::End);
        host.addInputRule(u"->"_s, u"→"_s);
        host.addInputRule(u"-->"_s, u"⟶"_s);
        host.addInputRule(u"zolw"_s, u"żółw"_s, true);
        rules.setHost(&host);
        rules.setDocument(&doc);
        rules.setTarget(&edit);
    }
    /// Types `text` one key at a time, then lets the replacement (made after the key's event) in.
    void type(const QString &text)
    {
        for (const QChar c : text) {
            QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, QString(c));
            QCoreApplication::sendEvent(&edit, &key);
            QCoreApplication::processEvents();
        }
    }
    QString text() const { return doc.toPlainText(); }
};

} // namespace

class TstSourceInputRules : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void typingReplacesAndUndoBringsTheTypedTextBack()
    {
        Fixture f(u"# Notatka\n\n- Dom "_s);
        f.type(u"->"_s);
        QCOMPARE(f.text(), u"# Notatka\n\n- Dom →"_s);
        f.doc.undo();
        QCOMPARE(f.text(), u"# Notatka\n\n- Dom ->"_s);
        f.doc.redo();
        QCOMPARE(f.text(), u"# Notatka\n\n- Dom →"_s);
    }

    void typingAfterTheReplacementIsAStepOfItsOwn()
    {
        Fixture f(u"a "_s);
        f.type(u"-> b"_s);
        QCOMPARE(f.text(), u"a → b"_s);
        f.doc.undo();
        QCOMPARE(f.text(), u"a →"_s);
        f.doc.undo();
        QCOMPARE(f.text(), u"a ->"_s);
    }

    void theLongestWinsAndTheCaretFollows()
    {
        Fixture f(u"x "_s);
        f.type(u"--> y"_s);
        // At ">" both "->" and "-->" end at the caret: the longer one replaces.
        QCOMPARE(f.text(), u"x ⟶ y"_s);
        QCOMPARE(f.edit.cursor.position(), int(f.text().size()));
    }

    void wholeWordKeepsTheCharacterTyped()
    {
        Fixture f(u"Mój "_s);
        f.type(u"zolw jest"_s);
        QCOMPARE(f.text(), u"Mój żółw jest"_s);
        Fixture inside(u"Mój "_s);
        inside.type(u"zolwik "_s);
        QCOMPARE(inside.text(), u"Mój zolwik "_s);
    }

    void neverInCodeMathTheFrontmatterOrAComment()
    {
        Fixture fence(u"Tekst\n\n```luau\nprint(1 "_s);
        fence.type(u"->"_s);
        QCOMPARE(fence.text(), u"Tekst\n\n```luau\nprint(1 ->"_s);

        Fixture front(u"---\ntytuł: a\n---\nTreść\n"_s);
        front.edit.cursor.setPosition(12); // after "tytuł: a"
        front.type(u" ->"_s);
        QCOMPARE(front.text(), u"---\ntytuł: a ->\n---\nTreść\n"_s);

        Fixture span(u"Akapit `kod\ndalej "_s);
        span.type(u"->"_s);
        QCOMPARE(span.text(), u"Akapit `kod\ndalej ->"_s);

        Fixture after(u"```\nkod\n```\nza nim "_s);
        after.type(u"->"_s);
        QCOMPARE(after.text(), u"```\nkod\n```\nza nim →"_s);
    }

    void neverOnAPasteOrAnEditOfCode()
    {
        Fixture paste(u"a "_s);
        QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, u"b ->"_s); // more typed at once than the match
        QCoreApplication::sendEvent(&paste.edit, &key);
        QCoreApplication::processEvents();
        QCOMPARE(paste.text(), u"a b ->"_s);

        // An edit no key made (find and replace, a spelling correction, the toolbar's insert).
        Fixture code(u"a -"_s);
        QTextCursor c(&code.doc);
        c.movePosition(QTextCursor::End);
        c.insertText(u">"_s);
        QCoreApplication::processEvents();
        QCOMPARE(code.text(), u"a ->"_s);
    }

    void typingEndsWithItsEvent()
    {
        // Keys typed earlier: a later edit by code (another turn of the event loop) is no typing.
        Fixture f(u"a "_s);
        f.type(u"x -"_s);
        QTextCursor c(&f.doc);
        c.movePosition(QTextCursor::End);
        c.insertText(u">"_s);
        QCoreApplication::processEvents();
        QCOMPARE(f.text(), u"a x ->"_s);
        // Nor is an edit while another kind of event reaches the TextEdit.
        Fixture other(u"a -"_s);
        QEvent focus(QEvent::FocusIn);
        QCoreApplication::sendEvent(&other.edit, &focus);
        other.edit.cursor.insertText(u">"_s);
        QCoreApplication::processEvents();
        QCOMPARE(other.text(), u"a ->"_s);
    }

    void anotherDocumentDropsAPendingReplacement()
    {
        Fixture f(u"a -"_s);
        QTextDocument second;
        second.setPlainText(u"ą ->"_s); // as long as the first, "->" where the rule saw it
        QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, u">"_s);
        QCoreApplication::sendEvent(&f.edit, &key); // a replacement pending in the first …
        f.rules.setDocument(&second);               // … which is no longer watched
        QCoreApplication::processEvents();
        QCOMPARE(f.text(), u"a ->"_s);
        QCOMPARE(second.toPlainText(), u"ą ->"_s);
    }

    void aRealTextEdit()
    {
        // QtQuick's own TextEdit: its textDocument property and its handling of the keys.
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { text: \"Pół \" }", QUrl());
        std::unique_ptr<QObject> edit(component.create());
        QVERIFY2(edit, qPrintable(component.errorString()));
        auto *document = edit->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(document);
        FakeEditorHost host;
        host.addInputRule(u"->"_s, u"→"_s);
        SourceInputRules rules;
        rules.setHost(&host);
        rules.setTextDocument(document);
        rules.setTarget(edit.get());
        QCOMPARE(rules.textDocument(), document);
        edit->setProperty("cursorPosition", 4);
        for (const QString &c : {u"-"_s, u">"_s}) {
            QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, c);
            QCoreApplication::sendEvent(edit.get(), &key);
            QCoreApplication::processEvents();
        }
        QCOMPARE(edit->property("text").toString(), u"Pół →"_s);
        QCOMPARE(edit->property("cursorPosition").toInt(), 5);
        QMetaObject::invokeMethod(edit.get(), "undo");
        QCOMPARE(edit->property("text").toString(), u"Pół ->"_s);
    }

    void anInputMethodsCommitIsTyping()
    {
        Fixture f(u"a -"_s);
        QInputMethodEvent commit;
        commit.setCommitString(u">"_s);
        QCoreApplication::sendEvent(&f.edit, &commit);
        QCoreApplication::processEvents();
        QCOMPARE(f.text(), u"a →"_s);
    }

    void textChangedBeforeTheReplacementIsLeftAlone()
    {
        // The replacement comes after the key's event: an edit in between (the document no longer
        // what the rule saw) cancels it.
        Fixture f(u"a -"_s);
        QKeyEvent key(QEvent::KeyPress, 0, Qt::NoModifier, u">"_s);
        QCoreApplication::sendEvent(&f.edit, &key);
        QTextCursor c(&f.doc);
        c.movePosition(QTextCursor::End);
        c.insertText(u"!"_s);
        QCoreApplication::processEvents();
        QCOMPARE(f.text(), u"a ->!"_s);
    }

    void noRulesNoHostNothing()
    {
        Fixture f(u"a "_s);
        f.host.clearInputRules();
        f.type(u"->"_s);
        QCOMPARE(f.text(), u"a ->"_s);
        Fixture none(u"a "_s);
        none.rules.setHost(nullptr);
        none.type(u"->"_s);
        QCOMPARE(none.text(), u"a ->"_s);
    }
};

QTEST_MAIN(TstSourceInputRules)
#include "tst_editor_sourceinputrules.moc"
