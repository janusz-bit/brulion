// ScrollProbe — a developer tool that measures scrolling in the real app (docs/UI.md "Dev tools").
// Built unless BRULION_SCROLL_PROBE is OFF (the default of the shipped Android and browser builds,
// Release / MinSizeRel: src/app/CMakeLists.txt; scrollprobe_off.cpp then only says so).
// It scrolls one Flickable of the window with synthetic input, as a user would — touch drags and
// flings, mouse wheel notches, touchpad pixel deltas — down to the end and back up, and records
// every frame. Then it prints a summary and quits. DevTools::attach() starts it when the arguments
// contain --scroll-probe:
//
//   --scroll-probe <target>   auto: the largest interactive Flickable of the main view;
//                             sidebar: the sidebar's; name:<objectName>; type:<QML type>: the
//                             largest Flickable inside an item of that type (e.g. type:PageView)
//   --scroll-script <script>  drag (touch, the finger stops before it lifts: no fling), flick
//                             (touch flings), wheel (mouse wheel notches), touchpad (pixel deltas
//                             with scroll phases), observe (no synthetic input: record the real
//                             input — a finger, `adb shell input swipe` — for --probe-max ms;
//                             follows the view after navigation; judges touch drags only, not a
//                             mouse's or a stylus's); default drag
//   --probe-out <file.csv>    one row per frame (default: no CSV); the log lines also go to
//                             <file.csv>.log (and to logcat on Android)
//   --probe-passes <n>        down to the end and back up n times (default 1)
//   --probe-wait <ms>         settle time before the first stroke (default 1500)
//   --probe-max <ms>          time limit of one direction; observe: of the recording (default 60000)
//   --probe-speed <px/s>      finger / touchpad speed of drag and touchpad (default 1500)
//   --probe-check <px>        fail (exit code 1, "CHECK FAIL" line) when the content jumps by more
//                             than <px>: moves on screen more than the input explains (drag,
//                             touchpad) or shifts inside the list (any script) — geometry only, no
//                             timing, so it also holds in slow sanitizer runs (ctest
//                             tst_scroll_jumps_qml)
//   --probe-cache <px>        set the target's cacheBuffer first (0: every row is created at the
//                             edge of the view — as on a phone whose cache cannot keep ahead —
//                             so a row that grows after the list placed it moves the content
//                             every time, whatever the timing)
//
// On Android the arguments come from the activity's intent: `adb shell am start -n
// io.github.januszbit.brulion/.android.BrulionActivity --es applicationArguments "'--scroll-probe
// auto --scroll-script observe --probe-out /data/user/0/io.github.januszbit.brulion/files/p.csv'"`
// (Qt's QtLoader appends that extra to the arguments of debuggable APKs only: build one with
// android:debuggable="true" on <application> for measurements; split at spaces, so no paths with
// spaces). Such an APK needs the probe built in: BRULION_ANDROID_BUILD_TYPE=RelWithDebInfo (or
// -DBRULION_SCROLL_PROBE=ON).
//
// Per frame (QQuickWindow::afterAnimating, the GUI thread, after polish): time, frame interval,
// the GUI work between frames and in polish, the Flickable's contentY / originY / contentHeight /
// visibleArea, the input delivered since the last frame and the scroll it asks for, the delegates
// created (once the view has placed them: itemAtIndex()) with their kind of block, resized and
// destroyed, the nested block delegates (BlockDelegate) created, and an anchor — the
// item under the viewport's centre — whose movement on screen is what the user sees. A JUMP is
// content moving on screen by more than the input explains (direct scripts: drag, touchpad) or a
// layout shift (the anchor moving inside the content); STUTTER is a frame interval > 33 ms while
// scrolling. Both are listed with what happened in that frame (delegates resized above / in /
// below the viewport, created, originY / contentHeight changes). Touch input is also counted per
// frame (moves that arrived, the finger's movement, how long before the frame the last one
// arrived) and per stroke (finger speed at the release, the fling's velocity and distance): Qt
// Quick applies the newest touch move once per frame, so a frame without a new move does not
// scroll while the finger moves (judder).
#pragma once

#include <QStringList>

class QQmlApplicationEngine;

namespace brulion::ui {

/// Start the probe for the engine's first window when `arguments` contain --scroll-probe.
void startScrollProbe(QQmlApplicationEngine *engine, const QStringList &arguments);

} // namespace brulion::ui
