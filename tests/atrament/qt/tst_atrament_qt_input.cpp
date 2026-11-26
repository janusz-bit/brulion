// The Qt host's input-method bridge (src/atrament/qt/qthost.h): AtramentItem's answers to
// Qt's input-method queries and how it applies QInputMethodEvents, as Qt's own text controls do —
// the composition left out of the text Qt is told about, replacement ranges around the caret,
// Selection attributes — driven with the event sequences Qt's Android input context sends for
// Gboard (setComposingText, commitText, deleteSurroundingText, setComposingRegion, replaceText,
// finishComposingText) and a desktop input method's preedit; the queries with an argument; the
// signal Android listens to; a press or another field ending a composition.
#include "qthost.h"
#include "text/fonts.h"
#include "ui/atrament.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTextCharFormat>
#include <QtTest>

#include <memory>
#include <string>

using namespace brulion::atrament;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        f->addFontFile(BRULION_SOURCE_DIR "/assets/fonts/ui/BrulionInter-Regular.ttf");
        return f;
    }();
    return fonts;
}

QString q(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), qsizetype(s.size()));
}

struct View {
    Entity<TextInput> search;
    Entity<TextInput> note;

    explicit View(Context<View> &cx)
        : search(cx.newEntity<TextInput>(
                  TextInputOptions{u"Szukaj…", false, InputHandler::Purpose::Text, {}, u"Szukaj"}))
        , note(cx.newEntity<TextInput>(
                  TextInputOptions{u"Napisz coś…", true, InputHandler::Purpose::Multiline, {}, u"Notatka"}))
    { }

    AnyElement render(Window &, Context<View> &)
    {
        return div()
                .sizeFull()
                .flexCol()
                .p(10)
                .gap(10)
                .textSize(16)
                .child(div().h(30).child(search))
                .child(div().h(80).child(note));
    }
};

using Attributes = QList<QInputMethodEvent::Attribute>;

struct Fixture {
    std::shared_ptr<QtPlatform> platform = std::make_shared<QtPlatform>();
    App app{platform, loadFonts()};
    Entity<View> view = app.newEntity<View>();
    QQuickWindow window;
    AtramentItem *item = nullptr;

    Fixture()
    {
        window.resize(320, 200);
        item = new AtramentItem(window.contentItem());
        item->setSize(QSizeF(320, 200));
        item->attach(app, AnyView{view.id(), &renderView<View>});
        window.show();
        item->forceActiveFocus();
        draw();
    }
    ~Fixture() { delete item; }
    Window &atrament() { return *item->atramentWindow(); }
    void draw()
    {
        atrament().refresh();
        atrament().draw();
    }
    void click(QPoint at)
    {
        QTest::mouseClick(&window, Qt::LeftButton, {}, at);
        draw();
    }
    const TextInput &search() { return view.read(app).search.read(app); }
    const TextInput &note() { return view.read(app).note.read(app); }
    // An input method's event, then the frame it gives.
    void ime(const QString &preedit, const Attributes &attributes, const QString &commit = {}, int from = 0,
             int length = 0)
    {
        QInputMethodEvent e(preedit, attributes);
        if (!commit.isEmpty() || length != 0 || from != 0)
            e.setCommitString(commit, from, length);
        QCoreApplication::sendEvent(item, &e);
        draw();
    }
    // What Qt's Android input context sends for composing text (setComposingText).
    void compose(const QString &text) { compose(text, int(text.size())); }
    void compose(const QString &text, int cursor)
    {
        QTextCharFormat underline;
        underline.setFontUnderline(true);
        ime(text,
            {{QInputMethodEvent::TextFormat, 0, int(text.size()), underline}, {QInputMethodEvent::Cursor, cursor, 1}});
    }
    // finishComposingText (focusObjectStopComposing): the commit, then the caret where it was.
    void finish(const QString &composing, int caret)
    {
        ime({}, {}, composing);
        ime({}, {{QInputMethodEvent::Selection, caret, 0}});
    }
    void select(int anchor, int focus) { ime({}, {{QInputMethodEvent::Selection, anchor, focus - anchor}}); }
    QVariant query(Qt::InputMethodQuery query) { return item->inputMethodQuery(query); }
    QVariant query(Qt::InputMethodQuery query, const QVariant &argument)
    {
        // As QInputMethod::queryFocusObject calls it (by name, through the meta-object).
        QVariant result;
        const bool ok = QMetaObject::invokeMethod(item, "inputMethodQuery", Qt::DirectConnection,
                                                  Q_RETURN_ARG(QVariant, result), Q_ARG(Qt::InputMethodQuery, query),
                                                  Q_ARG(QVariant, argument));
        return ok ? result : QVariant(QStringLiteral("not invokable"));
    }
    void undo()
    {
        QTest::keyClick(&window, Qt::Key_Z, Qt::ControlModifier);
        draw();
    }
};

