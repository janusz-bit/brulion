// The Web Clipper's host manifests (docs/CLIPPER.md "Connecting the browser"): `clip install` writes
// them where Chrome, Chromium, Brave, Edge and Firefox look (the user's folders or, with --system, the
// system's; on Windows a file named by a registry key), `clip uninstall` removes them (only Brulion's
// own), `clip status` tells what is there and which program it starts, `clip manifest` prints one
// for packages (the Nix package installs Firefox's and Chromium's: docs/NIXOS.md).
#include "clip_p.h"
#include "commands_p.h"

#include "clipper/nativehost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>

using namespace Qt::StringLiterals;

namespace brulion::cli {

using namespace brulion::clipper;

namespace {

/// A folder of the environment with "/" separators ("" when unset).
QString envFolder(const char *name)
{
    return QDir::fromNativeSeparators(qEnvironmentVariable(name));
}

Environment environment()
{
    Environment env;
    env.home = QDir::fromNativeSeparators(QDir::homePath());
    env.xdgConfigHome = envFolder("XDG_CONFIG_HOME");
    env.localAppData = envFolder("LOCALAPPDATA");
    env.appData = envFolder("APPDATA");
    env.programData = envFolder("ProgramData");
    return env;
}

/// The browsers of --browser ("chrome, firefox"), all of them without it; nullopt + error for an
/// unknown name.
std::optional<QList<Browser>> chosenBrowsers(Call &call)
{
    if (!call.has(u"browser"_s) || call.str(u"browser"_s).trimmed().isEmpty() || call.str(u"browser"_s) == u"all")
        return allBrowsers();
    QList<Browser> out;
    for (const QString &raw : call.str(u"browser"_s).split(u',', Qt::SkipEmptyParts)) {
        const auto b = parseBrowser(raw.trimmed());
        if (!b) {
            QJsonArray known;
            for (const Browser k : allBrowsers())
                known.append(browserId(k));
            call.setError(ExitUsage, u"unknown_browser"_s, tr("Unknown browser: %1").arg(raw.trimmed()),
                          {{u"browsers"_s, known}});
            return std::nullopt;
        }
        if (!out.contains(*b))
            out.append(*b);
    }
    return out;
}

/// The program the browsers start: --program, the AppImage, a stable link on PATH to a program of
/// the Nix store (a store path is gone after an upgrade and a garbage collection), else this program.
QString hostProgram(Call &call)
{
    if (call.has(u"program"_s) && !call.str(u"program"_s).trimmed().isEmpty())
        return QDir::cleanPath(QFileInfo(call.str(u"program"_s).trimmed()).absoluteFilePath());
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty() && QFileInfo(appImage).isFile())
        return appImage;
    const QString self = QCoreApplication::applicationFilePath();
    if (self.startsWith(u"/nix/store/"_s)) {
        const QString name = QFileInfo(self).fileName();
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
    return self;
}

QStringList extraIds(Call &call)
{
    return call.str(u"extensionId"_s).split(u',', Qt::SkipEmptyParts);
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

bool isNixOs()
{
    return QFileInfo::exists(u"/etc/NIXOS"_s);
}

Scope scopeOf(Call &call)
{
    return call.flag(u"system"_s) ? Scope::System : Scope::User;
}

QJsonObject entryJson(const HostLocation &l)
{
    QJsonObject o{{u"browser"_s, browserId(l.browser)},
                  {u"name"_s, browserName(l.browser)},
                  {u"manifest"_s, QDir::toNativeSeparators(l.manifestPath)}};
    if (!l.registryKey.isEmpty())
        o.insert(u"registry"_s, l.registryKey);
    return o;
}

} // namespace

QList<Arg> clipInstallArgs(bool install)
{
    QList<Arg> args{
            option(u"browser"_s, ArgType::String,
                   QT_TRANSLATE_NOOP("brulion::cli",
                                     "Only these browsers: chrome, chromium, brave, edge, firefox (commas)")),
            flag(u"system"_s, QT_TRANSLATE_NOOP("brulion::cli",
                                                "For every user of the computer (system folders; administrator)")),
    };
    if (install) {
        args.append(option(u"program"_s, ArgType::String,
                           QT_TRANSLATE_NOOP("brulion::cli", "The program the browsers start (default: this one)")));
        args.append(option(u"extensionId"_s, ArgType::String,
                           QT_TRANSLATE_NOOP("brulion::cli",
                                             "More Chromium extension ids, separated by commas (a store's copy)")));
        args.append(flag(u"all"_s, QT_TRANSLATE_NOOP("brulion::cli",
                                                     "Also the browsers not found for this user")));
    }
    return args;
}

Result clipInstall(Call &call)
{
    const auto browsers = chosenBrowsers(call);
    if (!browsers)
        return call.failed();
    const Os os = currentOs();
    const Scope scope = scopeOf(call);
    if (scope == Scope::System && os == Os::Linux && isNixOs())
        return call.fail(ExitRefused, u"nixos_module"_s,
                         tr("On NixOS the system's browsers find the host through the module: programs.brulion "
                            "(docs/NIXOS.md); for yourself run clip install without --system"));
    const QString program = hostProgram(call);
    if (!QFileInfo(program).isExecutable())
        call.warn(u"program_missing"_s, tr("%1 is not a program that can be run").arg(program), {{u"program"_s, program}});
    if (program.startsWith(u"/nix/store/"_s))
        call.warn(u"store_path"_s,
                  tr("The manifests name a program in the Nix store, which an upgrade replaces: run clip install "
                     "again after one, or install Brulion with the NixOS module"));
    const Environment env = environment();
    const bool explicitBrowsers = call.has(u"browser"_s) && call.str(u"browser"_s) != u"all";
    QJsonArray installed;
    QJsonArray skipped;
    QJsonArray failed;
    for (const Browser b : *browsers) {
        const HostLocation l = hostLocation(b, os, scope, env);
        QJsonObject entry = entryJson(l);
        if (scope == Scope::User && !explicitBrowsers && !call.flag(u"all"_s) && !l.browserFolder.isEmpty()
            && !QFileInfo(l.browserFolder).isDir()) {
            entry.insert(u"reason"_s, u"not_found"_s);
            skipped.append(entry);
            continue;
        }
        const QJsonObject manifest = hostManifest(familyOf(b), QDir::toNativeSeparators(program), extraIds(call));
        entry.insert(u"program"_s, QDir::toNativeSeparators(program));
        if (call.dryRun()) {
            entry.insert(u"dryRun"_s, true);
            entry.insert(u"content"_s, manifest);
            installed.append(entry);
            continue;
        }
        QString error;
        if (!QDir().mkpath(QFileInfo(l.manifestPath).absolutePath())) {
            error = tr("Could not create the folder %1").arg(QFileInfo(l.manifestPath).absolutePath());
        } else {
            QSaveFile file(l.manifestPath);
            if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(manifest).toJson()) < 0 || !file.commit())
                error = file.errorString();
        }
        if (error.isEmpty() && !l.registryKey.isEmpty()) {
            // Windows: the key's default value names the manifest.
            QSettings registry(l.registryKey, QSettings::NativeFormat);
            registry.setValue(u"Default"_s, QDir::toNativeSeparators(l.manifestPath));
            registry.sync();
            if (registry.status() != QSettings::NoError)
                error = tr("Could not write the registry key %1").arg(l.registryKey);
        }
        if (!error.isEmpty()) {
            entry.insert(u"error"_s, error);
            failed.append(entry);
            continue;
        }
        installed.append(entry);
    }
    const QJsonObject out{{u"installed"_s, installed},
                          {u"skipped"_s, skipped},
                          {u"failed"_s, failed},
                          {u"program"_s, QDir::toNativeSeparators(program)},
                          {u"os"_s, osId(os)},
                          {u"scope"_s, scope == Scope::System ? u"system"_s : u"user"_s}};
    if (!failed.isEmpty() && installed.isEmpty())
        return call.fail(ExitUnavailable, u"install_failed"_s,
                         tr("No browser could be connected: %1").arg(failed.first().toObject().value(u"error"_s).toString()),
                         out);
    if (installed.isEmpty())
        call.warn(u"no_browser"_s, tr("No browser found for this user: name one with --browser, or use --all"));
    QString text;
    for (const QJsonValue &e : std::as_const(installed))
        text += e.toObject().value(u"name"_s).toString() + u": "_s + e.toObject().value(u"manifest"_s).toString() + u'\n';
    return call.ok(out, text);
}

