#include "keyboardsources.h"

#include "keyboardinsets.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

#include <memory>

#ifdef Q_OS_ANDROID
#include "android/androidbridge.h"
#endif
#ifdef Q_OS_WASM
#include "wasm/wasmselftest.h"
#include "wasm/wasmshell.h"

#include <QJsonObject>
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
    // The browser's visualViewport: CSS pixels are Qt's logical pixels (wasm/index.html).
    QPointer<KeyboardInsets> target(insets);
    platform::watchVisualViewport([target](double overlap) {
        if (target)
            target->setPlatformState(overlap, overlap > 0.5, false);
    });
    if (QCoreApplication::arguments().contains(u"--selftest-keyboard"_s)) {
        const auto publish = [insets] {
            platform::publishSelfTestResult("brulionKeyboardSelfTest",
                                            QJsonObject{{u"overlap"_s, insets->overlap()},
                                                        {u"shown"_s, insets->shown()},
                                                        {u"animating"_s, insets->animating()},
                                                        {u"source"_s, insets->sourceName()}});
        };
        QObject::connect(insets, &KeyboardInsets::overlapChanged, insets, publish);
        QObject::connect(insets, &KeyboardInsets::shownChanged, insets, publish);
        QObject::connect(insets, &KeyboardInsets::sourceChanged, insets, publish);
        publish();
    }
#endif
}

} // namespace brulion::ui