const QPoint kSearch(60, 25);
const QPoint kNote(60, 70);

} // namespace

class TestAtramentQtInput : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void enabledOnlyInAField();
    void queriesLeaveTheCompositionOut();
    void queriesWithArguments();
    void gboardTypesAWord();
    void gboardBackspacesIntoAWord();
    void gboardReplacesAWord();
    void deletionAroundTheCaret();
    void desktopPreedit();
    void signalForEveryChange();
    void pressEndsTheComposition();
    void anotherFieldEndsTheComposition();
};

void TestAtramentQtInput::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
}

void TestAtramentQtInput::enabledOnlyInAField()
{
    Fixture f;
    QVERIFY(f.item->hasActiveFocus());
    QCOMPARE(f.query(Qt::ImEnabled).toBool(), false);
    QVERIFY(!(f.item->flags() & QQuickItem::ItemAcceptsInputMethod));
    f.click(kSearch);
    QCOMPARE(f.query(Qt::ImEnabled).toBool(), true);
    QVERIFY(f.item->flags() & QQuickItem::ItemAcceptsInputMethod);
    QCOMPARE(f.query(Qt::ImHints).toInt(), int(Qt::ImhNone));
    // Qt's Android input context drops the keyboard's calls to a focus object without this property.
    QVERIFY(f.item->property("inputMethodHints").isValid());
    QCOMPARE(f.query(Qt::ImReadOnly).toBool(), false);
    QSignalSpy hints(f.item, SIGNAL(inputMethodHintsChanged()));
    f.click(kNote);
    QCOMPARE(f.query(Qt::ImHints).toInt(), int(Qt::ImhMultiLine));
    QCOMPARE(f.item->property("inputMethodHints").value<Qt::InputMethodHints>(), Qt::ImhMultiLine);
    QCOMPARE(hints.count(), 1);
    // Without a field the item has nothing to say to an input method; it does not eat its events.
    f.atrament().blur();
    f.draw();
    QCOMPARE(f.query(Qt::ImEnabled).toBool(), false);
    QInputMethodEvent e(QString(), {});
    e.setCommitString(QStringLiteral("x"));
    QCoreApplication::sendEvent(f.item, &e);
    QVERIFY(!e.isAccepted());
    QCOMPARE(q(f.note().text()), QString());
}

void TestAtramentQtInput::queriesLeaveTheCompositionOut()
{
    Fixture f;
    f.click(kSearch);
    f.ime({}, {}, QStringLiteral("Zażółć "));
    f.compose(QStringLiteral("gęś"), 2);
    // Atrament: the composition is in the text, marked, the caret inside it.
    QCOMPARE(q(f.search().text()), QStringLiteral("Zażółć gęś"));
    QCOMPARE(f.search().markedRange(), std::optional<Range>(Range{7, 10}));
    QCOMPARE(f.search().selection().focus, 9);
    // Qt: the text without it, the caret and the anchor where it sits.
    QCOMPARE(f.query(Qt::ImSurroundingText).toString(), QStringLiteral("Zażółć "));
    QCOMPARE(f.query(Qt::ImCursorPosition).toInt(), 7);
    QCOMPARE(f.query(Qt::ImAnchorPosition).toInt(), 7);
    QCOMPARE(f.query(Qt::ImAbsolutePosition).toInt(), 7);
    QCOMPARE(f.query(Qt::ImTextBeforeCursor).toString(), QStringLiteral("Zażółć "));
    QCOMPARE(f.query(Qt::ImTextAfterCursor).toString(), QString());
    QCOMPARE(f.query(Qt::ImCurrentSelection).toString(), QString());
    // The cursor rectangle is the caret on screen, inside the composition.
    const QRectF caret = f.query(Qt::ImCursorRectangle).toRectF();
    const std::optional<Rect> at7 = f.search().boundsForRange({7, 7});
    QVERIFY(at7);
    QVERIFY(caret.height() > 10);
    QVERIFY(caret.x() > at7->left() + 4);
    // Committed: the whole text again.
    f.finish(QStringLiteral("gęś"), 9);
    QCOMPARE(q(f.search().text()), QStringLiteral("Zażółć gęś"));
    QVERIFY(!f.search().markedRange());
    QCOMPARE(f.query(Qt::ImSurroundingText).toString(), QStringLiteral("Zażółć gęś"));
    QCOMPARE(f.query(Qt::ImCursorPosition).toInt(), 9);
    QCOMPARE(f.query(Qt::ImTextAfterCursor).toString(), QStringLiteral("ś"));
    // A selection: its text, the anchor where it began.
    f.select(0, 6);
    QCOMPARE(f.query(Qt::ImCurrentSelection).toString(), QStringLiteral("Zażółć"));
    QCOMPARE(f.query(Qt::ImAnchorPosition).toInt(), 0);
    QCOMPARE(f.query(Qt::ImCursorPosition).toInt(), 6);
}

