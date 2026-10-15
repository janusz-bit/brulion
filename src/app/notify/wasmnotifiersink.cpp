#include "wasmnotifiersink.h"

#include "wasm/wasmnotifier.h"

namespace brulion::notify {

WasmNotifierSink::WasmNotifierSink(QObject *parent) : Notifier(parent), m_notifier(new platform::WasmNotifier(this))
{
    connect(m_notifier, &platform::WasmNotifier::activated, this,
            [this](const QString &, const QString &path, int line) {
                if (!path.isEmpty())
                    Q_EMIT activated(path, line);
            });
}

WasmNotifierSink::~WasmNotifierSink() = default;

QString WasmNotifierSink::backend() const
{
    return QStringLiteral("browser");
}

bool WasmNotifierSink::canShow() const
{
    return m_notifier->permission() == platform::WasmNotifier::Permission::Granted;
}

void WasmNotifierSink::showNow(const ScheduledNotification &n)
{
    // Not permitted: canShow() is false, and the reminders integration shows it in the page.
    if (!m_notifier->showNow(n.id, n.alarm ? QStringLiteral("⏰ ") + n.title : n.title, n.body, n.path, n.line,
                             n.alarm))
        qInfo("Browser notification not shown (no permission)");
}

} // namespace brulion::notify
