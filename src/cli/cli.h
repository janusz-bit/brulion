// brulion-cli — Brulion's command line for scripts and AI agents (docs/CLI.md): reads and changes
// the notes of a notebook folder headless, prints JSON (or text), serves the same commands as MCP
// tools (`brulion-cli mcp`) and asks a running app to open a page (`app open`). Built on the core
// module notebook, so it behaves like the app; it never starts reminders or sync and never writes
// the app's settings.
//
//   brulion-cli [global options] <command> [<arguments>] [--options]
//   brulion cli …        the same inside the app's binary (also `Brulion.AppImage cli …`)
#pragma once

namespace brulion::cli {

/// Run the command line: `argv[0]` is the program name, the rest its arguments (UTF-8). Creates
/// the QCoreApplication itself (call it before any other Qt application object exists) and
/// returns the exit code (docs/CLI.md "Exit codes").
int run(int argc, char **argv);

} // namespace brulion::cli
