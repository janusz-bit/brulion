//! Brulion — Tauri 2 shell for Linux desktop and Android.
//!
//! The UI is the same web app as the PWA (`../src`); this crate provides native vault access
//! (`fs_*` commands with atomic writes, a recursive watcher), the tray / close-to-tray
//! behaviour on desktop and the official plugins. The IPC contract is documented in
//! `docs/ARCHITECTURE.md` ("Tauri IPC contract").

mod fs_cmds;
mod paths;
mod tray;
mod watch;

/// Warnings and errors of Brulion, Tauri and the plugins go to stderr (terminal / journal).
/// Without a logger the `log` facade drops them, e.g. "tray icon unavailable".
struct StderrLogger;

impl log::Log for StderrLogger {
    fn enabled(&self, metadata: &log::Metadata) -> bool {
        metadata.level() <= log::Level::Warn
    }

    fn log(&self, record: &log::Record) {
        if self.enabled(record.metadata()) {
            eprintln!(
                "[brulion {} {}] {}",
                record.level(),
                record.target(),
                record.args()
            );
        }
    }

    fn flush(&self) {}
}

static LOGGER: StderrLogger = StderrLogger;

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    if log::set_logger(&LOGGER).is_ok() {
        log::set_max_level(log::LevelFilter::Warn);
    }

    #[allow(unused_mut)]
    let mut builder = tauri::Builder::default();

    #[cfg(desktop)]
    {
        use tauri_plugin_window_state::StateFlags;
        builder = builder
            // Must be registered first: a second launch only brings the running window up
            // (unless it was started with `--hidden`, e.g. by autostart).
            .plugin(tauri_plugin_single_instance::init(|app, args, _cwd| {
                if !tray::start_hidden(&args) {
                    tray::reveal_main_window(app);
                }
            }))
            // Visibility is decided at startup (`--hidden`), never restored.
            .plugin(
                tauri_plugin_window_state::Builder::new()
                    .with_state_flags(StateFlags::all().difference(StateFlags::VISIBLE))
                    .build(),
            )
            .on_window_event(tray::on_window_event);
    }

    #[cfg(target_os = "android")]
    {
        // Used from Rust only (`write_export` to `content://` URIs); no JS permissions.
        builder = builder.plugin(tauri_plugin_fs::init());
    }

    builder
        .plugin(tauri_plugin_notification::init())
        .plugin(tauri_plugin_http::init())
        .plugin(tauri_plugin_dialog::init())
        .plugin(tauri_plugin_opener::init())
        .plugin(tauri_plugin_os::init())
        // Local Android plugin (src-tauri/plugins/brulion-android); neutral on desktop.
        .plugin(tauri_plugin_brulion_android::init())
        .manage(watch::Watchers::default())
        .manage(tray::CloseToTray::default())
        .invoke_handler(tauri::generate_handler![
            fs_cmds::fs_list,
            fs_cmds::fs_stat,
            fs_cmds::fs_read_text,
            fs_cmds::fs_read_binary,
            fs_cmds::fs_write_text,
            fs_cmds::fs_write_binary,
            fs_cmds::fs_remove,
            fs_cmds::fs_rename,
            fs_cmds::fs_mkdir,
            fs_cmds::write_export,
            fs_cmds::default_vault_dir,
            fs_cmds::allow_vault_assets,
            watch::watch_start,
            watch::watch_stop,
            tray::set_close_to_tray,
            tray::quit_app,
            tray::show_main_window,
        ])
        .setup(|app| {
            tray::setup(app);
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running Brulion");
}

/// Sanity checks of tauri.conf.json and the capabilities against the IPC contract.
#[cfg(test)]
mod config_tests {
    use serde_json::Value;

    fn json(src: &str) -> Value {
        serde_json::from_str(src).expect("valid JSON")
    }

    fn conf() -> Value {
        json(include_str!("../tauri.conf.json"))
    }

    fn permissions(cap: &Value) -> Vec<String> {
        cap["permissions"]
            .as_array()
            .unwrap()
            .iter()
            .map(|p| match p {
                Value::String(s) => s.clone(),
                other => other["identifier"].as_str().unwrap().to_string(),
            })
            .collect()
    }

    /// Same parsing as tauri-plugin-http's scope entries (`src/scope.rs`).
    fn http_pattern(s: &str) -> urlpattern::UrlPattern {
        let mut init =
            urlpattern::UrlPatternInit::parse_constructor_string::<regex::Regex>(s, None)
                .unwrap_or_else(|e| panic!("{s}: {e}"));
        if init.search.as_ref().is_none_or(|p| p.is_empty()) {
            init.search = Some("*".into());
        }
        if init.hash.as_ref().is_none_or(|p| p.is_empty()) {
            init.hash = Some("*".into());
        }
        if init
            .pathname
            .as_ref()
            .is_none_or(|p| p.is_empty() || p == "/")
        {
            init.pathname = Some("*".into());
        }
        urlpattern::UrlPattern::parse(init, Default::default())
            .unwrap_or_else(|e| panic!("{s}: {e}"))
    }

    #[test]
    fn http_scope_allows_user_configured_sync_servers() {
        let cap = json(include_str!("../capabilities/default.json"));
        let http = cap["permissions"]
            .as_array()
            .unwrap()
            .iter()
            .find(|p| p["identifier"] == "http:default")
            .expect("http:default with a scope");
        let patterns: Vec<_> = http["allow"]
            .as_array()
            .unwrap()
            .iter()
            .map(|e| http_pattern(e["url"].as_str().unwrap()))
            .collect();
        let allowed = |u: &str| {
            let url = url::Url::parse(u).unwrap();
            patterns.iter().any(|p| {
                p.test(urlpattern::UrlPatternMatchInput::Url(url.clone()))
                    .unwrap_or_default()
            })
        };
        for ok in [
            "https://cloud.example.com/remote.php/dav/files/jan/Brulion/Zażółć.md",
            "https://nc.local:8443/remote.php/dav/",
            "http://127.0.0.1:8384/rest/system/status",
            "http://localhost:8384/rest/db/status?folder=abcd-1234",
            "http://192.168.1.10/remote.php/dav/",
        ] {
            assert!(allowed(ok), "{ok}");
        }
        for denied in ["file:///etc/passwd", "ftp://example.com/x"] {
            assert!(!allowed(denied), "{denied}");
        }
    }

    #[test]
    fn capabilities_cover_the_plugins() {
        let all = permissions(&json(include_str!("../capabilities/default.json")));
        for p in [
            "core:default",
            "notification:default",
            "dialog:default",
            "opener:default",
            "os:default",
            "http:default",
            "brulion-android:default",
        ] {
            assert!(all.contains(&p.to_string()), "default.json lacks {p}");
        }
        let desktop = json(include_str!("../capabilities/desktop.json"));
        assert!(permissions(&desktop).contains(&"window-state:default".to_string()));
        assert_eq!(
            desktop["platforms"],
            serde_json::json!(["linux", "macOS", "windows"])
        );
        let mobile = json(include_str!("../capabilities/mobile.json"));
        assert_eq!(mobile["platforms"], serde_json::json!(["android", "iOS"]));
        for cap in [&desktop, &mobile] {
            assert_eq!(
                cap["windows"],
                serde_json::json!([crate::tray::MAIN_WINDOW])
            );
        }
    }

    #[test]
    fn main_window_and_identity() {
        let c = conf();
        assert_eq!(c["identifier"], "io.github.januszbit.brulion");
        assert_eq!(c["productName"], "Brulion");
        assert_eq!(c["version"], "../package.json");
        assert_eq!(c["build"]["devUrl"], "http://localhost:1420");
        assert_eq!(c["build"]["frontendDist"], "../dist");
        let w = &c["app"]["windows"][0];
        assert_eq!(w["label"], crate::tray::MAIN_WINDOW);
        assert_eq!(w["visible"], false, "setup shows it unless --hidden");
        assert_eq!(
            (w["width"].as_u64(), w["height"].as_u64()),
            (Some(1200), Some(800))
        );
        assert_eq!(
            (w["minWidth"].as_u64(), w["minHeight"].as_u64()),
            (Some(360), Some(480))
        );
        assert_eq!(c["bundle"]["android"]["minSdkVersion"], 24);
        assert_eq!(
            c["bundle"]["targets"],
            serde_json::json!(["deb", "appimage", "rpm"])
        );
        assert_eq!(c["app"]["security"]["assetProtocol"]["enable"], true);
    }

    #[test]
    fn release_builds_do_not_abort_on_panic() {
        // A panicking command/plugin/watcher thread must not kill the app with unsaved edits.
        let cargo = include_str!("../Cargo.toml");
        assert!(!cargo
            .lines()
            .map(str::trim)
            .any(|l| l.starts_with("panic") && l.contains("abort")));
    }

    #[test]
    fn documented_events_match_the_code() {
        let doc = include_str!("../../docs/ARCHITECTURE.md");
        for name in [crate::watch::EVENT, crate::tray::QUIT_EVENT] {
            assert!(
                doc.contains(&format!("`{name}`")),
                "{name} not in the IPC contract"
            );
        }
        for cmd in [
            "fs_list",
            "fs_stat",
            "fs_read_text",
            "fs_read_binary",
            "fs_write_text",
            "fs_write_binary",
            "fs_remove",
            "fs_rename",
            "fs_mkdir",
            "watch_start",
            "watch_stop",
            "default_vault_dir",
            "write_export",
            "allow_vault_assets",
            "set_close_to_tray",
            "quit_app",
            "show_main_window",
        ] {
            assert!(
                doc.contains(&format!("| `{cmd}` |")),
                "{cmd} not in the table"
            );
        }
    }

    #[test]
    fn csp_allows_what_the_frontend_needs() {
        let c = conf();
        for key in ["csp", "devCsp"] {
            let csp = &c["app"]["security"][key];
            let has = |directive: &str, source: &str| {
                csp[directive]
                    .as_str()
                    .unwrap_or_else(|| panic!("{key} lacks {directive}"))
                    .split_whitespace()
                    .any(|s| s == source)
            };
            for src in ["ipc:", "http://ipc.localhost", "https:", "http:"] {
                assert!(has("connect-src", src), "{key} connect-src {src}");
            }
            for src in ["asset:", "http://asset.localhost", "data:", "blob:"] {
                assert!(has("img-src", src), "{key} img-src {src}");
            }
            assert!(has("media-src", "asset:") && has("media-src", "http://asset.localhost"));
            assert!(
                has("style-src", "'unsafe-inline'"),
                "CodeMirror inline styles"
            );
            assert!(has("script-src", "'wasm-unsafe-eval'"));
            assert!(!has("script-src", "'unsafe-eval'"));
        }
        // Tauri must not add nonces to style-src, or 'unsafe-inline' would be ignored.
        assert_eq!(
            c["app"]["security"]["dangerousDisableAssetCspModification"],
            serde_json::json!(["style-src"])
        );
    }
}
