#include "appsettings.h"

#include "text/text.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

constexpr auto kTheme = "ui/themeMode";
constexpr auto kLanguage = "ui/language";
constexpr auto kFontScale = "ui/fontScale";
constexpr auto kReducedMotion = "ui/reducedMotion";
constexpr auto kEditorFont = "editor/font";
constexpr auto kEditorFontSize = "editor/fontSize";
constexpr auto kEditorWidth = "editor/maxWidth";
constexpr auto kEditorSource = "editor/sourceMode";
constexpr auto kCurrentVault = "vaults/current";
constexpr auto kRecentVaults = "vaults/recent";
constexpr auto kCloseToTray = "app/closeToTray";
constexpr auto kRemindersEnabled = "reminders/enabled";
constexpr auto kSnooze = "reminders/snoozeMinutes";
constexpr auto kAlarmRing = "reminders/alarmRingMinutes";
constexpr auto kNotifyMissed = "reminders/notifyMissed";
constexpr auto kNcUrl = "sync/nextcloud/url";
constexpr auto kNcUser = "sync/nextcloud/user";
// Up to 0.3.0 one switch and one folder for every notebook; read only to hand them over.
constexpr auto kNcSharedEnabled = "sync/nextcloud/enabled";
constexpr auto kNcSharedFolder = "sync/nextcloud/folder";
constexpr auto kNcNotebooks = "sync/nextcloud/notebooks";
constexpr auto kSyncInterval = "sync/intervalMinutes";
/// The server folder of a notebook without one (WorkspaceSync syncs with it then).
constexpr auto kDefaultServerFolder = "Brulion";

QString key(const char *k)
{
    return QString::fromLatin1(k);
}

QString cleanPath(const QString &path);

/// The id of a notebook's folder, as its device state is named (Vault::idForPath: the
/// canonical path); "" without a notebook.
QString notebookId(const QString &notebook)
{
    const QString clean = cleanPath(notebook);
    return clean.isEmpty() ? QString() : Vault::idForPath(clean);
}

/// The id 0.3.1–0.3.5 gave a notebook's settings: the hash of its path as written.
QString legacyNotebookId(const QString &notebook)
{
    const QString clean = cleanPath(notebook);
    return clean.isEmpty() ? QString() : text::fnv1a64Hex(clean.toUtf8());
}

QString groupKey(const QString &id, QStringView name)
{
    return key(kNcNotebooks) + u'/' + id + u'/' + name;
}

QString absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(cleanPath(path)).absoluteFilePath());
}

QString defaultLanguage()
{
    return QLocale().name().startsWith(u"en"_s) ? u"en"_s : u"pl"_s;
}

QString cleanPath(const QString &path)
{
    if (path.trimmed().isEmpty())
        return {};
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}

} // namespace

AppSettings::AppSettings(const QString &settingsFile, const QString &dataDir, QObject *parent)
    : QObject(parent),
      m_dataDir(dataDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : dataDir)
{
    if (settingsFile.isEmpty())
        m_settings = std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                                 QCoreApplication::organizationName(),
                                                 QCoreApplication::applicationName());
    else
        m_settings = std::make_unique<QSettings>(settingsFile, QSettings::IniFormat);
    adoptNotebookKeys(currentVault());
}

AppSettings::~AppSettings()
{
    m_settings->sync();
}

template<typename T>
bool AppSettings::store(const QString &k, const T &value, const T &current)
{
    if (value == current && m_settings->contains(k))
        return false;
    m_settings->setValue(k, QVariant::fromValue(value));
    return value != current;
}

// ---- General --------------------------------------------------------------------------------

QString AppSettings::themeMode() const
{
    const QString mode = m_settings->value(key(kTheme)).toString();
    return mode == u"light" || mode == u"dark" ? mode : u"system"_s;
}

void AppSettings::setThemeMode(const QString &mode)
{
    const QString v = mode == u"light" || mode == u"dark" ? mode : u"system"_s;
    if (store(key(kTheme), v, themeMode()))
        Q_EMIT themeModeChanged();
}

QString AppSettings::language() const
{
    const QString lang = m_settings->value(key(kLanguage)).toString();
    return lang == u"pl" || lang == u"en" ? lang : defaultLanguage();
}

