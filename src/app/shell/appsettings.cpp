#include "appsettings.h"

#include "workspaceagents.h"

#include "workspacegithub.h"
#include "workspacesync.h"

#include "ai/ai.h"
#include "ai/local.h"
#include "ai/ownmodels.h"
#include "log/categories.h"
#include "log/redact.h"
#include "notebook/journals.h"
#include "text/text.h"
#include "uiscale.h"
#include "util/unicode.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

constexpr auto kTheme = "ui/themeMode"_L1;
constexpr auto kLanguage = "ui/language"_L1;
constexpr auto kFontScale = "ui/fontScale"_L1;
constexpr auto kReducedMotion = "ui/reducedMotion"_L1;
constexpr auto kInterfaceScale = "ui/interfaceScale"_L1;
constexpr auto kAccent = "ui/accent"_L1;
constexpr auto kCustomAccent = "ui/customAccent"_L1;
constexpr auto kLightSurface = "ui/lightSurface"_L1;
constexpr auto kDarkSurface = "ui/darkSurface"_L1;
constexpr auto kUiFontFamily = "ui/fontFamily"_L1;
constexpr auto kDensity = "ui/density"_L1;
constexpr auto kSidebarHidden = "ui/sidebar/hidden"_L1;
constexpr auto kSidebarSections = "ui/sidebar/sections"_L1;
constexpr auto kBottomNavTabs = "ui/bottomNav/tabs"_L1;
constexpr auto kCaptureButton = "ui/captureButton"_L1;
constexpr auto kRailExpands = "ui/rail/expands"_L1;
constexpr auto kGym = "extensions/silownia"_L1;
constexpr auto kHabits = "extensions/nawyki"_L1;
constexpr auto kAssistant = "extensions/asystent"_L1;
constexpr auto kFlashcards = "extensions/fiszki"_L1;
constexpr auto kFlashcardsNew = "flashcards/newPerDay"_L1;
constexpr auto kFlashcardsRetention = "flashcards/retention"_L1;
constexpr auto kFlashcardsTags = "flashcards/tags"_L1;
constexpr auto kAiProvider = "ai/provider"_L1;
constexpr auto kAiModel = "ai/model"_L1;
constexpr auto kAiLocalModel = "ai/localModel"_L1;
constexpr auto kAiBaseUrl = "ai/baseUrl"_L1;
constexpr auto kAcpAgents = "ai/agents"_L1;
constexpr auto kMcpServerEnabled = "mcpServer/enabled"_L1;
constexpr auto kMcpServerPort = "mcpServer/port"_L1;
constexpr auto kMcpServerPublicUrl = "mcpServer/publicUrl"_L1;
constexpr auto kMcpServerReadOnly = "mcpServer/readOnly"_L1;
constexpr auto kAcpShare = "ai/share"_L1;
constexpr auto kAcpShareAddress = "ai/shareAddress"_L1;
constexpr auto kAcpShareTrust = "ai/shareTrust"_L1;
constexpr auto kChatHooks = "ai/hooks"_L1;
constexpr auto kChatHooksAddress = "ai/hooksAddress"_L1;
constexpr auto kChatHooksPort = "ai/hooksPort"_L1;
constexpr auto kChatHooksPublicUrl = "ai/hooksPublicUrl"_L1;
constexpr auto kAiChatNotify = "ai/chatNotify"_L1;
constexpr auto kAiChatAutoArchive = "ai/chatAutoArchive"_L1;
constexpr auto kAiDefaultPartner = "ai/defaultPartner"_L1;
constexpr auto kAiAgentNotify = "ai/agentNotify"_L1;
constexpr auto kExtensionsNotify = "extensions/notify"_L1;
constexpr auto kAppMode = "ui/mode"_L1;
constexpr auto kChatToolCalls = "ai/chatToolCalls"_L1;
constexpr auto kEditorFont = "editor/font"_L1;
constexpr auto kEditorFontFamily = "editor/fontFamily"_L1;
constexpr auto kEditorMonoFamily = "editor/monoFamily"_L1;
constexpr auto kEditorLineSpacing = "editor/lineSpacing"_L1;
constexpr auto kEditorFontSize = "editor/fontSize"_L1;
constexpr auto kEditorWidth = "editor/maxWidth"_L1;
constexpr auto kEditorSource = "editor/sourceMode"_L1;
constexpr auto kPropertiesExpanded = "editor/propertiesExpanded"_L1;
constexpr auto kWebPictures = "editor/webPicturesShown"_L1;
constexpr auto kPictureFrames = "editor/pictureFrames"_L1;
constexpr auto kLightPictureBacking = "editor/lightPictureBacking"_L1;
constexpr auto kCodeLineNumbers = "editor/codeLineNumbers"_L1;
constexpr auto kSpellCheck = "spelling/enabled"_L1;
constexpr auto kTaskStatusHistory = "tasks/statusHistory"_L1;
constexpr auto kSpellLanguages = "spelling/languages"_L1;
constexpr auto kSpellWholePage = "spelling/wholePage"_L1;
constexpr auto kSpellingWords = "spelling/words"_L1;
constexpr auto kAttachmentSearch = "search/attachments"_L1;
constexpr auto kAttachmentOcr = "search/ocr"_L1;
constexpr auto kAttachmentOcrLanguages = "search/ocrLanguages"_L1;
constexpr auto kExportWebPictures = "export/webPictures"_L1;
constexpr auto kWordCount = "editor/wordCountShown"_L1;
constexpr auto kFocusMode = "ui/focusMode"_L1;
constexpr auto kReadingMode = "ui/readingMode"_L1;
constexpr auto kTypewriter = "editor/typewriterScrolling"_L1;
constexpr auto kVimMode = "editor/vimMode"_L1;
constexpr auto kShortcuts = "shortcuts"_L1;
constexpr auto kIntroDone = "app/introDone"_L1;
constexpr auto kNewNotebookConfig = "app/newNotebookConfig"_L1;
constexpr auto kCurrentVault = "vaults/current"_L1;
constexpr auto kRecentVaults = "vaults/recent"_L1;
constexpr auto kFollowOutsideLinks = "vaults/followOutsideLinks"_L1;
constexpr auto kOutsideLinksChecked = "vaults/outsideLinksChecked"_L1; ///< + "/<notebook id>"
constexpr auto kCloseToTray = "app/closeToTray"_L1;
constexpr auto kTrayClick = "app/trayClick"_L1;
constexpr auto kRemindersEnabled = "reminders/enabled"_L1;
constexpr auto kSnooze = "reminders/snoozeMinutes"_L1;
constexpr auto kAlarmRing = "reminders/alarmRingMinutes"_L1;
constexpr auto kNotifyMissed = "reminders/notifyMissed"_L1;
constexpr auto kNcUrl = "sync/nextcloud/url"_L1;
constexpr auto kNcUser = "sync/nextcloud/user"_L1;
// Up to 0.3.0 one switch and one folder for every notebook; read only to hand them over.
constexpr auto kNcSharedEnabled = "sync/nextcloud/enabled"_L1;
constexpr auto kNcSharedFolder = "sync/nextcloud/folder"_L1;
/// The notebook that was current when this version first saw them.
constexpr auto kNcSharedOwner = "sync/nextcloud/sharedOwner"_L1;
constexpr auto kNcNotebooks = "sync/nextcloud/notebooks"_L1;
constexpr auto kSyncInterval = "sync/intervalMinutes"_L1;
/// "<this>/<Vault::idForPath>/address|name|where|path": a notebook's folder of this device.
constexpr auto kDeviceFolders = "sync/folder/notebooks"_L1;
constexpr auto kRecentSearches = "search/recent"_L1;
/// The server folder of a notebook without one (WorkspaceSync syncs with it then).
constexpr auto kDefaultServerFolder = "Brulion"_L1;
/// SearchView's recent searches kept.
constexpr qsizetype kMaxRecentSearches = 8;

// The values of the text settings, the default first (oneOf()).
constexpr std::array kThemeChoices{u"dark", u"light", u"system"};
constexpr std::array kAccentChoices{u"coral", u"red", u"blue", u"green", u"teal", u"violet", u"graphite", u"custom"};
constexpr std::array kLightSurfaceChoices{u"paper", u"white"};
constexpr std::array kDarkSurfaceChoices{u"graphite", u"ink", u"black", u"slate"};
constexpr std::array kDensityChoices{u"comfortable", u"compact"};
constexpr std::array kEditorFontChoices{u"sans", u"serif"};
constexpr std::array kLineSpacingChoices{u"normal", u"compact", u"relaxed"};
constexpr std::array kSpellLanguageChoices{u"auto", u"pl", u"en", u"pl,en", u"en,pl"};
constexpr std::array kWebPictureExportChoices{u"link", u"include"};
constexpr std::array kTrayClickChoices{u"calendar", u"window"};
constexpr std::array kChatToolCallsChoices{u"all", u"hideFinished", u"hideAll"};

QString cleanPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    return trimmed.isEmpty() ? QString() : QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
}

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
    return kNcNotebooks + u'/' + id + u'/' + name;
}

QString absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(cleanPath(path)).absoluteFilePath());
}

/// Record `notebook`'s path in its group `id` (unless it is there already).
void recordPath(QSettings &settings, const QString &id, const QString &notebook)
{
    const QString pathKey = groupKey(id, u"path");
    const QString path = absolutePath(notebook);
    if (settings.value(pathKey).toString() != path)
        settings.setValue(pathKey, path);
}

/// A server folder as compared: no outer slashes, the default for none, NFC, case folded.
QString folderIdentity(const QString &folder)
{
    QString v = QDir::cleanPath(QDir::fromNativeSeparators(folder.trimmed()));
    while (v.startsWith(u'/'))
        v.remove(0, 1);
    while (v.endsWith(u'/'))
        v.chop(1);
    if (v.isEmpty() || v == u".")
        v = kDefaultServerFolder;
    return unicode::nfc(v).toCaseFolded();
}

/// One folder is the other or holds it: notebooks syncing with them would mix their notes.
bool foldersOverlap(const QString &a, const QString &b)
{
    const QString x = folderIdentity(a);
    const QString y = folderIdentity(b);
    return x == y || x.startsWith(y + u'/') || y.startsWith(x + u'/');
}

