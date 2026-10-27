// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.content.Context
import android.content.res.Configuration
import java.time.LocalDate
import java.util.Locale

/**
 * Dates in the widgets as the app writes them (src/core/dates/names.cpp, format.cpp) in the
 * app's language — the snapshot's `locale`, which may differ from the phone's.
 */
internal object WidgetDates {
  private val PL_WEEKDAYS = listOf("poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota", "niedziela")
  private val PL_WEEKDAYS_SHORT = listOf("pn", "wt", "śr", "czw", "pt", "sob", "nd")
  private val PL_MONTHS =
    listOf("styczeń", "luty", "marzec", "kwiecień", "maj", "czerwiec", "lipiec", "sierpień", "wrzesień",
      "październik", "listopad", "grudzień")
  private val PL_MONTHS_GEN =
    listOf("stycznia", "lutego", "marca", "kwietnia", "maja", "czerwca", "lipca", "sierpnia", "września",
      "października", "listopada", "grudnia")
  private val PL_MONTHS_SHORT = listOf("sty", "lut", "mar", "kwi", "maj", "cze", "lip", "sie", "wrz", "paź", "lis", "gru")
  private val EN_WEEKDAYS = listOf("Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday")
  private val EN_WEEKDAYS_SHORT = listOf("Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun")
  private val EN_MONTHS =
    listOf("January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
      "November", "December")
  private val EN_MONTHS_SHORT = listOf("Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec")

  private fun en(locale: String) = locale == "en"

  /** "pn" … "nd" / "Mon" … "Sun" for ISO day 1 … 7. */
  fun weekdayShort(day: Int, locale: String): String = (if (en(locale)) EN_WEEKDAYS_SHORT else PL_WEEKDAYS_SHORT)[(day - 1).coerceIn(0, 6)]

  /** "wtorek, 27 października" / "Tuesday, October 27". */
  fun long(date: LocalDate, locale: String): String {
    val weekday = date.dayOfWeek.value - 1
    val month = date.monthValue - 1
    return if (en(locale)) "${EN_WEEKDAYS[weekday]}, ${EN_MONTHS[month]} ${date.dayOfMonth}"
    else "${PL_WEEKDAYS[weekday]}, ${date.dayOfMonth} ${PL_MONTHS_GEN[month]}"
  }

  /** dates::formatDateShort(): "wt 27 paź" / "Tue, Oct 27"; another year: "27 paź 2027" / "Oct 27, 2027". */
  fun short(date: LocalDate, locale: String, currentYear: Int? = null): String {
    val weekday = weekdayShort(date.dayOfWeek.value, locale)
    val month = date.monthValue - 1
    val sameYear = currentYear == null || currentYear == date.year
    return if (en(locale)) {
      if (sameYear) "$weekday, ${EN_MONTHS_SHORT[month]} ${date.dayOfMonth}" else "${EN_MONTHS_SHORT[month]} ${date.dayOfMonth}, ${date.year}"
    } else {
      if (sameYear) "$weekday ${date.dayOfMonth} ${PL_MONTHS_SHORT[month]}" else "${date.dayOfMonth} ${PL_MONTHS_SHORT[month]} ${date.year}"
    }
  }

  /** "Październik 2026" / "October 2026". */
  fun monthTitle(year: Int, month: Int, locale: String): String {
    val name = (if (en(locale)) EN_MONTHS else PL_MONTHS)[(month - 1).coerceIn(0, 11)]
    return name.replaceFirstChar { it.uppercaseChar() } + " " + year
  }

  /** "dziś" / "jutro" / "wczoraj" (relative to [today]), else [short]. */
  fun relative(date: LocalDate, today: LocalDate, locale: String): String =
    when (date) {
      today -> if (en(locale)) "today" else "dziś"
      today.plusDays(1) -> if (en(locale)) "tomorrow" else "jutro"
      today.minusDays(1) -> if (en(locale)) "yesterday" else "wczoraj"
      else -> short(date, locale, today.year)
    }

  /** [context] with the app's language (the widgets' texts in res/values, values-en). */
  fun localized(context: Context, locale: String): Context {
    val config = Configuration(context.resources.configuration)
    config.setLocale(if (en(locale)) Locale.ENGLISH else Locale.forLanguageTag("pl"))
    return context.createConfigurationContext(config)
  }
}
