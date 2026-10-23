// UI driver — real input and item finding for the app's own automation: the shell self-test
// (shellselftest.h), the user journey (shelljourney.h) and the remote control of the running app
// (remotecontrol.h). Library brulion_uidriver (desktop builds; tests/app/shell tst_uidriver).
//
// Input goes through QtGui as a device's does — mouse events through qt_handleMouseEvent (what
// QtTest calls), touch points through QTest::QTouchEventSequence, wheel events through
// qt_handleWheelEvent, key events either to the focused item (KeyDelivery::FocusItem: the
// self-tests) or as a keyboard's (KeyDelivery::System: shortcuts first, then the focused item) —
// so pointer handlers see whole clicks and long presses, and shortcuts fire.
//
// A gesture is a list of steps (an event, then a pause). run() plays one at once, letting the
// event loop run in the pauses (nested event processing: only for the self-tests and journeys,
// which own the app while they run); play() plays one from timers on the app's own event loop
// (the remote control: the app stays in real use, nothing is nested).
//
//   uidriver::run(uidriver::mouseClick(window, uidriver::pointIn(button)));
//   uidriver::play(owner, window, uidriver::typeText(window, u"Zażółć"_s), [](bool done) { … });
#pragma once

#include <QList>
#include <QPointF>
#include <QString>

#include <functional>
#include <optional>

class QObject;
class QQuickItem;
class QQuickWindow;

namespace brulion::uidriver {

// ---- Waiting (nested event processing: self-tests and journeys only) -------------------------

/// Let the event loop run for `ms` (gestures, transitions, queued calls). Not scaled by
/// BRULION_TEST_SLOWDOWN: these are real input timings (a longer pause turns a tap into a long
/// press), unlike waitFor()'s deadlines.
void settle(int ms);
/// Run the event loop until `condition` holds (true) or `timeoutMs` passed (false). Sanitizer test
/// runs are ~25x slower: the deadline is multiplied by BRULION_TEST_SLOWDOWN.
bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000);

// ---- Items ------------------------------------------------------------------------------------

/// True when the item's QML type (or one it is based on) is `prefix` ("TasksRow" matches the
/// class "TasksRow_QMLTYPE_12").
bool isA(const QObject *object, const char *prefix);
/// `item` or one of its ancestors isA(prefix).
bool hasAncestor(const QQuickItem *item, const char *prefix);
/// The QML type of an object as people name it: "BButton" (not "BButton_QMLTYPE_42"),
/// "QQuickText" for C++ types.
QString typeName(const QObject *object);
/// The item and all its ancestors are visible (opacity included) and it is not empty.
bool shown(const QQuickItem *item);
/// The item's middle is inside the window and every clipping ancestor.
bool onScreen(const QQuickWindow *window, const QQuickItem *item);
/// A point of the item in window (scene) coordinates: (fx, fy) of its size from its top left.
QPointF pointIn(const QQuickItem *item, qreal fx = 0.5, qreal fy = 0.5);
/// The text an item shows: its text, label or title property ("" when none).
QString labelOf(const QQuickItem *item);
/// `root` and every item below it in the visual tree (depth first, in child order).
QList<QQuickItem *> allItems(QQuickItem *root);
/// The items of the window for which `pred` holds, topmost first: in a popup (the overlay) before
/// the rest, on screen before off it, later painted before earlier (`shownOnly`: shown() only).
QList<QQuickItem *> matching(QQuickWindow *window, const std::function<bool(QQuickItem *)> &pred,
                             bool shownOnly = true);