/// A notebook folder's name as a server folder: characters a folder on the server cannot have
/// become spaces, no leading or trailing dots and spaces.
QString serverFolderFromName(const QString &name)
{
    static const QString forbidden = u"\\/:*?\"<>|"_s;
    QString out;
    out.reserve(name.size());
    for (const QChar c : name)
        out += forbidden.contains(c) || c.category() == QChar::Other_Control ? QChar(u' ') : c;
    out = out.simplified();
    while (!out.isEmpty() && (out.front() == u'.' || out.front() == u' '))
        out.remove(0, 1);
    while (!out.isEmpty() && (out.back() == u'.' || out.back() == u' '))
        out.chop(1);
    return unicode::nfc(out);
}

/// `value` when it is one of `choices`, else the first of them (the default).
QString oneOf(const QString &value, std::span<const char16_t *const> choices)
{
    const bool known = std::ranges::any_of(choices, [&](const char16_t *c) { return value == QStringView(c); });
    return known ? value : QString::fromUtf16(choices.front());
}

/// "#rrggbb" in lower case, else "".
QString cleanColor(const QString &color)
{
    const QString v = color.trimmed().toLower();
    const auto hexDigit = [](QChar c) { return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); };
    return v.size() == 7 && v.front() == u'#' && std::all_of(v.cbegin() + 1, v.cend(), hexDigit) ? v : QString();
}

/// The UI text size factor within 0.85 … 1.4 (NaN, as a settings file may hold, is the default 1).
qreal cleanFontScale(qreal scale)
{
    return std::isnan(scale) ? 1.0 : std::clamp(scale, 0.85, 1.4);
}

/// The known ids of `ids` in their order, each once.
QStringList knownIds(QStringList ids, const QStringList &known)
{
    ids.removeIf([&](const QString &id) { return !known.contains(id); });
    ids.removeDuplicates();
    return ids;
}

/// The bottom navigation's number of views.
constexpr qsizetype kBottomNavSlots = 5;

/// Five views for the bottom navigation: the known ones first, the rest from the default order.
QStringList normalizedTabs(const QStringList &views)
{
    // The default five after the known ones, then the other views.
    QStringList out = knownIds(views, AppSettings::bottomNavViews()).mid(0, kBottomNavSlots)
                      + AppSettings::defaultBottomNavTabs() + AppSettings::bottomNavViews();
    out.removeDuplicates();
    return out.mid(0, kBottomNavSlots);
}

/// All the sidebar's page sections: the known ones in their order, the others after them in the
/// default order.
QStringList normalizedSections(const QStringList &ids)
{
    QStringList out = knownIds(ids, AppSettings::sidebarSectionIds()) + AppSettings::sidebarSectionIds();
    out.removeDuplicates();
    return out;
}

/// A list stored in the INI file (one entry comes back as a plain string).
QStringList storedList(const QVariant &v)
{
    if (v.typeId() == QMetaType::QString) {
        const QString text = v.toString();
        return text.isEmpty() ? QStringList() : QStringList{text};
    }
    return v.toStringList();
}

/// An entry of the recent notebooks: { path, name ("": its folder's), lastOpened }.
QVariantMap recentEntry(const QString &path, const QString &name, const QString &lastOpened)
{
    return {{u"path"_s, path},
            {u"name"_s, name.isEmpty() ? QFileInfo(path).fileName() : name},
            {u"lastOpened"_s, lastOpened}};
}

/// Remove the entries of `path` from the recent notebooks `list`; how many there were.
qsizetype removeRecent(QVariantList &list, const QString &path)
{
    return list.removeIf([&](const QVariant &v) { return v.toMap().value(u"path"_s).toString() == path; });
}

/// The usable part of the intro's notebook settings (Workspace::setConfig() validates again).
QVariantMap cleanNotebookConfig(const QVariantMap &config)
{
    QVariantMap out;
    for (const QString &name : {u"journalMode"_s, u"pageMode"_s}) {
        const QString mode = config.value(name).toString();
        if (mode == u"outline" || mode == u"document")
            out.insert(name, mode);
    }
    bool ok = false;
    const int day = config.value(u"weekStartsOn"_s).toInt(&ok);
    if (ok && (day == 1 || day == 6 || day == 7))
        out.insert(u"weekStartsOn"_s, day);
    const QTime time = QTime::fromString(config.value(u"defaultReminderTime"_s).toString().trimmed(), u"H:mm"_s);
    if (time.isValid())
        out.insert(u"defaultReminderTime"_s, time.toString(u"HH:mm"_s));
    const QString format = config.value(u"journalFileFormat"_s).toString().trimmed();
    if (format == u"yyyy-MM-dd" || format == u"yyyy_MM_dd" || format == u"dd.MM.yyyy")
        out.insert(u"journalFileFormat"_s, format);
    return out;
}

