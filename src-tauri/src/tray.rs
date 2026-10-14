//! Main window helpers, tray icon, close-to-tray and quitting (desktop), `--hidden` start.
//!
//! On desktop Brulion keeps running in the tray so in-app reminders can fire; closing the
//! window only hides it (configurable with `set_close_to_tray`). The main window is created
//! invisible (tauri.conf.json) and shown in `setup` unless the app was started with
//! `--hidden` (e.g. from autostart). On mobile none of this applies.
//!
//! Quitting (tray "Zakończ", or closing the window while it is not hidden to the tray) never
//! tears the webview down in the middle of a save: the window is hidden, [`QUIT_EVENT`] is
//! emitted so the frontend can flush pending writes and then call `quit_app`, and the app
//! exits by itself after [`QUIT_GRACE`] when the frontend does not answer.

use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use tauri::{AppHandle, Manager, Runtime, State};

/// Label of the main window (tauri.conf.json).
pub const MAIN_WINDOW: &str = "main";

/// CLI flag: start minimised to the tray.
#[cfg_attr(mobile, allow(dead_code))]
pub const HIDDEN_FLAG: &str = "--hidden";

/// Emitted (no payload) when the app is about to quit: flush, then call `quit_app`.
#[cfg_attr(mobile, allow(dead_code))]
pub const QUIT_EVENT: &str = "brulion://quit-requested";

/// How long a quit waits for the frontend's `quit_app` before exiting anyway.
#[cfg_attr(mobile, allow(dead_code))]
pub const QUIT_GRACE: Duration = Duration::from_secs(3);

/// What closing the main window does.
#[cfg_attr(mobile, allow(dead_code))]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CloseAction {
    /// Hide it; the app keeps running in the tray.
    Hide,
    /// Quit the app (after giving the frontend a chance to flush).
    Quit,
}

/// Managed state for close-to-tray and quitting.
pub struct CloseToTray {
    /// User setting (default on).
    enabled: AtomicBool,
    /// Whether the tray icon could be created (no tray = never hide on close).
    tray: AtomicBool,
    /// A quit is in progress (waiting for the frontend).
    quitting: AtomicBool,
}

impl Default for CloseToTray {
    fn default() -> Self {
        CloseToTray {
            enabled: AtomicBool::new(true),
            tray: AtomicBool::new(false),
            quitting: AtomicBool::new(false),
        }
    }
}

#[cfg_attr(mobile, allow(dead_code))]
impl CloseToTray {
    pub fn set_enabled(&self, enabled: bool) {
        self.enabled.store(enabled, Ordering::Relaxed);
    }

    pub fn set_tray_available(&self, available: bool) {
        self.tray.store(available, Ordering::Relaxed);
    }

    /// Hide instead of close?
    pub fn active(&self) -> bool {
        self.enabled.load(Ordering::Relaxed) && self.tray.load(Ordering::Relaxed)
    }

    pub fn on_close(&self) -> CloseAction {
        if self.active() {
            CloseAction::Hide
        } else {
            CloseAction::Quit
        }
    }

    /// Marks the app as quitting. True for the first request only.
    pub fn begin_quit(&self) -> bool {
        !self.quitting.swap(true, Ordering::SeqCst)
    }
}

/// True when the command line asks to start hidden in the tray.
#[cfg_attr(mobile, allow(dead_code))]
pub fn start_hidden<S: AsRef<str>>(args: &[S]) -> bool {
    args.iter().skip(1).any(|a| a.as_ref() == HIDDEN_FLAG)
}

/// Shows, un-minimises and focuses the main window.
pub fn reveal_main_window<R: Runtime>(app: &AppHandle<R>) {
    let Some(window) = app.get_webview_window(MAIN_WINDOW) else {
        return;
    };
    #[cfg(desktop)]
    {
        let _ = window.unminimize();
        let _ = window.show();
        let _ = window.set_focus();
    }
    #[cfg(mobile)]
    {
        let _ = window.show();
    }
}

