#pragma once
// Hostile extensions against the engine where it runs (docs/EXTENSIONS.md "Sandbox and limits"):
// scripts that loop for ever, backtrack, eat memory, recurse, nest requires, ask with megabytes or
// look for a way out — each must end as the limits say, in time, and leave the engine working. The
// same checks run in the tests (tst_extensions_hostile) and in the browser build, where the engine
// has no threads and its clock is JavaScript's (`?selftest=extensions`, scripts/wasm-smoke.mjs
// --extensions: src/app/main.cpp publishes the report as `window.brulionExtensionsSelfTest`).
//
//   const QVariantMap r = hostileSelfTest();   // {ok, engine, limitMs, failed, checks: [{name, ok, got, ms, detail}]}
//
// Nothing of a notebook is touched: the engine runs over a host of its own with no notes.

#include <QVariantMap>

namespace brulion::extensions {

/// Runs every hostile script in a fresh engine (a time limit of `limitMs` a call, 16 MB) and
/// reports each one's outcome; `ok` when all ended as they must.
QVariantMap hostileSelfTest(int limitMs = 300);

} // namespace brulion::extensions
