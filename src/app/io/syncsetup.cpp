#include "syncsetup.h"

#include "workspaceintegration.h"

#include "sync/http.h"
#include "sync/syncthing.h"
#include "sync/url.h"
#include "sync/webdavclient.h"
#include "vault/vault.h"

#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include <algorithm>
#include <type_traits>

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

/// `member = value`; false when it already had that value.
template<typename T>
bool assign(T &member, const std::type_identity_t<T> &value)
{
    if (member == value)
        return false;
    member = value;
    return true;
}

/// The server address as a URL (https:// added when no scheme is given).
QUrl serverUrl(const QString &server)
{
    QString s = server.trimmed();
    if (!s.contains(u"://"_s))
        s.prepend(u"https://"_s);
    return QUrl(s, QUrl::StrictMode);
}

bool isLocalHost(const QString &host)
{
    if (host == u"localhost" || host.endsWith(u".local"_s) || host.endsWith(u".lan"_s) || host.endsWith(u".home"_s))
        return true;
    const QHostAddress address(host);
    if (address.isNull())
        return false;
    return address.isLoopback() || address.isPrivateUse() || address.isLinkLocal();
}

bool containsAny(const QString &text, std::initializer_list<QStringView> words)
{
    return std::ranges::any_of(words, [&text](QStringView w) { return text.contains(w, Qt::CaseInsensitive); });
}

/// Rules of a .stignore: lines that are neither comments nor blank, trimmed.
QStringList stignoreRules(const QString &text)
{
    QStringList out;
    for (const QStringView raw : text.tokenize(u'\n')) {
        const QStringView line = raw.trimmed();
        if (!line.isEmpty() && !line.startsWith(u"//") && !line.startsWith(u'#'))
            out.append(line.toString());
    }
    return out;
}

/// The rules of `recommended` that `existing` lacks, in their order.
QStringList missingRules(const QString &existing, const QString &recommended)
{
    const QStringList have = stignoreRules(existing);
    const QSet<QString> haveSet(have.cbegin(), have.cend());
    QStringList missing = stignoreRules(recommended);
    missing.removeIf([&haveSet](const QString &rule) { return haveSet.contains(rule); });
    return missing;
}

Vault *vaultOf(const shell::Workspace *workspace)
{
    return workspace ? workspace->vault() : nullptr;
}

/// The vault's .stignore (Syncthing reads it in the folder's root); "" without a vault.
QString stignoreFile(const Vault *vault)
{
    return vault ? vault->rootPath() + u"/.stignore"_s : QString();
}

} // namespace

// ---- Validation -----------------------------------------------------------------------------------

QString validateServerUrl(const QString &url)
{
    static const QRegularExpression space(u"\\s"_s);
    const QString s = url.trimmed();
    if (s.isEmpty())
        return NextcloudSetup::tr("Enter the address of your Nextcloud server.");
    if (s.contains(space))
        return NextcloudSetup::tr("The address cannot contain spaces.");
    const QUrl u = serverUrl(s);
    if (!u.isValid() || u.host().isEmpty())
        return NextcloudSetup::tr("This is not a server address. Enter it like https://cloud.example.com.");
    if (u.scheme() != u"https" && u.scheme() != u"http")
        return NextcloudSetup::tr("The address must start with https://.");
    return {};
}

bool isUnencryptedUrl(const QString &url)
{
    const QUrl u = serverUrl(url);
    return u.isValid() && u.scheme() == u"http" && !isLocalHost(u.host());
}

QString validateUserName(const QString &user)
{
    const QString s = user.trimmed();
    if (s.isEmpty())
        return NextcloudSetup::tr("Enter your Nextcloud user name.");
    if (s.contains(u'/') || s.contains(u'\\'))
        return NextcloudSetup::tr("A user name cannot contain slashes.");
    return {};
}

QString validateServerFolder(const QString &folder)
{
    static const QRegularExpression forbidden(u"[\\\\:*?\"<>|]"_s);
    const QString s = folder.trimmed();
    if (s.isEmpty() || s == u"/" || s == u".")
        return NextcloudSetup::tr("Enter a folder for the notes – an empty one would sync all your Nextcloud files.");
    for (const QStringView seg : s.tokenize(u'/', Qt::SkipEmptyParts)) {
        if (seg == u"..")
            return NextcloudSetup::tr("The folder cannot contain “..”.");
        if (seg.contains(forbidden))
            return NextcloudSetup::tr("The folder name cannot contain \\ : * ? \" < > |.");
    }
    return {};
}

