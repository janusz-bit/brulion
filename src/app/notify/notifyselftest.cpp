#include "notifyselftest.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "remindersounds.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <qtestsupport_gui.h>

#include <algorithm>
#include <cstdio>
#include <functional>

QT_BEGIN_NAMESPACE
// QtGui's entry point for synthetic mouse input (what QtTest's mouse functions call).
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers mods, int timestamp);
QT_END_NAMESPACE

using namespace Qt::StringLiterals;

namespace brulion::notify {

namespace {

/// Sanitizer builds run many times slower: ctest passes BRULION_TEST_SLOWDOWN (tests/CMakeLists.txt).
int slowdown()
{
    static const int factor = qMax(1, qEnvironmentVariableIntValue("BRULION_TEST_SLOWDOWN"));
    return factor;
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > qint64(timeoutMs) * slowdown())
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

/// Let input, bindings, transitions and queued calls run for `ms` (not scaled).
void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

/// A tap with one finger at `scenePos`, as a touch screen sends it.
void touchTap(QQuickWindow *window, QPointF scenePos)
{
    // QTouchEventSequence's constructor is protected; QtTest's touchEvent() is a subclass too.
    struct Sequence : QTest::QTouchEventSequence {
        Sequence(QWindow *w, QPointingDevice *d) : QTest::QTouchEventSequence(w, d, false) { }
    };
    static QPointingDevice *device = QTest::createTouchDevice();
    Sequence sequence(window, device);
    sequence.press(0, scenePos.toPoint(), window).commit();
    settle(110);
    sequence.release(0, scenePos.toPoint(), window).commit();
    settle(200);
}

/// A left click at `scenePos`, as a real mouse sends it.
void mouseClick(QQuickWindow *window, QPointF scenePos)
{
    static int timestamp = 0;
    timestamp += 1000; // never a double click
    const QPointF global = window->mapToGlobal(scenePos);
    qt_handleMouseEvent(window, scenePos, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, Qt::NoModifier,
                        timestamp);
    qt_handleMouseEvent(window, scenePos, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress,
                        Qt::NoModifier, timestamp += 20);
    qt_handleMouseEvent(window, scenePos, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease,
                        Qt::NoModifier, timestamp += 80);
    settle(200);
}

/// Shown: the item is visible (isVisible() includes its ancestors) and has a size.
bool shown(const QQuickItem *item)
{
    return item && item->isVisible() && item->width() > 0 && item->height() > 0;
}

/// Scrolls the nearest Flickable around `item` so that the item's middle is in its viewport,
/// away from the phone's bottom bar and the floating button.
void ensureVisible(QQuickItem *item)
{
    for (QQuickItem *p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
        if (!p->inherits("QQuickFlickable"))
            continue;
        const qreal mid = item->mapToItem(p, QPointF(item->width() / 2, item->height() / 2)).y();
        if (mid >= p->height() * 0.15 && mid <= p->height() * 0.7)
            return;
        auto *content = p->property("contentItem").value<QQuickItem *>();
        if (!content)
            return;
        const qreal originY = p->property("originY").toReal();
        const qreal y = item->mapToItem(content, QPointF(0, item->height() / 2)).y();
        const qreal maxY = originY + qMax<qreal>(0, p->property("contentHeight").toReal() - p->height());
        p->setProperty("contentY", qBound<qreal>(originY, y - p->height() / 2, maxY));
        settle(200);
        return;
    }
}

/// The items below `root` in the visual tree (Repeater and list delegates are not in the
/// window's QObject tree), depth first.
void visualDescendants(QQuickItem *root, QList<QQuickItem *> &out)
{
    if (!root)
        return;
    for (QQuickItem *child : root->childItems()) {
        out.append(child);
        visualDescendants(child, out);
    }
}

/// Press and release `key` straight to the focused item (as the shell's self-test does).
void sendKey(QQuickWindow *window, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
    settle(100);
}

class SelfTest {
public:
    SelfTest(QQmlApplicationEngine *engine, shell::AppController *app)
        : m_engine(engine), m_app(app), m_touch(QCoreApplication::arguments().contains(u"--touch"_s))
    { }

