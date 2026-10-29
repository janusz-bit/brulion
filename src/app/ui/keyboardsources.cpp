#include "keyboardsources.h"

#include "gestureinsets.h"
#include "keyboardinsets.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include <memory>

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"

#include <QOperatingSystemVersion>
#endif
#ifdef Q_OS_WASM
#include "wasm/wasmselftest.h"
#include "wasm/wasmshell.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <functional>
#endif

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

// Qt for Android reports the keyboard's rectangle in physical screen pixels; the other platforms
// in logical ones.
#ifdef Q_OS_ANDROID
constexpr bool kNativeKeyboardRect = true;
#else
constexpr bool kNativeKeyboardRect = false;
#endif

#ifdef Q_OS_WASM
ViewportSample toSample(const platform::BrowserViewport &v)
{
    ViewportSample sample;
    sample.innerWidth = v.innerWidth;
    sample.innerHeight = v.innerHeight;
    sample.height = v.height;
    sample.offsetTop = v.offsetTop;
    sample.scale = v.scale;
    sample.containerHeight = v.containerHeight;
    sample.touch = v.touch;
    sample.editing = v.editing;
    sample.settled = v.settled;
    sample.mode = v.mode == u"content" || v.mode == u"visual" ? v.mode : QString();
    return sample;
}

QJsonValue jsonNumber(qreal value)
{
    return std::isfinite(value) ? QJsonValue(value) : QJsonValue();
}

// The visible item named `name` in the visual tree under `item` (the journal has an editing
// toolbar per day: the one shown).
QQuickItem *visibleItem(QQuickItem *item, QStringView name)
{
    if (!item || !item->isVisible())
        return nullptr;
    if (item->objectName() == name)
        return item;
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = visibleItem(child, name))
            return found;
    }
    return nullptr;
}

// `?selftest=keyboard` (scripts/wasm-smoke.mjs --keyboard and --keyboard-phone,
// scripts/android-emulator-test.sh --web-keyboard): the keyboard's state, the browser's last
// sample, the old formula (innerHeight − height − offsetTop, for the record), where the shell's
// content and the docked editing toolbar end in the window (logical px) and the focused editor's
// text (its first 120 characters; not a password field's).
QJsonObject keyboardSelfTestResult(const KeyboardInsets *insets, QQuickWindow *window)
{
    const ViewportSample &s = insets->viewportSample();
    const qreal legacy = s.innerHeight - s.height - s.offsetTop;
    QJsonObject result{
            {u"overlap"_s, insets->overlap()},
            {u"shown"_s, insets->shown()},
            {u"animating"_s, insets->animating()},
            {u"source"_s, insets->sourceName()},
            {u"mode"_s, s.mode},
            {u"sample"_s, QJsonObject{{u"innerWidth"_s, jsonNumber(s.innerWidth)},
                                      {u"innerHeight"_s, jsonNumber(s.innerHeight)},
                                      {u"height"_s, jsonNumber(s.height)},
                                      {u"offsetTop"_s, jsonNumber(s.offsetTop)},
                                      {u"scale"_s, jsonNumber(s.scale)},
                                      {u"containerHeight"_s, jsonNumber(s.containerHeight)},
                                      {u"touch"_s, s.touch},
                                      {u"editing"_s, s.editing},
                                      {u"settled"_s, s.settled}}},
            {u"legacyOverlap"_s, std::isfinite(legacy) ? std::max<qreal>(0, legacy) : 0},
    };
    qreal windowHeight = 0;
    qreal contentBottom = 0;
    qreal toolbarBottom = -1;
    if (window) {
        windowHeight = window->height();
        contentBottom = windowHeight;
        if (QQuickItem *room = visibleItem(window->contentItem(), u"shellKeyboardRoom"))
            contentBottom = room->mapToScene(QPointF(0, 0)).y();
        if (QQuickItem *toolbar = visibleItem(window->contentItem(), u"editToolbarSlot"))
            toolbarBottom = toolbar->mapToScene(QPointF(0, toolbar->height())).y();
        result.insert(u"dpr"_s, window->devicePixelRatio());
        // What the keyboard types into (an editor's text): typing and Enter reach the app. Never
        // a password's (a field that hides its text or marks it sensitive).
        if (QQuickItem *focus = window->activeFocusItem()) {
            const QVariant text = focus->property("text");
            const QVariant echoMode = focus->property("echoMode");
            const auto hints = Qt::InputMethodHints(focus->inputMethodQuery(Qt::ImHints).toInt());
            const bool secret = (echoMode.isValid() && echoMode.toInt() != 0)
                                || hints.testAnyFlags(Qt::ImhHiddenText | Qt::ImhSensitiveData);
            if (!secret && text.metaType() == QMetaType::fromType<QString>())
                result.insert(u"focusText"_s, text.toString().left(120));
        }
    }
    result.insert(u"windowHeight"_s, windowHeight);
    result.insert(u"contentBottom"_s, contentBottom);
    result.insert(u"toolbarBottom"_s, toolbarBottom);
    return result;
}
#endif

} // namespace