QString nextcloudSecurityUrl(const QString &server)
{
    if (!validateServerUrl(server).isEmpty())
        return {};
    // nextcloudDavUrl() understands pasted DAV and web UI addresses; its prefix is the server.
    const QString dav = sync::nextcloudDavUrl(server, u"x"_s, QString());
    const qsizetype at = dav.indexOf(u"/remote.php/"_s);
    if (at < 0)
        return {};
    return dav.left(at) + u"/index.php/settings/user/security"_s;
}

QVariantMap describeConnectionError(const sync::WebDavError &error, bool browser, const QString &server)
{
    using sync::WebDavErrorCode;
    const QString host = serverUrl(server).host();
    QString code = sync::errorCodeName(error.code);
    QString title;
    QString message;
    QString hint;
    switch (error.code) {
    case WebDavErrorCode::Auth:
        code = u"auth"_s;
        title = NextcloudSetup::tr("Wrong user name or password");
        message = NextcloudSetup::tr("Nextcloud did not accept the user name or the password.");
        hint = NextcloudSetup::tr("Use an app password, not your account password: in Nextcloud open Settings → Security, enter a "
                  "name such as “Brulion – phone” under “Devices & sessions” and click “Create new app password”. "
                  "Copy the password shown and paste it here.");
        break;
    case WebDavErrorCode::Forbidden:
        title = NextcloudSetup::tr("Access denied");
        message = NextcloudSetup::tr("The server refused access to the folder (HTTP 403).");
        hint = NextcloudSetup::tr("After many failed attempts Nextcloud blocks logins for a while. Check that the folder is yours or "
                  "shared with you with permission to edit.");
        break;
    case WebDavErrorCode::NotFound:
        title = NextcloudSetup::tr("Folder not found");
        message = NextcloudSetup::tr("The server has no such folder.");
        break;
    case WebDavErrorCode::Timeout:
        title = NextcloudSetup::tr("No answer");
        message = NextcloudSetup::tr("The server did not answer in time.");
        hint = NextcloudSetup::tr("Try again in a moment. A slow mobile connection or an overloaded server can cause this.");
        break;
    case WebDavErrorCode::Parse:
        code = u"not-webdav"_s;
        title = NextcloudSetup::tr("This is not a Nextcloud address");
        message = NextcloudSetup::tr("The server answered, but not like Nextcloud's file access (WebDAV).");
        hint = NextcloudSetup::tr("Enter the address you open Nextcloud with in a web browser, for example "
                  "https://cloud.example.com or https://example.com/nextcloud.");
        break;
    case WebDavErrorCode::InsufficientStorage:
        title = NextcloudSetup::tr("No space on the server");
        message = NextcloudSetup::tr("Your Nextcloud storage is full.");
        hint = NextcloudSetup::tr("Free some space or ask the server administrator for a larger quota.");
        break;
    case WebDavErrorCode::Server:
        title = NextcloudSetup::tr("Server error");
        message = NextcloudSetup::tr("The server reported an error (HTTP %1).").arg(error.status);
        hint = NextcloudSetup::tr("This is a problem on the server side. Try again later.");
        break;
    case WebDavErrorCode::Network: {
        const QString &detail = error.message;
        if (browser) {
            code = u"cors"_s;
            title = NextcloudSetup::tr("The browser blocked the connection");
            message = NextcloudSetup::tr("A web page may only talk to a Nextcloud server that allows it (CORS), and Nextcloud does "
                         "not allow it by default. The server may also be unreachable.");
            hint = NextcloudSetup::tr("Ways that work: use the Brulion app for Linux or Android (no such limit there); host Brulion's "
                      "web version on the same server as Nextcloud (on NixOS the module services.brulion-web with its "
                      "Nextcloud proxy); or add CORS headers for this site on the server. See docs/WEB.md.");
        } else if (containsAny(detail, { u"ssl", u"tls", u"certificate", u"certyfikat", u"handshake" })) {
            code = u"tls"_s;
            title = NextcloudSetup::tr("Secure connection failed");
            message = NextcloudSetup::tr("The server's certificate is not trusted: it may have expired, be self-signed or be "
                         "issued for another name than “%1”.")
                              .arg(host);
            hint = NextcloudSetup::tr("Use the exact address the certificate is for, renew the certificate (for example with Let's "
                      "Encrypt), or install your own certificate authority on this device. Brulion never sends your "
                      "password to an untrusted server.");
        } else if ((detail.contains(u"host"_s, Qt::CaseInsensitive) && detail.contains(u"not found"_s, Qt::CaseInsensitive))
                   || containsAny(detail, { u"not known", u"unknown host", u"no address", u"name resolution",
                                            u"nie znaleziono" })) {
            code = u"dns"_s;
            title = NextcloudSetup::tr("Server not found");
            message = NextcloudSetup::tr("No server answers to the name “%1”.").arg(host);
            hint = NextcloudSetup::tr("Check the address for typos and that this device is online.");
        } else if (containsAny(detail, { u"refused", u"odmowa" })) {
            code = u"refused"_s;
            title = NextcloudSetup::tr("The server does not answer");
            message = NextcloudSetup::tr("“%1” refused the connection.").arg(host);
            hint = NextcloudSetup::tr("Is the server running, and is the port in the address right?");
        } else {
            title = NextcloudSetup::tr("No connection");
            message = NextcloudSetup::tr("Cannot reach the server.");
            hint = NextcloudSetup::tr("Check the address and the internet connection of this device.");
        }
        break;
    }
    case WebDavErrorCode::Cancelled:
        title = NextcloudSetup::tr("Cancelled");
        message = NextcloudSetup::tr("The test was cancelled.");
        break;
    default:
        title = NextcloudSetup::tr("Unexpected answer");
        message = error.status > 0 ? NextcloudSetup::tr("The server answered with HTTP %1.").arg(error.status)
                                   : NextcloudSetup::tr("The connection test failed.");
        break;
    }
    return {
        { u"ok"_s, false },        { u"code"_s, code },       { u"title"_s, title },
        { u"message"_s, message }, { u"hint"_s, hint },       { u"details"_s, error.message },
        { u"canCreateFolder"_s, false },
    };
}