    int run()
    {
        m_sounds = ReminderSounds::instance();
        m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
        if (!m_window || !m_sounds) {
            check(false, "the main window and the sound settings exist");
            return m_failures;
        }
        if (!waitFor([this] { return m_app->status() == u"ready"; }, 30000)) {
            check(false, "the sample notebook opens");
            return m_failures;
        }
        // The shell is built in the turn that reports "ready"; let its first page apply its route
        // (a queued call) before leaving it, as any user would.
        settle(200);
        m_window->requestActivate();
        waitFor([this] { return m_window->isActive(); }, 2000);
        m_app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"reminders"_s}});
        check(waitFor([this] { return shown(item("reminderSoundMode")) && shown(item("alarmSoundMode")); }),
              "Settings → Reminders shows the sounds of reminders and alarms");
        settle(1200); // the narrow settings scroll to the section while it is laid out
        if (!shown(item("reminderSoundMode")))
            return m_failures;

        silentReminders();
        ringtoneWithoutOne();
        themeSoundForAlarms();
        ringtoneComesBack();
        previewDefault();
        return m_failures;
    }

private:
    void check(bool ok, const char *what)
    {
        std::fprintf(stderr, "selftest: %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++m_failures;
    }

    /// The shown item called `name` (QObject tree, then the visual tree — popups included),
    /// else the first one found.
    QQuickItem *item(const char *name) const
    {
        const QString objectName = QString::fromLatin1(name);
        QList<QQuickItem *> found = m_window->findChildren<QQuickItem *>(objectName);
        QList<QQuickItem *> visual;
        visualDescendants(m_window->contentItem(), visual);
        for (QQuickItem *i : std::as_const(visual)) {
            if (i->objectName() == objectName && !found.contains(i))
                found.append(i);
        }
        for (QQuickItem *i : std::as_const(found)) {
            if (shown(i))
                return i;
        }
        return found.value(0);
    }

    void tap(QQuickItem *target)
    {
        if (!target)
            return;
        ensureVisible(target);
        const QPointF at = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        if (m_touch)
            touchTap(m_window, at);
        else
            mouseClick(m_window, at);
    }

    /// The segment of `mode` in the BSegmented called `control` (the modes' order).
    QQuickItem *segment(const char *control, const QString &mode) const
    {
        QQuickItem *segmented = item(control);
        const qsizetype index = m_sounds->modes().indexOf(mode);
        if (!segmented || index < 0)
            return nullptr;
        QList<QQuickItem *> all;
        visualDescendants(segmented, all);
        QList<QQuickItem *> buttons;
        for (QQuickItem *c : std::as_const(all)) {
            if (c->inherits("QQuickAbstractButton") && shown(c))
                buttons.append(c);
        }
        std::sort(buttons.begin(), buttons.end(),
                  [](QQuickItem *a, QQuickItem *b) { return a->mapToScene({}).x() < b->mapToScene({}).x(); });
        return buttons.value(index);
    }

    QString stored(const char *kind, const char *field) const
    {
        return m_app->settings()
                ->value(u"reminders/sound/"_s + QLatin1String(kind) + u'/' + QLatin1String(field), QString())
                .toString();
    }

    QString summary(const char *kind) const
    {
        QQuickItem *text = item(kind[0] == 'a' ? "alarmSoundSummary" : "reminderSoundSummary");
        return text ? text->property("text").toString() : QString();
    }

    /// The sound menu of a setting is open. (Phones: the settings sections are Repeater
    /// delegates, outside the window's QObject tree — look below the setting's item.)
    bool menuOpen(const char *name) const
    {
        const QString menuName = QString::fromLatin1(name);
        const QString settingName = menuName.left(menuName.indexOf(u"SoundMenu"_s)) + u"SoundSetting"_s;
        QQuickItem *setting = item(settingName.toLatin1().constData());
        QObject *menu = setting ? setting->findChild<QObject *>(menuName) : nullptr;
        return menu && menu->property("opened").toBool();
    }

    // 1. Silent reminders.
    void silentReminders()
    {
        tap(segment("reminderSoundMode", u"silent"_s));
        check(waitFor([this] { return m_sounds->choice(SoundKind::Reminder).mode == SoundChoice::Mode::Silent; }),
              "tapping “Silent” silences reminders");
        check(stored("reminder", "mode") == u"silent", "the choice is stored in the settings");
        QQuickItem *preview = item("reminderSoundPreview");
        check(preview && !preview->isEnabled(), "“Play sample” is off for silence");
        check(summary("reminder") == QCoreApplication::translate("ReminderSoundSetting", "No sound"),
              "the summary says “No sound”");
        check(item("reminderSoundMode")->property("currentValue").toString() == u"silent",
              "the selection shows “Silent”");
    }

    // 2. "Ringtone" for alarms with none chosen yet.
    void ringtoneWithoutOne()
    {
        tap(segment("alarmSoundMode", u"custom"_s));
        check(waitFor([this] { return menuOpen("alarmSoundMenu"); }),
              "“Ringtone” without a ringtone opens the sound menu");
        settle(200);
        check(m_sounds->choice(SoundKind::Alarm).mode == SoundChoice::Mode::Default,
              "… and keeps the alarm sound meanwhile");
        check(item("alarmSoundMode")->property("currentValue").toString() == u"default",
              "the selection stays on “Default”");
    }

    // 3. A theme sound from the menu.
    void themeSoundForAlarms()
    {
        QQuickItem *bell = item("alarmThemeSound_bell");
        check(waitFor([bell] { return shown(bell); }), "the menu lists the sound theme’s “Bell”");
        tap(bell);
        check(waitFor([this] { return m_sounds->choice(SoundKind::Alarm).isCustom(); })
                      && m_sounds->choice(SoundKind::Alarm).uri == u"bell",
              "choosing “Bell” makes it the alarm sound");
        check(stored("alarm", "mode") == u"custom" && stored("alarm", "uri") == u"bell",
              "the alarm sound is stored in the settings");
        check(waitFor([this] { return !menuOpen("alarmSoundMenu"); }), "the menu closes");
        check(summary("alarm")
                      == QCoreApplication::translate("ReminderSoundSetting", "Ringtone: %1")
                                 .arg(QCoreApplication::translate("ReminderSoundSetting", "Bell")),
              "the summary names the sound");
        check(item("alarmSoundMode")->property("currentValue").toString() == u"custom",
              "the selection shows “Ringtone”");
        // The pick button opens the same menu; Escape closes it again.
        tap(item("alarmSoundPick"));
        check(waitFor([this] { return menuOpen("alarmSoundMenu"); }), "“Choose ringtone…” opens the sound menu");
        sendKey(m_window, Qt::Key_Escape);
        check(waitFor([this] { return !menuOpen("alarmSoundMenu"); }), "Escape closes it");
        settle(200);
    }

    // 4. Silent, then "Ringtone" again: the chosen sound comes back.
    void ringtoneComesBack()
    {
        tap(segment("alarmSoundMode", u"silent"_s));
        check(waitFor([this] { return m_sounds->choice(SoundKind::Alarm).mode == SoundChoice::Mode::Silent; }),
              "“Silent” silences alarms");
        tap(segment("alarmSoundMode", u"custom"_s));
        check(waitFor([this] { return m_sounds->choice(SoundKind::Alarm).isCustom(); })
                      && m_sounds->choice(SoundKind::Alarm).uri == u"bell",
              "“Ringtone” brings the chosen sound back");
        settle(300);
        check(!menuOpen("alarmSoundMenu"), "… without the menu");
    }

    // 5. Default reminders and "Play sample".
    void previewDefault()
    {
        tap(segment("reminderSoundMode", u"default"_s));
        check(waitFor([this] { return m_sounds->choice(SoundKind::Reminder).mode == SoundChoice::Mode::Default; }),
              "“Default” brings the default sound back");
        QStringList previewed;
        const QMetaObject::Connection connection =
                QObject::connect(m_sounds, &ReminderSounds::previewRequested, m_sounds,
                                 [&previewed](const QString &kind) { previewed.append(kind); });
        QQuickItem *preview = item("reminderSoundPreview");
        check(preview && preview->isEnabled(), "“Play sample” is on again");
        tap(preview);
        check(waitFor([&previewed] { return !previewed.isEmpty(); }) && previewed == QStringList{u"reminder"_s},
              "“Play sample” plays the reminder sound");
        QObject::disconnect(connection);
    }

    QQmlApplicationEngine *m_engine;
    shell::AppController *m_app;
    ReminderSounds *m_sounds = nullptr;
    QQuickWindow *m_window = nullptr;
    bool m_touch;
    int m_failures = 0;
};

} // namespace

void startSoundsSelfTest(QQmlApplicationEngine *engine, shell::AppController *app)
{
    QTimer::singleShot(0, app, [engine, app] {
        int failures = 0;
        {
            SelfTest test(engine, app);
            failures = test.run();
        }
        std::fprintf(stderr, "selftest: %d failure(s)\n", failures);
        QCoreApplication::exit(failures);
    });
}

} // namespace brulion::notify