void AppSettings::setLanguage(const QString &language)
{
    const QString v = language == u"en" ? u"en"_s : u"pl"_s;
    if (store(key(kLanguage), v, this->language()))
        Q_EMIT languageChanged();
}

qreal AppSettings::fontScale() const
{
    bool ok = false;
    const qreal s = m_settings->value(key(kFontScale)).toReal(&ok);
    return ok ? std::clamp(s, 0.85, 1.4) : 1.0;
}

void AppSettings::setFontScale(qreal scale)
{
    const qreal v = std::clamp(scale, 0.85, 1.4);
    if (store(key(kFontScale), v, fontScale()))
        Q_EMIT fontScaleChanged();
}

bool AppSettings::reducedMotion() const
{
    return m_settings->value(key(kReducedMotion), false).toBool();
}

void AppSettings::setReducedMotion(bool on)
{
    if (store(key(kReducedMotion), on, reducedMotion()))
        Q_EMIT reducedMotionChanged();
}

// ---- Editor ---------------------------------------------------------------------------------

QString AppSettings::editorFont() const
{
    return m_settings->value(key(kEditorFont)).toString() == u"sans" ? u"sans"_s : u"serif"_s;
}

void AppSettings::setEditorFont(const QString &font)
{
    const QString v = font == u"sans" ? u"sans"_s : u"serif"_s;
    if (store(key(kEditorFont), v, editorFont()))
        Q_EMIT editorFontChanged();
}

int AppSettings::editorFontSize() const
{
    bool ok = false;
    const int px = m_settings->value(key(kEditorFontSize)).toInt(&ok);
    return ok ? std::clamp(px, 12, 28) : 16;
}

void AppSettings::setEditorFontSize(int px)
{
    if (store(key(kEditorFontSize), std::clamp(px, 12, 28), editorFontSize()))
        Q_EMIT editorFontSizeChanged();
}

int AppSettings::editorWidth() const
{
    bool ok = false;
    const int px = m_settings->value(key(kEditorWidth)).toInt(&ok);
    if (!ok)
        return 760;
    return px <= 0 ? 0 : std::clamp(px, 480, 1600);
}

void AppSettings::setEditorWidth(int px)
{
    const int v = px <= 0 ? 0 : std::clamp(px, 480, 1600);
    if (store(key(kEditorWidth), v, editorWidth()))
        Q_EMIT editorWidthChanged();
}

bool AppSettings::editorSourceMode() const
{
    return m_settings->value(key(kEditorSource), false).toBool();
}

void AppSettings::setEditorSourceMode(bool on)
{
    if (store(key(kEditorSource), on, editorSourceMode()))
        Q_EMIT editorSourceModeChanged();
}

// ---- Vaults ---------------------------------------------------------------------------------

QString AppSettings::currentVault() const
{
    return m_settings->value(key(kCurrentVault)).toString();
}

void AppSettings::setCurrentVault(const QString &path)
{
    adoptNotebookKeys(path);
    if (store(key(kCurrentVault), cleanPath(path), currentVault())) {
        Q_EMIT currentVaultChanged();
        Q_EMIT nextcloudChanged(); // the sync switch and folder belong to the notebook
    }
}

QVariantList AppSettings::recentVaults() const
{
    QVariantList out;
    const QVariantList stored = m_settings->value(key(kRecentVaults)).toList();
    for (const QVariant &v : stored) {
        const QVariantMap m = v.toMap();
        const QString path = m.value(u"path"_s).toString();
        if (path.isEmpty())
            continue;
        QVariantMap entry;
        entry.insert(u"path"_s, path);
        const QString name = m.value(u"name"_s).toString();
        entry.insert(u"name"_s, name.isEmpty() ? QFileInfo(path).fileName() : name);
        entry.insert(u"lastOpened"_s, m.value(u"lastOpened"_s).toString());
        out.append(entry);
    }
    return out;
}