void TestAtramentQtInput::queriesWithArguments()
{
    Fixture f;
    f.click(kSearch);
    f.ime({}, {}, QStringLiteral("Łódź i Kraków"));
    // QInputMethod::queryFocusObject finds the overload by its signature.
    QVERIFY(f.item->metaObject()->indexOfMethod("inputMethodQuery(Qt::InputMethodQuery,QVariant)") >= 0);
    QCOMPARE(f.query(Qt::ImTextBeforeCursor, 6).toString(), QStringLiteral("Kraków"));
    QCOMPARE(f.query(Qt::ImTextBeforeCursor, 100).toString(), QStringLiteral("Łódź i Kraków"));
    f.select(4, 4);
    QCOMPARE(f.query(Qt::ImTextAfterCursor, 2).toString(), QStringLiteral(" i"));
    // The offset under a point (Android's handles; a tap while composing), the item's coordinates.
    const std::optional<Rect> end = f.search().boundsForRange({13, 13});
    const std::optional<Rect> start = f.search().boundsForRange({0, 0});
    QVERIFY(end && start);
    QCOMPARE(f.query(Qt::ImCursorPosition, QPointF(end->left() + 30, end->center().y)).toInt(), 13);
    QCOMPARE(f.query(Qt::ImCursorPosition, QPointF(start->left() + 1, start->center().y)).toInt(), 0);
    // Without a point: the caret.
    QCOMPARE(f.query(Qt::ImCursorPosition, QVariant()).toInt(), 4);
}

void TestAtramentQtInput::gboardTypesAWord()
{
    Fixture f;
    f.click(kNote);
    // Letters into a composition, autocorrect's word and a space committed (commitText: the
    // composing text, then finishComposingText).
    f.compose(QStringLiteral("z"));
    f.compose(QStringLiteral("za"));
    f.compose(QStringLiteral("zaz"));
    QCOMPARE(q(f.note().text()), QStringLiteral("zaz"));
    QCOMPARE(f.note().markedRange(), std::optional<Range>(Range{0, 3}));
    f.compose(QStringLiteral("zaż "));
    f.finish(QStringLiteral("zaż "), 4);
    QCOMPARE(q(f.note().text()), QStringLiteral("zaż "));
    QVERIFY(!f.note().markedRange());
    QCOMPARE(f.note().selection(), (InputHandler::Selection{4, 4}));
    // The next word, then a gesture-typed one (the whole word at once, then committed).
    f.compose(QStringLiteral("ó"));
    f.compose(QStringLiteral("ół"));
    f.compose(QStringLiteral("ółw"));
    f.finish(QStringLiteral("ółw"), 7);
    f.ime({}, {}, QStringLiteral(" "));
    f.compose(QStringLiteral("jaźń"));
    f.finish(QStringLiteral("jaźń"), 12);
    QCOMPARE(q(f.note().text()), QStringLiteral("zaż ółw jaźń"));
    QCOMPARE(f.note().selection(), (InputHandler::Selection{12, 12}));
    // Each composition is an undo step.
    f.undo();
    QCOMPARE(q(f.note().text()), QStringLiteral("zaż ółw "));
}

