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
    const SoundChoice tone = sound(n.alarm ? SoundKind::Alarm : SoundKind::Reminder);
    const bool silent = tone.mode == SoundChoice::Mode::Silent || tone.mode == SoundChoice::Mode::VibrateOnly;
    if (n.alarm && !silent)
        m_notifier->playTone(true);
    // Not permitted: canShow() is false, and the reminders integration shows it in the page.
    if (!m_notifier->showNow(n.id, n.alarm ? QStringLiteral("⏰ ") + n.title : n.title, n.body, n.path, n.line, n.alarm,
                                                            silent))
        qInfo("Browser notification not shown (no permission)");
}

bool WasmNotifierSink::previewSound(SoundKind kind, const SoundChoice &choice)
{
    const bool silent = choice.mode == SoundChoice::Mode::Silent || choice.mode == SoundChoice::Mode::VibrateOnly;
    if (kind == SoundKind::Alarm) {
        if (!silent)
            m_notifier->playTone(true);
        return !silent;
    }
    // Reminders sound as the browser's notifications do: show a sample.
    if (!m_notifier->showNow(QStringLiteral("brulion-sound-sample"), soundText(SoundText::ReminderSample),
                                            soundText(SoundText::ReminderSampleBody), QString(), -1, false, silent)) {
        Q_EMIT errorOccurred(soundText(SoundText::BrowserRefused));
        return false;
    }
    return true;
}

} // namespace brulion::notify
