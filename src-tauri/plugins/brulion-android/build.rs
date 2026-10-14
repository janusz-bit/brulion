// Single source of the command list (also used by the crate's contract tests).
include!("src/command_names.rs");

fn main() {
    tauri_plugin::Builder::new(COMMANDS)
        .android_path("android")
        .build();
}