void AppSettings::addRecentVault(const QString &rawPath)
{
    const QString path = cleanPath(rawPath);
    if (path.isEmpty())
        return;
    QVariantList list = recentVaults();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const QVariant &v) { return v.toMap().value(u"path"_s).toString() == path; }),
               list.end());
    QVariantMap entry;
    entry.insert(u"path"_s, path);
    entry.insert(u"name"_s, QFileInfo(path).fileName());
    entry.insert(u"lastOpened"_s, QDateTime::currentDateTime().toString(Qt::ISODate));
    list.prepend(entry);
    while (list.size() > MaxRecentVaults)
        list.removeLast();
    m_settings->setValue(key(kRecentVaults), list);
    Q_EMIT recentVaultsChanged();
    setCurrentVault(path);
}

void AppSettings::forgetRecentVault(const QString &rawPath)
{
    const QString path = cleanPath(rawPath);
    QVariantList list = recentVaults();
    const auto before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [&](const QVariant &v) { return v.toMap().value(u"path"_s).toString() == path; }),
               list.end());
    if (list.size() == before)
        return;
    m_settings->setValue(key(kRecentVaults), list);
    Q_EMIT recentVaultsChanged();
}

// ---- Application ----------------------------------------------------------------------------

bool AppSettings::closeToTray() const
{
    return m_settings->value(key(kCloseToTray), true).toBool();
}

void AppSettings::setCloseToTray(bool on)
{
    if (store(key(kCloseToTray), on, closeToTray()))
        Q_EMIT closeToTrayChanged();
}

// ---- Reminders ------------------------------------------------------------------------------

bool AppSettings::remindersEnabled() const
{
    return m_settings->value(key(kRemindersEnabled), true).toBool();
}

void AppSettings::setRemindersEnabled(bool on)
{
    if (store(key(kRemindersEnabled), on, remindersEnabled()))
        Q_EMIT remindersEnabledChanged();
}

int AppSettings::snoozeMinutes() const
{
    bool ok = false;
    const int v = m_settings->value(key(kSnooze)).toInt(&ok);
    return ok ? std::clamp(v, 1, 120) : 10;
}

void AppSettings::setSnoozeMinutes(int minutes)
{
    if (store(key(kSnooze), std::clamp(minutes, 1, 120), snoozeMinutes()))
        Q_EMIT snoozeMinutesChanged();
}

int AppSettings::alarmRingMinutes() const
{
    bool ok = false;
    const int v = m_settings->value(key(kAlarmRing)).toInt(&ok);
    return ok ? std::clamp(v, 1, 60) : 10;
}

void AppSettings::setAlarmRingMinutes(int minutes)
{
    if (store(key(kAlarmRing), std::clamp(minutes, 1, 60), alarmRingMinutes()))
        Q_EMIT alarmRingMinutesChanged();
}

bool AppSettings::notifyMissed() const
{
    return m_settings->value(key(kNotifyMissed), true).toBool();
}

void AppSettings::setNotifyMissed(bool on)
{
    if (store(key(kNotifyMissed), on, notifyMissed()))
        Q_EMIT notifyMissedChanged();
}

// ---- Nextcloud ------------------------------------------------------------------------------

// Whether to sync and with which server folder is a property of the notebook: one folder on
// the server holds one notebook, so a second notebook must never inherit it (its notes would
// flow into the first one). The server and the account are shared.

template<typename T>
bool AppSettings::storeNotebookValue(const QString &notebook, QStringView name, const T &value, const T &current)
{
    const QString id = notebookId(notebook);
    if (id.isEmpty())
        return false; // no notebook open: nothing to set
    adoptNotebookKeys(notebook);
    const QString pathKey = groupKey(id, u"path");
    const QString path = absolutePath(notebook);
    if (m_settings->value(pathKey).toString() != path)
        m_settings->setValue(pathKey, path);
    return store(groupKey(id, name), value, current);
}

QString AppSettings::notebookKey(const QString &notebook, QStringView name) const
{
    const QString id = notebookId(notebook);
    if (id.isEmpty())
        return {};
    const QString own = groupKey(id, name);
    if (!m_settings->contains(own)) {
        const QString legacy = groupKey(legacyNotebookId(notebook), name);
        if (m_settings->contains(legacy))
            return legacy;
    }
    return own;
}

