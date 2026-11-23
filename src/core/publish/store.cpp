#include "publish/store.h"

#include "log/categories.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::publish {

namespace {

/// A target id that may name a file ("t" + hex digits, as newTargetId() makes them; letters,
/// digits, '-' and '_' from elsewhere).
bool isSafeId(const QString &id)
{
    if (id.isEmpty() || id.size() > 64)
        return false;
    return std::ranges::all_of(id, [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-'
               || c == u'_';
    });
}

QJsonDocument readJson(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(in.readAll());
}

bool writeJson(const QString &file, const QJsonDocument &doc)
{
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
        return false;
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    out.write(doc.toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        qCWarning(logging::lcIo).noquote()
                << u"Publishing: %1 could not be written"_s.arg(QDir::toNativeSeparators(file));
        return false;
    }
    return true;
}

} // namespace

PublishStore::PublishStore(const QString &stateDir)
    : m_folder(stateDir.isEmpty() ? QString() : QDir(stateDir).filePath(u"publish"_s))
{ }

QString PublishStore::targetsFile() const
{
    return m_folder + u"/targets.json"_s;
}

QString PublishStore::manifestFile(const QString &id) const
{
    return m_folder + u'/' + id + u".json"_s;
}

QList<PublishTarget> PublishStore::targets() const
{
    if (m_folder.isEmpty())
        return m_memoryTargets;
    QList<PublishTarget> out;
    const QJsonArray list = readJson(targetsFile()).object().value(u"targets"_s).toArray();
    for (const QJsonValue v : list) {
        PublishTarget t = targetFromJson(v.toObject());
        if (isSafeId(t.id) && std::ranges::none_of(out, [&](const PublishTarget &o) { return o.id == t.id; }))
            out.append(std::move(t));
    }
    return out;
}

std::optional<PublishTarget> PublishStore::target(const QString &id) const
{
    for (const QList<PublishTarget> all = targets(); const PublishTarget &t : all) {
        if (t.id == id)
            return t;
    }
    return std::nullopt;
}

bool PublishStore::writeTargets(const QList<PublishTarget> &list)
{
    if (m_folder.isEmpty()) {
        m_memoryTargets = list;
        return true;
    }
    QJsonArray array;
    for (const PublishTarget &t : list)
        array.append(toJson(t));
    return writeJson(targetsFile(), QJsonDocument(QJsonObject{{u"version"_s, 1}, {u"targets"_s, array}}));
}

bool PublishStore::saveTarget(const PublishTarget &saved)
{
    if (!isSafeId(saved.id))
        return false;
    PublishTarget target = saved;
    ensureProtectSalt(target); // a protected target always has its salt in the store
    QList<PublishTarget> list = targets();
    const auto it = std::ranges::find(list, target.id, &PublishTarget::id);
    if (it == list.end())
        list.append(target);
    else
        *it = target;
    return writeTargets(list);
}

bool PublishStore::removeTarget(const QString &id)
{
    QList<PublishTarget> list = targets();
    const auto removed = list.removeIf([&](const PublishTarget &t) { return t.id == id; });
    if (removed == 0)
        return false;
    if (m_folder.isEmpty())
        m_memoryManifests.remove(id);
    else if (isSafeId(id))
        QFile::remove(manifestFile(id));
    return writeTargets(list);
}

bool PublishStore::setStatus(const QString &id, const PublishStatus &status)
{
    QList<PublishTarget> list = targets();
    const auto it = std::ranges::find(list, id, &PublishTarget::id);
    if (it == list.end())
        return false;
    it->last = status;
    return writeTargets(list);
}

PublishManifest PublishStore::manifest(const QString &id) const
{
    if (m_folder.isEmpty())
        return m_memoryManifests.value(id);
    if (!isSafeId(id))
        return {};
    return manifestFromJson(readJson(manifestFile(id)).object());
}

bool PublishStore::saveManifest(const QString &id, const PublishManifest &manifest)
{
    if (!isSafeId(id))
        return false;
    if (m_folder.isEmpty()) {
        m_memoryManifests.insert(id, manifest);
        return true;
    }
    if (manifest.isEmpty())
        return !QFileInfo::exists(manifestFile(id)) || QFile::remove(manifestFile(id));
    return writeJson(manifestFile(id), QJsonDocument(toJson(manifest)));
}

QString secretFile(const QString &appDataDir, const QString &targetId, const QString &which)
{
    if (!isSafeId(targetId) || appDataDir.isEmpty())
        return {};
    QString suffix;
    if (which == u"share")
        suffix = u"-share"_s;
    else if (which == u"pages")
        suffix = u"-pages"_s;
    return appDataDir + u"/secrets/publish-"_s + targetId + suffix;
}

QString githubTokenFile(const QString &appDataDir)
{
    return appDataDir.isEmpty() ? QString() : appDataDir + u"/secrets/github-token"_s;
}

bool writeSecret(const QString &file, const QString &value)
{
    if (file.isEmpty())
        return false;
    if (value.isEmpty())
        return !QFileInfo::exists(file) || QFile::remove(file);
    const QString dir = QFileInfo(file).absolutePath();
    if (!QDir().mkpath(dir))
        return false;
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    // Restricted before the secret is written; the rename keeps the permissions.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(value.toUtf8());
    if (!out.commit())
        return false;
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

QString readSecret(const QString &file)
{
    if (file.isEmpty())
        return {};
    QFile in(file);
    return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()).trimmed() : QString();
}

} // namespace brulion::publish
