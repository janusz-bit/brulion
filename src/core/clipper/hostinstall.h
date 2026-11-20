// Connecting the browsers to the Web Clipper's host (docs/CLIPPER.md "Installing"): the host manifests
// written where each browser looks (nativehost.h's places), Brulion's own removed, and what is there
// now — for `brulion-cli clip install / uninstall / status` and the app's Settings → Application →
// "Browser extension". Files through QSaveFile; on Windows also the registry key naming the manifest
// (QSettings' native format, used only when `os` is Windows). Part of the clipper module — see
// clipper/clipper.h.
#pragma once

#include "clipper/nativehost.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace brulion::clipper {

/// One browser's host: where it is, and what is there.
struct HostState {
    HostLocation location;
    /// The browser's profile folder exists (it is installed for this user); true when that cannot be
    /// told (the system's scope).
    bool browserFound = true;
    bool installed = false;        ///< Brulion's manifest is there (its "name")
    QString program;               ///< the program it names
    bool programExists = false;    ///< … and that can be run
    bool thisProgram = false;      ///< … and it is `selfProgram` (the same file)
    bool extensionAllowed = false; ///< it lets Brulion's extension in
    QString error;                 ///< install / uninstall: why it failed ("" = fine)
};

struct HostSetup {
    Os os = Os::Linux;
    Scope scope = Scope::User;
    Environment env;
    QString program;             ///< the program the manifests name (absolute)
    QStringList extraChromeIds;  ///< more Chromium extension ids to let in
    bool dryRun = false;         ///< compute, write nothing
};

/// The environment of this process ($HOME, $XDG_CONFIG_HOME, %LOCALAPPDATA%, …).
Environment currentEnvironment();

/// The program the manifests should name when started as `applicationFilePath`: the AppImage
/// ($APPIMAGE), a stable link on PATH (or /run/current-system/sw/bin, ~/.nix-profile/bin) to a
/// program of the Nix store — a store path is gone after an upgrade and a garbage collection —,
/// else the program itself.
QString defaultHostProgram(const QString &applicationFilePath);

/// NixOS: the system's browsers get the host through the module (no /etc writes of ours).
bool isNixOs();

/// The state of `browser`'s host (`selfProgram`: compared with the manifest's program).
HostState hostState(Browser browser, Os os, Scope scope, const Environment &env, const QString &selfProgram);

/// Write `browser`'s manifest naming `setup.program` (and, on Windows, its registry key). The state
/// after it (`error` when it failed; nothing written in a dry run).
HostState installHost(Browser browser, const HostSetup &setup);

/// Remove Brulion's manifest of `browser` (another program's file of that name stays) and its
/// registry key. `keepShared`: browsers whose host stays — on Windows the Chromium browsers share
/// one file, removed only with the last of them.
HostState uninstallHost(Browser browser, const HostSetup &setup, const QList<Browser> &keepShared = {});

} // namespace brulion::clipper
