#include "clipper/hostinstall.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace brulion::clipper {

namespace {

QString envFolder(const char *name)
{
    return QDir::fromNativeSeparators(qEnvironmentVariable(name));
}

/// The manifest at `path` when it is Brulion's host (its "name").
std::optional<QJsonObject> ourManifest(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return std::nullopt;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject() || doc.object().value(u"name"_s).toString() != hostName())
        return std::nullopt;
    return doc.object();
}

bool registryNames(const QString &key)
{
    if (key.isEmpty())
        return false;
    const QSettings registry(key, QSettings::NativeFormat);
    return registry.contains(u"Default"_s);
}

} // namespace

Environment currentEnvironment()
{
    Environment env;
    env.home = QDir::fromNativeSeparators(QDir::homePath());
    env.xdgConfigHome = envFolder("XDG_CONFIG_HOME");
    env.localAppData = envFolder("LOCALAPPDATA");
    env.appData = envFolder("APPDATA");
    env.programData = envFolder("ProgramData");
    return env;
}

QString defaultHostProgram(const QString &applicationFilePath)
{
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty() && QFileInfo(appImage).isFile())
        return appImage;
    if (applicationFilePath.startsWith(u"/nix/store/"_s)) {
        const QString name = QFileInfo(applicationFilePath).fileName();
        QStringList dirs = qEnvironmentVariable("PATH").split(u':', Qt::SkipEmptyParts);
        dirs += {u"/run/current-system/sw/bin"_s, QDir::homePath() + u"/.nix-profile/bin"_s};
        for (const QString &dir : std::as_const(dirs)) {
            if (dir.startsWith(u"/nix/store/"_s))
                continue;
            const QFileInfo candidate(dir + u'/' + name);
            if (candidate.isExecutable() && candidate.canonicalFilePath().startsWith(u"/nix/store/"_s))
                return candidate.absoluteFilePath();
        }
    }
    return applicationFilePath;
}

bool isNixOs()
{
    return QFileInfo::exists(u"/etc/NIXOS"_s);
}

HostState hostState(Browser browser, Os os, Scope scope, const Environment &env, const QString &selfProgram)
{
    HostState s;
    s.location = hostLocation(browser, os, scope, env);
    if (!s.location.browserFolder.isEmpty())
        s.browserFound = QFileInfo(s.location.browserFolder).isDir();
    const auto manifest = ourManifest(s.location.manifestPath);
    // On Windows the browser reads the manifest only through its registry key.
    s.installed = manifest.has_value() && (s.location.registryKey.isEmpty() || registryNames(s.location.registryKey));
    if (!manifest)
        return s;
    s.program = manifest->value(u"path"_s).toString();
    const QFileInfo info(s.program);
    s.programExists = info.isExecutable();
    const QString self = QFileInfo(selfProgram).canonicalFilePath();
    s.thisProgram = !self.isEmpty() && info.canonicalFilePath() == self;
    const bool firefox = familyOf(browser) == Family::Firefox;
    const QJsonArray allowed = manifest->value(firefox ? u"allowed_extensions"_s : u"allowed_origins"_s).toArray();
    s.extensionAllowed = allowed.contains(firefox ? firefoxExtensionId() : chromeOrigin(chromeExtensionId()));
    return s;
}

HostState installHost(Browser browser, const HostSetup &setup)
{
    HostState s = hostState(browser, setup.os, setup.scope, setup.env, setup.program);
    if (setup.dryRun)
        return s;
    const QJsonObject manifest =
            hostManifest(familyOf(browser), QDir::toNativeSeparators(setup.program), setup.extraChromeIds);
    const QString folder = QFileInfo(s.location.manifestPath).absolutePath();
    if (!QDir().mkpath(folder)) {
        s.error = u"Could not create the folder %1"_s.arg(QDir::toNativeSeparators(folder));
        return s;
    }
    QSaveFile file(s.location.manifestPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(manifest).toJson()) < 0 || !file.commit()) {
        s.error = file.errorString();
        return s;
    }
    if (!s.location.registryKey.isEmpty()) {
        QSettings registry(s.location.registryKey, QSettings::NativeFormat);
        registry.setValue(u"Default"_s, QDir::toNativeSeparators(s.location.manifestPath));
        registry.sync();
        if (registry.status() != QSettings::NoError) {
            s.error = u"Could not write the registry key %1"_s.arg(s.location.registryKey);
            return s;
        }
    }
    HostState after = hostState(browser, setup.os, setup.scope, setup.env, setup.program);
    return after;
}

HostState uninstallHost(Browser browser, const HostSetup &setup, const QList<Browser> &keepShared)
{
    HostState s = hostState(browser, setup.os, setup.scope, setup.env, setup.program);
    const bool ours = ourManifest(s.location.manifestPath).has_value();
    if (setup.dryRun || (!ours && !registryNames(s.location.registryKey)))
        return s;
    bool fileStays = false;
    if (!s.location.registryKey.isEmpty()) {
        QSettings registry(s.location.registryKey, QSettings::NativeFormat);
        registry.remove(QString());
        registry.sync();
        for (const Browser other : keepShared) {
            if (other != browser && familyOf(other) == familyOf(browser))
                fileStays = fileStays
                            || registryNames(hostLocation(other, setup.os, setup.scope, setup.env).registryKey);
        }
    }
    if (ours && !fileStays && !QFile::remove(s.location.manifestPath)) {
        s.error = u"Could not remove %1"_s.arg(QDir::toNativeSeparators(s.location.manifestPath));
        return s;
    }
    return hostState(browser, setup.os, setup.scope, setup.env, setup.program);
}

} // namespace brulion::clipper
