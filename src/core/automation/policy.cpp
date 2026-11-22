#include "automation/policy.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>

#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::automation {

Access accessOf(const Request &request)
{
    switch (request.action) {
    case Action::Open:
    case Action::Daily:
    case Action::Search:
    case Action::View:
    case Action::Pair:
    case Action::Chat: return Access::Open;
    case Action::Capture: return request.value({u"text"}).trimmed().isEmpty() ? Access::Open : Access::Write;
    case Action::New:
    case Action::Append:
    case Action::Prepend:
    case Action::Task: return Access::Write;
    case Action::Run: return Access::Run;
    }
    return Access::Write;
}

QString accessName(Access access)
{
    switch (access) {
    case Access::Open: return u"open"_s;
    case Access::Write: return u"write"_s;
    case Access::Run: return u"run"_s;
    }
    return {};
}

QString policyName(LinkPolicy policy)
{
    switch (policy) {
    case LinkPolicy::Ask: return u"ask"_s;
    case LinkPolicy::Key: return u"key"_s;
    case LinkPolicy::Off: return u"off"_s;
    }
    return {};
}

LinkPolicy policyFromName(QStringView name)
{
    const QStringView n = name.trimmed();
    if (n.compare(u"key", Qt::CaseInsensitive) == 0)
        return LinkPolicy::Key;
    if (n.compare(u"off", Qt::CaseInsensitive) == 0)
        return LinkPolicy::Off;
    return DefaultPolicy;
}

KeyState keyState(QStringView given, QStringView stored)
{
    if (given.trimmed().isEmpty())
        return KeyState::None;
    return keyMatches(given, stored) ? KeyState::Valid : KeyState::Wrong;
}

Verdict decide(Access access, LinkPolicy policy, KeyState key, bool canAsk)
{
    if (policy == LinkPolicy::Off)
        return {Decision::Refuse, u"links_off"_s};
    if (key == KeyState::Wrong)
        return {Decision::Refuse, u"wrong_key"_s};
    if (access == Access::Open || key == KeyState::Valid)
        return {Decision::Allow, {}};
    if (policy == LinkPolicy::Ask && canAsk)
        return {Decision::Confirm, {}};
    return {Decision::Refuse, u"key_required"_s};
}

bool mayCallBack(KeyState key)
{
    return key == KeyState::Valid;
}

QString generateKey()
{
    static constexpr char32_t alphabet[] = U"abcdefghijklmnopqrstuvwxyz234567";
    QString out;
    out.reserve(32);
    QRandomGenerator *random = QRandomGenerator::system();
    for (int i = 0; i < 32; ++i)
        out.append(QChar(char16_t(alphabet[random->bounded(32)])));
    return out;
}

bool keyMatches(QStringView rawGiven, QStringView rawStored)
{
    const QString given = rawGiven.trimmed().toString().toLower();
    const QString stored = rawStored.trimmed().toString().toLower();
    if (given.isEmpty() || stored.isEmpty())
        return false;
    // Constant time over the stored key's length: the time says nothing about how much matched.
    char16_t diff = given.size() == stored.size() ? 0 : 1;
    for (qsizetype i = 0; i < stored.size(); ++i)
        diff |= char16_t(stored.at(i).unicode() ^ (i < given.size() ? given.at(i).unicode() : 0xffff));
    return diff == 0;
}

KeyStore::KeyStore(QString file)
    : m_file(std::move(file))
{
}

QString KeyStore::fileIn(const QString &appDataDir)
{
    return QDir::cleanPath(appDataDir + u"/secrets/automation-keys.json"_s);
}

namespace {

QJsonObject readKeys(const QString &file)
{
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly))
        return {};
    // A key file is a few hundred bytes; never read a planted giant.
    const QJsonDocument doc = QJsonDocument::fromJson(in.read(1024 * 1024));
    return doc.isObject() ? doc.object() : QJsonObject();
}

} // namespace

QString KeyStore::key(const QString &vaultId) const
{
    if (vaultId.isEmpty())
        return {};
    const QString key = readKeys(m_file).value(vaultId).toString().trimmed();
    return key.size() >= 16 ? key : QString(); // a damaged entry is no key (it would be guessable)
}

QString KeyStore::ensureKey(const QString &vaultId)
{
    const QString existing = key(vaultId);
    if (!existing.isEmpty()) {
        m_error.clear();
        return existing;
    }
    return renew(vaultId);
}

QString KeyStore::renew(const QString &vaultId)
{
    const QString fresh = generateKey();
    return save(vaultId, fresh) ? fresh : QString();
}

bool KeyStore::remove(const QString &vaultId)
{
    return save(vaultId, QString());
}

bool KeyStore::save(const QString &vaultId, const QString &key)
{
    m_error.clear();
    if (vaultId.isEmpty()) {
        m_error = u"no notebook"_s;
        return false;
    }
    QJsonObject keys = readKeys(m_file);
    if (key.isEmpty()) {
        if (!keys.contains(vaultId))
            return true;
        keys.remove(vaultId);
    } else {
        keys.insert(vaultId, key);
    }
    const QString folder = QFileInfo(m_file).absolutePath();
    if (!QDir().mkpath(folder)) {
        m_error = u"cannot create %1"_s.arg(folder);
        return false;
    }
    QFile::setPermissions(folder, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    QSaveFile out(m_file);
    if (!out.open(QIODevice::WriteOnly)) {
        m_error = out.errorString();
        return false;
    }
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(QJsonDocument(keys).toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        m_error = out.errorString();
        return false;
    }
    QFile::setPermissions(m_file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

} // namespace brulion::automation
