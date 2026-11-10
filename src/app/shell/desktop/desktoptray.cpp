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
using namespace std::chrono_literals;

namespace brulion::shell {

DesktopTray::DesktopTray(AppController *app, QObject *parent) : QObject(parent), m_app(app)
{
    if (create())
        return;
    // At login the panel's tray host may come up after Brulion (autostart with --hidden): look
    // again for a while instead of running without a tray for the whole session.
    auto *retry = new QTimer(this);
    retry->setInterval(1s);
    connect(retry, &QTimer::timeout, this, [this, retry, attempts = 0]() mutable {
        if (create() || ++attempts >= MaxAttempts)
            retry->stop();
    });
    retry->start();
}

bool DesktopTray::create()
{
    if (m_tray || !QSystemTrayIcon::isSystemTrayAvailable())
        return m_tray != nullptr;

    m_menu = std::make_unique<QMenu>();
    m_show = m_menu->addAction(QString());
    m_calendar = m_menu->addAction(QString());
    m_today = m_menu->addAction(QString());
    m_capture = m_menu->addAction(QString());
    m_menu->addSeparator();
    m_quit = m_menu->addAction(QString());
    connect(m_show, &QAction::triggered, m_app, &AppController::showWindow);
    connect(m_calendar, &QAction::triggered, m_app,
            [this] { m_app->showTrayCalendar(m_tray->geometry(), QCursor::pos()); });
    connect(m_today, &QAction::triggered, m_app, [this] {
        m_app->showWindow();
        m_app->openToday(); // waits while the notebook is opening; nothing without one
    });
    connect(m_capture, &QAction::triggered, m_app, &AppController::quickCapture);
    connect(m_quit, &QAction::triggered, m_app, &AppController::quit);

    m_tray = new QSystemTrayIcon(QIcon(u":/images/logo.svg"_s), this);
    m_tray->setContextMenu(m_menu.get());
    connect(m_tray, &QSystemTrayIcon::activated, m_app, [this](QSystemTrayIcon::ActivationReason reason) {
        // Windows sends Trigger and then DoubleClick for a double click: the calendar opens for
        // a moment, then the window comes up (and hides it), as with the system clock.
        if (reason == QSystemTrayIcon::DoubleClick)
            m_app->showWindow();
        else if (reason == QSystemTrayIcon::Trigger && m_app->settings()->trayClick() == u"calendar")
            m_app->showTrayCalendar(m_tray->geometry(), QCursor::pos(), true);
        else if (reason == QSystemTrayIcon::Trigger)
            m_app->requestUiAction(u"toggleWindow"_s);
    });
    connect(m_app, &AppController::notificationRequested, this, [this](const QString &title, const QString &body) {
        m_tray->showMessage(title, body, m_tray->icon(), 8000);
    });
    connect(m_tray, &QSystemTrayIcon::messageClicked, m_app, &AppController::showWindow);

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
