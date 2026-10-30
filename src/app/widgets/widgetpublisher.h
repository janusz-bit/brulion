// WidgetPublisher — the app's side of the Android home-screen widgets (Android builds only;
// docs/ANDROID.md "Widgety"). Created by main.cpp next to the AppController.
//
// The snapshot (widgetsnapshot.h) of the notebook whose reminders run (WorkspaceReminders::
// doneTarget(): the user's notebook once indexed — never the documentation) goes to
// AndroidBridge::updateWidgets():
//   * 2 s after the notebook's files, favourites or recent pages changed (debounced),
//   * at once when it became ready and when the app leaves the foreground (the widgets are fresh
//     when the home screen shows), and after midnight (a new "today");
//   * closing the notebook on purpose (onboarding) publishes the empty snapshot ("Otwórz
//     Brulion"); another notebook without reminders (the documentation) leaves the last one.
// Entries quick add could not write itself (AndroidBridge::takeWidgetActions()) are written into
// today's journal of that notebook while it is ready (Workspace::appendJournalBlocks), with a
// toast; one the app cannot write either goes back into the queue (requeueWidgetActions()) for
// the next try. After quick add wrote a note itself the reminders are handed over again
// (WorkspaceReminders::resendUpcoming()): its provisional reminder gives way to the app's set.
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>

namespace brulion::shell {
class AppController;
}

namespace brulion::widgets {

struct SnapshotCaches;

class WidgetPublisher : public QObject {
    Q_OBJECT
public:
    explicit WidgetPublisher(shell::AppController *app);
    ~WidgetPublisher() override;

private:
    QString target() const;
    void schedule();
    void publish();
    void publishEmpty();
    void takeActions();
    void armMidnight();

    QPointer<shell::AppController> m_app;
    QTimer m_debounce;
    QTimer m_midnight;
    /// Kept across snapshots: a publish after a save computes the changed notes only.
    std::shared_ptr<SnapshotCaches> m_caches;
};

} // namespace brulion::widgets