/// Creates the tray (desktop) and shows the main window unless started with `--hidden`.
pub fn setup<R: Runtime>(app: &tauri::App<R>) {
    #[cfg(desktop)]
    {
        let handle = app.handle();
        let tray_ok = match create_tray(handle) {
            Ok(()) => true,
            Err(e) => {
                log::warn!("tray icon unavailable, closing the window quits Brulion: {e}");
                false
            }
        };
        handle.state::<CloseToTray>().set_tray_available(tray_ok);
        let args: Vec<String> = std::env::args().collect();
        // Never start invisible without a tray to bring the window back.
        if !(tray_ok && start_hidden(&args)) {
            reveal_main_window(handle);
        }
    }
    #[cfg(mobile)]
    reveal_main_window(app.handle());
}

/// Shared libraries libappindicator-sys (Tauri's Linux tray backend) tries, in its order
/// (the unversioned names come from its `backcompat` feature, on by default). It panics when
/// none of them can be loaded, killing the app at startup, so the tray is only created when
/// one of them loads.
#[cfg(target_os = "linux")]
const APPINDICATOR_LIBS: &[&str] = &[
    "libayatana-appindicator3.so.1",
    "libappindicator3.so.1",
    "libayatana-appindicator3.so",
    "libappindicator3.so",
];

#[cfg(target_os = "linux")]
fn any_library_loads(names: &[&str]) -> bool {
    names.iter().any(|name| {
        // SAFETY: loading runs the library's initialisers; these are exactly the libraries
        // the tray backend loads right afterwards anyway. The handle is closed again.
        unsafe { libloading::Library::new(name) }.is_ok()
    })
}

#[cfg(desktop)]
fn create_tray<R: Runtime>(app: &AppHandle<R>) -> Result<(), String> {
    #[cfg(target_os = "linux")]
    if !any_library_loads(APPINDICATOR_LIBS) {
        return Err(format!(
            "none of {} can be loaded (install libayatana-appindicator)",
            APPINDICATOR_LIBS.join(", ")
        ));
    }
    build_tray(app).map_err(|e| e.to_string())
}

#[cfg(desktop)]
fn build_tray<R: Runtime>(app: &AppHandle<R>) -> tauri::Result<()> {
    use tauri::menu::{Menu, MenuItem, PredefinedMenuItem};
    use tauri::tray::{MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};

    let show = MenuItem::with_id(app, "show", "Pokaż Brulion", true, None::<&str>)?;
    let separator = PredefinedMenuItem::separator(app)?;
    let quit = MenuItem::with_id(app, "quit", "Zakończ", true, None::<&str>)?;
    let menu = Menu::with_items(app, &[&show, &separator, &quit])?;

    let mut builder = TrayIconBuilder::with_id("brulion")
        .tooltip("Brulion")
        .menu(&menu)
        .show_menu_on_left_click(false)
        .on_menu_event(|app, event| match event.id().as_ref() {
            "show" => reveal_main_window(app),
            "quit" => request_quit(app),
            _ => {}
        })
        .on_tray_icon_event(|tray, event| {
            // Not delivered by AppIndicator on Linux (menu only) — still useful elsewhere.
            if let TrayIconEvent::Click {
                button: MouseButton::Left,
                button_state: MouseButtonState::Up,
                ..
            } = event
            {
                reveal_main_window(tray.app_handle());
            }
        });
    if let Some(icon) = app.default_window_icon() {
        builder = builder.icon(icon.clone());
    }
    builder.build(app)?;
    Ok(())
}

/// Quits after letting the frontend flush: hides the main window, emits [`QUIT_EVENT`] and
/// exits after [`QUIT_GRACE`] unless `quit_app` comes first. A second request exits at once.
#[cfg(desktop)]
pub fn request_quit<R: Runtime>(app: &AppHandle<R>) {
    use tauri::Emitter;
    if !app.state::<CloseToTray>().begin_quit() {
        app.exit(0);
        return;
    }
    if let Some(window) = app.get_webview_window(MAIN_WINDOW) {
        let _ = window.hide();
    }
    if let Err(e) = app.emit(QUIT_EVENT, ()) {
        log::warn!("cannot emit {QUIT_EVENT}: {e}");
        app.exit(0);
        return;
    }
    let handle = app.clone();
    let timer = std::thread::Builder::new()
        .name("brulion-quit".into())
        .spawn(move || {
            std::thread::sleep(QUIT_GRACE);
            handle.exit(0);
        });
    if timer.is_err() {
        app.exit(0);
    }
}