bool AppSettings::hasNotebookValue(const QString &notebook, QStringView name) const
{
    const QString k = notebookKey(notebook, name);
    return !k.isEmpty() && m_settings->contains(k);
}

void AppSettings::setNotebookValue(const QString &notebook, QStringView name, const QVariant &value)
{
    const QString id = notebookId(notebook);
    if (id.isEmpty())
        return;
    adoptNotebookKeys(notebook);
    m_settings->setValue(groupKey(id, u"path"), absolutePath(notebook));
    m_settings->setValue(groupKey(id, name), value);
}

void AppSettings::adoptNotebookKeys(const QString &notebook)
{
    // Only for a folder that exists: the canonical path of one that does not (an unmounted card)
    // may not be the one it will have.
    const QString clean = cleanPath(notebook);
    if (clean.isEmpty() || !QFileInfo(clean).isDir())
        return;
    const QString id = notebookId(clean);
    const QString legacy = legacyNotebookId(clean);
    bool has = false;
    if (legacy != id) {
        const QString from = key(kNcNotebooks) + u'/' + legacy;
        m_settings->beginGroup(from);
        const QStringList keys = m_settings->childKeys();
        m_settings->endGroup();
        for (const QString &k : keys) {
            const QString to = groupKey(id, k);
            if (!m_settings->contains(to))
                m_settings->setValue(to, m_settings->value(from + u'/' + k));
            has = true;
        }
        if (has)
            m_settings->remove(from);
    }
    const QString pathKey = groupKey(id, u"path");
    if (!has) {
        m_settings->beginGroup(key(kNcNotebooks) + u'/' + id);
        has = !m_settings->childKeys().isEmpty();
        m_settings->endGroup();
    }
    if (has && m_settings->value(pathKey).toString() != absolutePath(clean))
        m_settings->setValue(pathKey, absolutePath(clean));
}

bool AppSettings::nextcloudEnabled() const
{
    const QString k = notebookKey(currentVault(), u"enabled");
    return !k.isEmpty() && m_settings->value(k, false).toBool();
}

void AppSettings::setNextcloudEnabled(bool on)
{
    if (storeNotebookValue(currentVault(), u"enabled", on, nextcloudEnabled()))
        Q_EMIT nextcloudChanged();
}

QString AppSettings::nextcloudUrl() const
{
    return m_settings->value(key(kNcUrl)).toString();
}

void AppSettings::setNextcloudUrl(const QString &url)
{
    QString v = url.trimmed();
    while (v.endsWith(u'/'))
        v.chop(1);
    if (store(key(kNcUrl), v, nextcloudUrl()))
        Q_EMIT nextcloudChanged();
}

QString AppSettings::nextcloudUser() const
{
    return m_settings->value(key(kNcUser)).toString();
}

void AppSettings::setNextcloudUser(const QString &user)
{
    if (store(key(kNcUser), user.trimmed(), nextcloudUser()))
        Q_EMIT nextcloudChanged();
}

QString AppSettings::nextcloudFolder() const
{
    const QString k = notebookKey(currentVault(), u"folder");
    return k.isEmpty() ? key(kDefaultServerFolder) : m_settings->value(k, key(kDefaultServerFolder)).toString();
}

void AppSettings::setNextcloudFolder(const QString &folder)
{
    QString v = QDir::cleanPath(folder.trimmed());
    while (v.startsWith(u'/'))
        v.remove(0, 1);
    if (v == u".")
        v.clear();
    if (storeNotebookValue(currentVault(), u"folder", v, nextcloudFolder()))
        Q_EMIT nextcloudChanged();
}

void AppSettings::claimSharedNextcloud(const QString &notebook)
{
    // Up to 0.3.0 the switch and the folder were shared by all notebooks. The first notebook
    // opened afterwards takes them over (it is the one that was being synced) and the shared
    // keys go away, so no other notebook picks them up.
    const bool hasEnabled = m_settings->contains(key(kNcSharedEnabled));
    const bool hasFolder = m_settings->contains(key(kNcSharedFolder));
    if (notebookId(notebook).isEmpty() || (!hasEnabled && !hasFolder))
        return;
    if (hasEnabled && !hasNotebookValue(notebook, u"enabled"))
        setNotebookValue(notebook, u"enabled", m_settings->value(key(kNcSharedEnabled)));
    if (hasFolder && !hasNotebookValue(notebook, u"folder"))
        setNotebookValue(notebook, u"folder", m_settings->value(key(kNcSharedFolder)));
    m_settings->remove(key(kNcSharedEnabled));
    m_settings->remove(key(kNcSharedFolder));
    if (notebookId(currentVault()) == notebookId(notebook))
        Q_EMIT nextcloudChanged();
}

