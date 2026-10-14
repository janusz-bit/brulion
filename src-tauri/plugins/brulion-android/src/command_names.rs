/// Commands of the plugin (snake_case, as invoked from JS: `plugin:brulion-android|<cmd>`).
///
/// Shared by `build.rs` (one `allow-*`/`deny-*` permission each) and the contract tests.
/// `register_listener` / `remove_listener` back `addPluginListener` from `@tauri-apps/api/core`:
/// on Android they are implemented by the Kotlin base class `app.tauri.plugin.Plugin`.
pub const COMMANDS: &[&str] = &[
    "has_all_files_access",
    "request_all_files_access",
    "notification_permission",
    "request_notification_permission",
    "can_schedule_exact",
    "request_exact_alarms",
    "schedule_reminders",
    "take_activation",
    "register_listener",
    "remove_listener",
];
