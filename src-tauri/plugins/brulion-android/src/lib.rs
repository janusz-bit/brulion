//! `brulion-android` — Brulion's local Tauri plugin for Android.
//!
//! * all-files access (MANAGE_EXTERNAL_STORAGE) so a Syncthing folder on shared storage can be
//!   the vault,
//! * notification and exact-alarm permissions,
//! * native reminders: AlarmManager alarms persisted in SharedPreferences and re-armed after a
//!   reboot / app update / clock or time-zone change, notification channels `reminders` and
//!   `alarms`, actions "Drzemka 10 min" (re-armed natively) and "OK", a full-screen alarm screen,
//! * the notification tap that opened the app (`take_activation`, event
//!   `notification-activated`).
//!
//! The IPC contract lives in `docs/ARCHITECTURE.md` (section "Tauri IPC contract"). JS calls
//! `invoke('plugin:brulion-android|<cmd>', args)`. On other platforms the commands exist and
//! return neutral values (`granted: true`, `scheduled: 0`, empty activation).

use tauri::{
    plugin::{Builder, TauriPlugin},
    Runtime,
};

mod command_names;
mod commands;
mod models;

#[cfg(target_os = "android")]
mod mobile;
#[cfg(target_os = "android")]
use mobile as platform;

#[cfg(not(target_os = "android"))]
mod desktop;
#[cfg(not(target_os = "android"))]
use desktop as platform;

pub use command_names::COMMANDS;
pub use models::{
    normalize_reminders, Activation, AndroidReminder, Granted, ScheduleArgs, ScheduleResult,
    MAX_REMINDERS,
};

/// Plugin name (`plugin:brulion-android|...`, capability `brulion-android:default`).
pub const PLUGIN_NAME: &str = "brulion-android";

/// Event emitted by the Kotlin plugin when a reminder notification is tapped while the app
/// runs (`addPluginListener('brulion-android', 'notification-activated', cb)`).
pub const ACTIVATION_EVENT: &str = "notification-activated";

/// Initializes the plugin.
pub fn init<R: Runtime>() -> TauriPlugin<R> {
    let builder = Builder::new(PLUGIN_NAME);

    // `register_listener` / `remove_listener` are not handled in Rust on Android: they fall
    // through to the Kotlin plugin (base class `app.tauri.plugin.Plugin`), which owns the
    // channels its `trigger()` sends events to.
    #[cfg(target_os = "android")]
    let builder = builder.invoke_handler(tauri::generate_handler![
        commands::has_all_files_access,
        commands::request_all_files_access,
        commands::notification_permission,
        commands::request_notification_permission,
        commands::can_schedule_exact,
        commands::request_exact_alarms,
        commands::schedule_reminders,
        commands::take_activation,
    ]);

    #[cfg(not(target_os = "android"))]
    let builder = builder.invoke_handler(tauri::generate_handler![
        commands::has_all_files_access,
        commands::request_all_files_access,
        commands::notification_permission,
        commands::request_notification_permission,
        commands::can_schedule_exact,
        commands::request_exact_alarms,
        commands::schedule_reminders,
        commands::take_activation,
        commands::register_listener,
        commands::remove_listener,
    ]);

    builder.setup(platform::setup).build()
}

/// The plugin's three faces (Rust commands, ACL permissions, Kotlin methods) and the
/// documented contract must name the same commands.
#[cfg(test)]
mod contract_tests {
    use super::COMMANDS;

    const ARCHITECTURE: &str = include_str!("../../../../docs/ARCHITECTURE.md");
    const DEFAULT_PERMISSIONS: &str = include_str!("../permissions/default.toml");
    const KOTLIN_PLUGIN: &str = include_str!(
        "../android/src/main/java/io/github/januszbit/brulion/android/BrulionAndroidPlugin.kt"
    );
    const MANIFEST: &str = include_str!("../android/src/main/AndroidManifest.xml");

    const LISTENER_COMMANDS: &[&str] = &["register_listener", "remove_listener"];

    fn contract_commands() -> impl Iterator<Item = &'static str> {
        COMMANDS
            .iter()
            .copied()
            .filter(|c| !LISTENER_COMMANDS.contains(c))
    }

    /// `heck::AsLowerCamelCase`, as Tauri converts plugin command names for Kotlin.
    fn lower_camel(snake: &str) -> String {
        let mut out = String::new();
        for (i, part) in snake.split('_').enumerate() {
            let mut chars = part.chars();
            if let Some(first) = chars.next() {
                if i == 0 {
                    out.push(first);
                } else {
                    out.extend(first.to_uppercase());
                }
                out.push_str(chars.as_str());
            }
        }
        out
    }

    #[test]
    fn every_command_is_in_the_documented_contract() {
        for cmd in contract_commands() {
            assert!(
                ARCHITECTURE.contains(&format!("| `{cmd}` |")),
                "{cmd} is missing from the brulion-android table of docs/ARCHITECTURE.md"
            );
        }
        assert!(ARCHITECTURE.contains(&format!("`{}`", super::ACTIVATION_EVENT)));
        assert!(ARCHITECTURE.contains(&format!("`{}`", super::PLUGIN_NAME)));
    }

    #[test]
    fn default_permission_allows_every_command() {
        for cmd in COMMANDS {
            let perm = format!("\"allow-{}\"", cmd.replace('_', "-"));
            assert!(
                DEFAULT_PERMISSIONS.contains(&perm),
                "permissions/default.toml lacks {perm}"
            );
        }
    }

    #[test]
    fn kotlin_implements_every_command() {
        for cmd in contract_commands() {
            let method = format!("fun {}(invoke: Invoke)", lower_camel(cmd));
            assert!(
                KOTLIN_PLUGIN.contains(&method),
                "BrulionAndroidPlugin.kt lacks `{method}` for {cmd}"
            );
        }
        assert!(KOTLIN_PLUGIN.contains(&format!("\"{}\"", super::ACTIVATION_EVENT)));
        assert_eq!(
            lower_camel("request_notification_permission"),
            "requestNotificationPermission"
        );
    }

    #[test]
    fn manifest_declares_the_contract_permissions_and_components() {
        for perm in [
            "MANAGE_EXTERNAL_STORAGE",
            "READ_EXTERNAL_STORAGE",
            "WRITE_EXTERNAL_STORAGE",
            "POST_NOTIFICATIONS",
            "SCHEDULE_EXACT_ALARM",
            "USE_EXACT_ALARM",
            "RECEIVE_BOOT_COMPLETED",
            "VIBRATE",
            "WAKE_LOCK",
            "USE_FULL_SCREEN_INTENT",
        ] {
            assert!(
                MANIFEST.contains(&format!("android.permission.{perm}\"")),
                "AndroidManifest.xml lacks {perm}"
            );
        }
        for component in [
            "ReminderReceiver",
            "ActionReceiver",
            "BootReceiver",
            "AlarmActivity",
        ] {
            assert!(
                MANIFEST.contains(&format!(
                    "android:name=\"io.github.januszbit.brulion.android.{component}\""
                )),
                "AndroidManifest.xml lacks {component}"
            );
        }
    }
}
