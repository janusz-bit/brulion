//! Android: every command is forwarded to the Kotlin plugin
//! `io.github.januszbit.brulion.android.BrulionAndroidPlugin` (camelCase method names).

use std::time::{SystemTime, UNIX_EPOCH};

use serde::{de::DeserializeOwned, Serialize};
use tauri::{
    plugin::{PluginApi, PluginHandle},
    AppHandle, Manager, Runtime,
};

use crate::models::{
    normalize_reminders, Activation, AndroidReminder, Granted, ScheduleArgs, ScheduleResult,
};

const PLUGIN_IDENTIFIER: &str = "io.github.januszbit.brulion.android";
const PLUGIN_CLASS: &str = "BrulionAndroidPlugin";

/// Handle of the registered Kotlin plugin, kept in the app state.
struct BrulionAndroid<R: Runtime>(PluginHandle<R>);

pub(crate) fn setup<R: Runtime, C: DeserializeOwned>(
    app: &AppHandle<R>,
    api: PluginApi<R, C>,
) -> Result<(), Box<dyn std::error::Error>> {
    let handle = api.register_android_plugin(PLUGIN_IDENTIFIER, PLUGIN_CLASS)?;
    app.manage(BrulionAndroid(handle));
    Ok(())
}

async fn call<R: Runtime, T: DeserializeOwned, P: Serialize + Send>(
    app: &AppHandle<R>,
    command: &str,
    payload: P,
) -> Result<T, String> {
    // Clone the handle out of the state so no borrow of the app is held across the await.
    let handle = app
        .try_state::<BrulionAndroid<R>>()
        .ok_or_else(|| "brulion-android: the plugin is not initialised".to_string())?
        .0
        .clone();
    handle
        .run_mobile_plugin_async(command, payload)
        .await
        .map_err(|e| format!("brulion-android: {command}: {e}"))
}

fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| i64::try_from(d.as_millis()).unwrap_or(i64::MAX))
        .unwrap_or(0)
}

pub(crate) async fn has_all_files_access<R: Runtime>(
    app: &AppHandle<R>,
) -> Result<Granted, String> {
    call(app, "hasAllFilesAccess", ()).await
}

pub(crate) async fn request_all_files_access<R: Runtime>(app: &AppHandle<R>) -> Result<(), String> {
    call(app, "requestAllFilesAccess", ()).await
}

pub(crate) async fn notification_permission<R: Runtime>(
    app: &AppHandle<R>,
) -> Result<Granted, String> {
    call(app, "notificationPermission", ()).await
}

pub(crate) async fn request_notification_permission<R: Runtime>(
    app: &AppHandle<R>,
) -> Result<Granted, String> {
    call(app, "requestNotificationPermission", ()).await
}

pub(crate) async fn can_schedule_exact<R: Runtime>(app: &AppHandle<R>) -> Result<Granted, String> {
    call(app, "canScheduleExact", ()).await
}

pub(crate) async fn request_exact_alarms<R: Runtime>(app: &AppHandle<R>) -> Result<(), String> {
    call(app, "requestExactAlarms", ()).await
}

pub(crate) async fn schedule_reminders<R: Runtime>(
    app: &AppHandle<R>,
    items: Vec<AndroidReminder>,
) -> Result<ScheduleResult, String> {
    let items = normalize_reminders(items, now_ms());
    call(app, "scheduleReminders", ScheduleArgs { items }).await
}

pub(crate) async fn take_activation<R: Runtime>(app: &AppHandle<R>) -> Result<Activation, String> {
    call(app, "takeActivation", ()).await
}
