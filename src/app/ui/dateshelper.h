// DatesHelper — the core dates module (src/core/dates) for QML: natural-language input,
// human formatting, recurrence rules, localised names and month grids for BDatePicker /
// BReminderDialog. QML singleton `DatesHelper` in module Brulion (registered by qmlforeign.h;
// the class itself is QML-free, library brulion_ui_helpers).
//
// Conventions (all values are plain strings so QML never touches JavaScript Date / UTC):
//   date  "YYYY-MM-DD"            time "HH:mm" (24 h)            spec "YYYY-MM-DD[ HH:mm]"
//   locale "pl" | "en" (UI language of generated texts; empty argument = the `locale` property)
//   weekdays 1 = Monday … 7 = Sunday (Qt::DayOfWeek)
//   rule object (formatRule / parseRule):
//     { freq: "day"|"week"|"month"|"year", interval: int ≥ 1, weekdays: [1..7],
//       monthDay: int (0 = none), lastDayOfMonth: bool, weekdaysOnly: bool, whenDone: bool }
//
// Nothing here caches "now": every call reads the clock (or the fixed `now` set by the dev
// tools for reproducible screenshots); `today` is re-emitted after midnight.
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::ui {

class DatesHelper : public QObject {
    Q_OBJECT

    /// UI language of generated texts: "pl" (default) or "en".
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// First day of the week (1 = Monday … 7 = Sunday) for month grids and "next week".
    Q_PROPERTY(int weekStart READ weekStart WRITE setWeekStart NOTIFY weekStartChanged FINAL)
    /// Today's date "YYYY-MM-DD"; changes at midnight.
    Q_PROPERTY(QString today READ today NOTIFY todayChanged FINAL)
    /// Fixed "now" ("YYYY-MM-DD HH:mm") for tests/screenshots; empty = the real clock.
    Q_PROPERTY(QString fixedNow READ fixedNow WRITE setFixedNow NOTIFY todayChanged FINAL)

public:
    explicit DatesHelper(QObject *parent = nullptr);

    /// Defaults for instances created afterwards (set by main/DevTools before QML loads).
    static void setDefaults(const QString &locale, const QString &fixedNow);

    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    int weekStart() const { return m_weekStart; }
    void setWeekStart(int weekStart);
    QString today() const;
    QString fixedNow() const;
    void setFixedNow(const QString &now);

    /// Current local date-time "YYYY-MM-DD HH:mm".
    Q_INVOKABLE QString now() const;

    /// First date/time phrase in `text` ("jutro o 9", "next friday 6pm"):
    /// { ok, date, time ("" when none), hasTime, spec, display ("czw, 15 paź 09:00"),
    ///   human ("jutro 09:00"), relative (yesterday…tomorrow), relativeWord ("jutro"),
    ///   matched, start, length }. ok = false when nothing is found.
    Q_INVOKABLE QVariantMap parseNatural(const QString &text) const;

    /// "dziś 09:00", "pt 17 paź", "17 paź 2027" ("today 09:00", "Fri, Oct 17"). Empty if invalid.
    Q_INVOKABLE QString formatHuman(const QString &spec) const;
    /// Always with weekday, no relative words: "czw, 15 paź 09:00" / "Thu, Oct 15 09:00"
    /// (year added when not the current year).
    Q_INVOKABLE QString formatShort(const QString &spec) const;
    /// Full sentence form: "czwartek, 15 października 2026" / "Thursday, October 15, 2026".
    Q_INVOKABLE QString formatLong(const QString &date) const;
    /// "październik 2026" / "October 2026" (nominative month, for calendar headers).
    Q_INVOKABLE QString monthTitle(int year, int month) const;

    /// Rule object -> text in `locale` ("en" = canonical text written into files). Empty when invalid.
    Q_INVOKABLE QString formatRule(const QVariantMap &rule, const QString &locale = {}) const;
    /// Any supported phrasing (canonical English or Polish) -> label in the UI locale
    /// ("every week on Monday" -> "co tydzień w poniedziałek"). Empty when not a rule.
    Q_INVOKABLE QString ruleLabel(const QString &text) const;
    /// Any supported phrasing -> canonical English ("co tydzień w pn" -> "every week on Monday").
    Q_INVOKABLE QString canonicalRule(const QString &text) const;
    /// Rule text -> rule object + { ok, canonical, label }.
    Q_INVOKABLE QVariantMap parseRule(const QString &text) const;
    /// Next `count` occurrences (specs) of a rule anchored at `fromSpec`, strictly after it.
    Q_INVOKABLE QStringList nextOccurrences(const QVariant &rule, const QString &fromSpec, int count = 3) const;

    /// Weekday names, index 0 = Monday. form: "short" ("pn"/"Mon"), "narrow" ("P"/"M"),
    /// "full" ("poniedziałek"/"Monday").
    Q_INVOKABLE QStringList weekdayNames(const QString &form = QStringLiteral("short"),
                                                                              const QString &locale = {}) const;
    /// Month names, index 0 = January. form: "full" (nominative), "genitive", "short".
    Q_INVOKABLE QStringList monthNames(const QString &form = QStringLiteral("full"), const QString &locale = {}) const;

    /// 42 cells (6 weeks) for a month view starting on `weekStart` (0 = the property):
    /// { date, day, inMonth, isToday, isWeekend, dayOfWeek }.
    Q_INVOKABLE QVariantList monthGrid(int year, int month, int weekStart = 0) const;

    Q_INVOKABLE QString addDays(const QString &date, int days) const;
    Q_INVOKABLE QString addMonths(const QString &date, int months) const;
    Q_INVOKABLE QString addMinutes(const QString &spec, int minutes) const;
    Q_INVOKABLE int dayOfWeek(const QString &date) const;
    Q_INVOKABLE QString startOfWeek(const QString &date) const;
    Q_INVOKABLE bool isValidDate(const QString &date) const;
    /// Lenient 24-hour time: "9" -> "09:00", "9:5" invalid, "930" / "0930" -> "09:30",
    /// "18.30" -> "18:30". Empty string when invalid.
    Q_INVOKABLE QString normalizeTime(const QString &text) const;
    /// Spec -> { ok, date, time } (time "" when the spec has none).
    Q_INVOKABLE QVariantMap splitSpec(const QString &spec) const;
    /// Chronological compare of two specs (negative when a is earlier).
    Q_INVOKABLE int compare(const QString &a, const QString &b) const;

Q_SIGNALS:
    void localeChanged();
    void weekStartChanged();
    void todayChanged();

private:
    QDateTime currentDateTime() const;
    void scheduleMidnight();
    bool english(const QString &locale) const;

    QString m_locale;
    int m_weekStart = 1;
    QDateTime m_fixedNow;
    QTimer m_midnight;
};

} // namespace brulion::ui
