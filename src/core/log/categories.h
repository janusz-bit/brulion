// Logging categories of Brulion. Use these (never a new Q_LOGGING_CATEGORY with the same name):
//   qCInfo(brulion::logging::lcSync) << "…";
// Levels: qCDebug = detail (on only in the "detailed" mode), qCInfo = what happened (normal
// mode), qCWarning = something failed but Brulion copes / retries, qCCritical = needs the user.
#pragma once

#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::logging {

Q_DECLARE_LOGGING_CATEGORY(lcApp)       ///< "brulion.app"       start-up, app state, settings
Q_DECLARE_LOGGING_CATEGORY(lcSync)      ///< "brulion.sync"      runs, phases, decisions, file errors
Q_DECLARE_LOGGING_CATEGORY(lcNet)       ///< "brulion.net"       HTTP exchanges (debug), slow / failed requests
Q_DECLARE_LOGGING_CATEGORY(lcVault)     ///< "brulion.vault"     open, scans, watcher, LocalStore
Q_DECLARE_LOGGING_CATEGORY(lcIndex)     ///< "brulion.index"     indexing, search index
Q_DECLARE_LOGGING_CATEGORY(lcReminders) ///< "brulion.reminders" scheduling, notifications
Q_DECLARE_LOGGING_CATEGORY(lcIo)        ///< "brulion.io"        import, export
Q_DECLARE_LOGGING_CATEGORY(lcPlatform)  ///< "brulion.platform"  Android / browser bridges

/// UI filter group of a category name:
///   "sync"      brulion.sync*
///   "net"       brulion.net*, qt.network*
///   "vault"     brulion.vault*, brulion.workspace*
///   "index"     brulion.index*
///   "reminders" brulion.reminders*, brulion.notify*
///   "io"        brulion.io*
///   "app"       every other brulion.* (app, platform, android, …)
///   "system"    everything else (qt.*, qml, js, default)
QString categoryGroup(QStringView category);
/// The groups in display order (the list above).
QStringList categoryGroups();

/// The brulion categories whose debug output the verbosity switches (named one by one, so a
/// category with a higher default threshold — brulion.touchflick — never starts logging every
/// touch). Patterns as QLoggingCategory rules take them ("brulion.notify.*").
QStringList verbosityCategories();

} // namespace brulion::logging