/// The topmost shown item for which `pred` holds (matching()'s first), else nullptr.
QQuickItem *topmost(QQuickWindow *window, const std::function<bool(QQuickItem *)> &pred);
/// The first shown item under `root` (depth first, in child order) for which `pred` holds and
/// whose middle lies inside `root` (lists keep delegates beyond their edges, pools hide some).
QQuickItem *findShown(QQuickItem *root, const std::function<bool(QQuickItem *)> &pred);
/// The item called `name` under `root` through the visual tree (Repeater items are not QObject
/// children of the window), visible but maybe scrolled out of its view; else nullptr.
QQuickItem *shownNamed(QQuickItem *root, const QString &name);
/// The nearest Flickable around `item`, else nullptr.
QQuickItem *enclosingFlickable(QQuickItem *item);
/// The visible rendered text (a Text with linkAt()) under `root` whose text contains `needle`
/// and whose middle is on screen inside `root`.
QQuickItem *renderedTextContaining(QQuickItem *root, const QString &needle);
/// A point of `text` (scene coordinates) on its link whose URL contains `url`; (-1, -1) if none.
QPointF linkPoint(QQuickItem *text, const QString &url);
/// The chain of items under a scene point, topmost first ("Class/objectName > …"; debugging
/// clicks that do nothing).
QString itemsAt(QQuickWindow *window, QPointF scenePos);
/// The menus (BMenu) open in the window: their popup items in the overlay (the visual tree also
/// reaches menus inside list delegates, which the QObject tree of the window does not).
int openMenus(QQuickWindow *window);
/// Close the menus open in the window and wait until they are (nested: self-tests).
void closeMenus(QQuickWindow *window);
/// The popups shown in the window (QQuickPopup objects: drawer, dialogs, menus, the palette).
QList<QObject *> openPopups(QQuickWindow *window);
/// Scroll the nearest flickables around `item` until it is on screen; `afterEach` runs after each
/// flickable moved (the journey lets it settle). True when it is on screen afterwards.
bool scrollIntoView(QQuickWindow *window, QQuickItem *item, const std::function<void()> &afterEach = {});

// ---- Input ------------------------------------------------------------------------------------

/// One event of a gesture and the pause after it. `flush`: process the events it posted at once
/// (what a committed touch sequence does), before the pause.
struct Step {
    std::function<void()> act;
    int pauseMs = 0;
    bool flush = false;
};
using Gesture = QList<Step>;

/// Play a gesture at once (nested event processing in its pauses).
void run(const Gesture &gesture);
/// Play a gesture from timers on the event loop; `done(true)` after its last step and pause,
/// `done(false)` when `window` went away first. The player lives as a child of `owner` (gone
/// with it: then nothing more happens, `done` included).
void play(QObject *owner, QQuickWindow *window, const Gesture &gesture, std::function<void(bool)> done = {});

/// Timestamps of synthetic mouse events: increasing, `advance` ms after the previous one.
int mouseTimestamp(int advance);

/// The pauses and event timestamps of a click.
struct ClickTiming {
    int afterMove = 0;  ///< pause after moving the pointer there
    int afterPress = 0; ///< pause while the button is down
    int afterRelease = 100;
    int pressGap = 20;   ///< timestamp gap move → press
    int releaseGap = 80; ///< timestamp gap press → release (a long press: its length)
};

/// The pointer moved to `scenePos`, no button pressed (hover).
Gesture mouseMove(QQuickWindow *window, QPointF scenePos, int pauseMs = 50);
/// A click (left by default; with `modifiers` held) at `scenePos` of the window, as a real mouse
/// sends it.
Gesture mouseClick(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button = Qt::LeftButton,
                   Qt::KeyboardModifiers modifiers = Qt::NoModifier, const ClickTiming &timing = {});
/// Two clicks within the double-click interval.
Gesture mouseDoubleClick(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button = Qt::LeftButton,
                         Qt::KeyboardModifiers modifiers = Qt::NoModifier);