void connectKeyboardSources(KeyboardInsets *insets, QQmlApplicationEngine *engine)
{
    // The app's window (the first one the engine creates): its height bounds the overlap, its
    // geometry and pixel ratio map Qt's keyboard rectangle.
    auto window = std::make_shared<QPointer<QQuickWindow>>();
    QInputMethod *inputMethod = QGuiApplication::inputMethod();
    const auto readInputMethod = [insets, inputMethod, window] {
        QQuickWindow *w = window->data();
        if (!w)
            return;
        const qreal overlap = keyboardOverlap(inputMethod->keyboardRectangle(), kNativeKeyboardRect,
                                              w->devicePixelRatio(), w->geometry(), inputMethod->isVisible());
        insets->setInputMethodState(overlap, inputMethod->isVisible());
    };
    QObject::connect(inputMethod, &QInputMethod::keyboardRectangleChanged, insets, readInputMethod);
    QObject::connect(inputMethod, &QInputMethod::visibleChanged, insets, readInputMethod);
    QObject::connect(engine, &QQmlApplicationEngine::objectCreated, insets,
                     [insets, window, readInputMethod](QObject *object, const QUrl &) {
                         auto *w = qobject_cast<QQuickWindow *>(object);
                         if (!w || *window)
                             return;
                         *window = w;
                         insets->setWindowHeight(w->height());
                         QObject::connect(w, &QWindow::heightChanged, insets,
                                          [insets](int height) { insets->setWindowHeight(height); });
                         QObject::connect(w, &QWindow::yChanged, insets, readInputMethod);
                         readInputMethod();
                     });

#ifdef Q_OS_ANDROID
    // Android 11–14 resize a window that fits inside the system bars for the keyboard, in one
    // jump, and report none of its slide (the window already ends at the keyboard's top). Drawn
    // behind the bars, as Android 15+ always does (Qt's expanded client area: the shell pads its
    // bars with the SafeArea margins), the window keeps its size and gets every frame of the
    // slide's insets, so the content follows the keyboard there too. The bars' icons then follow
    // the window's colour (the app's theme). (Android 9–10: no frames to follow, the window stays
    // as it was.)
    if (QOperatingSystemVersion::current() >= QOperatingSystemVersion(QOperatingSystemVersion::Android, 11)) {
        QObject::connect(engine, &QQmlApplicationEngine::objectCreated, insets, [](QObject *object, const QUrl &) {
            auto *w = qobject_cast<QQuickWindow *>(object);
            if (!w)
                return;
            w->setFlag(Qt::ExpandedClientAreaHint, true);
            const auto followColor = [w] {
                platform::AndroidBridge::instance()->setSystemBarsLight(w->color().lightnessF() >= 0.5);
            };
            QObject::connect(w, &QQuickWindow::colorChanged, w, followColor);
            followColor();
        });
    }
    // The IME insets of the activity (ImeInsets.kt) in physical pixels, frame by frame while the
    // keyboard slides (Android 11+).
    QObject::connect(platform::AndroidBridge::instance(), &platform::AndroidBridge::imeChanged, insets,
                     [insets, window](int overlap, bool shown, bool animating) {
                         const qreal dpr = *window ? (*window)->devicePixelRatio() : qGuiApp->devicePixelRatio();
                         insets->setPlatformState(KeyboardInsets::toLogical(overlap, dpr), shown, animating);
                     });
    platform::AndroidBridge::instance()->requestImeState();
#endif

#ifdef Q_OS_WASM
    // The browser's viewports: CSS pixels are Qt's logical pixels (wasm/index.html). With
    // --selftest-keyboard every change of the keyboard, of the browser's sample and of where the
    // content and the toolbar end is published as window.brulionKeyboardSelfTest, and the
    // keyboard's room and the docked toolbar get marker colours (KeyboardInsets::marked).
    const bool selfTest = QCoreApplication::arguments().contains(u"--selftest-keyboard"_s);
    std::function<void()> publish;
    if (selfTest) {
        insets->setMarked(true);
        auto last = std::make_shared<QByteArray>();
        publish = [insets, window, last] {
            const QJsonObject result = keyboardSelfTestResult(insets, window->data());
            const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Compact);
            if (json == *last)
                return;
            *last = json;
            platform::publishSelfTestResult("brulionKeyboardSelfTest", result);
        };
        QObject::connect(insets, &KeyboardInsets::overlapChanged, insets, publish);
        QObject::connect(insets, &KeyboardInsets::shownChanged, insets, publish);
        QObject::connect(insets, &KeyboardInsets::animatingChanged, insets, publish);
        QObject::connect(insets, &KeyboardInsets::sourceChanged, insets, publish);
        // Where things end changes a frame later (the layout): after every frame, only when the
        // result differs.
        QObject::connect(engine, &QQmlApplicationEngine::objectCreated, insets,
                         [insets, publish](QObject *object, const QUrl &) {
                             if (auto *w = qobject_cast<QQuickWindow *>(object))
                                 QObject::connect(w, &QQuickWindow::frameSwapped, insets, publish,
                                                  Qt::QueuedConnection);
                         });
    }
    QPointer<KeyboardInsets> target(insets);
    platform::watchVisualViewport([target, publish](const platform::BrowserViewport &viewport) {
        if (!target)
            return;
        target->setViewportSample(toSample(viewport));
        if (publish)
            publish();
    });
    if (publish)
        publish();
