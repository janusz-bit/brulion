//! `#[tauri::command]`s of the plugin. Each one delegates to the platform module:
//! `mobile` forwards to Kotlin on Android, `desktop` answers with neutral values elsewhere.

use tauri::{command, AppHandle, Runtime};

use crate::models::{Activation, AndroidReminder, Granted, ScheduleResult};
use crate::platform;

/// `{ granted }`: may the app read and write any folder on shared storage (Syncthing)?
/// Android 11+: "All files access" (MANAGE_EXTERNAL_STORAGE); older: WRITE_EXTERNAL_STORAGE.
#[command]
pub(crate) async fn has_all_files_access<R: Runtime>(app: AppHandle<R>) -> Result<Granted, String> {
    platform::has_all_files_access(&app).await
}

/// Opens the system screen granting all-files access (Android 11+) or asks for the legacy
/// storage permission. The app re-checks with `has_all_files_access` when it resumes.
#[command]
pub(crate) async fn request_all_files_access<R: Runtime>(app: AppHandle<R>) -> Result<(), String> {
    platform::request_all_files_access(&app).await
}

/// `{ granted }`: are notifications of the app enabled?
#[command]
pub(crate) async fn notification_permission<R: Runtime>(
    app: AppHandle<R>,
) -> Result<Granted, String> {
    platform::notification_permission(&app).await
}

/// Asks for POST_NOTIFICATIONS (Android 13+) and answers with the resulting state. When the
/// system will not ask any more, it opens the app's notification settings instead.
#[command]
pub(crate) async fn request_notification_permission<R: Runtime>(
    app: AppHandle<R>,
) -> Result<Granted, String> {
    platform::request_notification_permission(&app).await
}

/// `{ granted }`: can reminders fire at the exact minute (exact-alarm permission)?
#[command]
pub(crate) async fn can_schedule_exact<R: Runtime>(app: AppHandle<R>) -> Result<Granted, String> {
    platform::can_schedule_exact(&app).await
}

/// Opens the "Alarms & reminders" settings of the app (Android 12+ when not granted).
#[command]
pub(crate) async fn request_exact_alarms<R: Runtime>(app: AppHandle<R>) -> Result<(), String> {
    platform::request_exact_alarms(&app).await
}

/// `{ granted }`: may alarms open the full-screen alarm screen over the lock screen? Android
/// 14+ lets the user revoke this ("Full-screen notifications"); always true below.
#[command]
pub(crate) async fn can_use_full_screen_intent<R: Runtime>(
    app: AppHandle<R>,
) -> Result<Granted, String> {
    platform::can_use_full_screen_intent(&app).await
}

/// Opens the "Full-screen notifications" settings of the app (Android 14+ when not granted;
/// falls back to the app's notification settings). The app re-checks when it resumes.
#[command]
pub(crate) async fn request_full_screen_intent<R: Runtime>(
    app: AppHandle<R>,
) -> Result<(), String> {
    platform::request_full_screen_intent(&app).await
}

/// Replaces the whole set of natively scheduled reminders with `items`.
#[command]
pub(crate) async fn schedule_reminders<R: Runtime>(
    app: AppHandle<R>,
    items: Vec<AndroidReminder>,
) -> Result<ScheduleResult, String> {
    platform::schedule_reminders(&app, items).await
}

/// The notification tap that opened the app, if not delivered yet (cleared on read).
#[command]
pub(crate) async fn take_activation<R: Runtime>(app: AppHandle<R>) -> Result<Activation, String> {
    platform::take_activation(&app).await
}

/// Desktop stand-in for the Kotlin `registerListener`, so `addPluginListener` resolves there
/// too. Nothing is ever emitted outside Android.
#[cfg(not(target_os = "android"))]
#[command]
pub(crate) fn register_listener(
    event: String,
    handler: tauri::ipc::Channel<serde_json::Value>,
) -> Result<(), String> {
    let _ = (event, handler);
    Ok(())
}

/// Desktop stand-in for the Kotlin `removeListener` (`PluginListener.unregister`).
#[cfg(not(target_os = "android"))]
#[command]
pub(crate) fn remove_listener(event: String, channel_id: u32) -> Result<(), String> {
    let _ = (event, channel_id);
    Ok(())
}