int AppSettings::syncIntervalMinutes() const
{
    bool ok = false;
    const int v = m_settings->value(key(kSyncInterval)).toInt(&ok);
    return ok ? std::clamp(v, 0, 24 * 60) : 15;
}

void AppSettings::setSyncIntervalMinutes(int minutes)
{
    if (store(key(kSyncInterval), std::clamp(minutes, 0, 24 * 60), syncIntervalMinutes()))
        Q_EMIT nextcloudChanged();
}

QString AppSettings::passwordFile() const
{
    return m_dataDir + u"/secrets/nextcloud-password"_s;
}

bool AppSettings::hasNextcloudPassword() const
{
    return QFileInfo(passwordFile()).size() > 0;
}

bool AppSettings::setNextcloudPassword(const QString &password)
{
    const QString file = passwordFile();
    const bool had = hasNextcloudPassword();
    if (password.isEmpty()) {
        const bool ok = !QFileInfo::exists(file) || QFile::remove(file);
        m_passwordSecret.reset();
        if (had)
            Q_EMIT nextcloudPasswordChanged();
        return ok;
    }
    const QString dir = QFileInfo(file).absolutePath();
    if (!QDir().mkpath(dir))
        return false;
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    m_passwordSecret.reset(password);
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    // Restrict the temporary file before the secret is written into it; QSaveFile renames it
    // over the old file, which keeps these permissions.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    out.write(password.toUtf8());
    if (!out.commit())
        return false;
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    Q_EMIT nextcloudPasswordChanged();
    return true;
}

QString AppSettings::nextcloudPassword() const
{
    QFile in(passwordFile());
    if (!in.open(QIODevice::ReadOnly)) {
        m_passwordSecret.reset();
        return {};
    }
    const QString password = QString::fromUtf8(in.readAll());
    m_passwordSecret.reset(password);
    return password;
}

// ---- Generic --------------------------------------------------------------------------------

QVariant AppSettings::value(const QString &k, const QVariant &defaultValue) const
{
    QVariant v = m_settings->value(k, defaultValue);
    // The INI file keeps only text: after a restart a stored bool or number comes back as a
    // string. Give it the default's type again (QML compares with === and computes with it);
    // a stored value that does not convert yields the default.
    if (defaultValue.isValid() && !defaultValue.isNull() && v.typeId() == QMetaType::QString
        && defaultValue.typeId() != QMetaType::QString) {
        const QString text = v.toString().trimmed();
        switch (defaultValue.typeId()) {
        case QMetaType::Bool:
            if (text == u"true" || text == u"1")
                return true;
            if (text == u"false" || text == u"0")
                return false;
            return defaultValue;
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::Double: {
            bool ok = false;
            const double d = text.toDouble(&ok);
            if (!ok)
                return defaultValue;
            if (defaultValue.typeId() == QMetaType::Double)
                return d;
            const qlonglong n = text.toLongLong(&ok);
            if (!ok)
                return defaultValue;
            return defaultValue.typeId() == QMetaType::Int ? QVariant(int(n)) : QVariant(n);
        }
        default:
            if (v.convert(defaultValue.metaType()))
                return v;
            return defaultValue;
        }
    }
    return v;
}

void AppSettings::setValue(const QString &k, const QVariant &value)
{
    if (m_settings->contains(k) && m_settings->value(k) == value)
        return;
    m_settings->setValue(k, value);
    Q_EMIT valueChanged(k);
}

void AppSettings::sync()
{
    m_settings->sync();
}

QString AppSettings::fileName() const
{
    return m_settings->fileName();
}

} // namespace brulion::shell
