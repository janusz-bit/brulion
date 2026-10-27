#include "desktoptray.h"

#include "appcontroller.h"
#include "appsettings.h"

#include <QAction>
#include <QCoreApplication>
#include <QCursor>
#include <QEvent>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>

using namespace Qt::StringLiterals;

namespace brulion::shell {

DesktopTray::DesktopTray(AppController *app, QObject *parent) : QObject(parent), m_app(app)
{
    if (create())
        return;
    // At login the panel's tray host may come up after Brulion (autostart with --hidden): look
    // again for a while instead of running without a tray for the whole session.
    m_retry = new QTimer(this);
    m_retry->setInterval(1000);
    connect(m_retry, &QTimer::timeout, this, [this] {
        if (create() || ++m_attempts >= MaxAttempts)
            m_retry->stop();
    });
    m_retry->start();
}

bool DesktopTray::create()
{
    if (m_tray || !QSystemTrayIcon::isSystemTrayAvailable())
        return m_tray != nullptr;
    AppController *app = m_app;

    m_menu = new QMenu;
    m_show = m_menu->addAction(QString());
    m_calendar = m_menu->addAction(QString());
    m_today = m_menu->addAction(QString());
    m_capture = m_menu->addAction(QString());
    m_menu->addSeparator();
    m_quit = m_menu->addAction(QString());
    connect(m_show, &QAction::triggered, app, &AppController::showWindow);
    connect(m_calendar, &QAction::triggered, app, [this, app] {
        app->showTrayCalendar(m_tray->geometry(), QCursor::pos());
    });
    connect(m_today, &QAction::triggered, app, [app] {
        app->showWindow();
        app->openToday(); // waits while the notebook is opening; nothing without one
    });
    connect(m_capture, &QAction::triggered, app, &AppController::quickCapture);
    connect(m_quit, &QAction::triggered, app, &AppController::quit);

    m_tray = new QSystemTrayIcon(QIcon(u":/images/logo.svg"_s), this);
    m_tray->setContextMenu(m_menu);
    connect(m_tray, &QSystemTrayIcon::activated, app, [this, app](QSystemTrayIcon::ActivationReason reason) {
        // Windows sends Trigger and then DoubleClick for a double click: the calendar opens for
        // a moment, then the window comes up (and hides it), as with the system clock.
        if (reason == QSystemTrayIcon::DoubleClick)
            app->showWindow();
        else if (reason == QSystemTrayIcon::Trigger && app->settings()->trayClick() == u"calendar")
            app->showTrayCalendar(m_tray->geometry(), QCursor::pos(), true);
        else if (reason == QSystemTrayIcon::Trigger)
            app->requestUiAction(u"toggleWindow"_s);
    });
    connect(app, &AppController::notificationRequested, this, [this](const QString &title, const QString &body) {
        m_tray->showMessage(title, body, QIcon(u":/images/logo.svg"_s), 8000);
    });
    connect(m_tray, &QSystemTrayIcon::messageClicked, app, &AppController::showWindow);

    retranslate();
    // Follow language switches (installing a translator sends LanguageChange to the app object).
    QCoreApplication::instance()->installEventFilter(this);
    m_tray->show();
    Q_EMIT availableChanged(isAvailable());
    return true;
}

DesktopTray::~DesktopTray()
{
    if (m_tray)
        QCoreApplication::instance()->removeEventFilter(this);
    delete m_menu;
}

bool DesktopTray::isAvailable() const
{
    return m_tray && m_tray->isVisible();
}

bool DesktopTray::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == QCoreApplication::instance() && event->type() == QEvent::LanguageChange)
        retranslate();
    return QObject::eventFilter(watched, event);
}

void DesktopTray::retranslate()
{
    if (!m_tray)
        return;
    m_tray->setToolTip(tr("Brulion"));
    m_show->setText(tr("Show Brulion"));
    m_calendar->setText(tr("Calendar"));
    m_today->setText(tr("Today's journal"));
    m_capture->setText(tr("Quick capture…"));
    m_quit->setText(tr("Quit"));
}

} // namespace brulion::shell
