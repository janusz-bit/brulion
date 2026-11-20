#include "publish/plan.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

/// The site file's version in the target's terms (GitSha / Sha256; "" for Opaque).
QString contentVersion(const SiteFile &f, VersionKind kind)
{
    switch (kind) {
    case VersionKind::GitSha: return f.gitSha;
    case VersionKind::Sha256: return f.sha256;
    case VersionKind::Opaque: break;
    }
    return {};
}

/// A manifest entry's version as the target spells it (Opaque: the recorded ETag).
QString recordedVersion(const ManifestFile &f, VersionKind kind)
{
    if (!f.remote.isEmpty())
        return f.remote;
    return kind == VersionKind::Sha256 ? f.sha256 : QString();
}

void sortPlan(PublishPlan &plan)
{
    std::ranges::sort(plan.files, {}, &PlannedFile::path);
}

} // namespace

QString actionName(FileAction action)
{
    switch (action) {
    case FileAction::Upload: return u"upload"_s;
    case FileAction::Update: return u"update"_s;
    case FileAction::Unchanged: return u"unchanged"_s;
    case FileAction::Delete: return u"delete"_s;
    case FileAction::Conflict: return u"conflict"_s;
    }
    return {};
}

int PublishPlan::count(FileAction action) const
{
    return int(std::ranges::count(files, action, &PlannedFile::action));
}

qint64 PublishPlan::bytes(FileAction action) const
{
    qint64 sum = 0;
    for (const PlannedFile &f : files) {
        if (f.action == action)
            sum += f.size;
    }
    return sum;
}

bool PublishPlan::hasChanges() const
{
    return std::ranges::any_of(files, [](const PlannedFile &f) {
        return f.action == FileAction::Upload || f.action == FileAction::Update || f.action == FileAction::Delete;
    });
}

QList<PlannedFile> PublishPlan::conflicts() const
{
    QList<PlannedFile> out;
    for (const PlannedFile &f : files) {
        if (f.action == FileAction::Conflict)
            out.append(f);
    }
    return out;
}

PublishPlan planPublish(const SiteBuild &site, const PublishManifest &manifest,
                        const std::optional<RemoteState> &remote, VersionKind kind, bool force)
{
    PublishPlan plan;
    plan.remoteChecked = remote.has_value();
    QSet<QString> inSite;
    for (const SiteFile &f : site.files) {
        inSite.insert(f.path);
        PlannedFile p{f.path, FileAction::Upload, f.data.size(), false, {}};
        const auto recorded = manifest.files.constFind(f.path);
        const bool wasPublished = recorded != manifest.files.constEnd();
        if (!remote) {
            // Offline: what the last publish recorded.
            p.action = !wasPublished                  ? FileAction::Upload
                       : recorded->sha256 == f.sha256 ? FileAction::Unchanged
                                                      : FileAction::Update;
            plan.files.append(p);
            continue;
        }
        const auto there = remote->constFind(f.path);
        if (there == remote->constEnd()) {
            p.action = FileAction::Upload;
        } else {
            p.remote = there->version;
            const QString mine = contentVersion(f, kind);
            if (!mine.isEmpty() && mine == there->version)
                p.action = FileAction::Unchanged;
            else if (wasPublished && !there->version.isEmpty() && recordedVersion(*recorded, kind) == there->version)
                p.action = recorded->sha256 == f.sha256 ? FileAction::Unchanged : FileAction::Update;
            else
                p.action = force ? FileAction::Update : FileAction::Conflict;
        }
        plan.files.append(p);
    }
    for (auto it = manifest.files.cbegin(); it != manifest.files.cend(); ++it) {
        if (inSite.contains(it.key()))
            continue;
        PlannedFile p{it.key(), FileAction::Delete, it->size, true, {}};
        if (remote) {
            const auto there = remote->constFind(it.key());
            if (there == remote->constEnd())
                continue; // gone already
            p.remote = there->version;
            const bool same = (!there->version.isEmpty() && recordedVersion(*it, kind) == there->version)
                              || (kind == VersionKind::Sha256 && there->version == it->sha256);
            if (!same && !force)
                p.action = FileAction::Conflict;
        }
        plan.files.append(p);
    }
    sortPlan(plan);
    return plan;
}

PublishPlan planUnpublish(const PublishManifest &manifest, const std::optional<RemoteState> &remote, VersionKind kind,
                          bool force)
{
    return planPublish(SiteBuild{}, manifest, remote, kind, force);
}

} // namespace brulion::publish