/// The file of the app password in the app data folder `dataDir`.
QString passwordFileIn(const QString &dataDir)
{
    return dataDir + u"/secrets/nextcloud-password"_s;
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
bool AppSettings::store(QAnyStringView k, const T &value, const T &current)
{
    if (value == current && m_settings->contains(k))
        return false;
    m_settings->setValue(k, QVariant::fromValue(value));
    return value != current;
}

// ---- General --------------------------------------------------------------------------------

QString AppSettings::themeMode() const
{
    // Never chosen (or unknown): dark — with the graphite background (darkSurface()).
    return oneOf(m_settings->value(kTheme).toString(), kThemeChoices);
}

void AppSettings::setThemeMode(const QString &mode)
{
    if (store(kTheme, oneOf(mode, kThemeChoices), themeMode()))
        Q_EMIT themeModeChanged();
}

QString AppSettings::language() const
{
    // Never chosen: English, whatever the system's (brulion-cli reads this setting the same way).
    return notebook::appLanguage(m_settings->value(kLanguage).toString());
}

void AppSettings::setLanguage(const QString &language)
{
    if (store(kLanguage, notebook::appLanguage(language), this->language()))
        Q_EMIT languageChanged();
}

qreal AppSettings::fontScale() const
{
    bool ok = false;
    const qreal s = m_settings->value(kFontScale).toReal(&ok);
    return ok ? cleanFontScale(s) : 1.0;
}

void AppSettings::setFontScale(qreal scale)
{
    if (store(kFontScale, cleanFontScale(scale), fontScale()))
        Q_EMIT fontScaleChanged();
}

qreal AppSettings::interfaceScale() const
{
    bool ok = false;
    const qreal s = m_settings->value(kInterfaceScale).toReal(&ok);
    return ok ? brulion::ui::cleanInterfaceScale(s) : 0.0;
}

void AppSettings::setInterfaceScale(qreal scale)
{
    if (store(kInterfaceScale, brulion::ui::cleanInterfaceScale(scale), interfaceScale()))
        Q_EMIT interfaceScaleChanged();
}

bool AppSettings::reducedMotion() const
{
    return m_settings->value(kReducedMotion, false).toBool();
}

void AppSettings::setReducedMotion(bool on)
{
    if (store(kReducedMotion, on, reducedMotion()))
        Q_EMIT reducedMotionChanged();
}

// ---- Appearance -----------------------------------------------------------------------------

QStringList AppSettings::sidebarItems()
{
    return {u"journals"_s, u"calendar"_s, u"tasks"_s,  u"habits"_s,    u"gym"_s,   u"flashcards"_s, u"allPages"_s,
            u"tags"_s,     u"graph"_s,    u"search"_s, u"favorites"_s, u"recent"_s};
}

QStringList AppSettings::sidebarSectionIds()
{
    return {u"pages"_s, u"favorites"_s, u"recent"_s};
}

QStringList AppSettings::bottomNavViews()
{
    return {u"journals"_s, u"calendar"_s, u"search"_s, u"tasks"_s, u"habits"_s, u"gym"_s, u"flashcards"_s,
            u"allPages"_s, u"tags"_s, u"graph"_s};
}

QStringList AppSettings::defaultBottomNavTabs()
{
    return {u"journals"_s, u"allPages"_s, u"search"_s, u"tasks"_s, u"calendar"_s};
}

QString AppSettings::cleanFontFamily(const QString &family)
{
    const QString v = family.trimmed();
    if (v.size() > 128)
        return {};
    for (const QChar c : v) {
        const QChar::Category cat = c.category();
        if (cat == QChar::Other_Control || cat == QChar::Separator_Line || cat == QChar::Separator_Paragraph)
            return {};
    }
    return v;
}

QString AppSettings::accentColor() const
{
    return oneOf(m_settings->value(kAccent).toString(), kAccentChoices);
}

void AppSettings::setAccentColor(const QString &name)
{
    if (store(kAccent, oneOf(name, kAccentChoices), accentColor()))
        Q_EMIT accentChanged();
}

QString AppSettings::customAccent() const
{
    return cleanColor(m_settings->value(kCustomAccent).toString());
}

void AppSettings::setCustomAccent(const QString &color)
{
    if (store(kCustomAccent, cleanColor(color), customAccent()))
        Q_EMIT accentChanged();
}

QString AppSettings::lightSurface() const
{
    return oneOf(m_settings->value(kLightSurface).toString(), kLightSurfaceChoices);
}

void AppSettings::setLightSurface(const QString &name)
{
    if (store(kLightSurface, oneOf(name, kLightSurfaceChoices), lightSurface()))
        Q_EMIT surfacesChanged();
}

QString AppSettings::darkSurface() const
{
    return oneOf(m_settings->value(kDarkSurface).toString(), kDarkSurfaceChoices);
}

void AppSettings::setDarkSurface(const QString &name)
{
    if (store(kDarkSurface, oneOf(name, kDarkSurfaceChoices), darkSurface()))
        Q_EMIT surfacesChanged();
}

QString AppSettings::uiFontFamily() const
{
    return cleanFontFamily(m_settings->value(kUiFontFamily).toString());
}

void AppSettings::setUiFontFamily(const QString &family)
{
    if (store(kUiFontFamily, cleanFontFamily(family), uiFontFamily()))
        Q_EMIT fontFamiliesChanged();
}

QString AppSettings::density() const
{
    return oneOf(m_settings->value(kDensity).toString(), kDensityChoices);
}

void AppSettings::setDensity(const QString &density)
{
    if (store(kDensity, oneOf(density, kDensityChoices), this->density()))
        Q_EMIT densityChanged();
}

QStringList AppSettings::sidebarHidden() const
{
    return knownIds(storedList(m_settings->value(kSidebarHidden)), sidebarItems());
}

void AppSettings::setSidebarHidden(const QStringList &ids)
{
    if (store(kSidebarHidden, knownIds(ids, sidebarItems()), sidebarHidden()))
        Q_EMIT sidebarHiddenChanged();
}

void AppSettings::setSidebarItemShown(const QString &id, bool shown)
{
    QStringList hidden = sidebarHidden();
    hidden.removeAll(id);
    if (!shown)
        hidden.append(id);
    setSidebarHidden(hidden);
}

QStringList AppSettings::sidebarSections() const
{
    return normalizedSections(storedList(m_settings->value(kSidebarSections)));
}

void AppSettings::setSidebarSections(const QStringList &ids)
{
    if (store(kSidebarSections, normalizedSections(ids), sidebarSections()))
        Q_EMIT sidebarSectionsChanged();
}

void AppSettings::moveSidebarSection(const QString &id, int delta)
{
    QStringList sections = sidebarSections();
    const qsizetype from = sections.indexOf(id);
    const qsizetype to = from + (delta < 0 ? -1 : 1);
    if (from < 0 || delta == 0 || to < 0 || to >= sections.size())
        return;
    sections.swapItemsAt(from, to);
    setSidebarSections(sections);
}

QStringList AppSettings::bottomNavTabs() const
{
    const QStringList stored = storedList(m_settings->value(kBottomNavTabs));
    // The default of the four slots with Menu after them (up to 0.13), as stored by a reset or a
    // slot picked and put back: the default of today, not those four with a fifth view appended.
    if (stored == QStringList{u"journals"_s, u"calendar"_s, u"search"_s, u"tasks"_s})
        return defaultBottomNavTabs();
    return normalizedTabs(stored);
}

void AppSettings::setBottomNavTabs(const QStringList &views)
{
    if (store(kBottomNavTabs, normalizedTabs(views), bottomNavTabs()))
        Q_EMIT bottomNavTabsChanged();
}

void AppSettings::setBottomNavTab(int index, const QString &view)
{
    QStringList tabs = bottomNavTabs();
    if (index < 0 || index >= tabs.size() || !bottomNavViews().contains(view))
        return;
    const qsizetype other = tabs.indexOf(view);
    if (other >= 0)
        tabs[other] = tabs.at(index);
    tabs[index] = view;
    setBottomNavTabs(tabs);
}

bool AppSettings::gymEnabled() const
{
    return m_settings->value(kGym, false).toBool();
}

void AppSettings::setGymEnabled(bool on)
{
    if (store(kGym, on, gymEnabled()))
        Q_EMIT gymEnabledChanged();
}

bool AppSettings::habitsEnabled() const
{
    return m_settings->value(kHabits, true).toBool();
}

void AppSettings::setHabitsEnabled(bool on)
{
    if (store(kHabits, on, habitsEnabled()))
        Q_EMIT habitsEnabledChanged();
}

bool AppSettings::assistantEnabled() const
{
    return m_settings->value(kAssistant, false).toBool();
}

void AppSettings::setAssistantEnabled(bool on)
{
    if (store(kAssistant, on, assistantEnabled()))
        Q_EMIT assistantEnabledChanged();
}

bool AppSettings::acpShare() const
{
    return acpProgramsAvailable() && m_settings->value(kAcpShare, false).toBool();
}

void AppSettings::setAcpShare(bool on)
{
    if (store(kAcpShare, on, acpShare()))
        Q_EMIT acpShareChanged();
}

QString AppSettings::acpShareAddress() const
{
    return m_settings->value(kAcpShareAddress).toString().trimmed();
}

void AppSettings::setAcpShareAddress(const QString &address)
{
    if (store(kAcpShareAddress, address.trimmed(), acpShareAddress()))
        Q_EMIT acpShareChanged();
}

bool AppSettings::acpShareTrust() const
{
    return m_settings->value(kAcpShareTrust, true).toBool();
}

void AppSettings::setAcpShareTrust(bool on)
{
    if (store(kAcpShareTrust, on, acpShareTrust()))
        Q_EMIT acpShareChanged();
}

bool AppSettings::chatHooks() const
{
    return acpProgramsAvailable() && m_settings->value(kChatHooks, false).toBool();
}

void AppSettings::setChatHooks(bool on)
{
    if (store(kChatHooks, on, chatHooks()))
        Q_EMIT chatHooksChanged();
}

QString AppSettings::chatHooksAddress() const
{
    return m_settings->value(kChatHooksAddress).toString() == u"tailnet" ? u"tailnet"_s : QString();
}

void AppSettings::setChatHooksAddress(const QString &address)
{
    if (store(kChatHooksAddress, address.trimmed() == u"tailnet" ? u"tailnet"_s : QString(), chatHooksAddress()))
        Q_EMIT chatHooksChanged();
}

int AppSettings::chatHooksPort() const
{
    const int port = m_settings->value(kChatHooksPort, 8791).toInt();
    return port >= 1024 && port <= 65535 ? port : 8791;
}

void AppSettings::setChatHooksPort(int port)
{
    if (port < 1024 || port > 65535)
        return;
    if (store(kChatHooksPort, port, chatHooksPort()))
        Q_EMIT chatHooksChanged();
}

QString AppSettings::chatHooksPublicUrl() const
{
    return m_settings->value(kChatHooksPublicUrl).toString().trimmed();
}

void AppSettings::setChatHooksPublicUrl(const QString &url)
{
    QString clean = url.trimmed();
    while (clean.endsWith(u'/'))
        clean.chop(1);
    if (store(kChatHooksPublicUrl, clean, chatHooksPublicUrl()))
        Q_EMIT chatHooksChanged();
}

QString AppSettings::aiChatNotify() const
{
    const QString mode = m_settings->value(kAiChatNotify).toString();
    return mode == u"always" || mode == u"never" ? mode : u"background"_s;
}

void AppSettings::setAiChatNotify(const QString &mode)
{
    const QString value = mode == u"always" || mode == u"never" ? mode : u"background"_s;
    if (store(kAiChatNotify, value, aiChatNotify()))
        Q_EMIT aiChatNotifyChanged();
}

QString AppSettings::aiDefaultPartner() const
{
    return m_settings->value(kAiDefaultPartner).toString().trimmed();
}

void AppSettings::setAiDefaultPartner(const QString &partner)
{
    if (store(kAiDefaultPartner, partner.trimmed(), aiDefaultPartner()))
        Q_EMIT aiDefaultPartnerChanged();
}

bool AppSettings::aiAgentNotify() const
{
    return m_settings->value(kAiAgentNotify, true).toBool();
}

void AppSettings::setAiAgentNotify(bool on)
{
    if (store(kAiAgentNotify, on, aiAgentNotify()))
        Q_EMIT aiAgentNotifyChanged();
}

QString AppSettings::appMode() const
{
    return m_settings->value(kAppMode).toString() == u"code" ? u"code"_s : u"notes"_s;
}

void AppSettings::setAppMode(const QString &mode)
{
    if (store(kAppMode, mode == u"code" ? u"code"_s : u"notes"_s, appMode()))
        Q_EMIT appModeChanged();
}

bool AppSettings::extensionsNotify() const
{
    return m_settings->value(kExtensionsNotify, true).toBool();
}

void AppSettings::setExtensionsNotify(bool on)
{
    if (store(kExtensionsNotify, on, extensionsNotify()))
        Q_EMIT extensionsNotifyChanged();
}

QString AppSettings::aiChatAutoArchive() const
{
    const QString days = m_settings->value(kAiChatAutoArchive).toString();
    return days == u"1" || days == u"3" || days == u"7" || days == u"30" ? days : u"never"_s;
}

void AppSettings::setAiChatAutoArchive(const QString &days)
{
    const QString value = days == u"1" || days == u"3" || days == u"7" || days == u"30" ? days : u"never"_s;
    if (store(kAiChatAutoArchive, value, aiChatAutoArchive()))
        Q_EMIT aiChatAutoArchiveChanged();
}

int AppSettings::aiChatAutoArchiveDays() const
{
    const QString days = aiChatAutoArchive();
    return days == u"never" ? 0 : days.toInt();
}

QString AppSettings::chatToolCalls() const
{
    return oneOf(m_settings->value(kChatToolCalls).toString(), kChatToolCallsChoices);
}

void AppSettings::setChatToolCalls(const QString &shown)
{
    if (store(kChatToolCalls, oneOf(shown, kChatToolCallsChoices), chatToolCalls()))
        Q_EMIT chatToolCallsChanged();
}

bool AppSettings::flashcardsEnabled() const
{
    return m_settings->value(kFlashcards, true).toBool();
}

void AppSettings::setFlashcardsEnabled(bool on)
{
    if (store(kFlashcards, on, flashcardsEnabled()))
        Q_EMIT flashcardsEnabledChanged();
}

int AppSettings::flashcardsNewPerDay() const
{
    bool ok = false;
    const int n = m_settings->value(kFlashcardsNew, 20).toInt(&ok);
    return ok ? std::clamp(n, 0, 9999) : 20;
}

void AppSettings::setFlashcardsNewPerDay(int n)
{
    if (store(kFlashcardsNew, std::clamp(n, 0, 9999), flashcardsNewPerDay()))
        Q_EMIT flashcardsSettingsChanged();
}

int AppSettings::flashcardsRetention() const
{
    bool ok = false;
    const int n = m_settings->value(kFlashcardsRetention, 90).toInt(&ok);
    return ok ? std::clamp(n, 70, 99) : 90;
}

void AppSettings::setFlashcardsRetention(int percent)
{
    if (store(kFlashcardsRetention, std::clamp(percent, 70, 99), flashcardsRetention()))
        Q_EMIT flashcardsSettingsChanged();
}

QString AppSettings::flashcardsTags() const
{
    const QString tags = m_settings->value(kFlashcardsTags, QString()).toString().simplified();
    return tags.isEmpty() ? u"flashcards, fiszki"_s : tags;
}

void AppSettings::setFlashcardsTags(const QString &tags)
{
    QString clean = tags.simplified();
    if (clean.isEmpty())
        clean = u"flashcards, fiszki"_s;
    if (store(kFlashcardsTags, clean, flashcardsTags()))
        Q_EMIT flashcardsSettingsChanged();
}

QVariantMap AppSettings::flashcardsSettings() const
{
    return {{u"tags"_s, flashcardsTags()},
            {u"newPerDay"_s, flashcardsNewPerDay()},
            {u"retention"_s, flashcardsRetention() / 100.0}};
}

QStringList AppSettings::enabledExtensions() const
{
    QStringList out;
    if (gymEnabled())
        out << u"silownia"_s;
    if (habitsEnabled())
        out << u"nawyki"_s;
    if (assistantEnabled())
        out << u"asystent"_s;
    if (flashcardsEnabled())
        out << u"fiszki"_s;
    return out;
}

QString AppSettings::aiProvider() const
{
    const QString p = m_settings->value(kAiProvider, u"anthropic"_s).toString();
    return p == u"openai" || p == u"local" ? p : u"anthropic"_s;
}

void AppSettings::setAiProvider(const QString &provider)
{
    const QString p = provider == u"openai" || provider == u"local" ? provider : u"anthropic"_s;
    if (store(kAiProvider, p, aiProvider()))
        Q_EMIT aiChanged();
}

QString AppSettings::aiLocalModel() const
{
    // A model the app knows or one of the user's own (LocalAi knows which are there); one it no
    // longer does (or none chosen) is the default.
    const QString id = m_settings->value(kAiLocalModel).toString();
    return ai::localModel(id) || ai::isOwnModelId(id) ? id : QString();
}

void AppSettings::setAiLocalModel(const QString &id)
{
    if (store(kAiLocalModel, ai::localModel(id) || ai::isOwnModelId(id) ? id : QString(), aiLocalModel()))
        Q_EMIT aiChanged();
}

QString AppSettings::aiModel() const { return m_settings->value(kAiModel).toString(); }

void AppSettings::setAiModel(const QString &model)
{
    if (store(kAiModel, model.trimmed().left(200), aiModel()))
        Q_EMIT aiChanged();
}

QString AppSettings::aiBaseUrl() const { return m_settings->value(kAiBaseUrl).toString(); }

void AppSettings::setAiBaseUrl(const QString &url)
{
    if (store(kAiBaseUrl, url.trimmed().left(500), aiBaseUrl()))
        Q_EMIT aiChanged();
}

bool AppSettings::aiUnencrypted() const
{
    ai::Settings settings;
    settings.provider = aiProvider();
    settings.baseUrl = aiBaseUrl();
    return settings.unencrypted();
}

namespace {
QString aiKeyFileIn(const QString &dataDir) { return dataDir + u"/secrets/ai-key"_s; }
} // namespace

bool AppSettings::hasAiKey() const { return QFileInfo(aiKeyFileIn(m_dataDir)).size() > 0; }

bool AppSettings::setAiKey(const QString &key)
{
    const bool had = hasAiKey();
    const QString trimmed = key.trimmed();
    m_aiKeySecret.reset(trimmed); // masked in every log line from now on
    const bool ok = writeSecretFile(aiKeyFileIn(m_dataDir), trimmed); // owner-only; "" removes the file
    if (trimmed.isEmpty() ? had : ok)
        Q_EMIT aiChanged();
    return ok;
}

// ---- AI agents (the Agent Client Protocol) ---------------------------------------------------

namespace {

constexpr int MaxAcpAgents = 20;

bool validAgentId(const QString &id)
{
    if (id.isEmpty() || id.size() > 40)
        return false;
    return std::ranges::all_of(id, [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9') || c == u'-';
    });
}

/// The file of the token this device was given for agent `id` on another computer.
QString acpTokenFileIn(const QString &dataDir, const QString &id)
{
    return dataDir + u"/secrets/acp-"_s + id;
}

/// A host's name as the start of an agent's id: "raspberry-pi-4" stays, "Mój Laptop" is "m-j-laptop".
QString idPart(const QString &name)
{
    QString out;
    for (const QChar c : name.toLower()) {
        if ((c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9'))
            out += c;
        else if (!out.endsWith(u'-'))
            out += u'-';
    }
    while (out.startsWith(u'-'))
        out.remove(0, 1);
    while (out.endsWith(u'-'))
        out.chop(1);
    return out.left(20);
}

/// `base`, or `base-2`, `base-3` … — the first that none of `agents` has.
QString freeAgentId(const QString &base, const QList<AgentConfig> &agents)
{
    const auto taken = [&agents](const QString &id) {
        return std::ranges::any_of(agents, [&id](const AgentConfig &a) { return a.id == id; });
    };
    if (!taken(base))
        return base;
    for (int n = 2;; ++n) {
        const QString id = base + u'-' + QString::number(n);
        if (!taken(id))
            return id;
    }
}

} // namespace

QList<AgentConfig> AppSettings::acpAgents() const
{
    QList<AgentConfig> out;
    if (!WorkspaceAgents::available() && !WorkspaceAgents::remoteAvailable())
        return out;
    const QJsonArray rows = QJsonDocument::fromJson(m_settings->value(kAcpAgents).toString().toUtf8()).array();
    for (const QJsonValue &v : rows) {
        const QJsonObject o = v.toObject();
        AgentConfig a;
        a.id = o.value(u"id"_s).toString();
        a.name = o.value(u"name"_s).toString().simplified().left(80);
        a.command = o.value(u"command"_s).toString().trimmed().left(2000);
        a.tools = o.value(u"tools"_s).toBool(true);
        a.options = o.value(u"options"_s).toObject();
        a.known = o.value(u"known"_s).toArray();
        // An agent on another computer: its address (http(s) only), the agent's id there, the
        // computer's name and notebook folder, and this device's token for it (secrets).
        const QString url = o.value(u"url"_s).toString().trimmed().left(500);
        if (!url.isEmpty()) {
            const QUrl parsed(url, QUrl::StrictMode);
            if (!parsed.isValid() || (parsed.scheme() != u"https" && parsed.scheme() != u"http")
                || parsed.host().isEmpty())
                continue;
            a.url = url;
            a.remote = o.value(u"remote"_s).toString().trimmed().left(40);
            a.host = o.value(u"host"_s).toString().simplified().left(80);
            a.root = o.value(u"root"_s).toString().left(1000);
            if (a.remote.isEmpty())
                continue;
            if (a.host.isEmpty())
                a.host = parsed.host();
            a.token = readSecretFile(acpTokenFileIn(m_dataDir, a.id));
            if (!a.token.isEmpty())
                logging::addSecret(a.token); // (masked in every log line from now on)
        }
        // "Let it change the app": a program of this device, or an agent on another computer whose
        // tools there reach this app through that computer (a grant answered in process).
        a.appAccess = o.value(u"app"_s).toBool(false);
        if (!validAgentId(a.id) || std::ranges::any_of(out, [&a](const AgentConfig &b) { return b.id == a.id; }))
            continue; // (malformed, or the same id twice: the first one)
        if (!WorkspaceAgents::runs(a))
            continue; // (a program on a device that starts none)
        if (a.name.isEmpty())
            a.name = a.id;
        out << a;
        if (out.size() == MaxAcpAgents)
            break;
    }
    return out;
}

namespace {
QJsonArray agentRows(const QList<AgentConfig> &agents)
{
    QJsonArray rows;
    for (const AgentConfig &a : agents)
    {
        QJsonObject row{{u"id"_s, a.id}, {u"name"_s, a.name}, {u"command"_s, a.command}, {u"tools"_s, a.tools}};
        if (a.isRemote()) {
            // (Never the token: it stays in the app's secrets.)
            row.insert(u"url"_s, a.url);
            row.insert(u"remote"_s, a.remote);
            row.insert(u"host"_s, a.host);
            row.insert(u"root"_s, a.root);
        }
        if (!a.options.isEmpty())
            row.insert(u"options"_s, a.options);
        if (!a.known.isEmpty())
            row.insert(u"known"_s, a.known);
        if (a.appAccess)
            row.insert(u"app"_s, true);
        rows.append(row);
    }
    return rows;
}
} // namespace

QVariantList AppSettings::acpAgentRows() const
{
    QVariantList rows;
    for (const AgentConfig &a : acpAgents()) {
        // What the settings' row says of it: its command (the middle left out of a long one) and
        // whether it gets Brulion's tools.
        QString command = a.command;
        if (command.size() > 56)
            command = command.left(24) + u'…' + command.right(31);
        const QString tools = a.tools ? tr("with Brulion's tools") : tr("without Brulion's tools");
        QString summary = command.isEmpty() ? tr("No command yet: Edit… sets it.") : command + u" · "_s + tools;
        if (!command.isEmpty() && a.tools && a.appAccess)
            summary += u" · "_s + tr("may change the app");
        if (a.isRemote())
            summary = a.token.isEmpty()
                              ? tr("On %1 — not paired: remove it and pair again").arg(a.host)
                              : tr("On %1 (%2) · %3").arg(a.host, a.url, tools)
                                        + (a.tools && a.appAccess ? u" · "_s + tr("may change the app") : QString());
        rows.append(QVariantMap{{u"id"_s, a.id},
                                {u"name"_s, a.name},
                                {u"command"_s, a.command},
                                {u"tools"_s, a.tools},
                                {u"app"_s, a.appAccess},
                                {u"remote"_s, a.isRemote()},
                                {u"host"_s, a.host},
                                {u"summary"_s, summary}});
    }
    return rows;
}

bool AppSettings::acpAvailable() const
{
    return WorkspaceAgents::available() || WorkspaceAgents::remoteAvailable();
}

bool AppSettings::acpProgramsAvailable() const
{
    return WorkspaceAgents::available();
}

// ---- The remote MCP server (mcpserversupervisor.h) ----------------------------------------------

bool AppSettings::mcpServerEnabled() const
{
    return m_settings->value(kMcpServerEnabled, false).toBool();
}

void AppSettings::setMcpServerEnabled(bool on)
{
    if (store(kMcpServerEnabled, on, mcpServerEnabled()))
        Q_EMIT mcpServerChanged();
}

int AppSettings::mcpServerPort() const
{
    bool ok = false;
    const int port = m_settings->value(kMcpServerPort, 8780).toInt(&ok);
    return ok && port >= 1024 && port <= 65535 ? port : 8780;
}

void AppSettings::setMcpServerPort(int port)
{
    if (port < 1024 || port > 65535)
        return; // (the field keeps showing the port in use)
    if (store(kMcpServerPort, port, mcpServerPort()))
        Q_EMIT mcpServerChanged();
}

QString AppSettings::mcpServerPublicUrl() const
{
    return m_settings->value(kMcpServerPublicUrl).toString();
}

void AppSettings::setMcpServerPublicUrl(const QString &url)
{
    // An address as typed: the server says what is wrong with one it cannot use.
    QString clean = url.trimmed().left(500);
    while (clean.endsWith(u'/'))
        clean.chop(1);
    if (store(kMcpServerPublicUrl, clean, mcpServerPublicUrl()))
        Q_EMIT mcpServerChanged();
}

bool AppSettings::mcpServerReadOnly() const
{
    return m_settings->value(kMcpServerReadOnly, false).toBool();
}

void AppSettings::setMcpServerReadOnly(bool on)
{
    if (store(kMcpServerReadOnly, on, mcpServerReadOnly()))
        Q_EMIT mcpServerChanged();
}

QString AppSettings::mcpServerPasswordFile() const
{
    return m_dataDir + u"/secrets/mcp-password"_s;
}

bool AppSettings::hasMcpServerPassword() const
{
    return QFileInfo(mcpServerPasswordFile()).size() > 0;
}

bool AppSettings::setMcpServerPassword(const QString &password)
{
    // As `brulion-cli mcp password` takes one (oauth::kMinPasswordLength); kept as typed, spaces
    // too — the sign-in page compares it so.
    if (!password.isEmpty() && (password.size() < 8 || password.size() > 1024))
        return false;
    const bool had = hasMcpServerPassword();
    m_mcpPasswordSecret.reset(password); // masked in every log line from now on
    const bool ok = writeSecretFile(mcpServerPasswordFile(), password); // owner-only; "" removes the file
    if (ok || had != hasMcpServerPassword())
        Q_EMIT mcpServerChanged();
    return ok;
}

QStringList AppSettings::addRemoteAcpAgents(const QList<AgentConfig> &paired)
{
    QList<AgentConfig> agents = acpAgents();
    QStringList ids;
    for (const AgentConfig &r : paired) {
        if (!r.isRemote() || r.remote.isEmpty())
            continue;
        // The same agent of the same computer paired again: its token and what the computer
        // says of itself are new, its name and remembered options stay.
        const auto same = std::ranges::find_if(
                agents, [&r](const AgentConfig &a) { return a.url == r.url && a.remote == r.remote; });
        AgentConfig *a = nullptr;
        if (same != agents.end()) {
            a = &*same;
        } else {
            if (agents.size() >= MaxAcpAgents)
                break;
            AgentConfig fresh;
            const QString host = idPart(r.host);
            const QString base = (host.isEmpty() ? QString() : host + u'-') + idPart(r.remote);
            fresh.id = freeAgentId(base.isEmpty() ? u"agent"_s : base, agents);
            fresh.name = r.name.simplified().left(80);
            if (fresh.name.isEmpty())
                fresh.name = r.remote;
            agents << fresh;
            a = &agents.last();
        }
        a->command = r.command.trimmed().left(2000);
        a->tools = r.tools;
        a->url = r.url;
        a->remote = r.remote;
        a->host = r.host.simplified().left(80);
        a->root = r.root;
        a->token = r.token;
        if (!writeSecretFile(acpTokenFileIn(m_dataDir, a->id), r.token))
            qCWarning(logging::lcApp, "AI agents: the token for %s could not be kept", qUtf8Printable(a->id));
        logging::addSecret(r.token);
        ids << a->id;
    }
    if (!ids.isEmpty())
        storeAcpAgents(agents);
    return ids;
}

void AppSettings::storeAcpAgents(const QList<AgentConfig> &agents)
{
    const QString value = QString::fromUtf8(QJsonDocument(agentRows(agents)).toJson(QJsonDocument::Compact));
    if (store(kAcpAgents, value, m_settings->value(kAcpAgents).toString()))
        Q_EMIT acpAgentsChanged();
}

QString AppSettings::addAcpAgent(const QString &preset)
{
    QList<AgentConfig> agents = acpAgents();
    if (!WorkspaceAgents::available() || agents.size() >= MaxAcpAgents)
        return {};
    AgentConfig a;
    const QList<AgentConfig> presets = WorkspaceAgents::presets();
    const auto known = std::ranges::find_if(presets, [&preset](const AgentConfig &p) { return p.id == preset; });
    if (known != presets.end()) {
        a = *known;
    } else {
        a.id = u"agent"_s;
        a.name = tr("My agent");
    }
    a.id = freeAgentId(a.id, agents);
    agents << a;
    storeAcpAgents(agents);
    return a.id;
}

void AppSettings::setAcpAgentName(const QString &id, const QString &name)
{
    QList<AgentConfig> agents = acpAgents();
    for (AgentConfig &a : agents) {
        if (a.id == id && !name.simplified().isEmpty())
            a.name = name.simplified().left(80);
    }
    storeAcpAgents(agents);
}

void AppSettings::setAcpAgentCommand(const QString &id, const QString &command)
{
    QList<AgentConfig> agents = acpAgents();
    for (AgentConfig &a : agents) {
        if (a.id == id)
            a.command = command.trimmed().left(2000);
    }
    storeAcpAgents(agents);
}

void AppSettings::setAcpAgentTools(const QString &id, bool on)
{
    QList<AgentConfig> agents = acpAgents();
    for (AgentConfig &a : agents) {
        if (a.id == id)
            a.tools = on;
    }
    storeAcpAgents(agents);
}

void AppSettings::setAcpAgentApp(const QString &id, bool on)
{
    QList<AgentConfig> agents = acpAgents();
    for (AgentConfig &a : agents) {
        if (a.id == id)
            a.appAccess = on;
    }
    storeAcpAgents(agents);
}

void AppSettings::setAcpAgentMemory(const QString &id, const QJsonObject &options, const QJsonArray &known)
{
    QList<AgentConfig> agents = acpAgents();
    for (AgentConfig &a : agents) {
        if (a.id == id) {
            a.options = options;
            a.known = known;
        }
    }
    storeAcpAgents(agents);
}

void AppSettings::removeAcpAgent(const QString &id)
{
    QList<AgentConfig> agents = acpAgents();
    agents.removeIf([&id](const AgentConfig &a) { return a.id == id; });
    if (validAgentId(id))
        writeSecretFile(acpTokenFileIn(m_dataDir, id), QString()); // (its token, if it had one)
    storeAcpAgents(agents);
}

QVariantList AppSettings::acpPresets() const
{
    QVariantList out;
    if (!WorkspaceAgents::available())
        return out; // (no programs start here: only an agent on another computer)
    for (const AgentConfig &p : WorkspaceAgents::presets())
        out.append(QVariantMap{{u"id"_s, p.id}, {u"name"_s, p.name}, {u"command"_s, p.command}});
    return out;
}

QString AppSettings::aiKey() const
{
    QFile in(aiKeyFileIn(m_dataDir));
    const QString key = in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()).trimmed() : QString();
    m_aiKeySecret.reset(key);
    return key;
}

bool AppSettings::captureButtonShown() const
{
    return m_settings->value(kCaptureButton, true).toBool();
}

void AppSettings::setCaptureButtonShown(bool on)
{
    if (store(kCaptureButton, on, captureButtonShown()))
        Q_EMIT captureButtonShownChanged();
}

bool AppSettings::railExpands() const
{
    return m_settings->value(kRailExpands, true).toBool();
}

void AppSettings::setRailExpands(bool on)
{
    if (store(kRailExpands, on, railExpands()))
        Q_EMIT railExpandsChanged();
}

void AppSettings::resetAppearance()
{
    setAccentColor(u"coral"_s);
    setLightSurface(u"paper"_s);
    setDarkSurface(u"graphite"_s);
    setUiFontFamily(QString());
    setDensity(u"comfortable"_s);
    setFontScale(1.0);
    setInterfaceScale(0.0);
    setEditorFont(u"sans"_s);
    setEditorFontFamily(QString());
    setEditorMonoFamily(QString());
    setEditorFontSize(16);
    setEditorLineSpacing(u"normal"_s);
}

// ---- Editor ---------------------------------------------------------------------------------

QString AppSettings::editorFont() const
{
    return oneOf(m_settings->value(kEditorFont).toString(), kEditorFontChoices);
}

void AppSettings::setEditorFont(const QString &font)
{
    if (store(kEditorFont, oneOf(font, kEditorFontChoices), editorFont()))
        Q_EMIT editorFontChanged();
}

QString AppSettings::editorFontFamily() const
{
    return cleanFontFamily(m_settings->value(kEditorFontFamily).toString());
}

void AppSettings::setEditorFontFamily(const QString &family)
{
    if (store(kEditorFontFamily, cleanFontFamily(family), editorFontFamily()))
        Q_EMIT fontFamiliesChanged();
}

QString AppSettings::editorMonoFamily() const
{
    return cleanFontFamily(m_settings->value(kEditorMonoFamily).toString());
}

void AppSettings::setEditorMonoFamily(const QString &family)
{
    if (store(kEditorMonoFamily, cleanFontFamily(family), editorMonoFamily()))
        Q_EMIT fontFamiliesChanged();
}

QString AppSettings::editorLineSpacing() const
{
    return oneOf(m_settings->value(kEditorLineSpacing).toString(), kLineSpacingChoices);
}

void AppSettings::setEditorLineSpacing(const QString &spacing)
{
    if (store(kEditorLineSpacing, oneOf(spacing, kLineSpacingChoices), editorLineSpacing()))
        Q_EMIT editorLineSpacingChanged();
}

int AppSettings::editorFontSize() const
{
    bool ok = false;
    const int px = m_settings->value(kEditorFontSize).toInt(&ok);
    return ok ? std::clamp(px, 12, 28) : 16;
}

void AppSettings::setEditorFontSize(int px)
{
    if (store(kEditorFontSize, std::clamp(px, 12, 28), editorFontSize()))
        Q_EMIT editorFontSizeChanged();
}

int AppSettings::editorWidth() const
{
    bool ok = false;
    const int px = m_settings->value(kEditorWidth).toInt(&ok);
    if (!ok)
        return 680;
    // The presets before 0.6.0 (Narrow 640, Normal 760) read as today's (600, 680), so the
    // setting shows the choice made; any other width stays as stored.
    if (px == 760)
        return 680;
    if (px == 640)
        return 600;
    return px <= 0 ? 0 : std::clamp(px, 480, 1600);
}

void AppSettings::setEditorWidth(int px)
{
    const int v = px <= 0 ? 0 : std::clamp(px, 480, 1600);
    if (store(kEditorWidth, v, editorWidth()))
        Q_EMIT editorWidthChanged();
}

bool AppSettings::editorSourceMode() const
{
    return m_settings->value(kEditorSource, false).toBool();
}

void AppSettings::setEditorSourceMode(bool on)
{
    if (store(kEditorSource, on, editorSourceMode()))
        Q_EMIT editorSourceModeChanged();
}

bool AppSettings::propertiesExpanded() const
{
    return m_settings->value(kPropertiesExpanded, false).toBool();
}

void AppSettings::setPropertiesExpanded(bool on)
{
    if (store(kPropertiesExpanded, on, propertiesExpanded()))
        Q_EMIT propertiesExpandedChanged();
}

bool AppSettings::webPicturesShown() const
{
    return m_settings->value(kWebPictures, false).toBool();
}

void AppSettings::setWebPicturesShown(bool on)
{
    if (store(kWebPictures, on, webPicturesShown()))
        Q_EMIT webPicturesShownChanged();
}

bool AppSettings::pictureFrames() const
{
    return m_settings->value(kPictureFrames, false).toBool();
}

void AppSettings::setPictureFrames(bool on)
{
    if (store(kPictureFrames, on, pictureFrames()))
        Q_EMIT pictureFramesChanged();
}

bool AppSettings::lightPictureBacking() const
{
    return m_settings->value(kLightPictureBacking, true).toBool();
}

void AppSettings::setLightPictureBacking(bool on)
{
    if (store(kLightPictureBacking, on, lightPictureBacking()))
        Q_EMIT lightPictureBackingChanged();
}

bool AppSettings::codeLineNumbers() const
{
    return m_settings->value(kCodeLineNumbers, false).toBool();
}

void AppSettings::setCodeLineNumbers(bool on)
{
    if (store(kCodeLineNumbers, on, codeLineNumbers()))
        Q_EMIT codeLineNumbersChanged();
}

bool AppSettings::spellCheck() const
{
    return m_settings->value(kSpellCheck, true).toBool();
}

void AppSettings::setSpellCheck(bool on)
{
    if (store(kSpellCheck, on, spellCheck()))
        Q_EMIT spellCheckChanged();
}

bool AppSettings::attachmentSearch() const
{
    return m_settings->value(kAttachmentSearch, true).toBool();
}

void AppSettings::setAttachmentSearch(bool on)
{
    if (store(kAttachmentSearch, on, attachmentSearch()))
        Q_EMIT attachmentSearchChanged();
}

bool AppSettings::attachmentOcr() const
{
    return m_settings->value(kAttachmentOcr, false).toBool();
}

void AppSettings::setAttachmentOcr(bool on)
{
    if (store(kAttachmentOcr, on, attachmentOcr()))
        Q_EMIT attachmentSearchChanged();
}

QString AppSettings::attachmentOcrLanguages() const
{
    const QString value = m_settings->value(kAttachmentOcrLanguages).toString().trimmed();
    return value.isEmpty() ? u"auto"_s : value;
}

void AppSettings::setAttachmentOcrLanguages(const QString &languages)
{
    const QString value = languages.trimmed().isEmpty() ? u"auto"_s : languages.trimmed().left(200);
    if (store(kAttachmentOcrLanguages, value, attachmentOcrLanguages()))
        Q_EMIT attachmentSearchChanged();
}

bool AppSettings::taskStatusHistory() const
{
    return m_settings->value(kTaskStatusHistory, false).toBool();
}

void AppSettings::setTaskStatusHistory(bool on)
{
    if (store(kTaskStatusHistory, on, taskStatusHistory()))
        Q_EMIT taskStatusHistoryChanged();
}

QString AppSettings::spellLanguages() const
{
    return oneOf(m_settings->value(kSpellLanguages).toString(), kSpellLanguageChoices);
}

void AppSettings::setSpellLanguages(const QString &languages)
{
    if (store(kSpellLanguages, oneOf(languages, kSpellLanguageChoices), spellLanguages()))
        Q_EMIT spellLanguagesChanged();
}

bool AppSettings::spellWholePage() const
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    constexpr bool byDefault = false;
#else
    constexpr bool byDefault = true;
#endif
    return m_settings->value(kSpellWholePage, byDefault).toBool();
}

void AppSettings::setSpellWholePage(bool on)
{
    if (store(kSpellWholePage, on, spellWholePage()))
        Q_EMIT spellWholePageChanged();
}

QStringList AppSettings::spellingWords() const
{
    return storedList(m_settings->value(kSpellingWords));
}

void AppSettings::setSpellingWords(const QStringList &words)
{
    QStringList clean;
    for (const QString &w : words) {
        const QString t = w.trimmed();
        // Single words: no line breaks, at most 100 characters.
        if (!t.isEmpty() && t.size() <= 100 && !t.contains(u'\n'))
            clean.append(t);
    }
    clean.removeDuplicates();
    if (store(kSpellingWords, clean, spellingWords()))
        Q_EMIT spellingWordsChanged();
}

QString AppSettings::exportWebPictures() const
{
    return oneOf(m_settings->value(kExportWebPictures).toString(), kWebPictureExportChoices);
}

void AppSettings::setExportWebPictures(const QString &mode)
{
    if (store(kExportWebPictures, oneOf(mode, kWebPictureExportChoices), exportWebPictures()))
        Q_EMIT exportWebPicturesChanged();
}

bool AppSettings::wordCountShown() const
{
    return value(kWordCount, true).toBool(); // (not a bool: the default)
}

void AppSettings::setWordCountShown(bool on)
{
    if (store(kWordCount, on, wordCountShown()))
        Q_EMIT wordCountShownChanged();
}

bool AppSettings::focusMode() const
{
    return value(kFocusMode, false).toBool(); // (not a bool: the default)
}

void AppSettings::setFocusMode(bool on)
{
    if (store(kFocusMode, on, focusMode()))
        Q_EMIT focusModeChanged();
}

bool AppSettings::readingMode() const
{
    return value(kReadingMode, false).toBool(); // (not a bool: the default)
}

void AppSettings::setReadingMode(bool on)
{
    if (store(kReadingMode, on, readingMode()))
        Q_EMIT readingModeChanged();
}

bool AppSettings::typewriterScrolling() const
{
    return value(kTypewriter, false).toBool(); // (not a bool: the default)
}

void AppSettings::setTypewriterScrolling(bool on)
{
    if (store(kTypewriter, on, typewriterScrolling()))
        Q_EMIT typewriterScrollingChanged();
}

bool AppSettings::vimMode() const
{
    return value(kVimMode, false).toBool();
}

void AppSettings::setVimMode(bool on)
{
    if (store(kVimMode, on, vimMode()))
        Q_EMIT vimModeChanged();
}

// ---- Keyboard shortcuts (ShortcutMap) -------------------------------------------------------

QVariantMap AppSettings::shortcutOverrides() const
{
    QVariantMap out;
    m_settings->beginGroup(kShortcuts);
    const QStringList ids = m_settings->childKeys();
    for (const QString &id : ids)
        out.insert(id, m_settings->value(id));
    m_settings->endGroup();
    return out;
}

void AppSettings::setShortcutOverride(const QString &id, const std::optional<QString> &sequence)
{
    // (An id never holds a '/': it would be a group of its own.)
    if (id.isEmpty() || id.contains(u'/') || id.contains(u'\\'))
        return;
    const QString k = kShortcuts + u'/' + id;
    if (sequence)
        m_settings->setValue(k, *sequence);
    else
        m_settings->remove(k);
}

void AppSettings::clearShortcutOverrides()
{
    m_settings->remove(kShortcuts);
}

// ---- Vaults ---------------------------------------------------------------------------------

QString AppSettings::currentVault() const
{
    return m_settings->value(kCurrentVault).toString();
}

void AppSettings::setCurrentVault(const QString &path)
{
    adoptNotebookKeys(path);
    if (store(kCurrentVault, cleanPath(path), currentVault())) {
        Q_EMIT currentVaultChanged();
        Q_EMIT nextcloudChanged(); // the sync switch and folder belong to the notebook
        Q_EMIT deviceFolderChanged();
    }
}

// ---- A folder of this device ------------------------------------------------------------------

AppSettings::DeviceFolderLink AppSettings::deviceFolderLink(const QString &notebook) const
{
    const QString id = notebookId(notebook);
    if (id.isEmpty())
        return {};
    const QString group = kDeviceFolders + u'/' + id + u'/';
    DeviceFolderLink link;
    link.address = m_settings->value(group + u"address"_s).toString();
    if (link.address.isEmpty())
        return {};
    link.name = m_settings->value(group + u"name"_s).toString();
    link.where = m_settings->value(group + u"where"_s).toString();
    return link;
}

void AppSettings::setDeviceFolderLink(const QString &notebook, const DeviceFolderLink &link)
{
    const QString id = notebookId(notebook);
    if (id.isEmpty() || link == deviceFolderLink(notebook))
        return;
    const QString group = kDeviceFolders + u'/' + id;
    if (link.isEmpty()) {
        m_settings->remove(group);
    } else {
        m_settings->setValue(group + u"/address"_s, link.address);
        m_settings->setValue(group + u"/name"_s, link.name);
        m_settings->setValue(group + u"/where"_s, link.where);
        m_settings->setValue(group + u"/path"_s, absolutePath(notebook));
    }
    m_settings->sync();
    Q_EMIT deviceFolderChanged();
}

QString AppSettings::deviceFolderPlace(const QString &where)
{
    // The phone's storage and memory cards name a folder "<volume>:<path>" (TreeFolder.kt).
    static const QRegularExpression card(u"^[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}:(.*)$"_s);
    if (where.startsWith(u"primary:"_s))
        return tr("Phone storage › %1").arg(where.mid(8));
    if (const QRegularExpressionMatch m = card.match(where); m.hasMatch())
        return tr("Memory card › %1").arg(m.captured(1));
    return where;
}

QString AppSettings::deviceFolderNotebook(const QString &address) const
{
    if (address.isEmpty())
        return {};
    m_settings->beginGroup(kDeviceFolders);
    const QStringList ids = m_settings->childGroups();
    QString found;
    for (const QString &id : ids) {
        if (m_settings->value(id + u"/address"_s).toString() == address) {
            found = m_settings->value(id + u"/path"_s).toString();
            break;
        }
    }
    m_settings->endGroup();
    return found;
}

QVariantList AppSettings::recentVaults() const
{
    QVariantList out;
    const QVariantList stored = m_settings->value(kRecentVaults).toList();
    for (const QVariant &v : stored) {
        const QVariantMap m = v.toMap();
        const QString path = m.value(u"path"_s).toString();
        if (!path.isEmpty())
            out.append(recentEntry(path, m.value(u"name"_s).toString(), m.value(u"lastOpened"_s).toString()));
    }
    return out;
}

void AppSettings::addRecentVault(const QString &rawPath, const QDateTime &when)
{
    const QString path = cleanPath(rawPath);
    if (path.isEmpty())
        return;
    QVariantList list = recentVaults();
    removeRecent(list, path);
    const QDateTime opened = when.isValid() ? when : QDateTime::currentDateTime();
    list.prepend(recentEntry(path, {}, opened.toString(Qt::ISODate)));
    m_settings->setValue(kRecentVaults, list.mid(0, MaxRecentVaults));
    Q_EMIT recentVaultsChanged();
    setCurrentVault(path);
}

void AppSettings::forgetRecentVault(const QString &rawPath)
{
    QVariantList list = recentVaults();
    if (removeRecent(list, cleanPath(rawPath)) == 0)
        return;
    m_settings->setValue(kRecentVaults, list);
    Q_EMIT recentVaultsChanged();
}

bool AppSettings::followsOutsideLinks() const
{
    return m_settings->value(kFollowOutsideLinks, false).toBool();
}

void AppSettings::setFollowsOutsideLinks(bool on)
{
    if (store(kFollowOutsideLinks, on, followsOutsideLinks()))
        Q_EMIT followsOutsideLinksChanged();
}

void AppSettings::checkOutsideLinksOnce(const QString &notebook, bool knownBefore, bool outsideLinks)
{
    const QString id = notebookId(notebook);
    if (id.isEmpty() || m_settings->contains(kFollowOutsideLinks))
        return; // chosen here, by the user or by an earlier check
    const QString checked = kOutsideLinksChecked + u'/' + id;
    if (m_settings->value(checked, false).toBool())
        return;
    m_settings->setValue(checked, true);
    const QVariantList recent = recentVaults(); // before this open adds it
    const bool recentHere = std::ranges::any_of(recent, [&id](const QVariant &entry) {
        return notebookId(entry.toMap().value(u"path"_s).toString()) == id;
    });
    if (outsideLinks && (knownBefore || recentHere))
        setFollowsOutsideLinks(true);
}

// ---- Application ----------------------------------------------------------------------------

bool AppSettings::closeToTray() const
{
    return m_settings->value(kCloseToTray, true).toBool();
}

void AppSettings::setCloseToTray(bool on)
{
    if (store(kCloseToTray, on, closeToTray()))
        Q_EMIT closeToTrayChanged();
}

QString AppSettings::defaultTrayClick()
{
    return QString::fromUtf16(kTrayClickChoices.front());
}

QString AppSettings::trayClick() const
{
    return oneOf(m_settings->value(kTrayClick).toString(), kTrayClickChoices);
}

void AppSettings::setTrayClick(const QString &action)
{
    if (store(kTrayClick, oneOf(action, kTrayClickChoices), trayClick()))
        Q_EMIT trayClickChanged();
}

// ---- Intro ----------------------------------------------------------------------------------

bool AppSettings::introDone() const
{
    // Brulion was used before the intro existed: an update never shows it.
    const bool usedBefore = m_settings->contains(kCurrentVault) || !recentVaults().isEmpty();
    return m_settings->value(kIntroDone, usedBefore).toBool();
}

void AppSettings::setIntroDone(bool done)
{
    if (store(kIntroDone, done, introDone()))
        Q_EMIT introDoneChanged();
}

QVariantMap AppSettings::newNotebookConfig() const
{
    return cleanNotebookConfig(m_settings->value(kNewNotebookConfig).toMap());
}

void AppSettings::setNewNotebookConfig(const QVariantMap &config)
{
    const QVariantMap clean = cleanNotebookConfig(config);
    if (clean == newNotebookConfig())
        return;
    if (clean.isEmpty())
        m_settings->remove(kNewNotebookConfig);
    else
        m_settings->setValue(kNewNotebookConfig, clean);
    Q_EMIT newNotebookConfigChanged();
}

// ---- Reminders ------------------------------------------------------------------------------

bool AppSettings::remindersEnabled() const
{
    return m_settings->value(kRemindersEnabled, true).toBool();
}

void AppSettings::setRemindersEnabled(bool on)
{
    if (store(kRemindersEnabled, on, remindersEnabled()))
        Q_EMIT remindersEnabledChanged();
}

int AppSettings::snoozeMinutes() const
{
    bool ok = false;
    const int v = m_settings->value(kSnooze).toInt(&ok);
    return ok ? std::clamp(v, 1, 120) : 10;
}

void AppSettings::setSnoozeMinutes(int minutes)
{
    if (store(kSnooze, std::clamp(minutes, 1, 120), snoozeMinutes()))
        Q_EMIT snoozeMinutesChanged();
}

int AppSettings::alarmRingMinutes() const
{
    bool ok = false;
    const int v = m_settings->value(kAlarmRing).toInt(&ok);
    return ok ? std::clamp(v, 1, 60) : 10;
}

void AppSettings::setAlarmRingMinutes(int minutes)
{
    if (store(kAlarmRing, std::clamp(minutes, 1, 60), alarmRingMinutes()))
        Q_EMIT alarmRingMinutesChanged();
}

bool AppSettings::notifyMissed() const
{
    return m_settings->value(kNotifyMissed, true).toBool();
}

void AppSettings::setNotifyMissed(bool on)
{
    if (store(kNotifyMissed, on, notifyMissed()))
        Q_EMIT notifyMissedChanged();
}

// ---- Nextcloud ------------------------------------------------------------------------------

// Whether to sync and with which server folder is a property of the notebook: one folder on
// the server holds one notebook, so a second notebook must never inherit it (its notes would
// flow into the first one). The server and the account are shared.

QString AppSettings::notebookGroup(const QString &notebook)
{
    const QString id = notebookId(notebook);
    if (!id.isEmpty()) {
        adoptNotebookKeys(notebook);
        recordPath(*m_settings, id, notebook);
    }
    return id;
}

template<typename T>
bool AppSettings::storeNotebookValue(const QString &notebook, QStringView name, const T &value, const T &current)
{
    const QString id = notebookGroup(notebook);
    return !id.isEmpty() && store(groupKey(id, name), value, current); // no notebook open: nothing to set
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
    if (const QString id = notebookGroup(notebook); !id.isEmpty())
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
        const QString from = kNcNotebooks + u'/' + legacy;
        m_settings->beginGroup(from);
        const QStringList keys = m_settings->childKeys();
        m_settings->endGroup();
        for (const QString &k : keys) {
            const QString to = groupKey(id, k);
            if (!m_settings->contains(to))
                m_settings->setValue(to, m_settings->value(from + u'/' + k));
        }
        has = !keys.isEmpty();
        if (has)
            m_settings->remove(from);
    }
    if (!has) {
        m_settings->beginGroup(kNcNotebooks + u'/' + id);
        has = !m_settings->childKeys().isEmpty();
        m_settings->endGroup();
    }
    if (has)
        recordPath(*m_settings, id, clean);
}

