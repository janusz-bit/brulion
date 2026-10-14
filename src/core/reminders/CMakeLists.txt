# reminders — notifications for ⏰ reminders / ⏰! alarms (upcomingNotifications, NotificationSink),
# the in-app ReminderScheduler, calendar entries (calendarItems) and snoozeReminder
# (docs/SYNTAX.md §5-§7). Public headers: reminders.h (overview), notifications.h, scheduler.h,
# calendar.h, snooze.h; internal helpers in reminders_p.h.
brulion_add_core_module(reminders
    SOURCES
        reminders.h
        reminders_p.h support.cpp
        notifications.h notifications.cpp
        scheduler.h scheduler.cpp
        calendar.h calendar.cpp
        snooze.h snooze.cpp
    DEPS util text dates markdown index vault)
