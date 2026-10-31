// DevTools — developer/test command-line options and the screenshot hook (QML singleton
// `DevTools`). Used by main.cpp and by the component gallery (qml/dev/Gallery.qml).
//
//   --gallery              load the component gallery instead of Main.qml
//   --section <id>         gallery: show only this section (popups in it open by themselves)
//   --editor               load the block editor playground (qml/dev/EditorPlayground.qml)
//   --editor-script <name> playground: put the editor into a state for screenshots (editing,
//                          suggest, slash, date, zoom, source, reminder, …; see the playground)
//   --screenshot <file>    after loading (+ --delay ms, default 800) save the window as an
//                          image and quit (exit code 0 = saved, 1 = failed); the window is
//                          activated first (also on an X server without a window manager); with
//                          the app's --popup it also waits until that popup has opened (its enter
//                          transition over: holdScreenshot), at most 5 s longer
//   --delay <ms>           see --screenshot
//   --dump-layout <file>   with --screenshot: also write what the window shows (texts with their
//                          colours and fonts, targets, boxes, the theme's tokens) as JSON for the
//                          UI benchmark (layoutdump.h; tests/app/uibench), in the same frame
//   --size <WxH>           initial window size, e.g. 1280x800 (desktop), 412x915 (phone)
//   --scale <factor>       device pixel ratio (QT_SCALE_FACTOR), e.g. 2 or 2.625
//   --theme light|dark     force the colour scheme (Theme.mode)
//   --accent <name|#hex>   force the accent colour (red, blue, green, teal, violet, graphite, or
//                          a custom "#rrggbb"; Theme.accentName / customAccent)
//   --surface <name>       force a background: paper | white (light), slate | black (dark)
//   --density compact|comfortable  force the density (Theme.density)
//   --locale pl|en         UI language (translation + DatesHelper.locale)
//   --touch                touch UI sizes (Theme.forceTouch), as on a phone
//   --keyboard <px>        a simulated on-screen keyboard of that height (KeyboardInsets.simulated;
//                          0 = none, and no real one either), drawn grey in the window
//   --reduced-motion       Theme.reducedMotion
//   --now "<date> <time>"  fixed clock for DatesHelper ("2026-10-14 10:00"), for reproducible
//                          screenshots
//   --scroll-probe <target> scroll a view with synthetic input and record every frame, then quit
//                          (--scroll-script, --probe-out, …: scrollprobe.h)
//
// Environment: BRULION_TEST_SLOWDOWN (testSlowdown) — ctest's factor for slow builds.
//
// Headless use: QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ./brulion --gallery
// --section buttons --size 412x915 --screenshot out.png. scripts/screenshots.sh produces the
// standard set. Unknown arguments are ignored (other subsystems have their own).
#pragma once

#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QQmlApplicationEngine;
class QQmlEngine;
#include "qttranslations.h"

class QTranslator;

namespace brulion::ui {

class DevTools : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool gallery READ gallery CONSTANT FINAL)
    Q_PROPERTY(QString section READ section CONSTANT FINAL)
    Q_PROPERTY(bool editor READ editor CONSTANT FINAL)
    Q_PROPERTY(QString editorScript READ editorScript CONSTANT FINAL)
    Q_PROPERTY(QString theme READ theme CONSTANT FINAL)
    /// --accent: a preset's name, "custom" for a colour (customAccent), "" without the option.
    Q_PROPERTY(QString accentName READ accentName CONSTANT FINAL)
    Q_PROPERTY(QString customAccent READ customAccent CONSTANT FINAL)
    /// --surface: the light or dark background it names ("" otherwise).
    Q_PROPERTY(QString lightSurface READ lightSurface CONSTANT FINAL)
    Q_PROPERTY(QString darkSurface READ darkSurface CONSTANT FINAL)
    Q_PROPERTY(QString density READ density CONSTANT FINAL)
    Q_PROPERTY(QString language READ language NOTIFY languageChanged FINAL)
    Q_PROPERTY(bool touch READ touch CONSTANT FINAL)
    /// --keyboard <px>: the simulated keyboard's height, -1 without the option.
    Q_PROPERTY(int keyboard READ keyboard CONSTANT FINAL)
    Q_PROPERTY(bool reducedMotion READ reducedMotion CONSTANT FINAL)
    /// True when the run ends with a screenshot (QML may skip intro animations).
    Q_PROPERTY(bool screenshotMode READ screenshotMode CONSTANT FINAL)
    /// BRULION_TEST_SLOWDOWN from the environment, at least 1: ctest sets it, 25 in sanitizer builds
    /// (tests/CMakeLists.txt). The playground's self-tests wait that many times longer for
    /// something asynchronous.
    Q_PROPERTY(int testSlowdown READ testSlowdown CONSTANT FINAL)

