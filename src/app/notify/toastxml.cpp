#include "toastxml.h"

#include <QXmlStreamWriter>

using namespace Qt::StringLiterals;

namespace brulion::notify {

namespace {

const QString ArgumentsPrefix = u"brulion-toast:"_s;

/// Without the characters XML 1.0 cannot carry (a note may hold any of them).
QString xmlSafe(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
            out.append(c);
            out.append(text.at(++i));
            continue;
        }
        const char16_t u = c.unicode();
        const bool allowed =
                u == 0x9 || u == 0xA || u == 0xD || (u >= 0x20 && u <= 0xD7FF) || (u >= 0xE000 && u <= 0xFFFD);
        if (allowed)
            out.append(c);
    }
    return out;
}

bool isActionKey(const QString &key)
{
    return key == u"default" || key == u"snooze" || key == u"ok" || key == u"done";
}

} // namespace

QStringList windowsToastSounds()
{
    QStringList sounds{u"Default"_s, u"IM"_s, u"Mail"_s, u"Reminder"_s, u"SMS"_s, u"Looping.Alarm"_s};
    for (int i = 2; i <= 10; ++i)
        sounds.append(u"Looping.Alarm%1"_s.arg(i));
    sounds.append(u"Looping.Call"_s);
    for (int i = 2; i <= 10; ++i)
        sounds.append(u"Looping.Call%1"_s.arg(i));
    return sounds;
}

QString windowsToastSoundFor(const QString &soundName)
{
    if (windowsToastSounds().contains(soundName))
        return soundName;
    // Freedesktop names (desktopThemeSounds(); a setting carried over from Linux, and the default
    // of alarms) with a Windows counterpart.
    if (soundName == u"alarm-clock-elapsed")
        return u"Looping.Alarm"_s;
    if (soundName == u"phone-incoming-call")
        return u"Looping.Call"_s;
    if (soundName == u"message-new-instant")
        return u"IM"_s;
    return {};
}

QString toastArguments(uint id, const QString &action)
{
    return ArgumentsPrefix + u"id="_s + QString::number(id) + u"&action="_s + action;
}

ToastActivation parseToastArguments(const QString &arguments)
{
    ToastActivation result;
    if (!arguments.startsWith(ArgumentsPrefix))
        return result;
    const QStringList parts = arguments.mid(ArgumentsPrefix.size()).split(u'&');
    if (parts.size() != 2 || !parts.at(0).startsWith(u"id=") || !parts.at(1).startsWith(u"action="))
        return result;
    const QString digits = parts.at(0).mid(3);
    if (digits.isEmpty() || digits.size() > 10)
        return result;
    for (const QChar c : digits) {
        if (c < u'0' || c > u'9')
            return result;
    }
    bool ok = false;
    const qulonglong id = digits.toULongLong(&ok);
    if (!ok || id == 0 || id > 0xFFFFFFFFull)
        return result;
    const QString action = parts.at(1).mid(7);
    if (!isActionKey(action))
        return result;
    result.valid = true;
    result.id = uint(id);
    result.action = action;
    return result;
}

QString buildToastXml(const DesktopNotification &notification, uint id)
{
    const QVariantMap &hints = notification.hints;
    const bool alarm = hints.value(u"urgency"_s).value<uchar>() >= 2 || hints.value(u"resident"_s).toBool();

    // The buttons: every action but "default" (the click on the toast itself).
    QList<QPair<QString, QString>> buttons;
    for (qsizetype i = 0; i + 1 < notification.actions.size(); i += 2) {
        const QString &key = notification.actions.at(i);
        if (key != u"default" && isActionKey(key))
            buttons.append({key, notification.actions.at(i + 1)});
    }

    QString xml;
    QXmlStreamWriter w(&xml);
    w.writeStartElement(u"toast"_s);
    w.writeAttribute(u"launch"_s, toastArguments(id, u"default"_s));
    // Alarms and reminders that wait for the user stay on screen; Windows wants a button for that.
    QString scenario;
    if (!buttons.isEmpty() && alarm)
        scenario = u"alarm"_s;
    else if (!buttons.isEmpty() && notification.expireTimeout == 0)
        scenario = u"reminder"_s;
    if (!scenario.isEmpty())
        w.writeAttribute(u"scenario"_s, scenario);
    else if (!hints.value(u"transient"_s).toBool())
        w.writeAttribute(u"duration"_s, u"long"_s);

    w.writeStartElement(u"visual"_s);
    w.writeStartElement(u"binding"_s);
    w.writeAttribute(u"template"_s, u"ToastGeneric"_s);
    w.writeTextElement(u"text"_s, xmlSafe(notification.summary));
    const QString body = xmlSafe(notification.body);
    if (!body.isEmpty())
        w.writeTextElement(u"text"_s, body);
    w.writeEndElement(); // binding
    w.writeEndElement(); // visual

    if (!buttons.isEmpty()) {
        w.writeStartElement(u"actions"_s);
        for (const auto &[key, label] : std::as_const(buttons)) {
            w.writeEmptyElement(u"action"_s);
            w.writeAttribute(u"content"_s, xmlSafe(label));
            w.writeAttribute(u"arguments"_s, toastArguments(id, key));
            w.writeAttribute(u"activationType"_s, u"foreground"_s);
        }
        w.writeEndElement(); // actions
    }

    if (hints.value(u"suppress-sound"_s).toBool()) {
        w.writeEmptyElement(u"audio"_s);
        w.writeAttribute(u"silent"_s, u"true"_s);
    } else if (const QString sound = windowsToastSoundFor(hints.value(u"sound-name"_s).toString()); !sound.isEmpty()) {
        w.writeEmptyElement(u"audio"_s);
        w.writeAttribute(u"src"_s, u"ms-winsoundevent:Notification."_s + sound);
        // Looping sounds loop only while an alarm stays on screen (Windows wants a long or alarm toast).
        if (scenario == u"alarm" && sound.startsWith(u"Looping."))
            w.writeAttribute(u"loop"_s, u"true"_s);
    }
    w.writeEndElement(); // toast
    return xml;
}

} // namespace brulion::notify
