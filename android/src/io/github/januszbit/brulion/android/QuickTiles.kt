// Brulion — offline-first Markdown notes. SPDX-License-Identifier: AGPL-3.0-or-later

package io.github.januszbit.brulion.android

import android.annotation.SuppressLint
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.drawable.Icon
import android.net.Uri
import android.os.Build
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import android.util.Log
import io.github.januszbit.brulion.R

/**
 * Quick Settings tiles and launcher shortcuts (docs/ANDROID.md "Quick Settings tiles and app
 * shortcuts"). Both open the widgets' quick add window ([QuickAddActivity]) over whatever is on
 * the screen — without starting Brulion: a note or a task goes into today's journal at once
 * ([QuickAdd]). The shortcuts also open today's journal and search (brulion:// addresses through
 * [AutomationActivity]).
 */
internal object QuickEntry {
  /** The quick add window for [kind] ([WidgetAction.KIND_NOTE] or [WidgetAction.KIND_TASK]). */
  fun quickAddIntent(context: Context, kind: String): Intent =
    Intent(context, QuickAddActivity::class.java)
      .putExtra(QuickAddActivity.EXTRA_KIND, kind)
      .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)

  /** An address opened through [AutomationActivity]. */
  fun addressIntent(context: Context, uri: String): Intent =
    Intent(Intent.ACTION_VIEW, Uri.parse(uri), context, AutomationActivity::class.java)
      .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)

  /**
   * The launcher's shortcuts (a long press on the icon): quick note, new task, today's journal,
   * search — published once per start (ShortcutManager, Android 7.1+), labels in the system's
   * language (the launcher shows them while Brulion is closed).
   */
  fun publishShortcuts(context: Context) {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N_MR1) return
    val app = context.applicationContext
    val manager = app.getSystemService(ShortcutManager::class.java) ?: return
    fun shortcut(id: String, label: Int, icon: Int, intent: Intent) =
      ShortcutInfo.Builder(app, id)
        .setShortLabel(app.getString(label))
        .setIcon(Icon.createWithResource(app, icon))
        .setIntent(intent.setAction(intent.action ?: Intent.ACTION_VIEW))
        .build()
    val shortcuts = listOf(
      shortcut("quick-note", R.string.brulion_shortcut_note, R.drawable.ic_shortcut_note, quickAddIntent(app, WidgetAction.KIND_NOTE)),
      shortcut("new-task", R.string.brulion_shortcut_task, R.drawable.ic_shortcut_task, quickAddIntent(app, WidgetAction.KIND_TASK)),
      shortcut("today", R.string.brulion_shortcut_today, R.drawable.ic_shortcut_today, addressIntent(app, "brulion://daily")),
      shortcut("search", R.string.brulion_shortcut_search, R.drawable.ic_shortcut_search, addressIntent(app, "brulion://search"))
    )
    try {
      manager.dynamicShortcuts = shortcuts
    } catch (e: RuntimeException) {
      Log.w(Automation.TAG, "cannot publish the launcher shortcuts", e)
    }
  }

  /** The tile opens [intent] and closes the Quick Settings panel. */
  @SuppressLint("StartActivityAndCollapseDeprecated")
  fun open(service: TileService, intent: Intent) {
    try {
      if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
        val pending = PendingIntent.getActivity(
          service, intent.component.hashCode(), intent, PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        service.startActivityAndCollapse(pending)
      } else {
        @Suppress("DEPRECATION")
        service.startActivityAndCollapse(intent)
      }
    } catch (e: RuntimeException) {
      Log.w(Automation.TAG, "the tile cannot open its window", e)
    }
  }
}

/** The "Quick note" tile: the quick add window for a note ([QuickEntry]). */
class QuickNoteTileService : TileService() {
  override fun onStartListening() {
    super.onStartListening()
    qsTile?.apply {
      state = Tile.STATE_INACTIVE
      updateTile()
    }
  }

  override fun onClick() {
    super.onClick()
    QuickEntry.open(this, QuickEntry.quickAddIntent(this, WidgetAction.KIND_NOTE))
  }
}

/** The "New task" tile: the quick add window for a task ([QuickEntry]). */
class QuickTaskTileService : TileService() {
  override fun onStartListening() {
    super.onStartListening()
    qsTile?.apply {
      state = Tile.STATE_INACTIVE
      updateTile()
    }
  }

  override fun onClick() {
    super.onClick()
    QuickEntry.open(this, QuickEntry.quickAddIntent(this, WidgetAction.KIND_TASK))
  }
}