QList<AppSettings::NotebookSync> AppSettings::notebookSyncs() const
{
    // A group without its path (0.3.1–0.3.5 wrote none) is found among the recent notebooks.
    QHash<QString, QString> recent;
    for (const QVariantList vaults = recentVaults(); const QVariant &v : vaults) {
        const QString path = v.toMap().value(u"path"_s).toString();
        recent.insert(legacyNotebookId(path), path);
        recent.insert(notebookId(path), path);
    }
    m_settings->beginGroup(kNcNotebooks);
    const QStringList ids = m_settings->childGroups();
    m_settings->endGroup();
    QList<NotebookSync> out;
    out.reserve(ids.size());
    for (const QString &id : ids) {
        const QString path = m_settings->value(groupKey(id, u"path")).toString();
        const QString folderKey = groupKey(id, u"folder");
        out.append(NotebookSync{.id = id,
                                .path = path.isEmpty() ? recent.value(id) : path,
                                .enabled = m_settings->value(groupKey(id, u"enabled"), false).toBool(),
                                .hasFolder = m_settings->contains(folderKey),
                                .folder = m_settings->value(folderKey, kDefaultServerFolder).toString()});
    }
    return out;
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
    return m_settings->value(kNcUrl).toString();
}

void AppSettings::setNextcloudUrl(const QString &url)
{
    QString v = url.trimmed();
    while (v.endsWith(u'/'))
        v.chop(1);
    if (store(kNcUrl, v, nextcloudUrl()))
        Q_EMIT nextcloudChanged();
}

