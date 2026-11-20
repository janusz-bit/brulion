// The Web Clipper's host manifests (docs/CLIPPER.md "Connecting the browser"): `clip install` writes
// them where Chrome, Chromium, Brave, Edge and Firefox look (the user's folders or, with --system, the
// system's; on Windows a file named by a registry key), `clip uninstall` removes them (only Brulion's
// own), `clip status` tells what is there and which program it starts, `clip manifest` prints one
// for packages (the Nix package installs Firefox's and Chromium's: docs/NIXOS.md).
#include "clip_p.h"
#include "commands_p.h"

#include "clipper/hostinstall.h"
#include "clipper/nativehost.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

using namespace Qt::StringLiterals;

namespace brulion::cli {

using namespace brulion::clipper;

namespace {

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

/// The program the browsers start: --program, else defaultHostProgram() of this one.
QString hostProgram(Call &call)
{
    if (call.has(u"program"_s) && !call.str(u"program"_s).trimmed().isEmpty())
        return QDir::cleanPath(QFileInfo(call.str(u"program"_s).trimmed()).absoluteFilePath());
    return defaultHostProgram(QCoreApplication::applicationFilePath());
}

HostSetup setupOf(Call &call)
{
    HostSetup setup;
    setup.os = currentOs();
    setup.scope = call.flag(u"system"_s) ? Scope::System : Scope::User;
    setup.env = currentEnvironment();
    setup.program = hostProgram(call);
    setup.extraChromeIds = call.str(u"extensionId"_s).split(u',', Qt::SkipEmptyParts);
    setup.dryRun = call.dryRun();
    return setup;
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
            flag(u"system"_s,
                 QT_TRANSLATE_NOOP("brulion::cli", "For every user of the computer (system folders; administrator)")),
    };
    if (install) {
        args.append(option(u"program"_s, ArgType::String,
                           QT_TRANSLATE_NOOP("brulion::cli", "The program the browsers start (default: this one)")));
        args.append(option(u"extensionId"_s, ArgType::String,
                           QT_TRANSLATE_NOOP("brulion::cli",
                                             "More Chromium extension ids, separated by commas (a store's copy)")));
        args.append(flag(u"all"_s, QT_TRANSLATE_NOOP("brulion::cli", "Also the browsers not found for this user")));
    }
    return args;
}

Result clipInstall(Call &call)
{
    const auto browsers = chosenBrowsers(call);
    if (!browsers)
        return call.failed();
    const HostSetup setup = setupOf(call);
    if (setup.scope == Scope::System && setup.os == Os::Linux && isNixOs())
        return call.fail(ExitRefused, u"nixos_module"_s,
                         tr("On NixOS the system's browsers find the host through the module: programs.brulion "
                            "(docs/NIXOS.md); for yourself run clip install without --system"));
    if (!QFileInfo(setup.program).isExecutable())
        call.warn(u"program_missing"_s, tr("%1 is not a program that can be run").arg(setup.program),
                  {{u"program"_s, setup.program}});
    if (setup.program.startsWith(u"/nix/store/"_s))
        call.warn(u"store_path"_s,
                  tr("The manifests name a program in the Nix store, which an upgrade replaces: run clip install "
                     "again after one, or install Brulion with the NixOS module"));
    const bool explicitBrowsers = call.has(u"browser"_s) && call.str(u"browser"_s) != u"all";
    QJsonArray installed;
    QJsonArray skipped;
    QJsonArray failed;
    for (const Browser b : *browsers) {
        const HostState before = hostState(b, setup.os, setup.scope, setup.env, setup.program);
        QJsonObject entry = entryJson(before.location);
        if (setup.scope == Scope::User && !explicitBrowsers && !call.flag(u"all"_s) && !before.browserFound) {
            entry.insert(u"reason"_s, u"not_found"_s);
            skipped.append(entry);
            continue;
        }
        entry.insert(u"program"_s, QDir::toNativeSeparators(setup.program));
        if (setup.dryRun) {
            entry.insert(u"dryRun"_s, true);
            entry.insert(u"content"_s,
                         hostManifest(familyOf(b), QDir::toNativeSeparators(setup.program), setup.extraChromeIds));
            installed.append(entry);
            continue;
        }
        const HostState after = installHost(b, setup);
        if (!after.error.isEmpty()) {
            entry.insert(u"error"_s, after.error);
            failed.append(entry);
            continue;
        }
        installed.append(entry);
    }
    const QJsonObject out{
            {u"installed"_s, installed}, {u"skipped"_s, skipped},
            {u"failed"_s, failed},       {u"program"_s, QDir::toNativeSeparators(setup.program)},
            {u"os"_s, osId(setup.os)},   {u"scope"_s, setup.scope == Scope::System ? u"system"_s : u"user"_s}};
    if (!failed.isEmpty() && installed.isEmpty())
        return call.fail(
                ExitUnavailable, u"install_failed"_s,
                tr("No browser could be connected: %1").arg(failed.first().toObject().value(u"error"_s).toString()),
                out);
    if (installed.isEmpty())
        call.warn(u"no_browser"_s, tr("No browser found for this user: name one with --browser, or use --all"));
    QString text;
    for (const QJsonValue &e : std::as_const(installed))
        text += e.toObject().value(u"name"_s).toString() + u": "_s + e.toObject().value(u"manifest"_s).toString()
                + u'\n';
    return call.ok(out, text);
}

Result clipUninstall(Call &call)
{
    const auto browsers = chosenBrowsers(call);
    if (!browsers)
        return call.failed();
    const HostSetup setup = setupOf(call);
    // The Chromium browsers not being removed keep the file they share on Windows.
    QList<Browser> keep;
    for (const Browser b : allBrowsers()) {
        if (!browsers->contains(b))
            keep.append(b);
    }
    QJsonArray removed;
    QJsonArray absent;
    QJsonArray failed;
    for (const Browser b : *browsers) {
        const HostState before = hostState(b, setup.os, setup.scope, setup.env, setup.program);
        QJsonObject entry = entryJson(before.location);
        if (!before.installed && !QFileInfo::exists(before.location.manifestPath)) {
            absent.append(entry);
            continue;
        }
        const HostState after = uninstallHost(b, setup, keep);
        if (!after.error.isEmpty()) {
            entry.insert(u"error"_s, after.error);
            failed.append(entry);
            continue;
        }
        if (setup.dryRun)
            entry.insert(u"dryRun"_s, true);
        (after.installed && !setup.dryRun ? absent : removed).append(entry);
    }
    const QJsonObject out{{u"removed"_s, removed}, {u"absent"_s, absent}, {u"failed"_s, failed}};
    if (!failed.isEmpty() && removed.isEmpty())
        return call.fail(ExitUnavailable, u"uninstall_failed"_s, failed.first().toObject().value(u"error"_s).toString(),
                         out);
    return call.ok(out);
}

Result clipStatus(Call &call)
{
    const auto browsers = chosenBrowsers(call);
    if (!browsers)
        return call.failed();
    const HostSetup setup = setupOf(call);
    const QString self = QCoreApplication::applicationFilePath();
    QJsonArray list;
    QString text;
    for (const Browser b : *browsers) {
        const HostState s = hostState(b, setup.os, setup.scope, setup.env, self);
        QJsonObject entry = entryJson(s.location);
        entry.insert(u"browserFound"_s, s.browserFound);
        entry.insert(u"installed"_s, s.installed);
        QString state = tr("not connected");
        if (s.installed) {
            entry.insert(u"program"_s, s.program);
            entry.insert(u"programExists"_s, s.programExists);
            entry.insert(u"thisProgram"_s, s.thisProgram);
            entry.insert(u"extensionAllowed"_s, s.extensionAllowed);
            state = s.programExists ? tr("connected: %1").arg(s.program) : tr("broken: %1 is missing").arg(s.program);
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
    const QJsonObject manifest = hostManifest(familyOf(*browser), QDir::toNativeSeparators(hostProgram(call)),
                                              call.str(u"extensionId"_s).split(u',', Qt::SkipEmptyParts));
    Result r = call.ok(manifest);
    r.text = QString::fromUtf8(QJsonDocument(manifest).toJson());
    r.raw = true;
    return r;
}

} // namespace brulion::cli