Result clipUninstall(Call &call)
{
    const auto browsers = chosenBrowsers(call);
    if (!browsers)
        return call.failed();
    const Os os = currentOs();
    const Scope scope = scopeOf(call);
    const Environment env = environment();
    QJsonArray removed;
    QJsonArray absent;
    QJsonArray failed;
    for (const Browser b : *browsers) {
        const HostLocation l = hostLocation(b, os, scope, env);
        QJsonObject entry = entryJson(l);
        const bool ours = ourManifest(l.manifestPath).has_value();
        bool registryEntry = false;
        if (!l.registryKey.isEmpty()) {
            const QSettings registry(l.registryKey, QSettings::NativeFormat);
            registryEntry = registry.contains(u"Default"_s);
        }
        if (!ours && !registryEntry) {
            absent.append(entry);
            continue;
        }
        if (call.dryRun()) {
            entry.insert(u"dryRun"_s, true);
            removed.append(entry);
            continue;
        }
        // The same file serves every Chromium browser on Windows: removed with the last of them.
        bool fileStays = false;
        if (!l.registryKey.isEmpty()) {
            QSettings registry(l.registryKey, QSettings::NativeFormat);
            registry.remove(QString());
            registry.sync();
            for (const Browser other : allBrowsers()) {
                if (other == b || familyOf(other) != familyOf(b) || browsers->contains(other))
                    continue;
                const QSettings sibling(hostLocation(other, os, scope, env).registryKey, QSettings::NativeFormat);
                fileStays = fileStays || sibling.contains(u"Default"_s);
            }
        }
        if (ours && !fileStays && !QFile::remove(l.manifestPath)) {
            entry.insert(u"error"_s, tr("Could not remove %1").arg(QDir::toNativeSeparators(l.manifestPath)));
            failed.append(entry);
            continue;
        }
        removed.append(entry);
    }
    const QJsonObject out{{u"removed"_s, removed}, {u"absent"_s, absent}, {u"failed"_s, failed}};
    if (!failed.isEmpty() && removed.isEmpty())
        return call.fail(ExitUnavailable, u"uninstall_failed"_s,
                         failed.first().toObject().value(u"error"_s).toString(), out);
    return call.ok(out);
}

