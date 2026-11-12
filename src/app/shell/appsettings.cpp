#include "appsettings.h"

#include "workspacegithub.h"
#include "workspacesync.h"

#include "notebook/journals.h"
#include "text/text.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>

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
constexpr auto kGym = "extensions/silownia"_L1;
constexpr auto kAssistant = "extensions/asystent"_L1;
constexpr auto kAiProvider = "ai/provider"_L1;
constexpr auto kAiModel = "ai/model"_L1;
constexpr auto kAiBaseUrl = "ai/baseUrl"_L1;
constexpr auto kEditorFont = "editor/font"_L1;
constexpr auto kEditorFontFamily = "editor/fontFamily"_L1;
constexpr auto kEditorMonoFamily = "editor/monoFamily"_L1;
constexpr auto kEditorLineSpacing = "editor/lineSpacing"_L1;
constexpr auto kEditorFontSize = "editor/fontSize"_L1;
constexpr auto kEditorWidth = "editor/maxWidth"_L1;
constexpr auto kEditorSource = "editor/sourceMode"_L1;
constexpr auto kPropertiesExpanded = "editor/propertiesExpanded"_L1;
constexpr auto kWebPictures = "editor/webPicturesShown"_L1;
constexpr auto kSpellCheck = "spelling/enabled"_L1;
constexpr auto kTaskStatusHistory = "tasks/statusHistory"_L1;
constexpr auto kSpellLanguages = "spelling/languages"_L1;
constexpr auto kSpellingWords = "spelling/words"_L1;
constexpr auto kExportWebPictures = "export/webPictures"_L1;
constexpr auto kWordCount = "editor/wordCountShown"_L1;
constexpr auto kFocusMode = "ui/focusMode"_L1;
constexpr auto kReadingMode = "ui/readingMode"_L1;
constexpr auto kTypewriter = "editor/typewriterScrolling"_L1;
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
constexpr auto kRecentSearches = "search/recent"_L1;
/// The server folder of a notebook without one (WorkspaceSync syncs with it then).
constexpr auto kDefaultServerFolder = "Brulion"_L1;
/// SearchView's recent searches kept.
constexpr qsizetype kMaxRecentSearches = 8;

// The values of the text settings, the default first (oneOf()).
constexpr std::array kThemeChoices{u"dark", u"light", u"system"};
constexpr std::array kAccentChoices{u"coral", u"red", u"blue", u"green", u"teal", u"violet", u"graphite", u"custom"};
constexpr std::array kLightSurfaceChoices{u"paper", u"white"};
constexpr std::array kDarkSurfaceChoices{u"black", u"slate"};
constexpr std::array kDensityChoices{u"comfortable", u"compact"};
constexpr std::array kEditorFontChoices{u"serif", u"sans"};
constexpr std::array kLineSpacingChoices{u"normal", u"compact", u"relaxed"};
constexpr std::array kSpellLanguageChoices{u"auto", u"pl", u"en", u"pl,en", u"en,pl"};
constexpr std::array kWebPictureExportChoices{u"link", u"include"};
constexpr std::array kTrayClickChoices{u"calendar", u"window"};

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
    return v.normalized(QString::NormalizationForm_C).toCaseFolded();
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
    return out.normalized(QString::NormalizationForm_C);
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
    // Never chosen (or unknown): dark — with the black background (darkSurface()).
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
    return {u"journals"_s, u"calendar"_s, u"tasks"_s,  u"habits"_s,    u"gym"_s,   u"allPages"_s,
            u"tags"_s,     u"graph"_s,    u"search"_s, u"favorites"_s, u"recent"_s};
}

QStringList AppSettings::sidebarSectionIds()
{
    return {u"favorites"_s, u"recent"_s, u"pages"_s};
}

QStringList AppSettings::bottomNavViews()
{
    return {u"journals"_s, u"calendar"_s, u"search"_s, u"tasks"_s, u"habits"_s, u"gym"_s, u"allPages"_s, u"tags"_s, u"graph"_s};
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

bool AppSettings::assistantEnabled() const
{
    return m_settings->value(kAssistant, false).toBool();
}

void AppSettings::setAssistantEnabled(bool on)
{
    if (store(kAssistant, on, assistantEnabled()))
        Q_EMIT assistantEnabledChanged();
}

QStringList AppSettings::enabledExtensions() const
{
    QStringList out;
    if (gymEnabled())
        out << u"silownia"_s;
    if (assistantEnabled())
        out << u"asystent"_s;
    return out;
}

QString AppSettings::aiProvider() const
{
    const QString p = m_settings->value(kAiProvider, u"anthropic"_s).toString();
    return p == u"openai" ? p : u"anthropic"_s;
}

void AppSettings::setAiProvider(const QString &provider)
{
    if (store(kAiProvider, provider == u"openai" ? u"openai"_s : u"anthropic"_s, aiProvider()))
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

void AppSettings::resetAppearance()
{
    setAccentColor(u"coral"_s);
    setLightSurface(u"paper"_s);
    setDarkSurface(u"black"_s);
    setUiFontFamily(QString());
    setDensity(u"comfortable"_s);
    setFontScale(1.0);
    setEditorFont(u"serif"_s);
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

bool AppSettings::spellCheck() const
{
    return m_settings->value(kSpellCheck, true).toBool();
}

void AppSettings::setSpellCheck(bool on)
{
    if (store(kSpellCheck, on, spellCheck()))
        Q_EMIT spellCheckChanged();
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
    }
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

void AppSettings::addRecentVault(const QString &rawPath)
{
    const QString path = cleanPath(rawPath);
    if (path.isEmpty())
        return;
    QVariantList list = recentVaults();
    removeRecent(list, path);
    list.prepend(recentEntry(path, {}, QDateTime::currentDateTime().toString(Qt::ISODate)));
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
