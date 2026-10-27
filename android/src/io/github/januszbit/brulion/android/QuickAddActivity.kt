// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.app.Activity
import android.app.DatePickerDialog
import android.app.TimePickerDialog
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.text.InputType
import android.util.TypedValue
import android.view.Gravity
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.view.inputmethod.EditorInfo
import android.widget.Button
import android.widget.EditText
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.TextView
import io.github.januszbit.brulion.R
import java.time.LocalDate
import java.time.LocalDateTime
import java.time.LocalTime
import java.util.UUID

/**
 * Quick add of the widgets ("+"): a small dialog over the home screen — the text, and for a task
 * the day chips **Dziś** (picked), **Jutro**, **Bez terminu**, **Data…**, the time **Godzina…** and
 * **Alarm** (no date words are read from the text: "jutro o 9" stays text). **Dodaj** writes the
 * entry into today's journal at once ([QuickAdd]) and empties the field for the next one; back or
 * a tap outside closes it. A plain Android activity: it never starts Qt. In the app's language.
 */
class QuickAddActivity : Activity() {
  private lateinit var input: EditText
  private lateinit var status: TextView
  private val chips = HashMap<String, TextView>()
  private var kind = WidgetAction.KIND_TASK
  private var locale = "pl"

  /** The day picked (null: no date), its time (null: none), alarm. */
  private var day: LocalDate? = LocalDate.now()
  private var time: LocalTime? = null
  private var alarm = false
  private var customDay = false