QString AppSettings::nextcloudUser() const
{
    return m_settings->value(kNcUser).toString();
}

void AppSettings::setNextcloudUser(const QString &user)
{
    if (store(kNcUser, user.trimmed(), nextcloudUser()))
        Q_EMIT nextcloudChanged();
}

QString AppSettings::nextcloudFolder() const
{
    const QString k = notebookKey(currentVault(), u"folder");
    return k.isEmpty() ? QString(kDefaultServerFolder) : m_settings->value(k, kDefaultServerFolder).toString();
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

bool AppSettings::hasOwnNextcloudFolder() const
{
    return hasNotebookValue(currentVault(), u"folder");
}

QString AppSettings::proposedNextcloudFolder() const
{
    // Syncing without a folder of its own (settings of 0.3.1–0.3.5): the folder it syncs with.
    if (hasOwnNextcloudFolder() || nextcloudEnabled())
        return nextcloudFolder();
    const QString notebook = currentVault();
    QString base;
    if (!nextcloudUrl().isEmpty() && !cleanPath(notebook).isEmpty())
        base = serverFolderFromName(QFileInfo(absolutePath(notebook)).fileName());
    if (base.isEmpty())
        base = QString::fromLatin1(NewAccountFolder);
    // Folders given to other notebooks (synced or not) and the one of 0.3.0 waiting for its notebook.
    const QString id = notebookId(notebook);
    const QString legacy = legacyNotebookId(notebook);
    QStringList taken;
    for (const QList<NotebookSync> syncs = notebookSyncs(); const NotebookSync &n : syncs) {
        if (n.id != id && n.id != legacy && (n.hasFolder || n.enabled))
            taken.append(n.folder);
    }
    if (hasSharedNextcloud())
        taken.append(m_settings->value(kNcSharedFolder, kDefaultServerFolder).toString());
    QString candidate = base;
    // (Not "%1 %2".arg(base).arg(i): a "%2" in the folder's name would be replaced as well.)
    for (int i = 2;
         std::any_of(taken.cbegin(), taken.cend(), [&](const QString &f) { return foldersOverlap(f, candidate); }); ++i)
        candidate = base + u' ' + QString::number(i);
    return candidate;
}

QString AppSettings::nextcloudFolderUser(const QString &folder) const
{
    return nextcloudFolderUser(folder, currentVault());
}

QString AppSettings::nextcloudFolderUser(const QString &folder, const QString &except) const
{
    const QString exceptId = notebookId(except);
    const QString exceptLegacy = legacyNotebookId(except);
    for (const QList<NotebookSync> syncs = notebookSyncs(); const NotebookSync &n : syncs) {
        if (!n.enabled || n.path.isEmpty() || n.id == exceptId || n.id == exceptLegacy)
            continue;
        // A notebook whose folder is gone (moved, deleted) syncs nothing any more; the same folder
        // reached through another path is the notebook itself.
        if (!QFileInfo(n.path).isDir() || notebookId(n.path) == exceptId)
            continue;
        if (foldersOverlap(n.folder, folder))
            return n.path;
    }
    return {};
}

void AppSettings::disableNextcloudEverywhere(bool exceptCurrent)
{
    const bool wasOn = nextcloudEnabled();
    const QString current = currentVault();
    const QString currentId = notebookId(current);
    const QString currentLegacy = legacyNotebookId(current);
    for (const QList<NotebookSync> syncs = notebookSyncs(); const NotebookSync &n : syncs) {
        const bool isCurrent = !currentId.isEmpty() && (n.id == currentId || n.id == currentLegacy);
        if (n.enabled && !(exceptCurrent && isCurrent))
            m_settings->setValue(groupKey(n.id, u"enabled"), false);
    }
    if (m_settings->value(kNcSharedEnabled, false).toBool())
        m_settings->setValue(kNcSharedEnabled, false); // its folder still goes to its notebook
    if (wasOn != nextcloudEnabled())
        Q_EMIT nextcloudChanged();
}

bool AppSettings::hasSharedNextcloud() const
{
    return m_settings->contains(kNcSharedEnabled) || m_settings->contains(kNcSharedFolder);
}

void AppSettings::recordSharedNextcloudOwner(const QString &notebook)
{
    if (!hasSharedNextcloud() || m_settings->contains(kNcSharedOwner))
        return;
    m_settings->setValue(kNcSharedOwner, cleanPath(notebook));
}

bool AppSettings::claimSharedNextcloud(const QString &notebook, const QString &syncedRemote)
{
    // Not "the first notebook opened": that may be another one (the synced one is on an
    // unmounted card, the user opened the documentation and then created a notebook), which
    // would then sync with the old folder and mix both notebooks there.
    if (!hasSharedNextcloud())
        return false;
    const QString folder = m_settings->value(kNcSharedFolder, kDefaultServerFolder).toString();
    const bool synced =
            !syncedRemote.isEmpty() && syncedRemote == nextcloudRemote(nextcloudUrl(), nextcloudUser(), folder);
    const QString owner = m_settings->value(kNcSharedOwner).toString();
    if (!synced && (owner.isEmpty() || notebookId(owner) != notebookId(notebook)))
        return false;
    if (!hasNotebookValue(notebook, u"enabled")) {
        const bool on =
                m_settings->value(kNcSharedEnabled, false).toBool() && nextcloudFolderUser(folder, notebook).isEmpty();
        setNotebookValue(notebook, u"enabled", on);
    }
    // Always written: a notebook without a folder of its own would be offered another one.
    if (!hasNotebookValue(notebook, u"folder"))
        setNotebookValue(notebook, u"folder", folder);
    m_settings->remove(kNcSharedEnabled);
    m_settings->remove(kNcSharedFolder);
    m_settings->remove(kNcSharedOwner);
    return true;
}

void AppSettings::notebookOpened(const QString &notebook, const QString &syncedRemote)
{
    const QString id = notebookId(notebook);
    if (id.isEmpty())
        return;
    adoptNotebookKeys(notebook);
    bool changed = claimSharedNextcloud(notebook, syncedRemote);
    // Off and without a folder of its own, yet synced with a folder of this account before (up
    // to 0.3.0, or its settings went to another notebook in 0.3.1–0.3.5): that folder stays its
    // folder, so turning sync on again continues there instead of starting over elsewhere.
    if (!hasNotebookValue(notebook, u"folder") && !hasNotebookValue(notebook, u"enabled")) {
        const QString folder = nextcloudRemoteFolder(nextcloudUrl(), nextcloudUser(), syncedRemote);
        if (!folder.isEmpty()) {
            setNotebookValue(notebook, u"folder", folder);
            changed = true;
        }
    }
    if (changed && notebookId(currentVault()) == id)
        Q_EMIT nextcloudChanged();
}

int AppSettings::syncIntervalMinutes() const
{
    bool ok = false;
    const int v = m_settings->value(kSyncInterval).toInt(&ok);
    return ok ? std::clamp(v, 0, 24 * 60) : 15;
}

void AppSettings::setSyncIntervalMinutes(int minutes)
{
    if (store(kSyncInterval, std::clamp(minutes, 0, 24 * 60), syncIntervalMinutes()))
        Q_EMIT nextcloudChanged();
}

QString AppSettings::passwordFile() const
{
    return passwordFileIn(m_dataDir);
}

QString AppSettings::storedNextcloudPassword(const QString &dataDir)
{
    QFile in(passwordFileIn(dataDir));
    return in.open(QIODevice::ReadOnly) ? QString::fromUtf8(in.readAll()) : QString();
}

bool AppSettings::hasNextcloudPassword() const
{
    return QFileInfo(passwordFile()).size() > 0;
}

bool AppSettings::setNextcloudPassword(const QString &password)
{
    const bool had = hasNextcloudPassword();
    // Masked in every log line from now on, also when it cannot be stored ("" masks nothing).
    m_passwordSecret.reset(password);
    const bool ok = writeSecretFile(passwordFile(), password); // owner-only; "" removes the file
    // Forgetting it is a change when there was one, also when the file could not be removed.
    if (password.isEmpty() ? had : ok)
        Q_EMIT nextcloudPasswordChanged();
    return ok;
}

QString AppSettings::nextcloudPassword() const
{
    const QString password = storedNextcloudPassword(m_dataDir);
    m_passwordSecret.reset(password);
    return password;
}

// ---- Generic --------------------------------------------------------------------------------

QVariant AppSettings::value(const QString &k, const QVariant &defaultValue) const
{
    QVariant v = m_settings->value(k, defaultValue);
    // The INI file keeps only text: after a restart a stored bool or number comes back as a
    // string. Give it the default's type again (QML compares with === and computes with it);
    // a stored value that does not convert (a number out of the type's range too) yields the
    // default.
    const int type = defaultValue.typeId();
    if (defaultValue.isNull() || v.typeId() != QMetaType::QString || type == QMetaType::QString)
        return v;
    const QString text = v.toString().trimmed();
    bool ok = false;
    switch (type) {
    case QMetaType::Bool:
        if (text == u"true" || text == u"1")
            return true;
        return text == u"false" || text == u"0" ? QVariant(false) : defaultValue;
    case QMetaType::Int: v = text.toInt(&ok); break;
    case QMetaType::LongLong: v = text.toLongLong(&ok); break;
    case QMetaType::Double: v = text.toDouble(&ok); break;
    default: ok = v.convert(defaultValue.metaType());
    }
    return ok ? v : defaultValue;
}

void AppSettings::setValue(const QString &k, const QVariant &value)
{
    if (m_settings->contains(k) && m_settings->value(k) == value)
        return;
    m_settings->setValue(k, value);
    Q_EMIT valueChanged(k);
}

QStringList AppSettings::recentSearches() const
{
    const QVariant saved = m_settings->value(kRecentSearches);
    if (saved.typeId() == QMetaType::QVariantList || saved.typeId() == QMetaType::QStringList)
        return saved.toStringList(); // (each entry as text)
    const QString one = saved.toString();
    return one.isEmpty() ? QStringList() : QStringList{one};
}

QStringList AppSettings::rememberSearch(const QString &query)
{
    const QString q = query.trimmed();
    if (q.isEmpty())
        return recentSearches();
    const QString folded = text::foldKey(q);
    QStringList list{q};
    for (const QStringList recent = recentSearches(); const QString &r : recent) {
        if (list.size() >= kMaxRecentSearches)
            break;
        if (text::foldKey(r) != folded)
            list.append(r);
    }
    setValue(kRecentSearches, list);
    return list;
}

void AppSettings::clearRecentSearches()
{
    setValue(kRecentSearches, QStringList());
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
