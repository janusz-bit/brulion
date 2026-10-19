// Log — Brulion's event log (UI: "Rejestr zdarzeń"). Module "log"; QtCore only; no deps.
//
// Code keeps logging with Qt's own macros and the categories below; the Logger captures every
// Qt message (brulion.*, Qt's qt.*, QML warnings, console.*) through a chained message handler.
//
// API overview (namespace brulion::logging)
//
//   Categories                                                              log/categories.h
//     lcApp lcSync lcNet lcVault lcIndex lcReminders lcIo lcPlatform ("brulion.app", …):
//       qCInfo(brulion::logging::lcSync) << "#42 started (edit)";
//     Levels: qCDebug = detail (only in the "detailed" mode), qCInfo = what happened,
//     qCWarning = something failed but Brulion copes / retries, qCCritical = needs the user.
//     categoryGroup("brulion.net") -> "net"; categoryGroups() — the UI's filter groups.
//
//   Entries                                                                      log/entry.h
//     Level, Entry{seq, time, level, category, message, repeat, lastTime}; formatLine(entry)
//     ("2026-10-18 14:03:22.517\tW\tbrulion.sync\t…", continuation lines start with "\t");
//     parseLines(text) (never fails).
//
//   Redaction                                                                   log/redact.h
//     redact(text): registered live secrets (addSecret / ScopedSecret: the app password, the
//     Basic token, the Syncthing API key — plain, percent-encoded and Base64), Authorization /
//     Cookie / API-key header values, "Basic …" / "Bearer …", user:password@ in URLs,
//     secret-like query parameters and key=value / "key": "value" pairs, Nextcloud app
//     passwords (xxxxx-xxxxx-xxxxx-xxxxx-xxxxx) and JWTs. Idempotent and thread-safe.
//
//   Logger                                                                      log/logger.h
//     Logger::install(LoggerOptions{capacity, directory, maxFileBytes, …}) — once, right after
//     the application object; entries(afterSeq), signal appended(lastSeq) (≤ 4 per second, no
//     timer while nothing is logged), exportText(header), clear(), flush(), endSession(),
//     setVerbosity(Normal | Detailed) (QLoggingCategory::setFilterRules on the brulion
//     categories; QT_LOGGING_RULES still wins). Messages are redacted, truncated, identical
//     consecutive ones collapsed, debug/info bursts rate-limited; files brulion.log /
//     brulion.log.1 rotate at maxFileBytes.
//
//   Helpers                                                                   log/slowscope.h
//     SlowScope (logs how long a scope took), formatBytes(), formatMs().
#pragma once

#include "log/categories.h"
#include "log/entry.h"
#include "log/logger.h"
#include "log/redact.h"
#include "log/slowscope.h"
