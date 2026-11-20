#include "clipper/nativehost.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QSysInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::clipper {

namespace {

/// The Windows registry path of a browser's hosts (under HKCU / HKLM).
QString registryPath(Browser browser)
{
    switch (browser) {
    case Browser::Chrome: return u"Software\\Google\\Chrome\\NativeMessagingHosts"_s;
    case Browser::Chromium: return u"Software\\Chromium\\NativeMessagingHosts"_s;
    case Browser::Brave: return u"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts"_s;
    case Browser::Edge: return u"Software\\Microsoft\\Edge\\NativeMessagingHosts"_s;
    case Browser::Firefox: return u"Software\\Mozilla\\NativeMessagingHosts"_s;
    }
    return {};
}

QString joined(const QString &folder, const QString &name)
{
    return folder.endsWith(u'/') ? QString(folder + name) : QString(folder + u'/' + name);
}

} // namespace

QString hostName()
{
    return u"io.github.januszbit.brulion"_s;
}

QString chromeExtensionId()
{
    return u"calcbkckojhmdfecpcohaangneahmkgg"_s;
}

QString firefoxExtensionId()
{
    return u"brulion-clipper@januszbit.github.io"_s;
}

QString chromeOrigin(const QString &extensionId)
{
    return u"chrome-extension://"_s + extensionId + u'/';
}

QList<Browser> allBrowsers()
{
    return {Browser::Chrome, Browser::Chromium, Browser::Brave, Browser::Edge, Browser::Firefox};
}

QString browserId(Browser browser)
{
    switch (browser) {
    case Browser::Chrome: return u"chrome"_s;
    case Browser::Chromium: return u"chromium"_s;
    case Browser::Brave: return u"brave"_s;
    case Browser::Edge: return u"edge"_s;
    case Browser::Firefox: return u"firefox"_s;
    }
    return {};
}

QString browserName(Browser browser)
{
    switch (browser) {
    case Browser::Chrome: return u"Google Chrome"_s;
    case Browser::Chromium: return u"Chromium"_s;
    case Browser::Brave: return u"Brave"_s;
    case Browser::Edge: return u"Microsoft Edge"_s;
    case Browser::Firefox: return u"Firefox"_s;
    }
    return {};
}

std::optional<Browser> parseBrowser(QStringView id)
{
    for (const Browser b : allBrowsers()) {
        if (id.compare(browserId(b), Qt::CaseInsensitive) == 0)
            return b;
    }
    if (id.compare(u"google-chrome", Qt::CaseInsensitive) == 0)
        return Browser::Chrome;
    if (id.compare(u"msedge", Qt::CaseInsensitive) == 0 || id.compare(u"microsoft-edge", Qt::CaseInsensitive) == 0)
        return Browser::Edge;
    return std::nullopt;
}

Family familyOf(Browser browser)
{
    return browser == Browser::Firefox ? Family::Firefox : Family::Chromium;
}

Os currentOs()
{
    const QString kernel = QSysInfo::kernelType();
    if (kernel == u"winnt")
        return Os::Windows;
    if (kernel == u"darwin")
        return Os::MacOs;
    return Os::Linux;
}

QString osId(Os os)
{
    switch (os) {
    case Os::Linux: return u"linux"_s;
    case Os::MacOs: return u"macos"_s;
    case Os::Windows: return u"windows"_s;
    }
    return {};
}

QJsonObject hostManifest(Family family, const QString &program, const QStringList &extraChromeIds)
{
    QJsonObject o{{u"name"_s, hostName()},
                  {u"description"_s, u"Brulion Web Clipper: saves web pages into a Brulion notebook"_s},
                  {u"path"_s, program},
                  {u"type"_s, u"stdio"_s}};
    if (family == Family::Firefox) {
        o.insert(u"allowed_extensions"_s, QJsonArray{firefoxExtensionId()});
        return o;
    }
    QJsonArray origins{chromeOrigin(chromeExtensionId())};
    for (const QString &raw : extraChromeIds) {
        const QString id = raw.trimmed().toLower();
        // A Chromium extension id: 32 letters a–p.
        const bool valid = id.size() == 32 && std::ranges::all_of(id, [](QChar c) { return c >= u'a' && c <= u'p'; });
        if (valid && !origins.contains(chromeOrigin(id)))
            origins.append(chromeOrigin(id));
    }
    o.insert(u"allowed_origins"_s, origins);
    return o;
}