// ---- .stignore ------------------------------------------------------------------------------------

QString stignoreState(const QString &existing, bool exists, const QString &recommended)
{
    if (!exists)
        return u"missing"_s;
    return missingRules(existing, recommended).isEmpty() ? u"current"_s : u"outdated"_s;
}

QString mergeStignore(const QString &existing, const QString &recommended)
{
    if (existing.trimmed().isEmpty())
        return recommended;
    const QStringList missing = missingRules(existing, recommended);
    if (missing.isEmpty())
        return existing;
    const QString eol = existing.contains(u"\r\n"_s) ? u"\r\n"_s : u"\n"_s;
    QString out = existing;
    while (out.endsWith(u'\n') || out.endsWith(u'\r') || out.endsWith(u' '))
        out.chop(1);
    out += eol + eol + u"// Brulion — dopisane zalecane reguły (patrz Synchronizacja → Syncthing)."_s + eol;
    out += missing.join(eol) + eol;
    return out;
}

// ---- NextcloudSetup -------------------------------------------------------------------------------

NextcloudSetup::NextcloudSetup(QObject *parent)
    : QObject(parent)
{
#ifdef Q_OS_WASM
    m_browser = true;
#endif
}

NextcloudSetup::~NextcloudSetup() = default;

void NextcloudSetup::setSettings(shell::AppSettings *settings)
{
    if (m_settings == settings)
        return;
    if (m_settings)
        disconnect(m_settings, nullptr, this, nullptr);
    m_settings = settings;
    if (m_settings) {
        connect(m_settings, &shell::AppSettings::nextcloudChanged, this, &NextcloudSetup::settingsChanged);
        connect(m_settings, &shell::AppSettings::nextcloudPasswordChanged, this, &NextcloudSetup::settingsChanged);
        connect(m_settings, &shell::AppSettings::nextcloudChanged, this, &NextcloudSetup::draftChanged);
        connect(m_settings, &shell::AppSettings::nextcloudPasswordChanged, this, &NextcloudSetup::draftChanged);
    }
    reload();
    Q_EMIT settingsChanged();
}

