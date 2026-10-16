#include "remindersounds.h"

#include "notifier.h"

#include <QUrl>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::notify {

namespace {

QPointer<ReminderSounds> s_instance;

constexpr SoundKind kKinds[] = {SoundKind::Reminder, SoundKind::Alarm};

} // namespace

ReminderSounds::ReminderSounds(QObject *parent) : QObject(parent)
{
    s_instance = this;
}

ReminderSounds::~ReminderSounds() = default;

ReminderSounds *ReminderSounds::instance()
{
    return s_instance.data();
}

QString ReminderSounds::settingsKey(SoundKind kind, const QString &field)
{
    return u"reminders/sound/"_s + soundKindKey(kind) + u'/' + field;
}

void ReminderSounds::setStorage(Load reader, Store writer)
{
    m_load = std::move(reader);
    m_store = std::move(writer);
    loadChoices();
    Q_EMIT changed();
    apply();
}

void ReminderSounds::setNotifier(Notifier *notifier)
{
    if (m_notifier == notifier)
        return;
    disconnect(m_pickedConnection);
    m_notifier = notifier;
    if (notifier) {
        m_pickedConnection = connect(notifier, &Notifier::soundPicked, this,
                                     [this](SoundKind kind, const SoundChoice &picked) { setChoice(kind, picked); });
    }
    // A mode this platform does not offer (settings copied from another device) plays the default.
    for (SoundKind kind : kKinds) {
        SoundChoice &c = kind == SoundKind::Alarm ? m_alarm : m_reminder;
        if (!offers(c.mode))
            c.mode = SoundChoice::Mode::Default;
    }
    Q_EMIT capabilitiesChanged();
    Q_EMIT changed();
    apply();
}

void ReminderSounds::loadChoices()
{
    for (SoundKind kind : kKinds) {
        SoundChoice c;
        if (m_load) {
            soundModeFromKey(m_load(settingsKey(kind, u"mode"_s)).toString(), &c.mode);
            c.uri = m_load(settingsKey(kind, u"uri"_s)).toString();
            c.title = m_load(settingsKey(kind, u"title"_s)).toString();
        }
        c = normalizedSound(c);
        if (m_notifier && !offers(c.mode))
            c.mode = SoundChoice::Mode::Default;
        (kind == SoundKind::Alarm ? m_alarm : m_reminder) = c;
    }
}

void ReminderSounds::saveChoice(SoundKind kind)
{
    if (!m_store)
        return;
    const SoundChoice c = choice(kind);
    m_store(settingsKey(kind, u"mode"_s), soundModeKey(c.mode));
    m_store(settingsKey(kind, u"uri"_s), c.uri);
    m_store(settingsKey(kind, u"title"_s), c.title);
}

void ReminderSounds::apply()
{
    if (m_notifier)
        m_notifier->setSounds(m_reminder, m_alarm);
}

bool ReminderSounds::offers(SoundChoice::Mode mode) const
{
    if (!m_notifier)
        return mode == SoundChoice::Mode::Default || mode == SoundChoice::Mode::Silent;
    return m_notifier->soundModes().contains(mode);
}

SoundChoice ReminderSounds::choice(SoundKind kind) const
{
    return kind == SoundKind::Alarm ? m_alarm : m_reminder;
}

bool ReminderSounds::setChoice(SoundKind kind, const SoundChoice &choice)
{
    const SoundChoice c = normalizedSound(choice);
    if (!offers(c.mode))
        return false;
    SoundChoice &current = kind == SoundKind::Alarm ? m_alarm : m_reminder;
    if (current == c)
        return true;
    current = c;
    saveChoice(kind);
    Q_EMIT changed();
    apply();
    return true;
}

QStringList ReminderSounds::modes() const
{
    const QList<SoundChoice::Mode> offered =
            m_notifier ? m_notifier->soundModes()
                       : QList<SoundChoice::Mode>{SoundChoice::Mode::Default, SoundChoice::Mode::Silent};
    QStringList keys;
    for (SoundChoice::Mode mode : offered)
        keys.append(soundModeKey(mode));
    return keys;
}

bool ReminderSounds::systemPicker() const
{
    return m_notifier && m_notifier->hasSystemSoundPicker();
}

bool ReminderSounds::systemSettings() const
{
    return m_notifier && m_notifier->hasSystemSoundSettings();
}

bool ReminderSounds::soundFiles() const
{
    return m_notifier && m_notifier->supportsSoundFiles();
}

QStringList ReminderSounds::themeSounds() const
{
    return m_notifier ? m_notifier->themeSounds() : QStringList();
}

bool ReminderSounds::setMode(const QString &kind, const QString &mode)
{
    SoundKind k;
    SoundChoice::Mode m;
    if (!soundKindFromKey(kind, &k) || !soundModeFromKey(mode, &m))
        return false;
    SoundChoice c = choice(k);
    if (m == SoundChoice::Mode::Custom && c.uri.isEmpty())
        return false; // nothing chosen yet: the page opens the picker
    c.mode = m;
    return setChoice(k, c);
}

bool ReminderSounds::setCustom(const QString &kind, const QString &uri, const QString &title)
{
    SoundKind k;
    if (!soundKindFromKey(kind, &k))
        return false;
    SoundChoice c;
    c.mode = SoundChoice::Mode::Custom;
    c.uri = uri.trimmed();
    c.title = title.simplified();
    if (c.uri.isEmpty())
        return false;
    if (c.title.isEmpty() && isSoundFile(c.uri))
        c.title = QUrl::fromLocalFile(soundFilePath(c.uri)).fileName();
    // Linux: a sound file or one of the offered theme sounds (Android: any ringtone URI).
    const QStringList names = themeSounds();
    if (!isSoundFile(c.uri) && !names.isEmpty() && !names.contains(c.uri))
        return false;
    return setChoice(k, c);
}

bool ReminderSounds::pick(const QString &kind)
{
    SoundKind k;
    return soundKindFromKey(kind, &k) && m_notifier && m_notifier->hasSystemSoundPicker() && m_notifier->pickSound(k);
}

bool ReminderSounds::preview(const QString &kind)
{
    SoundKind k;
    if (!soundKindFromKey(kind, &k))
        return false;
    Q_EMIT previewRequested(kind);
    return m_notifier && m_notifier->previewSound(k, choice(k));
}

bool ReminderSounds::openSystemSettings(const QString &kind)
{
    SoundKind k;
    return soundKindFromKey(kind, &k) && m_notifier && m_notifier->hasSystemSoundSettings()
           && m_notifier->openSystemSoundSettings(k);
}

} // namespace brulion::notify
