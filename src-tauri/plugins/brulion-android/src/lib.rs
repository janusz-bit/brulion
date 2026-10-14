//! `brulion-android` — Brulion's local Tauri plugin for Android.
//!
//! * all-files access (MANAGE_EXTERNAL_STORAGE) so a Syncthing folder on shared storage can be
//!   the vault,
//! * notification, exact-alarm and (Android 14+) full-screen-intent permissions,
//! * native reminders: AlarmManager alarms persisted in SharedPreferences and re-armed after a
//!   reboot / app update / clock or time-zone change, notification channels `reminders`,
//!   `alarms` and `missed-alarms`, actions "Drzemka 10 min" (re-armed natively) and "OK", a
//!   full-screen alarm screen; an alarm nobody reacts to stops ringing after `ringMinutes`
//!   (default [`DEFAULT_RING_MINUTES`]) and turns into a silent "Przegapiony alarm",
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
    DEFAULT_RING_MINUTES, MAX_REMINDERS, MAX_RING_MINUTES,
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
        commands::can_use_full_screen_intent,
        commands::request_full_screen_intent,
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
        commands::can_use_full_screen_intent,
        commands::request_full_screen_intent,
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
    const KOTLIN_REMINDER: &str =
        include_str!("../android/src/main/java/io/github/januszbit/brulion/android/Reminder.kt");
    const KOTLIN_RING_TIMEOUT: &str =
        include_str!("../android/src/main/java/io/github/januszbit/brulion/android/RingTimeout.kt");
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

    /// Every field of `AndroidReminder` is documented, parsed by Kotlin's `ReminderArg` and
    /// persisted by Kotlin's `Reminder`.
    #[test]
    fn reminder_fields_match_kotlin_and_docs() {
        let full = crate::AndroidReminder {
            id: 1,
            title: "t".into(),
            body: "b".into(),
            at: 2,
            alarm: true,
            path: "p.md".into(),
            line: 3,
            ring_minutes: Some(5),
        };
        let value = serde_json::to_value(full).unwrap();
        let fields = value.as_object().unwrap();
        assert_eq!(fields.len(), 8, "a new field needs a check here");
        let doc_line = ARCHITECTURE
            .lines()
            .find(|l| l.starts_with("`AndroidReminder = {"))
            .expect("AndroidReminder type in docs/ARCHITECTURE.md");
        for name in fields.keys() {
            assert!(
                doc_line.contains(&format!(" {name}: "))
                    || doc_line.contains(&format!(" {name}?: ")),
                "docs/ARCHITECTURE.md AndroidReminder lacks {name}"
            );
            assert!(
                KOTLIN_PLUGIN.contains(&format!("var {name}: ")),
                "ReminderArg (BrulionAndroidPlugin.kt) lacks `var {name}`"
            );
            assert!(
                KOTLIN_REMINDER.contains(&format!("json.put(\"{name}\"")),
                "Reminder.toJson (Reminder.kt) does not persist {name}"
            );
        }
    }

    /// Rust documents the ring-time defaults, Kotlin applies them: they must agree.
    #[test]
    fn ring_time_limits_match_kotlin() {
        assert!(KOTLIN_RING_TIMEOUT.contains(&format!(
            "const val DEFAULT_MINUTES = {}",
            crate::DEFAULT_RING_MINUTES
        )));
        assert!(KOTLIN_RING_TIMEOUT.contains(&format!(
            "const val MAX_MINUTES = {}",
            crate::MAX_RING_MINUTES
        )));
        assert!(ARCHITECTURE.contains(&format!(
            "default {}, clamped to 1–{}",
            crate::DEFAULT_RING_MINUTES,
            crate::MAX_RING_MINUTES
        )));
    }

    /// `build.rs` writes one permission file per command; none may be left over from a
    /// removed command.
    #[test]
    fn autogenerated_permissions_match_the_commands() {
        let dir = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("permissions/autogenerated/commands");
        let mut files: Vec<String> = std::fs::read_dir(&dir)
            .unwrap()
            .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
            .collect();
        files.sort();
        let mut expected: Vec<String> = COMMANDS.iter().map(|c| format!("{c}.toml")).collect();
        expected.sort();
        assert_eq!(files, expected);
        let reference = std::fs::read_to_string(dir.join("../reference.md")).unwrap();
        for cmd in COMMANDS {
            assert!(
                reference.contains(&format!(
                    "`brulion-android:allow-{}`",
                    cmd.replace('_', "-")
                )),
                "permissions/autogenerated/reference.md lacks {cmd}"
            );
        }
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
            "RingTimeoutReceiver",
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