void NextcloudSetup::reload()
{
    if (m_settings) {
        m_enabled = m_settings->nextcloudEnabled();
        m_url = m_settings->nextcloudUrl();
        m_user = m_settings->nextcloudUser();
        // The notebook's folder; for one without, a folder no other notebook here has.
        m_folder = m_settings->proposedNextcloudFolder();
        m_interval = m_settings->syncIntervalMinutes();
        m_deviceName = m_settings->value(u"sync/deviceName"_s, QString()).toString();
    }
    m_password.clear();
    m_showErrors = false;
    Q_EMIT draftChanged();
    clearTestResult();
}

void NextcloudSetup::setEnabled(bool on)
{
    if (assign(m_enabled, on))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setUrl(const QString &url)
{
    if (assign(m_url, url))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setUser(const QString &user)
{
    if (assign(m_user, user))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setPassword(const QString &password)
{
    if (assign(m_password, password))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setFolder(const QString &folder)
{
    if (assign(m_folder, folder))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setInterval(int minutes)
{
    if (assign(m_interval, std::clamp(minutes, 0, 24 * 60)))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setDeviceName(const QString &name)
{
    if (assign(m_deviceName, name))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setShowErrors(bool on)
{
    if (assign(m_showErrors, on))
        Q_EMIT draftChanged();
}

void NextcloudSetup::setBrowser(bool on)
{
    if (assign(m_browser, on))
        Q_EMIT settingsChanged();
}

bool NextcloudSetup::hasStoredPassword() const
{
    return m_settings && m_settings->hasNextcloudPassword();
}

bool NextcloudSetup::storedPasswordApplies() const
{
    return hasStoredPassword() && sameAccount();
}

bool NextcloudSetup::configured() const
{
    return m_settings && m_settings->nextcloudEnabled() && !m_settings->nextcloudUrl().isEmpty()
        && !m_settings->nextcloudUser().isEmpty() && m_settings->hasNextcloudPassword();
}

bool NextcloudSetup::dirty() const
{
    return m_settings
           && (m_enabled != m_settings->nextcloudEnabled() || m_url.trimmed() != m_settings->nextcloudUrl()
               || m_user.trimmed() != m_settings->nextcloudUser() || !m_password.isEmpty()
               || m_folder.trimmed() != m_settings->proposedNextcloudFolder()
               || m_interval != m_settings->syncIntervalMinutes()
               || m_deviceName.trimmed() != m_settings->value(u"sync/deviceName"_s, QString()).toString());
}

QString NextcloudSetup::urlError() const
{
    return m_showErrors ? validateServerUrl(m_url) : QString();
}

QString NextcloudSetup::urlWarning() const
{
    return isUnencryptedUrl(m_url)
                   ? tr("This address is not encrypted: the password would travel openly. Use https:// unless the "
                        "server is in your home network.")
                   : QString();
}

QString NextcloudSetup::userError() const
{
    return m_showErrors ? validateUserName(m_user) : QString();
}

QString NextcloudSetup::passwordError() const
{
    if (!m_showErrors || !m_password.isEmpty() || storedPasswordApplies())
        return {};
    if (hasStoredPassword())
        return tr("The server or user has changed: enter the app password of this account.");
    return tr("Enter an app password (Nextcloud: Settings → Security).");
}

QString NextcloudSetup::folderError() const
{
    const QString invalid = validateServerFolder(m_folder);
    if (!invalid.isEmpty())
        return m_showErrors ? invalid : QString();
    return folderConflict();
}

bool NextcloudSetup::sameAccount() const
{
    return m_settings
           && shell::sameNextcloudAccount(m_url, m_user, m_settings->nextcloudUrl(), m_settings->nextcloudUser());
}

QString NextcloudSetup::folderConflict() const
{
    // Shown at once, not only after a save attempt: the user should not find out from mixed notes.
    // Another account switches the other notebooks off when saved (save()).
    if (!m_enabled || !sameAccount())
        return {};
    const QString other = m_settings->nextcloudFolderUser(m_folder);
    if (other.isEmpty())
        return {};
    return tr("The notebook “%1” already syncs with this folder. Choose another folder, or turn sync off in that "
              "notebook first.")
            .arg(QFileInfo(other).fileName());
}

bool NextcloudSetup::valid() const
{
    return validateServerUrl(m_url).isEmpty() && validateUserName(m_user).isEmpty()
           && validateServerFolder(m_folder).isEmpty() && (!m_password.isEmpty() || storedPasswordApplies())
           && folderConflict().isEmpty();
}

QString NextcloudSetup::davUrl() const
{
    if (!validateServerUrl(m_url).isEmpty() || !validateUserName(m_user).isEmpty())
        return {};
    return sync::nextcloudDavUrl(m_url.trimmed(), m_user.trimmed(), m_folder.trimmed());
}

QString NextcloudSetup::securityUrl() const
{
    return nextcloudSecurityUrl(m_url);
}

QVariantList NextcloudSetup::intervalOptions() const
{
    QVariantList out;
    for (const int minutes : { 5, 15, 30, 60, 240 }) {
        const QString text = minutes < 60 ? tr("Every %n minute(s)", "", minutes) : tr("Every %n hour(s)", "", minutes / 60);
        out.append(QVariantMap{ { u"value"_s, minutes }, { u"text"_s, text } });
    }
    out.append(QVariantMap{ { u"value"_s, 0 }, { u"text"_s, tr("Only after changes and once a day") } });
    return out;
}

QString NextcloudSetup::intervalText() const
{
    const QVariantList options = intervalOptions();
    for (const QVariant &option : options) {
        if (const QVariantMap map = option.toMap(); map.value(u"value"_s).toInt() == m_interval)
            return map.value(u"text"_s).toString();
    }
    return tr("Every %n minute(s)", "", m_interval);
}

bool NextcloudSetup::save()
{
    if (!m_settings)
        return false;
    setShowErrors(true);
    // Switching off never needs a complete form.
    if (m_enabled && !valid())
        return false;
    // The other notebooks synced with the saved account: never with another one by themselves.
    const bool otherAccount = !sameAccount() && !(m_url.trimmed().isEmpty() && m_settings->nextcloudUrl().isEmpty());
    if (!m_password.isEmpty() && !m_settings->setNextcloudPassword(m_password))
        return false;
    // Another server or user without a new password (only possible while switched off): the
    // stored one belonged to the old account and must not reach the new one later.
    if (m_password.isEmpty() && hasStoredPassword() && !storedPasswordApplies()
        && !m_settings->setNextcloudPassword(QString()))
        return false;
    m_password.clear();
    if (otherAccount)
        m_settings->disableNextcloudEverywhere(true);
    m_settings->setNextcloudUrl(m_url.trimmed());
    m_settings->setNextcloudUser(m_user.trimmed());
    m_settings->setNextcloudFolder(m_folder.trimmed());
    m_settings->setSyncIntervalMinutes(m_interval);
    m_settings->setValue(u"sync/deviceName"_s, m_deviceName.trimmed());
    m_settings->setNextcloudEnabled(m_enabled);
    m_showErrors = false;
    Q_EMIT draftChanged();
    Q_EMIT saved();
    return true;
}

void NextcloudSetup::disconnectAccount()
{
    if (!m_settings)
        return;
    // The password is the account's, shared by the notebooks: every one of them stops (they
    // would start again with whatever account is signed in next).
    m_settings->disableNextcloudEverywhere();
    m_settings->setNextcloudPassword(QString());
    reload();
}

sync::HttpTransport *NextcloudSetup::transport()
{
    if (m_transport)
        return m_transport;
    if (!m_ownTransport)
        m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
    return m_ownTransport.get();
}

QString NextcloudSetup::effectivePassword() const
{
    if (!m_password.isEmpty())
        return m_password;
    // The stored password only for the server and user it was saved for.
    return storedPasswordApplies() ? m_settings->nextcloudPassword() : QString();
}

void NextcloudSetup::clearTestResult()
{
    ++m_testGeneration;
    setTestResult({});
}

void NextcloudSetup::setTestResult(const QVariantMap &result)
{
    if (assign(m_testResult, result))
        Q_EMIT testResultChanged();
}

void NextcloudSetup::finishTest(int generation, const QVariantMap &result)
{
    --m_testing;
    Q_EMIT testingChanged();
    if (generation == m_testGeneration)
        setTestResult(result);
}

QString NextcloudSetup::formProblem(const QString &password) const
{
    for (const QString &problem :
         {validateServerUrl(m_url), validateUserName(m_user), validateServerFolder(m_folder)}) {
        if (!problem.isEmpty())
            return problem;
    }
    return password.isEmpty() ? tr("Enter an app password (Nextcloud: Settings → Security).") : QString();
}

void NextcloudSetup::testConnection()
{
    setShowErrors(true);
    const QString password = effectivePassword();
    const QString problem = formProblem(password);
    const int generation = ++m_testGeneration;
    if (!problem.isEmpty()) {
        setTestResult({{u"ok"_s, false},
                       {u"code"_s, u"invalid"_s},
                       {u"title"_s, tr("Check the form")},
                       {u"message"_s, problem}});
        return;
    }
    setTestResult({});
    const QString server = m_url.trimmed();
    const QString user = m_user.trimmed();
    const QString folder = m_folder.trimmed();
    const bool browser = m_browser;
    ++m_testing;
    Q_EMIT testingChanged();
    auto *client = new sync::WebDavClient(sync::nextcloudDavUrl(server, user, folder), user, password, transport(), this);
    client->test([this, client, generation, server, user, folder, password, browser](const sync::WebDavTestResult &r) {
        client->deleteLater();
        if (r.ok) {
            finishTest(generation, {{u"ok"_s, true},
                                    {u"code"_s, u"ok"_s},
                                    {u"title"_s, tr("Connected")},
                                    {u"message"_s, tr("The folder “%1” is ready for syncing.").arg(folder)}});
            return;
        }
        // (A test left behind by an edit or a newer test ends here: finishTest() drops its result.)
        if (!r.error.is(sync::WebDavErrorCode::NotFound) || generation != m_testGeneration) {
            QVariantMap m = describeConnectionError(r.error, browser, server);
            if (m.value(u"code"_s).toString() == u"auth")
                m.insert(u"securityUrl"_s, nextcloudSecurityUrl(server));
            finishTest(generation, m);
            return;
        }
        // 404: is it the folder or the whole account? Ask for the user's root folder.
        auto *root = new sync::WebDavClient(sync::nextcloudDavUrl(server, user, QString()), user, password,
                                            transport(), this);
        root->test([this, root, generation, folder, user, server, browser](const sync::WebDavTestResult &rr) {
            root->deleteLater();
            if (rr.ok) {
                finishTest(generation,
                           {{u"ok"_s, false},
                            {u"code"_s, u"no-folder"_s},
                            {u"title"_s, tr("The folder does not exist yet")},
                            {u"message"_s,
                             tr("Your account works, but there is no folder “%1” on the server.").arg(folder)},
                            {u"hint"_s, tr("Brulion can create it now. The first sync would create it as well.")},
                            {u"canCreateFolder"_s, true}});
            } else if (rr.error.is(sync::WebDavErrorCode::NotFound)) {
                finishTest(generation,
                           {{u"ok"_s, false},
                            {u"code"_s, u"no-user"_s},
                            {u"title"_s, tr("Account not found")},
                            {u"message"_s, tr("The server has no files for the user “%1”.").arg(user)},
                            {u"hint"_s, tr("Use the user name shown in Nextcloud under Settings → Personal info – "
                                           "it can differ from your e-mail address.")}});
            } else {
                finishTest(generation, describeConnectionError(rr.error, browser, server));
            }
        });
    });
}

void NextcloudSetup::createFolder()
{
    if (testing())
        return;
    const QString password = effectivePassword();
    if (!formProblem(password).isEmpty())
        return;
    const QString server = m_url.trimmed();
    const QString user = m_user.trimmed();
    const QString folder = m_folder.trimmed();
    const int generation = ++m_testGeneration;
    const bool browser = m_browser;
    ++m_testing;
    Q_EMIT testingChanged();
    auto *client = new sync::WebDavClient(sync::nextcloudDavUrl(server, user, folder), user, password, transport(), this);
    client->ensureBase([this, client, generation, folder, server, browser](const sync::WebDavError &error) {
        client->deleteLater();
        if (error.isError()) {
            finishTest(generation, describeConnectionError(error, browser, server));
            return;
        }
        finishTest(generation,
                   {{u"ok"_s, true},
                    {u"code"_s, u"created"_s},
                    {u"title"_s, tr("Folder created")},
                    {u"message"_s, tr("“%1” was created on the server and is ready for syncing.").arg(folder)}});
    });
}

// ---- SyncthingSetup -------------------------------------------------------------------------------

SyncthingSetup::SyncthingSetup(QObject *parent) : QObject(parent) { }

void SyncthingSetup::setSettings(shell::AppSettings *settings)
{
    if (m_settings == settings)
        return;
    m_settings = settings;
    reload();
    Q_EMIT settingsChanged();
}

void SyncthingSetup::setWorkspace(shell::Workspace *workspace)
{
    if (m_workspace == workspace)
        return;
    if (m_workspace)
        disconnect(m_workspace, nullptr, this, nullptr);
    m_workspace = workspace;
    if (m_workspace)
        connect(m_workspace, &shell::Workspace::openChanged, this, &SyncthingSetup::refreshStignore);
    refreshStignore();
    Q_EMIT workspaceChanged();
}

void SyncthingSetup::reload()
{
    if (m_settings) {
        m_apiKey = shell::syncthingApiKey(m_settings); // owner-only file, not QSettings
        m_url = m_settings->value(u"sync/syncthingUrl"_s, QString()).toString();
    }
    Q_EMIT draftChanged();
}

void SyncthingSetup::setApiKey(const QString &key)
{
    if (assign(m_apiKey, key))
        Q_EMIT draftChanged();
}

void SyncthingSetup::setUrl(const QString &url)
{
    if (assign(m_url, url))
        Q_EMIT draftChanged();
}

QString SyncthingSetup::urlError() const
{
    const QString s = m_url.trimmed();
    if (s.isEmpty())
        return {};
    const QUrl u(s, QUrl::StrictMode);
    if (!u.isValid() || u.host().isEmpty() || (u.scheme() != u"http" && u.scheme() != u"https"))
        return tr("Enter an address like http://127.0.0.1:8384.");
    return {};
}

bool SyncthingSetup::dirty() const
{
    if (!m_settings)
        return false;
    return m_apiKey.trimmed() != shell::syncthingApiKey(m_settings)
        || m_url.trimmed() != m_settings->value(u"sync/syncthingUrl"_s, QString()).toString();
}

QString SyncthingSetup::defaultUrl() const
{
    return sync::syncthingDefaultUrl();
}

bool SyncthingSetup::save()
{
    if (!m_settings || !urlError().isEmpty())
        return false;
    if (!shell::setSyncthingApiKey(m_settings, m_apiKey)) {
        Q_EMIT errorOccurred(tr("The API key could not be saved."));
        return false;
    }
    m_settings->setValue(u"sync/syncthingUrl"_s, m_url.trimmed());
    reload();
    return true;
}

QString SyncthingSetup::stignoreTemplate() const
{
    return sync::stignoreTemplate();
}

QString SyncthingSetup::stignorePath() const
{
    return QDir::toNativeSeparators(stignoreFile(vaultOf(m_workspace)));
}

void SyncthingSetup::refreshStignore()
{
    QString state = u"unavailable"_s;
    if (Vault *vault = vaultOf(m_workspace)) {
        const bool exists = QFileInfo::exists(stignoreFile(vault));
        const QString text = exists ? vault->readText(u".stignore"_s) : QString();
        state = ioviews::stignoreState(text, exists, sync::stignoreTemplate());
    }
    if (assign(m_stignoreState, state))
        Q_EMIT stignoreChanged();
}

bool SyncthingSetup::writeStignore()
{
    Vault *vault = vaultOf(m_workspace);
    if (!vault)
        return false;
    bool ok = true;
    const QString existing = QFileInfo::exists(stignoreFile(vault)) ? vault->readText(u".stignore"_s, &ok) : QString();
    if (!ok) {
        Q_EMIT errorOccurred(tr("The existing .stignore could not be read: %1").arg(vault->errorString()));
        return false;
    }
    const QString merged = mergeStignore(existing, sync::stignoreTemplate());
    if (merged != existing && !vault->writeText(u".stignore"_s, merged, ChangeSource::User)) {
        Q_EMIT errorOccurred(tr(".stignore could not be saved: %1").arg(vault->errorString()));
        return false;
    }
    refreshStignore();
    return true;
}

} // namespace brulion::ioviews