void TestAtramentQtInput::gboardBackspacesIntoAWord()
{
    Fixture f;
    f.click(kNote);
    f.compose(QStringLiteral("zażółć"));
    f.finish(QStringLiteral("zażółć"), 6);
    f.ime({}, {}, QStringLiteral(" "));
    QCOMPARE(q(f.note().text()), QStringLiteral("zażółć "));
    // Backspace: deleteSurroundingText(1, 0) — the space; then Gboard takes the word back into a
    // composition (setComposingRegion(0, 6), applied at the end of its batch edit: the word goes
    // out of Qt's text as the preedit comes in) and shortens it.
    f.ime({}, {}, QString(), -1, 1);
    QCOMPARE(q(f.note().text()), QStringLiteral("zażółć"));
    QCOMPARE(f.note().selection(), (InputHandler::Selection{6, 6}));
    QTextCharFormat underline;
    underline.setFontUnderline(true);
    f.ime(QStringLiteral("zażółć"),
                         {{QInputMethodEvent::Cursor, 6, 1}, {QInputMethodEvent::TextFormat, 0, 6, underline}},
                         QString(), -6, 6);
    QCOMPARE(q(f.note().text()), QStringLiteral("zażółć"));
    QCOMPARE(f.note().markedRange(), std::optional<Range>(Range{0, 6}));
    QCOMPARE(f.query(Qt::ImSurroundingText).toString(), QString());
    f.compose(QStringLiteral("zażół"));
    f.compose(QStringLiteral("zażó"));
    QCOMPARE(q(f.note().text()), QStringLiteral("zażó"));
    f.finish(QStringLiteral("zażó"), 4);
    QCOMPARE(q(f.note().text()), QStringLiteral("zażó"));
    QVERIFY(!f.note().markedRange());
    // Undo goes back through what was on screen, never through a text without the word.
    f.undo();
    QCOMPARE(q(f.note().text()), QStringLiteral("zażółć"));
    f.undo();
    QCOMPARE(q(f.note().text()), QStringLiteral("zażółć "));
}

void TestAtramentQtInput::gboardReplacesAWord()
{
    Fixture f;
    f.click(kNote);
    f.ime({}, {}, QStringLiteral("Ala ma kota"));
    // replaceText(4, 6, "miała"): finishComposingText, setSelection(4, 6), commitText — the
    // composing text over the selection, then finished.
    f.select(4, 6);
    QCOMPARE(f.query(Qt::ImCurrentSelection).toString(), QStringLiteral("ma"));
    f.compose(QStringLiteral("miała"));
    QCOMPARE(q(f.note().text()), QStringLiteral("Ala miała kota"));
    QCOMPARE(f.note().markedRange(), std::optional<Range>(Range{4, 9}));
    f.finish(QStringLiteral("miała"), 9);
    QCOMPARE(q(f.note().text()), QStringLiteral("Ala miała kota"));
    QCOMPARE(f.note().selection(), (InputHandler::Selection{9, 9}));
    // A commit with a replacement range (an input method's correction of the word before the
    // caret): the range is Qt's, around the caret.
    f.select(14, 14);
    f.ime({}, {}, QStringLiteral("kotka"), -4, 4);
    QCOMPARE(q(f.note().text()), QStringLiteral("Ala miała kotka"));
    QCOMPARE(f.note().selection(), (InputHandler::Selection{15, 15}));
}

void TestAtramentQtInput::deletionAroundTheCaret()
{
    Fixture f;
    f.click(kSearch);
    f.ime({}, {}, QStringLiteral("abcdef"));
    f.select(2, 2);
    // After the caret: the caret stays.
    f.ime({}, {}, QString(), 1, 2);
    QCOMPARE(q(f.search().text()), QStringLiteral("abcf"));
    QCOMPARE(f.search().selection(), (InputHandler::Selection{2, 2}));
    // Both sides at once (deleteSurroundingText(1, 1)).
    f.ime({}, {}, QString(), -1, 2);
    QCOMPARE(q(f.search().text()), QStringLiteral("af"));
    QCOMPARE(f.search().selection(), (InputHandler::Selection{1, 1}));
    // Before it, not touching it: the caret moves back by what went.
    f.ime({}, {}, QStringLiteral("xyz"));
    QCOMPARE(q(f.search().text()), QStringLiteral("axyzf"));
    f.ime({}, {}, QString(), -3, 1);
    QCOMPARE(q(f.search().text()), QStringLiteral("ayzf"));
    QCOMPARE(f.search().selection(), (InputHandler::Selection{3, 3}));
    // Out of range: clamped, nothing outside the text.
    f.ime({}, {}, QString(), -100, 1);
    QCOMPARE(q(f.search().text()), QStringLiteral("yzf"));
    f.ime({}, {}, QString(), 50, 10);
    QCOMPARE(q(f.search().text()), QStringLiteral("yzf"));
    // A selection, then a commit: typed over it.
    f.select(0, 2);
    f.ime({}, {}, QStringLiteral("ż"));
    QCOMPARE(q(f.search().text()), QStringLiteral("żf"));
}

