// Shell journey — `brulion --journey <dir> [--size …] [--touch] [--theme …] [--now …]`: a dev
// tool that walks the real app like a first-time user, headlessly, and saves a screenshot after
// every step into <dir> (NN-<step>.png): onboarding → the sample notebook → writing in today's
// journal (an outline with Tab, a [[link]] from the suggestions, a task with a reminder from the
// slash menu) → the calendar with that reminder → ticking the task off in Tasks → a page reached
// through a [[link]] with its references → search "zolw" finding "żółw" → graph → sync center →
// import and export → page history → settings (switching the language and the theme).
//
// Clicks and keys are real mouse and key events at the items' positions (found by their text,
// like a user reads the screen), so the screenshots show what a user gets there. A step that
// cannot be done the user's way is logged as "journey: FALLBACK …" and done directly; a failed
// step logs "journey: FAIL …". Exit code: the number of failed steps. Phone runs (--touch) tap
// with touch events, desktop runs click with the mouse; both go through QGuiApplication like real
// input. --journey-theme light|dark sets the theme in the settings (the last step switches it),
// --journey-empty walks a new, empty notebook instead (every view before the first note).
// BRULION_JOURNEY_DEBUG=1 logs every click with the items under it.
// tests/app/shell/journey.sh runs it for desktop/phone × light/dark with a fresh home; ctest runs
// it as tst_shell_journey (phone) and tst_shell_journey_desktop.
// Desktop builds only (nested event processing, like the shell self-test).
#pragma once

#include <QString>

class QQmlApplicationEngine;

namespace brulion::shell {

class AppController;

/// Start the journey once the UI is loaded; quits the application when done.
void startShellJourney(QQmlApplicationEngine *engine, AppController *app, const QString &outDir);

} // namespace brulion::shell
