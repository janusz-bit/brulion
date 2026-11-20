// What a brulion:// address may do without asking (docs/UI.md "Automation", "Safety"). Any web
// page, any app and any program on the device can fire such an address — so:
//
//   * opening (a page, a day, a view, search, the capture dialog) is always allowed: it changes
//     nothing (a link never creates a page by opening it);
//   * writing (new, append, prepend, task, capture with text) and running an extension's command
//     need the notebook's automation key in the address (`key=…`, Settings → Automation: one
//     random key per notebook and device, never in the notebook's files, so it does not travel
//     with a sync) — or, without it, the user's "Write" in a dialog that shows what would be
//     written where (policy Ask, the default). Policy Key refuses writing without the key, policy
//     Off makes every address do nothing;
//   * a wrong key is refused whatever the address asks (the caller hears why);
//   * nothing ever overwrites or deletes: writes add a block or create a page whose name is free;
//     extension commands run only for extensions approved on this device.
//
// The x-callback addresses and Android's result broadcast are used only for a request with a
// valid key: otherwise a web page could learn from them which pages exist.
#pragma once

#include "automation/uri.h"

#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::automation {

/// What an action does to the notebook.
enum class Access {
    Open,  ///< shows something (open, daily, search, view, capture without text)
    Write, ///< adds to a page or creates one (new, append, prepend, task, capture with text)
    Run,   ///< runs an approved extension's command (which may write)
};
Access accessOf(const Request &request);
/// "open", "write", "run".
QString accessName(Access access);

/// The device's setting for addresses without a valid key (AppSettings "automation/links").
enum class LinkPolicy {
    Ask, ///< writing asks first (default)
    Key, ///< writing only with the key
    Off, ///< addresses do nothing
};
inline constexpr LinkPolicy DefaultPolicy = LinkPolicy::Ask;
/// "ask", "key", "off".
QString policyName(LinkPolicy policy);
/// The policy a setting's value names; DefaultPolicy for anything else.
LinkPolicy policyFromName(QStringView name);

/// The key an address carries, compared with the notebook's.
enum class KeyState {
    None,  ///< no key in the address
    Valid, ///< the notebook's key
    Wrong, ///< a key that is not the notebook's (or the notebook has none)
};
/// keyMatches(given, stored) as a KeyState ("" given: None).
KeyState keyState(QStringView given, QStringView stored);

enum class Decision {
    Allow,   ///< do it now
    Confirm, ///< show what it would do and wait for the user's "Write"
    Refuse,  ///< do nothing (Verdict::error says why)
};

struct Verdict {
    Decision decision = Decision::Refuse;
    /// Why it was refused: "links_off" (policy Off), "key_required" (writing without the key under
    /// policy Key, or where nobody can be asked), "wrong_key"; "" otherwise.
    QString error;
};

/// What to do with a request of `access` under `policy` with `key`. `canAsk`: a dialog can be shown
/// (the app's window; not Android's background run or a broadcast) — without it a write without
/// the key is refused.
Verdict decide(Access access, LinkPolicy policy, KeyState key, bool canAsk);

/// The x-callback addresses (and Android's result broadcast) may be used: only with a valid key.
bool mayCallBack(KeyState key);

/// A new random key: 32 characters of [a-z2-7] (160 bits from the system's generator).
QString generateKey();
/// `given` is `stored` (both non-empty), compared in constant time; letter case and spaces around
/// count as typos and are ignored.
bool keyMatches(QStringView given, QStringView stored);

/// The device's automation keys, one per notebook: a JSON file `{"<Vault::id()>": "<key>"}` with
/// owner-only permissions in the app's data folder (`secrets/automation-keys.json`, next to the
/// other secrets). The app shows, creates and renews them (Settings → Automation); brulion-cli
/// reads them for `uri build --with-key`, Android's background run to check a request.
class KeyStore {
public:
    explicit KeyStore(QString file);
    /// "<appDataDir>/secrets/automation-keys.json".
    static QString fileIn(const QString &appDataDir);

    QString file() const { return m_file; }
    /// The notebook's key, "" when it has none (or the file cannot be read).
    QString key(const QString &vaultId) const;
    /// The notebook's key, made now when it has none; "" (and error()) when it cannot be saved.
    QString ensureKey(const QString &vaultId);
    /// A new key for the notebook (the old one stops working); "" (and error()) on failure.
    QString renew(const QString &vaultId);
    /// The notebook has no key any more.
    bool remove(const QString &vaultId);
    /// Why the last change failed ("" after a success).
    QString error() const { return m_error; }

private:
    bool save(const QString &vaultId, const QString &key);

    QString m_file;
    QString m_error;
};

} // namespace brulion::automation
