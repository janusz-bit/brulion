// WasmNotifierSink — reminders in the browser (WebAssembly builds only) through
// platform::WasmNotifier (the web Notification API; the service worker shows them where the page
// may not). Browsers deliver nothing once the page is closed: firesWhenClosed() is false and the
// ReminderScheduler calls showNow() when a reminder is due. Clicks arrive as activated() (the
// AppController's own WasmNotifier also opens the note).
#pragma once

#include "notifier.h"

namespace brulion::platform {
class WasmNotifier;
}

namespace brulion::notify {

class WasmNotifierSink : public Notifier {
    Q_OBJECT
public:
    explicit WasmNotifierSink(QObject *parent = nullptr);
    ~WasmNotifierSink() override;

    QString backend() const override;
    bool canShow() const override;

    bool firesWhenClosed() const override { return false; }
    void showNow(const ScheduledNotification &notification) override;

private:
    platform::WasmNotifier *m_notifier = nullptr;
};

} // namespace brulion::notify
