// DesktopTray — Brulion's system tray icon on the Linux desktop (Qt Widgets: needs QApplication).
//
// Menu: Show Brulion · Today's journal · Quick capture… · Quit. A click on the icon shows (or,
// when it is in front, hides) the window. With AppSettings.closeToTray the window's close button
// only hides it (Main.qml) so reminders keep firing; `brulion --hidden` starts in the tray.
// Also shows AppController::notificationRequested() as tray messages (the test notification;
// the reminders integration may route its notifications here as a fallback for D-Bus).
// Without a tray at start (the panel at login may come up after an autostarted Brulion) it
// looks again once a second for MaxAttempts seconds; availableChanged() reports the late icon.
#pragma once

#include <QObject>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QTimer;

namespace brulion::shell {

class AppController;

class DesktopTray : public QObject {
    Q_OBJECT
public:
    explicit DesktopTray(AppController *app, QObject *parent = nullptr);
    ~DesktopTray() override;

    /// A tray icon is shown (false without a system tray, e.g. offscreen runs).
    bool isAvailable() const;

    /// How often (once a second) a missing tray is looked for again after start.
    static constexpr int MaxAttempts = 30;

Q_SIGNALS:
    /// The tray icon appeared later (the panel started after Brulion).
    void availableChanged(bool available);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool create();
    void retranslate();

    AppController *m_app;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_show = nullptr;
    QAction *m_today = nullptr;
    QAction *m_capture = nullptr;
    QAction *m_quit = nullptr;
    QTimer *m_retry = nullptr;
    int m_attempts = 0;
};

} // namespace brulion::shell