#endif
}

void connectGestureSources(GestureInsets *insets, QQmlApplicationEngine *engine)
{
#ifdef Q_OS_ANDROID
    // The system gesture insets of the activity (ImeInsets.kt) in physical pixels: the left and
    // right edges gesture navigation keeps for "back" (0 with buttons).
    auto window = std::make_shared<QPointer<QQuickWindow>>();
    QObject::connect(engine, &QQmlApplicationEngine::objectCreated, insets, [window](QObject *object, const QUrl &) {
        if (auto *w = qobject_cast<QQuickWindow *>(object); w && !*window)
            *window = w;
    });
    QObject::connect(platform::AndroidBridge::instance(), &platform::AndroidBridge::systemGesturesChanged, insets,
                     [insets, window](int left, int right) {
                         const qreal dpr = *window ? (*window)->devicePixelRatio() : qGuiApp->devicePixelRatio();
                         insets->setPlatformInsets(KeyboardInsets::toLogical(left, dpr),
                                                   KeyboardInsets::toLogical(right, dpr));
                     });
    // The observer reports them with the keyboard's state (connectKeyboardSources asks for it
    // again; asking here too keeps this independent of the order of the two calls).
    platform::AndroidBridge::instance()->requestImeState();
#else
    Q_UNUSED(insets)
    Q_UNUSED(engine)
#endif
}

} // namespace brulion::ui