/// A button held down for `holdMs` at one point, then released.
Gesture mouseLongPress(QQuickWindow *window, QPointF scenePos, int holdMs, Qt::MouseButton button = Qt::LeftButton);
/// A drag with the mouse from `from` to `to` (scene coordinates) in 16 steps; held there `holdMs`,
/// then — unless `escape` (Escape pressed first, the drag cancelled) — released.
Gesture mouseDrag(QQuickWindow *window, QPointF from, QPointF to, int holdMs = 0, bool escape = false,
                  Qt::MouseButton button = Qt::LeftButton, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
/// A tap with one finger at `scenePos`, as a touch screen sends it (press, `holdMs`, release,
/// `afterMs`). A long press is a tap held long.
Gesture touchTap(QQuickWindow *window, QPointF scenePos, int holdMs = 110, int afterMs = 100);
/// Two taps within the double-tap interval.
Gesture touchDoubleTap(QQuickWindow *window, QPointF scenePos);
/// A finger dragged from `from` by `delta` (scene coordinates) in `steps` moves `stepMs` apart,
/// held there `holdMs`, then lifted.
Gesture touchDrag(QQuickWindow *window, QPointF from, QPointF delta, int steps = 12, int stepMs = 16, int holdMs = 0,
                  int afterMs = 300);
/// Touchpad scrolling at `scenePos` by `delta` pixels (positive y: further down the content), in
/// steps of at most 40 px with scroll phases; a Flickable there moves by `delta` exactly.
Gesture wheel(QQuickWindow *window, QPointF scenePos, QPointF delta, Qt::KeyboardModifiers modifiers = Qt::NoModifier);

/// How key events reach the app.
enum class KeyDelivery {
    FocusItem, ///< straight to the window's focused item (window Shortcut objects are not involved)
    System,    ///< as a keyboard's: shortcuts first, then the focused item
};

struct KeyStroke {
    int key = 0;
    Qt::KeyboardModifiers modifiers;
    QString text;
};

/// Press and release one key. True when an item handled it (accepted the press or the release),
/// or a shortcut took it.
bool deliverKey(QQuickWindow *window, const KeyStroke &stroke, KeyDelivery delivery);
/// deliverKey() with FocusItem delivery, then a pause of `pauseMs`. True when it was handled — an
/// unhandled back key is what lets Android close the app.
bool sendKey(QQuickWindow *window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
             const QString &text = QString(), int pauseMs = 50);
/// The key strokes of a key sequence as people write it: "Ctrl+K", "Escape", "Shift+Tab",
/// "Ctrl+Shift+C", several separated by commas ("Down, Down, Return"); the text of printable keys
/// as a keyboard gives it. nullopt (and `error`) for a name Qt does not know.
std::optional<QList<KeyStroke>> parseKeySequence(const QString &sequence, QString *error = nullptr);
/// The key strokes that type `text`: printable characters (Polish letters too) with their key
/// codes and Shift for capitals, "\n" Return, "\t" Tab.
QList<KeyStroke> strokesFor(const QString &text);
/// Press `strokes` one after another, `pauseMs` after each.
Gesture keyStrokes(QQuickWindow *window, const QList<KeyStroke> &strokes, KeyDelivery delivery, int pauseMs);
/// Type `text` (strokesFor()) and wait `endMs` afterwards.
Gesture typeText(QQuickWindow *window, const QString &text, KeyDelivery delivery, int pauseMs = 0, int endMs = 150);

// ---- Synchronous helpers (run() of the gestures above) -----------------------------------------

void touchTapNow(QQuickWindow *window, QPointF scenePos, int holdMs = 110);
void touchDragNow(QQuickWindow *window, QPointF from, QPointF delta);
void mouseMoveNow(QQuickWindow *window, QPointF scenePos);
void mouseClickNow(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button = Qt::LeftButton,
                   Qt::KeyboardModifiers modifiers = Qt::NoModifier);
void mouseDragNow(QQuickWindow *window, QPointF from, QPointF to, int holdMs = 0, bool escape = false);
/// A tap on touch UIs (the phone run), a left click otherwise.
void tapOrClickNow(QQuickWindow *window, QPointF scenePos, bool touch);

} // namespace brulion::uidriver
