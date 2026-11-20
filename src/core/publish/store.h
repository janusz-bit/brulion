// Where a notebook's publishing targets and their manifests live on this device: the notebook's
// device state folder (notebook::deviceStatePath — the app and brulion-cli share it), never the
// notebook itself (a synced or imported notebook cannot name a server to send notes to):
//   <state>/publish/targets.json        [targets with their last status] (publish/target.h)
//   <state>/publish/<target id>.json    the target's manifest (publish/manifest.h)
// Read from disk on every call, written with QSaveFile (the app and the command line may both
// write). Secrets — the GitHub token, a WebDAV or Nextcloud password, a Nextcloud link's password —
// are owner-only files in the app's data folder (secretFile()), never in these files.
// Part of the publish module — see publish/publish.h.
#pragma once

#include "publish/manifest.h"
#include "publish/target.h"

#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace brulion::publish {

class PublishStore {
public:
    /// `stateDir`: the notebook's device state folder ("" = in memory only).
    explicit PublishStore(const QString &stateDir);

    QString folder() const { return m_folder; }

    /// The targets, in the order they were added.
    QList<PublishTarget> targets() const;
    std::optional<PublishTarget> target(const QString &id) const;
    /// Add `target`, or replace the one with its id. False when it cannot be written.
    bool saveTarget(const PublishTarget &target);
    /// Remove a target and its manifest.
    bool removeTarget(const QString &id);
    /// The target's last status (kept with the target).
    bool setStatus(const QString &id, const PublishStatus &status);

    /// The manifest of a target (empty when none).
    PublishManifest manifest(const QString &id) const;
    bool saveManifest(const QString &id, const PublishManifest &manifest);

private:
    bool writeTargets(const QList<PublishTarget> &targets);
    QString targetsFile() const;
    QString manifestFile(const QString &id) const;

    QString m_folder;
    // In memory (no state folder).
    mutable QList<PublishTarget> m_memoryTargets;
    mutable QHash<QString, PublishManifest> m_memoryManifests;
};

/// The owner-only file of a target's secret in the app's data folder:
/// `<appData>/secrets/publish-<id>` (`which` "password": WebDAV / Nextcloud) or
/// `<appData>/secrets/publish-<id>-share` ("share": the Nextcloud link's password).
QString secretFile(const QString &appDataDir, const QString &targetId, const QString &which);
/// The GitHub token's file, shared with the notebooks from GitHub: `<appData>/secrets/github-token`.
QString githubTokenFile(const QString &appDataDir);
/// Keep `value` in `file` (0600, its folder 0700); "" removes the file. False on I/O errors.
bool writeSecret(const QString &file, const QString &value);
/// The secret in `file`, trimmed ("" when there is none).
QString readSecret(const QString &file);

} // namespace brulion::publish
