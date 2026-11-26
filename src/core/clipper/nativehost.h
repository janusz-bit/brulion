// The native messaging host the Web Clipper talks to (docs/CLIPPER.md "Connecting the browser"):
// its name, the extension's ids, the host manifest a browser reads to find brulion-cli, and where
// each browser looks for that manifest — files on Linux and macOS (the user's own folder or the
// system's), a file named by a registry key on Windows. Pure functions over an Environment (no
// file is read or written here, no platform #ifdef: the caller says which system it runs on);
// `brulion-cli clip install` and the Nix package write what these return. Part of the clipper
// module — see clipper/clipper.h.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion::clipper {

/// The host's name (the manifest's file name without ".json"; the extension connects to it).
QString hostName();
/// The Chromium extension's id (fixed by the public key in clipper/manifest.json).
QString chromeExtensionId();
/// The Firefox extension's id (browser_specific_settings.gecko.id).
QString firefoxExtensionId();
/// "chrome-extension://<id>/" — what a Chromium browser passes the host as its first argument.
QString chromeOrigin(const QString &extensionId);

enum class Browser { Chrome, Chromium, Brave, Edge, Firefox };
enum class Family { Chromium, Firefox };
enum class Os { Linux, MacOs, Windows };
enum class Scope { User, System };

/// Every browser, in the order commands list them.
QList<Browser> allBrowsers();
/// "chrome", "chromium", "brave", "edge", "firefox".
QString browserId(Browser browser);
/// "Google Chrome", "Chromium", "Brave", "Microsoft Edge", "Firefox".
QString browserName(Browser browser);
std::optional<Browser> parseBrowser(QStringView id);
Family familyOf(Browser browser);
/// The system this program runs on (QSysInfo, at run time).
Os currentOs();
QString osId(Os os);

/// The host manifest for a browser family: `{name, description, path, type: "stdio",
/// allowed_origins: [chrome-extension://…/]}` (Chromium; `extraChromeIds` add origins, e.g. the
/// id a store gave the extension) or `{…, allowed_extensions: [id]}` (Firefox). `program` is
/// the absolute path of the program the browser starts (brulion-cli, or the AppImage).
QJsonObject hostManifest(Family family, const QString &program, const QStringList &extraChromeIds = {});

/// The folders a location is computed from (absolute, with "/" separators; Windows paths with
/// "/" too, as Qt writes them).
struct Environment {
    QString home;          ///< $HOME, %USERPROFILE%
    QString xdgConfigHome; ///< $XDG_CONFIG_HOME ("" = ~/.config)
    QString localAppData;  ///< %LOCALAPPDATA%
    QString appData;       ///< %APPDATA%
    QString programData;   ///< %ProgramData%
    /// The folder the system's own folders are in ("" = "/", the real ones; a test's private
    /// folder, so the machine's manifests stay out of it): /etc/… and /usr/lib/… on Linux,
    /// /Library/… on macOS. (Windows' system scope is %ProgramData% and the registry.)
    QString systemRoot;
};

/// Where a browser finds the host.
struct HostLocation {
    Browser browser = Browser::Chrome;
    QString manifestPath; ///< the manifest file to write
    /// Windows: the registry key whose default value names the manifest
    /// ("HKEY_CURRENT_USER\\Software\\Google\\Chrome\\NativeMessagingHosts\\<name>"); "" elsewhere.
    QString registryKey;
    /// A folder that exists when the browser is installed for the user (its profiles), to install
    /// only for the browsers there are; "" when it cannot be told (system scope).
    QString browserFolder;
};

/// The location for `browser` on `os` in `scope` (each family has its own manifest file on
/// Windows, where the file's folder is Brulion's own).
HostLocation hostLocation(Browser browser, Os os, Scope scope, const Environment &env);

/// Is this program started by a browser as the native messaging host? Chromium browsers pass the
/// extension's origin ("chrome-extension://…/", on Windows also "--parent-window=…"), Firefox
/// the manifest's path (a file named after the host) and the extension's id.
bool isNativeMessagingLaunch(const QStringList &arguments);

} // namespace brulion::clipper