HostLocation hostLocation(Browser browser, Os os, Scope scope, const Environment &env)
{
    HostLocation l;
    l.browser = browser;
    const QString file = hostName() + u".json"_s;
    if (os == Os::Windows) {
        const QString root = scope == Scope::User ? env.localAppData : env.programData;
        const QString family = familyOf(browser) == Family::Firefox ? u"firefox"_s : u"chromium"_s;
        l.manifestPath = joined(root, u"Brulion/NativeMessagingHosts/"_s + hostName() + u'.' + family + u".json"_s);
        l.registryKey = (scope == Scope::User ? u"HKEY_CURRENT_USER\\"_s : u"HKEY_LOCAL_MACHINE\\"_s)
                        + registryPath(browser) + u'\\' + hostName();
        if (scope == Scope::User) {
            switch (browser) {
            case Browser::Chrome: l.browserFolder = joined(env.localAppData, u"Google/Chrome/User Data"_s); break;
            case Browser::Chromium: l.browserFolder = joined(env.localAppData, u"Chromium/User Data"_s); break;
            case Browser::Brave:
                l.browserFolder = joined(env.localAppData, u"BraveSoftware/Brave-Browser/User Data"_s);
                break;
            case Browser::Edge: l.browserFolder = joined(env.localAppData, u"Microsoft/Edge/User Data"_s); break;
            case Browser::Firefox: l.browserFolder = joined(env.appData, u"Mozilla/Firefox"_s); break;
            }
        }
        return l;
    }
    if (os == Os::MacOs) {
        const QString support = scope == Scope::User ? joined(env.home, u"Library/Application Support"_s)
                                                     : u"/Library/Application Support"_s;
        QString folder;
        switch (browser) {
        case Browser::Chrome:
            folder = scope == Scope::User ? joined(support, u"Google/Chrome"_s) : u"/Library/Google/Chrome"_s;
            break;
        case Browser::Chromium: folder = joined(support, u"Chromium"_s); break;
        case Browser::Brave: folder = joined(support, u"BraveSoftware/Brave-Browser"_s); break;
        case Browser::Edge:
            folder = scope == Scope::User ? joined(support, u"Microsoft Edge"_s) : u"/Library/Microsoft/Edge"_s;
            break;
        case Browser::Firefox: folder = joined(support, u"Mozilla"_s); break;
        }
        l.manifestPath = joined(folder, u"NativeMessagingHosts/"_s + file);
        if (scope == Scope::User)
            l.browserFolder = folder;
        return l;
    }
    // Linux (and the other Unix systems these browsers run on).
    if (scope == Scope::System) {
        switch (browser) {
        case Browser::Chrome: l.manifestPath = u"/etc/opt/chrome/native-messaging-hosts/"_s + file; break;
        case Browser::Chromium: l.manifestPath = u"/etc/chromium/native-messaging-hosts/"_s + file; break;
        case Browser::Brave: l.manifestPath = u"/etc/opt/brave/native-messaging-hosts/"_s + file; break;
        case Browser::Edge: l.manifestPath = u"/etc/opt/edge/native-messaging-hosts/"_s + file; break;
        case Browser::Firefox: l.manifestPath = u"/usr/lib/mozilla/native-messaging-hosts/"_s + file; break;
        }
        return l;
    }
    const QString config = env.xdgConfigHome.isEmpty() ? joined(env.home, u".config"_s) : env.xdgConfigHome;
    QString folder;
    switch (browser) {
    case Browser::Chrome: folder = joined(config, u"google-chrome"_s); break;
    case Browser::Chromium: folder = joined(config, u"chromium"_s); break;
    case Browser::Brave: folder = joined(config, u"BraveSoftware/Brave-Browser"_s); break;
    case Browser::Edge: folder = joined(config, u"microsoft-edge"_s); break;
    case Browser::Firefox:
        l.manifestPath = joined(env.home, u".mozilla/native-messaging-hosts/"_s + file);
        l.browserFolder = joined(env.home, u".mozilla"_s);
        return l;
    }
    l.manifestPath = joined(folder, u"NativeMessagingHosts/"_s + file);
    l.browserFolder = folder;
    return l;
}

bool isNativeMessagingLaunch(const QStringList &arguments)
{
    if (arguments.isEmpty())
        return false;
    if (arguments.first().startsWith(u"chrome-extension://"_s))
        return arguments.size() == 1 || (arguments.size() == 2 && arguments.at(1).startsWith(u"--parent-window="_s));
    // Firefox: <the manifest's path> <the extension's id>.
    if (arguments.size() != 2)
        return false;
    // A Windows path, also when it is read on another system.
    const QString name = QFileInfo(QString(arguments.first()).replace(u'\\', u'/')).fileName();
    return name.startsWith(hostName()) && name.endsWith(u".json"_s, Qt::CaseInsensitive)
           && arguments.at(1) == firefoxExtensionId();
}

} // namespace brulion::clipper
