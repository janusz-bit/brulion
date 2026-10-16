#include "soundchoice.h"

#include <QDir>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace brulion::notify {

QString soundKindKey(SoundKind kind)
{
    return kind == SoundKind::Alarm ? u"alarm"_s : u"reminder"_s;
}

bool soundKindFromKey(const QString &key, SoundKind *kind)
{
    SoundKind k;
    if (key == u"reminder")
        k = SoundKind::Reminder;
    else if (key == u"alarm")
        k = SoundKind::Alarm;
    else
        return false;
    if (kind)
        *kind = k;
    return true;
}

QString soundModeKey(SoundChoice::Mode mode)
{
    switch (mode) {
    case SoundChoice::Mode::Custom: return u"custom"_s;
    case SoundChoice::Mode::VibrateOnly: return u"vibrate"_s;
    case SoundChoice::Mode::Silent: return u"silent"_s;
    case SoundChoice::Mode::Default: break;
    }
    return u"default"_s;
}

bool soundModeFromKey(const QString &key, SoundChoice::Mode *mode)
{
    SoundChoice::Mode m = SoundChoice::Mode::Default;
    bool ok = true;
    if (key == u"custom")
        m = SoundChoice::Mode::Custom;
    else if (key == u"vibrate")
        m = SoundChoice::Mode::VibrateOnly;
    else if (key == u"silent")
        m = SoundChoice::Mode::Silent;
    else
        ok = key == u"default";
    if (mode)
        *mode = m;
    return ok;
}

SoundChoice normalizedSound(SoundChoice choice)
{
    choice.uri = choice.uri.trimmed();
    choice.title = choice.title.simplified();
    if (choice.mode == SoundChoice::Mode::Custom && choice.uri.isEmpty())
        choice.mode = SoundChoice::Mode::Default;
    return choice;
}

QVariantMap soundToVariant(const SoundChoice &choice)
{
    return {
            {u"mode"_s, soundModeKey(choice.mode)},
            {u"uri"_s, choice.uri},
            {u"title"_s, choice.title},
    };
}

SoundChoice soundFromVariant(const QVariantMap &map)
{
    SoundChoice choice;
    soundModeFromKey(map.value(u"mode"_s).toString(), &choice.mode);
    choice.uri = map.value(u"uri"_s).toString();
    choice.title = map.value(u"title"_s).toString();
    return normalizedSound(choice);
}

bool isSoundFile(const QString &uri)
{
    return uri.startsWith(u"file:"_s, Qt::CaseInsensitive) || QDir::isAbsolutePath(uri);
}

QString soundFilePath(const QString &uri)
{
    if (uri.startsWith(u"file:"_s, Qt::CaseInsensitive))
        return QUrl(uri).toLocalFile();
    return QDir::isAbsolutePath(uri) ? QDir::cleanPath(uri) : QString();
}

QStringList desktopThemeSounds()
{
    return {u"message-new-instant"_s, u"bell"_s, u"complete"_s, u"alarm-clock-elapsed"_s, u"phone-incoming-call"_s};
}

QString defaultAlarmThemeSound()
{
    return u"alarm-clock-elapsed"_s;
}

void applyDesktopSoundHints(QVariantMap &hints, const SoundChoice &choice, SoundKind kind)
{
    hints.remove(u"sound-name"_s);
    hints.remove(u"sound-file"_s);
    hints.remove(u"suppress-sound"_s);
    switch (choice.mode) {
    case SoundChoice::Mode::Silent:
    case SoundChoice::Mode::VibrateOnly: // desktops do not vibrate: nothing to hear either
        hints.insert(u"suppress-sound"_s, true);
        return;
    case SoundChoice::Mode::Custom:
        if (choice.uri.isEmpty())
            break;
        if (isSoundFile(choice.uri)) {
            const QString path = soundFilePath(choice.uri);
            if (!path.isEmpty()) {
                hints.insert(u"sound-file"_s, path);
                return;
            }
        } else {
            hints.insert(u"sound-name"_s, choice.uri);
            return;
        }
        break;
    case SoundChoice::Mode::Default: break;
    }
    // The default: alarms ring like an alarm clock, reminders sound as the desktop decides.
    if (kind == SoundKind::Alarm)
        hints.insert(u"sound-name"_s, defaultAlarmThemeSound());
}

} // namespace brulion::notify
