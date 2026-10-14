//! Everything but Android: the commands exist so the frontend can call them unconditionally,
//! and answer with neutral values (nothing to grant, nothing scheduled natively).

use tauri::{plugin::PluginApi, AppHandle, Runtime};

use crate::models::{Activation, AndroidReminder, Granted, ScheduleResult};

pub(crate) fn setup<R: Runtime, C: serde::de::DeserializeOwned>(
    _app: &AppHandle<R>,
    _api: PluginApi<R, C>,
) -> Result<(), Box<dyn std::error::Error>> {
    Ok(())
}

const GRANTED: Granted = Granted { granted: true };

pub(crate) async fn has_all_files_access<R: Runtime>(
    _app: &AppHandle<R>,
) -> Result<Granted, String> {
    Ok(GRANTED)
}

pub(crate) async fn request_all_files_access<R: Runtime>(
    _app: &AppHandle<R>,
) -> Result<(), String> {
    Ok(())
}

pub(crate) async fn notification_permission<R: Runtime>(
    _app: &AppHandle<R>,
) -> Result<Granted, String> {
    Ok(GRANTED)
}

pub(crate) async fn request_notification_permission<R: Runtime>(
    _app: &AppHandle<R>,
) -> Result<Granted, String> {
    Ok(GRANTED)
}

pub(crate) async fn can_schedule_exact<R: Runtime>(_app: &AppHandle<R>) -> Result<Granted, String> {
    Ok(GRANTED)
}

pub(crate) async fn request_exact_alarms<R: Runtime>(_app: &AppHandle<R>) -> Result<(), String> {
    Ok(())
}

pub(crate) async fn can_use_full_screen_intent<R: Runtime>(
    _app: &AppHandle<R>,
) -> Result<Granted, String> {
    Ok(GRANTED)
}

pub(crate) async fn request_full_screen_intent<R: Runtime>(
    _app: &AppHandle<R>,
) -> Result<(), String> {
    Ok(())
}

/// Desktop keeps reminders in the in-app `ReminderScheduler`; nothing is armed natively.
pub(crate) async fn schedule_reminders<R: Runtime>(
    _app: &AppHandle<R>,
    _items: Vec<AndroidReminder>,
) -> Result<ScheduleResult, String> {
    Ok(ScheduleResult {
        scheduled: 0,
        exact: true,
    })
}

pub(crate) async fn take_activation<R: Runtime>(_app: &AppHandle<R>) -> Result<Activation, String> {
    Ok(Activation::default())
}