  override fun attachBaseContext(newBase: Context) {
    locale = WidgetStore.snapshot(newBase).locale
    super.attachBaseContext(WidgetDates.localized(newBase, locale))
  }

  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    kind = if (intent?.getStringExtra(EXTRA_KIND) == WidgetAction.KIND_NOTE) WidgetAction.KIND_NOTE else WidgetAction.KIND_TASK
    savedInstanceState?.let { restore(it) }
    setContentView(build())
    intent?.getStringExtra(Intent.EXTRA_TEXT)?.let { if (savedInstanceState == null) input.setText(it) }
    window.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
    input.requestFocus()
    refreshChips()
  }

  // Opened again while still open (another "+" or a launcher that keeps it): the new kind and text.
  override fun onNewIntent(intent: Intent) {
    super.onNewIntent(intent)
    val newKind = if (intent.getStringExtra(EXTRA_KIND) == WidgetAction.KIND_NOTE) WidgetAction.KIND_NOTE else WidgetAction.KIND_TASK
    setIntent(intent)
    if (newKind != kind) {
      recreate()
      return
    }
    // A new "+": a new entry, today's (as when the dialog opens).
    day = LocalDate.now()
    time = null
    alarm = false
    customDay = false
    refreshChips()
    status.text = ""
    input.setText(intent.getStringExtra(Intent.EXTRA_TEXT) ?: "")
  }

  // Left for the home screen or another app: the dialog does not linger behind it.
  override fun onStop() {
    super.onStop()
    if (!isChangingConfigurations) finish()
  }

  override fun onSaveInstanceState(outState: Bundle) {
    super.onSaveInstanceState(outState)
    outState.putString("day", day?.toString())
    outState.putString("time", time?.let { JournalAppend.hhmm(it) })
    outState.putBoolean("alarm", alarm)
    outState.putBoolean("customDay", customDay)
  }

  private fun restore(state: Bundle) {
    day = JournalAppend.parseDate(state.getString("day"))
    time = JournalAppend.parseTime(state.getString("time"))
    alarm = state.getBoolean("alarm")
    customDay = state.getBoolean("customDay")
  }

  private fun dp(value: Int): Int =
    TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value.toFloat(), resources.displayMetrics).toInt()

  private fun build(): View {
    val root = LinearLayout(this)
    root.orientation = LinearLayout.VERTICAL
    root.setPadding(dp(20), dp(18), dp(20), dp(12))

    val title = TextView(this)
    title.setText(if (kind == WidgetAction.KIND_NOTE) R.string.brulion_quick_add_note else R.string.brulion_quick_add_task)
    title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 18f)
    title.setTextColor(getColor(R.color.widget_text))
    title.typeface = android.graphics.Typeface.DEFAULT_BOLD
    root.addView(title)

    input = EditText(this)
    input.id = R.id.quick_add_text
    input.setHint(if (kind == WidgetAction.KIND_NOTE) R.string.brulion_quick_add_note_hint else R.string.brulion_quick_add_task_hint)
    input.setTextColor(getColor(R.color.widget_text))
    input.setHintTextColor(getColor(R.color.widget_faint))
    if (kind == WidgetAction.KIND_NOTE) {
      input.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES or InputType.TYPE_TEXT_FLAG_MULTI_LINE
      input.maxLines = 6
    } else {
      input.inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_SENTENCES
      input.imeOptions = EditorInfo.IME_ACTION_DONE
      input.setOnEditorActionListener { _, actionId, event ->
        if (actionId == EditorInfo.IME_ACTION_DONE || event?.keyCode == KeyEvent.KEYCODE_ENTER) {
          add()
          true
        } else {
          false
        }
      }
    }
    root.addView(input, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).also { it.topMargin = dp(8) })

    if (kind == WidgetAction.KIND_TASK) {
      // The day, then the time: two rows (each scrolls sideways on a narrow screen).
      val days = chipRow(root)
      days.addView(chip(CHIP_TODAY, R.string.brulion_quick_add_today) { pick(LocalDate.now()) })
      days.addView(chip(CHIP_TOMORROW, R.string.brulion_quick_add_tomorrow) { pick(LocalDate.now().plusDays(1)) })
      days.addView(chip(CHIP_NONE, R.string.brulion_quick_add_no_date) { day = null; time = null; alarm = false; customDay = false; refreshChips() })
      days.addView(chip(CHIP_DATE, R.string.brulion_quick_add_date) { pickDate() })
      val times = chipRow(root)
      times.addView(chip(CHIP_TIME, R.string.brulion_quick_add_time) { pickTime() })
      times.addView(chip(CHIP_ALARM, R.string.brulion_quick_add_alarm) { alarm = !alarm && time != null; refreshChips() })
    }

    status = TextView(this)
    status.id = R.id.quick_add_status
    status.setTextColor(getColor(R.color.widget_muted))
    status.setTextSize(TypedValue.COMPLEX_UNIT_SP, 13f)
    root.addView(status, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).also { it.topMargin = dp(8) })

    val buttons = LinearLayout(this)
    buttons.orientation = LinearLayout.HORIZONTAL
    buttons.gravity = Gravity.END
    val close = Button(this, null, android.R.attr.borderlessButtonStyle)
    close.setText(R.string.brulion_quick_add_close)
    close.setOnClickListener { finish() }
    val addButton = Button(this, null, android.R.attr.borderlessButtonStyle)
    addButton.id = R.id.quick_add_add
    addButton.setText(R.string.brulion_quick_add_add)
    addButton.setTextColor(getColor(R.color.widget_accent))
    addButton.setOnClickListener { add() }
    buttons.addView(close)
    buttons.addView(addButton)
    root.addView(buttons, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
    return root
  }

  private fun chipRow(root: LinearLayout): LinearLayout {
    val scroll = HorizontalScrollView(this)
    scroll.isHorizontalScrollBarEnabled = false
    val row = LinearLayout(this)
    row.orientation = LinearLayout.HORIZONTAL
    scroll.addView(row)
    root.addView(scroll, LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).also { it.topMargin = dp(10) })
    return row
  }

  private fun chip(key: String, text: Int, onClick: () -> Unit): TextView {
    val chip = TextView(this)
    chip.setText(text)
    chip.setBackgroundResource(R.drawable.quick_add_chip)
    chip.setTextColor(getColor(R.color.widget_text))
    chip.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14f)
    chip.setPadding(dp(14), dp(7), dp(14), dp(7))
    chip.isClickable = true
    chip.isFocusable = true
    chip.setOnClickListener { onClick() }
    chip.layoutParams = LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT).also { it.marginEnd = dp(8) }
    chips[key] = chip
    return chip
  }

  private fun pick(date: LocalDate) {
    day = date
    customDay = false
    refreshChips()
  }

  private fun pickDate() {
    val start = day ?: LocalDate.now()
    DatePickerDialog(this, { _, year, month, dayOfMonth ->
      day = LocalDate.of(year, month + 1, dayOfMonth)
      customDay = day != LocalDate.now() && day != LocalDate.now().plusDays(1)
      refreshChips()
    }, start.year, start.monthValue - 1, start.dayOfMonth).show()
  }

  private fun pickTime() {
    val start = time ?: LocalTime.of(9, 0)
    TimePickerDialog(this, { _, hour, minute ->
      time = LocalTime.of(hour, minute)
      if (day == null) day = LocalDate.now()
      refreshChips()
    }, start.hour, start.minute, true).show()
  }

  private fun refreshChips() {
    if (chips.isEmpty()) return
    val today = LocalDate.now()
    chips[CHIP_TODAY]?.isSelected = day == today
    chips[CHIP_TOMORROW]?.isSelected = day == today.plusDays(1)
    chips[CHIP_NONE]?.isSelected = day == null
    chips[CHIP_DATE]?.let {
      it.isSelected = customDay && day != null
      it.text = if (customDay && day != null) WidgetDates.short(day!!, locale, today.year) else getString(R.string.brulion_quick_add_date)
    }
    chips[CHIP_TIME]?.let {
      it.isSelected = time != null
      it.text = time?.let { t -> "⏰ " + JournalAppend.hhmm(t) } ?: getString(R.string.brulion_quick_add_time)
    }
    chips[CHIP_ALARM]?.let {
      if (time == null) alarm = false
      it.isEnabled = time != null
      it.isSelected = alarm
      it.alpha = if (time != null) 1f else 0.5f
    }
  }

  private fun add() {
    val text = input.text?.toString() ?: ""
    if (JournalAppend.qtTrim(text).isEmpty()) return
    val now = LocalDateTime.now()
    val action =
      WidgetAction(
        id = UUID.randomUUID().toString(),
        kind = kind,
        text = text,
        date = if (kind == WidgetAction.KIND_TASK) day?.toString() ?: "" else "",
        time = if (kind == WidgetAction.KIND_TASK) time?.let { JournalAppend.hhmm(it) } ?: "" else "",
        alarm = kind == WidgetAction.KIND_TASK && alarm && time != null,
        added = now.toString().take(16)
      )
    when (QuickAdd.add(this, action, now)) {
      QuickAdd.Outcome.WRITTEN -> status.setText(R.string.brulion_quick_add_added)
      QuickAdd.Outcome.QUEUED -> status.setText(R.string.brulion_quick_add_queued)
      QuickAdd.Outcome.EMPTY -> return
    }
    status.append(" — " + JournalAppend.cleanItemText(text))
    input.setText("")
  }

  internal companion object {
    const val EXTRA_KIND = "io.github.januszbit.brulion.android.extra.QUICK_ADD_KIND"
    private const val CHIP_TODAY = "today"
    private const val CHIP_TOMORROW = "tomorrow"
    private const val CHIP_NONE = "none"
    private const val CHIP_DATE = "date"
    private const val CHIP_TIME = "time"
    private const val CHIP_ALARM = "alarm"
  }
}
