#include "windowstoastbus.h"

#include "toastxml.h"

#include "log/categories.h"

#include <QCoreApplication>
#include <QHash>
#include <QMetaObject>
#include <QPointer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// CoInitializeEx (not in WIN32_LEAN_AND_MEAN); after windows.h.
#include <objbase.h>

#pragma warning(push, 0)
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#pragma warning(pop)

using namespace Qt::StringLiterals;

namespace wn = winrt::Windows::UI::Notifications;
namespace wx = winrt::Windows::Data::Xml::Dom;
namespace wf = winrt::Windows::Foundation;

namespace brulion::notify {

namespace {

winrt::hstring toHString(const QString &text)
{
    return winrt::hstring(reinterpret_cast<const wchar_t *>(text.constData()), uint32_t(text.size()));
}

QString fromHString(const winrt::hstring &text)
{
    return QString::fromWCharArray(text.c_str(), qsizetype(text.size()));
}

QString describe(const winrt::hresult_error &error)
{
    return u"0x%1 %2"_s.arg(quint32(error.code().value), 8, 16, QLatin1Char('0')).arg(fromHString(error.message()));
}

const wchar_t *const Group = L"brulion";

/// Runs `call` on the GUI thread (WinRT raises the toasts' events on a thread pool thread).
template<typename Call>
void onGuiThread(Call call)
{
    if (QCoreApplication *app = QCoreApplication::instance())
        QMetaObject::invokeMethod(app, std::move(call), Qt::QueuedConnection);
}

} // namespace

struct WindowsToastBus::Private {
    struct Shown {
        wn::ToastNotification toast{nullptr};
        winrt::event_token activated;
        winrt::event_token dismissed;
        winrt::event_token failed;
        quint64 generation = 0;
    };

    wn::ToastNotifier notifier{nullptr};
    QHash<uint, Shown> shown;
    quint64 generation = 0;
};

WindowsToastBus::WindowsToastBus(const QString &appUserModelId, QObject *parent)
    : NotificationBus(parent), d(std::make_unique<Private>()), m_appUserModelId(appUserModelId)
{
    // Qt's Windows platform has made the GUI thread a single-threaded apartment already; a
    // QCoreApplication (tests) has not. A second call only counts up (S_FALSE).
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        qCWarning(brulion::logging::lcReminders) << "Windows notifications: COM not initialised" << Qt::hex << com;
    try {
        d->notifier = wn::ToastNotificationManager::CreateToastNotifier(toHString(appUserModelId));
    } catch (const winrt::hresult_error &error) {
        m_lastError = describe(error);
        qCWarning(brulion::logging::lcReminders).noquote()
                << u"Windows notifications unavailable (%1): reminders go to the tray icon"_s.arg(m_lastError);
    }
    // No click on a toast reaches Brulion after it quit: none stays behind in the Action Center.
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &WindowsToastBus::clearAll);
}

WindowsToastBus::~WindowsToastBus()
{
    clearAll();
}

bool WindowsToastBus::isAvailable() const
{
    if (!d->notifier)
        return false;
    try {
        return d->notifier.Setting() == wn::NotificationSetting::Enabled;
    } catch (const winrt::hresult_error &) {
        return false;
    }
}

QStringList WindowsToastBus::capabilities() const
{
    // No "body-markup": toast text is never markup (buildToastXml() escapes everything).
    return {u"actions"_s, u"body"_s, u"sound"_s, u"persistence"_s};
}

QString WindowsToastBus::name() const
{
    return u"toast"_s;
}

QStringList WindowsToastBus::themeSounds() const
{
    return windowsToastSounds();
}