Result clipStatus(Call &call)
{
    const auto browsers = chosenBrowsers(call);
    if (!browsers)
        return call.failed();
    const Os os = currentOs();
    const Scope scope = scopeOf(call);
    const Environment env = environment();
    const QString self = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    QJsonArray list;
    QString text;
    for (const Browser b : *browsers) {
        const HostLocation l = hostLocation(b, os, scope, env);
        QJsonObject entry = entryJson(l);
        entry.insert(u"browserFound"_s, l.browserFolder.isEmpty() ? QJsonValue() : QFileInfo(l.browserFolder).isDir());
        const auto manifest = ourManifest(l.manifestPath);
        entry.insert(u"installed"_s, manifest.has_value());
        QString state = tr("not connected");
        if (manifest) {
            const QString program = manifest->value(u"path"_s).toString();
            const QFileInfo info(program);
            entry.insert(u"program"_s, program);
            entry.insert(u"programExists"_s, info.isExecutable());
            entry.insert(u"thisProgram"_s, info.canonicalFilePath() == self);
            const QJsonArray allowed = familyOf(b) == Family::Firefox ? manifest->value(u"allowed_extensions"_s).toArray()
                                                                      : manifest->value(u"allowed_origins"_s).toArray();
            const QString ours = familyOf(b) == Family::Firefox ? firefoxExtensionId()
                                                                : chromeOrigin(chromeExtensionId());
            entry.insert(u"extensionAllowed"_s, allowed.contains(ours));
            state = info.isExecutable() ? tr("connected: %1").arg(program) : tr("broken: %1 is missing").arg(program);
        }
        list.append(entry);
        text += browserName(b) + u": "_s + state + u'\n';
    }
    return call.ok(QJsonObject{{u"browsers"_s, list},
                               {u"host"_s, hostName()},
                               {u"chromeExtension"_s, chromeExtensionId()},
                               {u"firefoxExtension"_s, firefoxExtensionId()}},
                   text);
}

Result clipManifest(Call &call)
{
    const auto browser = parseBrowser(call.str(u"browser"_s));
    if (!browser)
        return call.fail(ExitUsage, u"unknown_browser"_s, tr("Unknown browser: %1").arg(call.str(u"browser"_s)));
    const QJsonObject manifest =
            hostManifest(familyOf(*browser), QDir::toNativeSeparators(hostProgram(call)), extraIds(call));
    Result r = call.ok(manifest);
    r.text = QString::fromUtf8(QJsonDocument(manifest).toJson());
    r.raw = true;
    return r;
}

} // namespace brulion::cli