void TestAtramentQtInput::desktopPreedit()
{
    Fixture f;
    f.click(kSearch);
    // An input method with a preedit (IBus, Fcitx): the caret inside it, then a commit that
    // replaces it, then one cancelled by an empty event.
    f.ime(QStringLiteral("ni"), {{QInputMethodEvent::Cursor, 2, 1}});
    f.ime(QStringLiteral("nihao"), {{QInputMethodEvent::Cursor, 3, 1}});
    QCOMPARE(f.search().selection().focus, 3);
    f.ime({}, {}, QStringLiteral("你好"));
    QCOMPARE(q(f.search().text()), QStringLiteral("你好"));
    QVERIFY(!f.search().markedRange());
    f.ime(QStringLiteral("x"), {});
    QCOMPARE(q(f.search().text()), QStringLiteral("你好x"));
    f.ime({}, {});
    QCOMPARE(q(f.search().text()), QStringLiteral("你好"));
    QVERIFY(!f.search().markedRange());
    // Commit and a new preedit in one event.
    f.ime(QStringLiteral("ą"), {}, QStringLiteral("ę"));
    QCOMPARE(q(f.search().text()), QStringLiteral("你好ęą"));
    QCOMPARE(f.search().markedRange(), std::optional<Range>(Range{3, 4}));
    QCOMPARE(f.query(Qt::ImSurroundingText).toString(), QStringLiteral("你好ę"));
}

void TestAtramentQtInput::signalForEveryChange()
{
    Fixture f;
    // Qt's Android input context connects to this signal by name when it shows the keyboard.
    QVERIFY(f.item->metaObject()->indexOfSignal("cursorPositionChanged()") >= 0);
    QSignalSpy spy(f.item, SIGNAL(cursorPositionChanged()));
    f.click(kSearch);
    QVERIFY(spy.count() >= 1); // a field took the focus
    spy.clear();
    f.ime({}, {}, QStringLiteral("dąb"));
    QCOMPARE(spy.count(), 1);
    f.select(1, 1);
    QCOMPARE(spy.count(), 2);
    // A frame that changes nothing (the caret blinking) tells the input method nothing.
    f.draw();
    f.draw();
    QCOMPARE(spy.count(), 2);
}

void TestAtramentQtInput::pressEndsTheComposition()
{
    Fixture f;
    f.click(kSearch);
    f.ime({}, {}, QStringLiteral("Zażółć"));
    f.compose(QStringLiteral("gęś"));
    QVERIFY(f.search().markedRange());
    // A press in the field moves the caret; the composition stays as text.
    const std::optional<Rect> start = f.search().boundsForRange({0, 0});
    QVERIFY(start);
    f.click(QPoint(int(start->left()) + 1, int(start->center().y)));
    QCOMPARE(q(f.search().text()), QStringLiteral("Zażółćgęś"));
    QVERIFY(!f.search().markedRange());
    QCOMPARE(f.search().selection(), (InputHandler::Selection{0, 0}));
    QCOMPARE(f.query(Qt::ImSurroundingText).toString(), QStringLiteral("Zażółćgęś"));
}

void TestAtramentQtInput::anotherFieldEndsTheComposition()
{
    Fixture f;
    f.click(kSearch);
    f.compose(QStringLiteral("żółw"));
    f.click(kNote);
    // The search keeps what was composed, as text; the note takes what comes next.
    QCOMPARE(q(f.search().text()), QStringLiteral("żółw"));
    QVERIFY(!f.search().markedRange());
    QCOMPARE(f.query(Qt::ImSurroundingText).toString(), QString());
    f.ime({}, {}, QStringLiteral("łoś"));
    QCOMPARE(q(f.note().text()), QStringLiteral("łoś"));
    QCOMPARE(q(f.search().text()), QStringLiteral("żółw"));
    // Undo in the search after it: the composition was its own step.
    f.click(kSearch);
    f.undo();
    QCOMPARE(q(f.search().text()), QString());
}

QTEST_MAIN(TestAtramentQtInput)
#include "tst_atrament_qt_input.moc"