void WindowsToastBus::notify(const DesktopNotification &notification, std::function<void(uint id)> done)
{
    uint id = notification.replacesId;
    if (id == 0) {
        if (++m_lastId == 0)
            m_lastId = 1;
        id = m_lastId;
    }
    // Turned off for Brulion (or all apps): Show() would succeed and show nothing — the tray
    // shows it instead (DesktopNotifier's fallback for a refused notification).
    if (!isAvailable()) {
        done(0);
        return;
    }
    try {
        wx::XmlDocument xml;
        xml.LoadXml(toHString(buildToastXml(notification, id)));
        wn::ToastNotification toast(xml);
        toast.Tag(toHString(QString::number(id)));
        toast.Group(Group);

        // The toast it replaces says nothing any more (its Dismissed must not close this one).
        forget(id);
        Private::Shown shown;
        shown.toast = toast;
        shown.generation = ++d->generation;
        const quint64 generation = shown.generation;
        const QPointer<WindowsToastBus> self(this);
        shown.activated = toast.Activated([self](const wn::ToastNotification &, const wf::IInspectable &args) {
            QString arguments;
            if (const auto activated = args.try_as<wn::ToastActivatedEventArgs>())
                arguments = fromHString(activated.Arguments());
            onGuiThread([self, arguments] {
                if (self)
                    self->onActivated(arguments);
            });
        });
        shown.dismissed = toast.Dismissed(
                [self, id, generation](const wn::ToastNotification &, const wn::ToastDismissedEventArgs &args) {
                    uint reason = 4;
                    switch (args.Reason()) {
                    case wn::ToastDismissalReason::UserCanceled: reason = 2; break;
                    case wn::ToastDismissalReason::ApplicationHidden: reason = 3; break;
                    case wn::ToastDismissalReason::TimedOut: reason = 1; break;
                    }
                    onGuiThread([self, id, generation, reason] {
                        if (self)
                            self->onDismissed(id, generation, reason);
                    });
                });
        shown.failed = toast.Failed(
                [self, id, generation](const wn::ToastNotification &, const wn::ToastFailedEventArgs &args) {
                    const QString error = u"0x%1"_s.arg(quint32(args.ErrorCode().value), 8, 16, QLatin1Char('0'));
                    onGuiThread([self, id, generation, error] {
                        if (self)
                            self->onFailed(id, generation, error);
                    });
                });
        d->shown.insert(id, shown);
        d->notifier.Show(toast);
        m_lastError.clear();
    } catch (const winrt::hresult_error &error) {
        forget(id);
        m_lastError = describe(error);
        qCWarning(brulion::logging::lcReminders).noquote() << u"Windows notification not shown: %1"_s.arg(m_lastError);
        done(0);
        return;
    }
    done(id);
}

void WindowsToastBus::close(uint id)
{
    forget(id);
    try {
        wn::ToastNotificationManager::History().Remove(toHString(QString::number(id)), Group,
                                                       toHString(m_appUserModelId));
    } catch (const winrt::hresult_error &error) {
        m_lastError = describe(error);
    }
}

void WindowsToastBus::clearAll()
{
    const QList<uint> ids = d->shown.keys();
    for (const uint id : ids)
        forget(id);
    if (!d->notifier)
        return;
    try {
        wn::ToastNotificationManager::History().Clear(toHString(m_appUserModelId));
    } catch (const winrt::hresult_error &error) {
        m_lastError = describe(error);
    }
}

QStringList WindowsToastBus::historyTags() const
{
    QStringList tags;
    try {
        const auto history = wn::ToastNotificationManager::History().GetHistory(toHString(m_appUserModelId));
        for (const wn::ToastNotification &toast : history)
            tags.append(fromHString(toast.Tag()));
    } catch (const winrt::hresult_error &) { }
    return tags;
}

void WindowsToastBus::onActivated(const QString &arguments)
{
    const ToastActivation activation = parseToastArguments(arguments);
    if (!activation.valid) {
        qCWarning(brulion::logging::lcReminders) << "Ignoring a malformed toast activation";
        return;
    }
    Q_EMIT actionInvoked(activation.id, activation.action);
}

void WindowsToastBus::onDismissed(uint id, quint64 generation, uint reason)
{
    const auto it = d->shown.constFind(id);
    if (it == d->shown.constEnd() || it->generation != generation)
        return; // replaced or closed meanwhile
    // Timed out: it waits in the Action Center, where a click still opens the note.
    if (reason == 1)
        return;
    forget(id);
    Q_EMIT notificationClosed(id, reason);
}

void WindowsToastBus::onFailed(uint id, quint64 generation, const QString &error)
{
    const auto it = d->shown.constFind(id);
    if (it == d->shown.constEnd() || it->generation != generation)
        return;
    m_lastError = error;
    qCWarning(brulion::logging::lcReminders).noquote() << u"Windows notification failed: %1"_s.arg(error);
    forget(id);
    Q_EMIT notificationClosed(id, 4);
}

void WindowsToastBus::forget(uint id)
{
    const auto it = d->shown.find(id);
    if (it == d->shown.end())
        return;
    try {
        it->toast.Activated(it->activated);
        it->toast.Dismissed(it->dismissed);
        it->toast.Failed(it->failed);
    } catch (const winrt::hresult_error &) { }
    d->shown.erase(it);
}

} // namespace brulion::notify