public:
    /// Options that must be applied before QGuiApplication exists (--scale).
    static void applyEarlyArguments(int argc, char **argv);

    explicit DevTools(const QStringList &arguments, QObject *parent = nullptr);
    ~DevTools() override;

    /// QML singleton factory: the instance created by main(), or a default one.
    static DevTools *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    bool gallery() const { return m_gallery; }
    QString section() const { return m_section; }
    bool editor() const { return m_editor; }
    QString editorScript() const { return m_editorScript; }
    QString theme() const { return m_theme; }
    QString accentName() const { return m_accentName; }
    QString customAccent() const { return m_customAccent; }
    QString lightSurface() const { return m_lightSurface; }
    QString darkSurface() const { return m_darkSurface; }
    QString density() const { return m_density; }
    /// "pl" or "en": --locale, else English when the system locale is English, else Polish.
    QString language() const { return m_language; }
    bool touch() const { return m_touch; }
    int keyboard() const { return m_keyboard; }
    bool reducedMotion() const { return m_reducedMotion; }
    bool screenshotMode() const { return !m_screenshot.isEmpty(); }
    int testSlowdown() const { return m_testSlowdown; }

    /// QML type to load from module Brulion: "Gallery" with --gallery, "EditorPlayground" with
    /// --editor, else "Main".
    QString rootComponent() const;
    /// The translator main() installed (brulion_pl.qm, or brulion_en.qm for English plurals);
    /// setLanguage() reloads it. Qt's own catalogs (dialog buttons, file dialogs: QtTranslations)
    /// are installed with it and follow the language too.
    void setTranslator(QTranslator *translator);
    /// Apply --size and schedule --screenshot for the engine's first window.
    void attach(QQmlApplicationEngine *engine);

    /// Switch the UI language at run time ("pl" | "en"): (un)installs the translation (also Qt's
    /// own, QtTranslations) and retranslates every qsTr() binding.
    Q_INVOKABLE void setLanguage(const QString &language);
    /// --screenshot waits while held (AppShell holds it from --popup until that popup emitted
    /// `opened`, so the shot never catches its enter transition half done): it looks again every
    /// 50 ms, takes the shot one poll after the release, and gives up waiting after 5 s with a
    /// warning. No effect without --screenshot.
    Q_INVOKABLE void holdScreenshot(bool hold);

Q_SIGNALS:
    void languageChanged();

private:
    void takeScreenshot(QObject *root);

    bool m_gallery = false;
    QString m_section;
    bool m_editor = false;
    QString m_editorScript;
    QString m_theme;
    QString m_accentName;
    QString m_customAccent;
    QString m_lightSurface;
    QString m_darkSurface;
    QString m_density;
    QString m_language;
    QString m_screenshot;
    QString m_dumpLayout;
    int m_delayMs = 800;
    /// holdScreenshot(): held now / the shot already waited this long for it / the release came
    /// after the last look (one more poll: the frame after the popup opened).
    bool m_screenshotHeld = false;
    int m_screenshotHeldMs = 0;
    bool m_screenshotReleased = false;
    QSize m_size;
    bool m_touch = false;
    int m_keyboard = -1;
    bool m_reducedMotion = false;
    int m_testSlowdown = 1;
    QPointer<QQmlApplicationEngine> m_engine;
    /// The arguments, kept when they ask for the scroll probe (scrollprobe.h).
    QStringList m_probeArguments;
    QTranslator *m_translator = nullptr;
    bool m_translatorInstalled = false;
    QtTranslations m_qtTranslations;
};

} // namespace brulion::ui