/// Closing the main window: hide it to the tray, or quit (with the flush handshake).
#[cfg(desktop)]
pub fn on_window_event<R: Runtime>(window: &tauri::Window<R>, event: &tauri::WindowEvent) {
    if let tauri::WindowEvent::CloseRequested { api, .. } = event {
        if window.label() != MAIN_WINDOW {
            return;
        }
        api.prevent_close();
        match window.state::<CloseToTray>().on_close() {
            CloseAction::Hide => {
                let _ = window.hide();
            }
            CloseAction::Quit => request_quit(window.app_handle()),
        }
    }
}

/// `set_close_to_tray { enabled }` — no effect on mobile.
#[tauri::command]
pub async fn set_close_to_tray(state: State<'_, CloseToTray>, enabled: bool) -> Result<(), String> {
    state.set_enabled(enabled);
    Ok(())
}

/// `quit_app` — exits right away (the frontend has flushed; also ends a pending
/// `brulion://quit-requested` wait).
#[tauri::command]
pub async fn quit_app<R: Runtime>(app: AppHandle<R>) -> Result<(), String> {
    app.exit(0);
    Ok(())
}

/// `show_main_window` — e.g. when a reminder notification is clicked.
#[tauri::command]
pub async fn show_main_window<R: Runtime>(app: AppHandle<R>) -> Result<(), String> {
    reveal_main_window(&app);
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hidden_flag_detection() {
        assert!(start_hidden(&["brulion", "--hidden"]));
        assert!(start_hidden(&["/usr/bin/brulion", "--foo", "--hidden"]));
        assert!(!start_hidden(&["brulion"]));
        assert!(!start_hidden(&["--hidden"]), "argv[0] is the program");
        assert!(!start_hidden(&["brulion", "--hidden=no"]));
        assert!(!start_hidden::<&str>(&[]));
    }

    #[test]
    fn close_to_tray_needs_a_tray() {
        let s = CloseToTray::default();
        assert!(!s.active(), "no tray yet");
        s.set_tray_available(true);
        assert!(s.active(), "enabled by default");
        s.set_enabled(false);
        assert!(!s.active());
        s.set_enabled(true);
        s.set_tray_available(false);
        assert!(!s.active());
    }

    #[test]
    fn closing_hides_only_with_a_tray_and_otherwise_quits() {
        let s = CloseToTray::default();
        assert_eq!(
            s.on_close(),
            CloseAction::Quit,
            "no tray: quit (with flush)"
        );
        s.set_tray_available(true);
        assert_eq!(s.on_close(), CloseAction::Hide);
        s.set_enabled(false);
        assert_eq!(s.on_close(), CloseAction::Quit);
    }

    #[test]
    fn only_the_first_quit_request_waits_for_the_frontend() {
        let s = CloseToTray::default();
        assert!(s.begin_quit());
        assert!(!s.begin_quit(), "second request exits at once");
        assert!(!s.begin_quit());
        assert!(QUIT_GRACE >= Duration::from_secs(1) && QUIT_GRACE <= Duration::from_secs(5));
        assert!(QUIT_EVENT.starts_with("brulion://"));
    }

    #[cfg(target_os = "linux")]
    #[test]
    fn library_probe_never_panics_on_missing_libraries() {
        assert!(!any_library_loads(&["libbrulion-does-not-exist.so.1"]));
        assert!(!any_library_loads(&[]));
        // Already loaded into every process: matched by soname.
        assert!(any_library_loads(&[
            "libbrulion-does-not-exist.so.1",
            "libc.so.6"
        ]));
        // Same names, same order as libappindicator-sys 0.9 with `backcompat`.
        assert_eq!(
            APPINDICATOR_LIBS,
            [
                "libayatana-appindicator3.so.1",
                "libappindicator3.so.1",
                "libayatana-appindicator3.so",
                "libappindicator3.so"
            ]
        );
    }
}
