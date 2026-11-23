#include "appscripting.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "navigation.h"
#include "workspace.h"
#include "workspaceextensions.h"

#include "log/categories.h"
#include "themepalette.h"
#include "util/paths.h"

#include <QMetaProperty>
#include <QRegularExpression>
#include <QPointer>
#include <QTimer>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using logging::lcApp;

namespace {

QList<SettingSpec> makeSpecs()
{
    const auto choice = [](const char *key, const char *group, const char *property, QStringList options,
                           const char *label) {
        SettingSpec s;
        s.key = QString::fromLatin1(key);
        s.group = QString::fromLatin1(group);
        s.kind = u"choice"_s;
        s.property = QString::fromLatin1(property);
        s.options = std::move(options);
        s.label = QString::fromLatin1(label);
        return s;
    };
    const auto toggle = [](const char *key, const char *group, const char *property, const char *label) {
        SettingSpec s;
        s.key = QString::fromLatin1(key);
        s.group = QString::fromLatin1(group);
        s.kind = u"toggle"_s;
        s.property = QString::fromLatin1(property);
        s.label = QString::fromLatin1(label);
        return s;
    };
    const auto number = [](const char *key, const char *group, const char *property, double min, double max,
                           bool integer, const char *label) {
        SettingSpec s;
        s.key = QString::fromLatin1(key);
        s.group = QString::fromLatin1(group);
        s.kind = u"number"_s;
        s.property = QString::fromLatin1(property);
        s.min = min;
        s.max = max;
        s.integer = integer;
        s.label = QString::fromLatin1(label);
        return s;
    };
    const auto text = [](const char *key, const char *group, const char *property, const char *label,
                         const char *kind = "text") {
        SettingSpec s;
        s.key = QString::fromLatin1(key);
        s.group = QString::fromLatin1(group);
        s.kind = QString::fromLatin1(kind);
        s.property = QString::fromLatin1(property);
        s.label = QString::fromLatin1(label);
        return s;
    };
    const auto list = [](const char *key, const char *group, const char *property, QStringList options, int size,
                         const char *label) {
        SettingSpec s;
        s.key = QString::fromLatin1(key);
        s.group = QString::fromLatin1(group);
        s.kind = u"list"_s;
        s.property = QString::fromLatin1(property);
        s.options = std::move(options);
        s.listSize = size;
        s.label = QString::fromLatin1(label);
        return s;
    };
    QList<SettingSpec> specs;
    // Appearance (Settings → Appearance)
    specs << choice("theme", "appearance", "themeMode", {u"system"_s, u"light"_s, u"dark"_s}, "the theme's mode");
    specs << choice("accent", "appearance", "accentColor",
                    {u"coral"_s, u"red"_s, u"blue"_s, u"green"_s, u"teal"_s, u"violet"_s, u"graphite"_s, u"custom"_s},
                    "the accent colour (custom: customAccent)");
    specs << text("customAccent", "appearance", "customAccent", "the custom accent colour, \"#rrggbb\"", "color");
    specs << choice("lightSurface", "appearance", "lightSurface", {u"paper"_s, u"white"_s}, "the light background");
    specs << choice("darkSurface", "appearance", "darkSurface", {u"graphite"_s, u"ink"_s, u"black"_s, u"slate"_s},
                    "the dark background");
    specs << choice("density", "appearance", "density", {u"comfortable"_s, u"compact"_s},
                    "the density of controls and rows (desktop)");
    specs << number("fontScale", "appearance", "fontScale", 0.85, 1.4, false, "the interface's text size, a factor");
    specs << text("uiFont", "appearance", "uiFontFamily", "the interface's font family (\"\": the default)");
    specs << toggle("reducedMotion", "appearance", "reducedMotion", "no animations");
    // Layout
    specs << list("sidebarHidden", "layout", "sidebarHidden", AppSettings::sidebarItems(), 0,
                  "the sidebar's entries that are hidden");
    specs << list("sidebarSections", "layout", "sidebarSections", AppSettings::sidebarSectionIds(), 0,
                  "the sidebar's sections in their order");
    specs << list("bottomNav", "layout", "bottomNavTabs", AppSettings::bottomNavViews(), 5,
                  "the phone's bottom navigation: five views");
    specs << toggle("captureButton", "layout", "captureButtonShown", "the quick note button on phones");
    specs << toggle("railExpands", "layout", "railExpands", "the rail of views widens with their names under the pointer (desktop)");
    {
        SettingSpec s = toggle("statusBar", "layout", "statusBarShown", "the status bar (desktop)");
        s.onApp = true;
        specs << s;
        s = toggle("sidePanel", "layout", "sidePanelVisible", "the side panel");
        s.onApp = true;
        specs << s;
    }
    // Editor
    specs << choice("editorFont", "editor", "editorFont", {u"sans"_s, u"serif"_s}, "the notes' font");
    specs << text("editorFontFamily", "editor", "editorFontFamily",
                  "a font family for notes (\"\": editorFont decides)");
    specs << text("monoFont", "editor", "editorMonoFamily", "the code's font family (\"\": the default)");
    specs << number("editorFontSize", "editor", "editorFontSize", 12, 28, true, "the notes' text size in px");
    specs << number("editorWidth", "editor", "editorWidth", 0, 1600, true,
                    "the text column's width in px (480 … 1600; 0: the full width)");
    specs << choice("lineSpacing", "editor", "editorLineSpacing", {u"compact"_s, u"normal"_s, u"relaxed"_s},
                    "the notes' line spacing");
    specs << toggle("sourceMode", "editor", "editorSourceMode", "pages open as Markdown source");
    specs << toggle("propertiesExpanded", "editor", "propertiesExpanded", "the page properties open expanded");
    specs << toggle("webPictures", "editor", "webPicturesShown", "pictures from the internet shown without asking");
    specs << toggle("taskStatusHistory", "editor", "taskStatusHistory", "a task's status changes are noted");
    specs << toggle("spellCheck", "editor", "spellCheck", "spell checking");
    specs << toggle("wordCount", "editor", "wordCountShown", "the word count in a page's corner");
    specs << toggle("focusMode", "editor", "focusMode", "focus mode: the text column alone");
    specs << toggle("readingMode", "editor", "readingMode", "reading mode: notes are not edited");
    specs << toggle("typewriter", "editor", "typewriterScrolling", "typewriter scrolling");
    specs << toggle("vim", "editor", "vimMode", "Vim keys");
    // The built-in extensions (switching one reloads the engine: after the call)
    for (const auto &[key, property, label] :
         {std::tuple{"gym", "gymEnabled", "the gym"}, std::tuple{"habits", "habitsEnabled", "the habits"},
          std::tuple{"flashcards", "flashcardsEnabled", "the flashcards"},
          std::tuple{"assistant", "assistantEnabled", "the AI assistant"}}) {
        SettingSpec s = toggle(key, "extensions", property, label);
        s.deferred = true;
        specs << s;
    }
    // Reminders
    specs << toggle("reminders", "reminders", "remindersEnabled", "reminders and alarms");
    specs << number("snoozeMinutes", "reminders", "snoozeMinutes", 1, 120, true, "a snooze, in minutes");
    specs << number("alarmRingMinutes", "reminders", "alarmRingMinutes", 1, 60, true, "how long an alarm rings");
    specs << toggle("notifyMissed", "reminders", "notifyMissed", "missed reminders are shown");
    // The app
    specs << toggle("closeToTray", "app", "closeToTray", "closing the window keeps Brulion in the tray");
    specs << choice("trayClick", "app", "trayClick", {u"calendar"_s, u"window"_s},
                    "what a click on the tray icon opens");
    return specs;
}

QList<SettingSpec> makeNotebookSpecs()
{
    const auto spec = [](const char *key, const char *kind, const char *label) {
        SettingSpec s;
        s.key = QString::fromLatin1(key);
        s.group = u"notebook"_s;
        s.kind = QString::fromLatin1(kind);
        s.property = s.key;
        s.label = QString::fromLatin1(label);
        s.notebook = true;
        return s;
    };
    const auto choice = [&](const char *key, QStringList options, const char *label) {
        SettingSpec s = spec(key, "choice", label);
        s.options = std::move(options);
        return s;
    };
    QList<SettingSpec> specs;
    specs << spec("journalsFolder", "path", "the folder of the daily notes");
    specs << spec("journalFileFormat", "text", "the daily notes' file names, a date format (\"yyyy-MM-dd\")");
    specs << spec("journalTemplate", "path", "the daily notes' template, a page's path (\"templates/journal.md\")");
    specs << spec("templatesFolder", "path", "the folder of the templates");
    specs << spec("newPagesFolder", "path", "the folder of new pages (\"\": the notebook's root)");
    specs << spec("attachmentsFolder", "path", "the folder of pasted pictures and files");
    specs << spec("weeklyFolder", "path", "the folder of the weekly notes");
    specs << spec("weeklyFileFormat", "text", "the weekly notes' file names (\"yyyy-'W'ww\")");
    specs << spec("weeklyTemplate", "path", "the weekly notes' template");
    specs << spec("monthlyFolder", "path", "the folder of the monthly notes");
    specs << spec("monthlyFileFormat", "text", "the monthly notes' file names (\"yyyy-MM\")");
    specs << spec("monthlyTemplate", "path", "the monthly notes' template");
    specs << choice("journalMode", {u"outline"_s, u"document"_s}, "how daily notes are edited");
    specs << choice("pageMode", {u"document"_s, u"outline"_s}, "how new pages are edited");
    specs << spec("defaultReminderTime", "text", "the time of a reminder given without one, \"HH:mm\"");
    specs << choice("weekStartsOn",
                    {u"monday"_s, u"tuesday"_s, u"wednesday"_s, u"thursday"_s, u"friday"_s, u"saturday"_s,
                     u"sunday"_s},
                    "the first day of the week");
    // The flashcards' card settings (the "flashcards" object; docs/SYNTAX.md §15).
    specs << spec("flashcards.tags", "text", "the tags of the decks, \"flashcards, fiszki\" (\"\": this device's)");
    specs << spec("flashcards.singleLineSeparator", "text", "a one-line card's separator (\"::\")");
    specs << spec("flashcards.singleLineReversedSeparator", "text", "a reversed one-line card's separator (\":::\")");
    specs << spec("flashcards.multilineSeparator", "text", "a multi-line card's separator line (\"?\")");
    specs << spec("flashcards.multilineReversedSeparator", "text", "a reversed multi-line card's separator (\"??\")");
    specs << spec("flashcards.multilineEndMarker", "text", "the line that ends a multi-line card (\"\": a blank line)");
    specs << spec("flashcards.clozeHighlights", "toggle", "==highlights== are clozes");
    specs << spec("flashcards.clozeBold", "toggle", "**bold text** is a cloze");
    specs << spec("flashcards.clozeCurly", "toggle", "{{curly brackets}} are clozes");
    specs << spec("flashcards.foldersAsDecks", "toggle", "a card's deck is its note's folder");
    specs << spec("themePage", "path", "the notebook's theme page (\"\": none; Settings → Appearance)");
    return specs;
}

const QStringList &weekDays()
{
    static const QStringList days{u"monday"_s, u"tuesday"_s,  u"wednesday"_s, u"thursday"_s,
                                  u"friday"_s, u"saturday"_s, u"sunday"_s};
    return days;
}

// Edit distance (the nearest key in an error).
int distance(QStringView a, QStringView b)
{
    QList<int> prev(b.size() + 1), cur(b.size() + 1);
    for (qsizetype j = 0; j <= b.size(); ++j)
        prev[j] = int(j);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        cur[0] = int(i);
        for (qsizetype j = 1; j <= b.size(); ++j)
            cur[j] = std::min(
                    {prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1].toLower() == b[j - 1].toLower() ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

QString nearest(const QString &name, const QStringList &names)
{
    QString best;
    int bestDistance = 4;
    for (const QString &n : names) {
        const int d = distance(name, n);
        if (d < bestDistance) {
            bestDistance = d;
            best = n;
        }
    }
    return best;
}

QString unknownWith(const QString &what, const QString &name, const QStringList &names)
{
    const QString near = nearest(name, names);
    return near.isEmpty() ? u"no %1 “%2”"_s.arg(what, name)
                          : u"no %1 “%2” (did you mean “%3”?)"_s.arg(what, name, near);
}

QString who(const QString &w)
{
    return w.startsWith(u"live:") ? w.mid(5) : w;
}

} // namespace

AppScripting::AppScripting(AppController *app, QObject *parent) : QObject(parent), m_app(app) { }

AppScripting::~AppScripting() = default;

const QList<SettingSpec> &AppScripting::settingSpecs()
{
    static const QList<SettingSpec> specs = makeSpecs();
    return specs;
}

const QList<SettingSpec> &AppScripting::notebookSettingSpecs()
{
    static const QList<SettingSpec> specs = makeNotebookSpecs();
    return specs;
}

const SettingSpec *AppScripting::settingSpec(const QString &key)
{
    for (const QList<SettingSpec> *specs : {&settingSpecs(), &notebookSettingSpecs()})
        for (const SettingSpec &s : *specs)
            if (s.key == key)
                return &s;
    return nullptr;
}

QVariant AppScripting::read(const SettingSpec &spec) const
{
    if (spec.notebook) {
        // Workspace.config: the same JSON the command line prints (notebook::vaultConfigJson).
        const QVariantMap config = m_app->workspace()->vaultConfig();
        const qsizetype dot = spec.property.indexOf(u'.');
        const QVariant v = dot < 0 ? config.value(spec.property)
                                   : config.value(spec.property.left(dot)).toMap().value(spec.property.mid(dot + 1));
        if (spec.key == u"weekStartsOn") {
            const int day = v.toInt();
            return day >= 1 && day <= 7 ? weekDays().at(day - 1) : weekDays().first();
        }
        return spec.kind == u"toggle" ? QVariant(v.toBool()) : QVariant(v.toString());
    }
    QObject *target = spec.onApp ? static_cast<QObject *>(m_app) : static_cast<QObject *>(m_app->settings());
    const QVariant v = target->property(spec.property.toLatin1().constData());
    if (spec.kind == u"number")
        return spec.integer ? QVariant(v.toLongLong()) : QVariant(v.toDouble());
    if (spec.kind == u"list")
        return v.toStringList();
    if (spec.kind == u"toggle")
        return v.toBool();
    return v.toString();
}

bool AppScripting::write(const SettingSpec &spec, const QVariant &value)
{
    if (spec.notebook) {
        QVariant v = value;
        if (spec.key == u"weekStartsOn")
            v = int(weekDays().indexOf(value.toString())) + 1;
        const qsizetype dot = spec.property.indexOf(u'.');
        const QVariantMap change = dot < 0 ? QVariantMap{{spec.property, v}}
                                           : QVariantMap{{spec.property.left(dot), QVariantMap{{spec.property.mid(dot + 1), v}}}};
        return m_app->workspace()->isOpen() && m_app->workspace()->setConfig(change);
    }
    QObject *target = spec.onApp ? static_cast<QObject *>(m_app) : static_cast<QObject *>(m_app->settings());
    const int index = target->metaObject()->indexOfProperty(spec.property.toLatin1().constData());
    if (index < 0)
        return false;
    const QMetaProperty property = target->metaObject()->property(index);
    QVariant v = value;
    if (property.metaType().id() == QMetaType::Int)
        v = int(value.toLongLong());
    else if (property.metaType().id() == QMetaType::Double)
        v = value.toDouble();
    else if (property.metaType().id() == QMetaType::QStringList)
        v = value.toStringList();
    return property.write(target, v);
}

QVariantList AppScripting::settings() const
{
    QVariantList out;
    QList<const SettingSpec *> specs;
    for (const SettingSpec &s : settingSpecs())
        specs << &s;
    // The notebook's own while one is open (with none they say nothing).
    if (m_app->workspace()->isOpen())
        for (const SettingSpec &s : notebookSettingSpecs())
            specs << &s;
    for (const SettingSpec *spec : std::as_const(specs)) {
        const SettingSpec &s = *spec;
        QVariantMap row{{u"key"_s, s.key},
                        {u"scope"_s, s.notebook ? u"notebook"_s : u"device"_s},
                        {u"group"_s, s.group},
                        {u"kind"_s, s.kind},
                        {u"label"_s, s.label},
                        {u"value"_s, read(s)}};
        if (!s.options.isEmpty())
            row.insert(u"options"_s, s.options);
        if (s.kind == u"number") {
            row.insert(u"min"_s, s.min);
            row.insert(u"max"_s, s.max);
        }
        if (s.listSize > 0)
            row.insert(u"size"_s, s.listSize);
        out << row;
    }
    return out;
}

QVariant AppScripting::setting(const QString &key, QString *error) const
{
    const SettingSpec *spec = settingSpec(key);
    if (!spec) {
        QStringList keys;
        for (const QList<SettingSpec> *specs : {&settingSpecs(), &notebookSettingSpecs()})
            for (const SettingSpec &s : *specs)
                keys << s.key;
        *error = unknownWith(u"setting"_s, key, keys);
        return {};
    }
    if (spec->notebook && !m_app->workspace()->isOpen()) {
        *error = u"“%1” is a setting of the notebook, and no notebook is open"_s.arg(key);
        return {};
    }
    return read(*spec);
}

bool AppScripting::setSetting(const QString &whoSet, const QString &key, const QVariant &value, QString *error)
{
    const SettingSpec *spec = settingSpec(key);
    if (!spec) {
        setting(key, error);
        return false;
    }
    const int type = value.metaType().id();
    QVariant v;
    if (spec->kind == u"toggle") {
        if (type != QMetaType::Bool)
            return (*error = u"“%1” is true or false"_s.arg(key), false);
        v = value.toBool();
    } else if (spec->kind == u"number") {
        bool ok = false;
        const double d = type == QMetaType::QString || type == QMetaType::Bool ? 0 : value.toDouble(&ok);
        if (!ok || !std::isfinite(d) || d < spec->min || d > spec->max || (spec->integer && d != std::floor(d)))
            return (*error = u"“%1” is a %2 from %3 to %4"_s.arg(key, spec->integer ? u"whole number"_s : u"number"_s)
                                     .arg(spec->min)
                                     .arg(spec->max),
                    false);
        if (key == u"editorWidth" && d > 0 && d < 480)
            return (*error = u"“editorWidth” is 0 (the full width) or 480 … 1600"_s, false);
        v = spec->integer ? QVariant(qint64(d)) : QVariant(d);
    } else if (spec->kind == u"choice") {
        if (type != QMetaType::QString || !spec->options.contains(value.toString()))
            return (*error = u"“%1” is one of %2"_s.arg(key, spec->options.join(u", "_s)), false);
        v = value.toString();
    } else if (spec->kind == u"color") {
        const QString color =
                type == QMetaType::QString ? extensions::normalizedThemeColor(value.toString()) : QString();
        if (color.size() != 7)
            return (*error = u"“%1” is a colour \"#rrggbb\""_s.arg(key), false);
        v = color;
    } else if (spec->kind == u"list") {
        QStringList items;
        if (type == QMetaType::QVariantList || type == QMetaType::QStringList) {
            for (const QVariant &item : value.toList()) {
                if (item.metaType().id() != QMetaType::QString || !spec->options.contains(item.toString()))
                    return (*error = u"“%1” holds %2"_s.arg(key, spec->options.join(u", "_s)), false);
                if (!items.contains(item.toString()))
                    items << item.toString();
            }
        } else if (!(type == QMetaType::QVariantMap && value.toMap().isEmpty())) {
            return (*error = u"“%1” is a list of %2"_s.arg(key, spec->options.join(u", "_s)), false);
        }
        if (spec->listSize > 0 && items.size() != spec->listSize)
            return (*error = u"“%1” holds exactly %2 of %3"_s.arg(key)
                                     .arg(spec->listSize)
                                     .arg(spec->options.join(u", "_s)),
                    false);
        v = items;
    } else if (spec->kind == u"path") { // the notebook's: a folder or a page from the notebook's root
        const QString path = type == QMetaType::QString ? paths::normalize(value.toString()) : QString();
        const QStringList parts = value.toString().split(QRegularExpression(u"[/\\\\]"_s));
        if (type != QMetaType::QString || value.toString().contains(u'\n') || value.toString().size() > 300
            || parts.contains(u".."_s) || (!path.isEmpty() && paths::isIgnored(path)))
            return (*error = u"“%1” is a path in the notebook, from its root (\"\": the root)"_s.arg(key), false);
        if (key == u"themePage" && !(m_themePageCheck && m_themePageCheck(path)))
            return (*error = u"“%1” is \"\" or the path of a theme page (a page tagged #theme)"_s.arg(key), false);
        v = path;
    } else { // text
        if (type != QMetaType::QString || value.toString().size() > 200 || value.toString().contains(u'\n'))
            return (*error = u"“%1” is one line of text (at most 200 characters)"_s.arg(key), false);
        // (The notebook's texts as given: a separator may keep its blanks; Workspace.setConfig checks them.)
        v = spec->notebook ? value.toString() : value.toString().trimmed();
    }
    if (spec->notebook && !m_app->workspace()->isOpen())
        return (*error = u"“%1” is a setting of the notebook, and no notebook is open"_s.arg(key), false);
    const QVariant before = read(*spec);
    if (before == v)
        return true;
    // Live changes are noted to be undone: a client's own, or those of the live code it loaded.
    const QString live = isLive(whoSet) ? whoSet : (m_liveOwner ? m_liveOwner(whoSet) : QString());
    const auto apply = [this, spec, v] {
        if (!write(*spec, v))
            qCWarning(lcApp, "b.app.set: %s was not written", qUtf8Printable(spec->key));
    };
    if (spec->notebook) {
        // The notebook checks its own (a date format that names one file for every day, a time):
        // what it refuses is an error, not a silent no.
        if (!write(*spec, v))
            return (*error = u"“%1” cannot be %2 in this notebook"_s.arg(key, v.toString().left(60)), false);
    } else if (spec->deferred) {
        later(apply);
    } else {
        apply();
    }
    if (!live.isEmpty()) {
        m_changes.append({QDateTime::currentDateTime(), live, u"setting"_s, key, before, v, {}});
        ++m_liveRevision;
        Q_EMIT liveChanged();
    }
    qCInfo(lcApp, "b.app.set %s by %s", qUtf8Printable(key), qUtf8Printable(whoSet));
    return true;
}

QVariantMap AppScripting::state() const
{
    const QVariantMap route = m_app->currentRoute();
    QVariantMap out{{u"view"_s, m_app->currentView()},
                    {u"route"_s, route},
                    {u"sidePanel"_s, m_app->sidePanelVisible()},
                    {u"platform"_s, m_app->platform()},
                    {u"language"_s, m_app->settings()->language()},
                    {u"version"_s, m_app->version()},
                    {u"dark"_s, false},
                    {u"touch"_s, m_app->touch()},
                    {u"narrow"_s, false},
                    {u"width"_s, 0},
                    {u"height"_s, 0}};
    const QString path = route.value(u"path"_s).toString();
    if (!path.isEmpty()) {
        out.insert(u"path"_s, path);
        out.insert(u"page"_s, paths::pageTitle(path));
    }
    if (route.contains(u"date"_s))
        out.insert(u"date"_s, route.value(u"date"_s).toString());
    if (m_ui) {
        const QVariantMap ui = m_ui();
        for (auto it = ui.cbegin(); it != ui.cend(); ++it)
            out.insert(it.key(), it.value());
    }
    return out;
}

void AppScripting::later(std::function<void()> f)
{
    QTimer::singleShot(0, this, std::move(f));
}

QVariant AppScripting::call(const QString &whoCalls, const QString &operation, const QVariantMap &args, QString *error)
{
    if (operation == u"state")
        return state();
    if (operation == u"commands") {
        QVariantList out;
        for (const QVariant &c : m_app->commands()->commands()) {
            const QVariantMap m = c.toMap();
            out << QVariantMap{{u"id"_s, m.value(u"id"_s)},
                               {u"title"_s, m.value(u"title"_s)},
                               {u"subtitle"_s, m.value(u"subtitle"_s)},
                               {u"shortcut"_s, m.value(u"shortcut"_s)},
                               {u"enabled"_s, m.value(u"enabled"_s)}};
        }
        return out;
    }
    if (operation == u"settings")
        return settings();
    if (operation == u"get")
        return setting(args.value(u"key"_s).toString(), error);
    if (operation == u"set") {
        setSetting(whoCalls, args.value(u"key"_s).toString(), args.value(u"value"_s), error);
        return {};
    }
    if (operation == u"navigate") {
        const QVariant r = args.value(u"route"_s);
        QVariantMap route =
                r.metaType().id() == QMetaType::QVariantMap ? r.toMap() : QVariantMap{{u"view"_s, r.toString()}};
        const QString view = route.value(u"view"_s).toString();
        if (!routeViews().contains(view) && view != u"home") {
            *error = unknownWith(u"view"_s, view, routeViews());
            return {};
        }
        // An extension's view by "<extension>/<view>" too ({ view = "extension", name = …}).
        const QVariantMap normalized = normalizeRoute(route);
        if (normalized.isEmpty()) {
            *error = u"the route { view = \"%1\" } lacks what it needs (a page's path, a day's date …)"_s.arg(view);
            return {};
        }
        QPointer<AppController> app(m_app);
        later([app, normalized] {
            if (app)
                app->navigate(normalized);
        });
        return {};
    }
    if (operation == u"open") {
        const QString page = args.value(u"page"_s).toString();
        const int line = args.contains(u"line"_s) ? int(args.value(u"line"_s).toLongLong()) - 1 : -1;
        const bool side = args.value(u"side"_s).toBool();
        QPointer<AppController> app(m_app);
        later([app, page, line, side] {
            if (!app)
                return;
            Workspace *ws = app->workspace();
            QString path = paths::isMarkdown(page) && ws->exists(page) ? page : ws->resolveLink(page, QString());
            if (path.isEmpty())
                path = ws->createPage(page);
            if (!path.isEmpty())
                app->openPage(path, line, side);
        });
        return {};
    }
    if (operation == u"back") {
        QPointer<AppController> app(m_app);
        later([app] {
            if (app)
                app->back();
        });
        return {};
    }
    if (operation == u"run") {
        const QString id = args.value(u"id"_s).toString();
        const QStringList ids = m_app->commands()->ids();
        if (!ids.contains(id)) {
            *error = unknownWith(u"command"_s, id, ids);
            return {};
        }
        QPointer<AppController> app(m_app);
        later([app, id] {
            if (app && !app->commands()->run(id))
                app->toast(tr("The command “%1” cannot run now.").arg(id), u"warning"_s);
        });
        return {};
    }
    if (operation == u"toast") {
        const QString text = args.value(u"text"_s).toString();
        const QString tone = args.value(u"tone"_s).toString();
        const QString kind = tone == u"success"   ? u"success"_s
                             : tone == u"danger"  ? u"error"_s
                             : tone == u"warning" ? u"warning"_s
                                                  : u"info"_s;
        const QVariantMap button = args.value(u"button"_s).toMap();
        QPointer<AppController> app(m_app);
        if (button.isEmpty()) {
            later([app, text, kind] {
                if (app)
                    app->toast(text, kind);
            });
            return {};
        }
        const QString extension = whoCalls;
        const QString command = button.value(u"command"_s).toString();
        const QString label = button.value(u"label"_s).toString();
        later([app, text, kind, label, extension, command] {
            if (!app)
                return;
            const int id = app->toastWithAction(text, kind, label);
            auto *connection = new QMetaObject::Connection;
            *connection = QObject::connect(app, &AppController::toastActionTriggered, app,
                                           [app, id, extension, command, connection](int pressed) {
                                               if (pressed != id)
                                                   return;
                                               QObject::disconnect(*connection);
                                               delete connection;
                                               app->workspace()->extensions()->runCommand(extension, command);
                                           });
        });
        return {};
    }
    *error = unknownWith(u"operation"_s, operation, extensions::appOperations());
    return {};
}

// ---- the theme ------------------------------------------------------------------------------------

void AppScripting::setExtensionThemes(const QList<extensions::ThemeInfo> &themes)
{
    bool same = themes.size() == m_extensionThemes.size();
    for (qsizetype i = 0; same && i < themes.size(); ++i)
        same = themes[i].extension == m_extensionThemes[i].extension && themes[i].layer == m_extensionThemes[i].layer;
    if (same)
        return;
    m_extensionThemes = themes;
    applyTheme();
}

void AppScripting::setLiveTheme(const QString &whoSets, const extensions::ThemeLayer &layer)
{
    QVariant before;
    for (qsizetype i = 0; i < m_liveThemes.size(); ++i) {
        if (m_liveThemes[i].first == whoSets) {
            before = extensions::themeLayerTable(m_liveThemes[i].second);
            m_liveThemes.removeAt(i);
            break;
        }
    }
    if (!layer.isEmpty())
        m_liveThemes.append({whoSets, layer});
    m_changes.append({QDateTime::currentDateTime(),
                      whoSets,
                      u"theme"_s,
                      QString(),
                      before,
                      extensions::themeLayerTable(layer),
                      {}});
    ++m_liveRevision;
    applyTheme();
    Q_EMIT liveChanged();
}

void AppScripting::setPageTheme(const QString &path, const extensions::ThemeLayer &layer)
{
    if (path == m_pageThemePath && layer == m_pageTheme)
        return;
    m_pageThemePath = path;
    m_pageTheme = layer;
    applyTheme();
}

QVariantMap AppScripting::theme() const
{
    QVariantList extensionLayers, liveLayers;
    QList<extensions::ThemeLayer> layers{m_pageTheme};
    for (const extensions::ThemeInfo &t : m_extensionThemes) {
        extensionLayers << QVariantMap{{u"extension"_s, t.extension},
                                       {u"tokens"_s, extensions::themeLayerTable(t.layer)}};
        layers << t.layer;
    }
    for (const auto &[w, layer] : m_liveThemes) {
        liveLayers << QVariantMap{{u"who"_s, who(w)}, {u"tokens"_s, extensions::themeLayerTable(layer)}};
        layers << layer;
    }
    QVariantMap out{{u"extensions"_s, extensionLayers},
                    {u"live"_s, liveLayers},
                    {u"light"_s, extensions::mergedTheme(layers, false)},
                    {u"dark"_s, extensions::mergedTheme(layers, true)}};
    if (!m_pageThemePath.isEmpty())
        out.insert(u"page"_s,
                   QVariantMap{{u"path"_s, m_pageThemePath}, {u"tokens"_s, extensions::themeLayerTable(m_pageTheme)}});
    return out;
}

void AppScripting::applyTheme()
{
    QList<extensions::ThemeLayer> layers{m_pageTheme};
    for (const extensions::ThemeInfo &t : m_extensionThemes)
        layers << t.layer;
    for (const auto &entry : m_liveThemes)
        layers << entry.second;
    ui::ThemeOverrides::instance()->setTokens(extensions::mergedTheme(layers, false),
                                              extensions::mergedTheme(layers, true));
}

// ---- the items of the UI --------------------------------------------------------------------------

bool AppScripting::activate(const QString &slot, const QString &extension, const QString &id, const QString &path,
                            int line)
{
    WorkspaceExtensions *extensions = m_app->workspace()->extensions();
    const QVariantMap item = extensions->contribution(slot, extension, id);
    if (item.isEmpty())
        return false;
    const QString command = item.value(u"command"_s).toString();
    const QString view = item.value(u"view"_s).toString();
    const QString page = item.value(u"page"_s).toString();
    const QString run = item.value(u"run"_s).toString();
    if (!command.isEmpty()) {
        // A block's menu and the slash menu: on that block (ctx.block); a page's menu: ctx.page.
        extensions->runCommand(extension, command, path, slot == u"blockMenu" || slot == u"slash" ? line : -1);
    } else if (!view.isEmpty()) {
        QVariantMap route = item.value(u"params"_s).toMap();
        route.insert(u"view"_s, u"extension"_s);
        route.insert(u"extension"_s, extension);
        route.insert(u"name"_s, view);
        m_app->navigate(route);
    } else if (!page.isEmpty()) {
        m_app->openNamedPage(page);
    } else if (!run.isEmpty()) {
        if (!m_app->commands()->run(run))
            m_app->toast(tr("The command “%1” cannot run now.").arg(run), u"warning"_s);
    }
    return true;
}

// ---- live changes ---------------------------------------------------------------------------------

QVariantList AppScripting::liveClients() const
{
    QStringList order;
    QHash<QString, int> counts;
    for (const Change &c : m_changes) {
        if (!counts.contains(c.who))
            order << c.who;
        ++counts[c.who];
    }
    QVariantList out;
    for (const QString &w : std::as_const(order))
        out << QVariantMap{{u"who"_s, w}, {u"name"_s, who(w)}, {u"changes"_s, counts.value(w)}};
    return out;
}

QVariantList AppScripting::changes() const
{
    QVariantList out;
    for (const Change &c : m_changes) {
        QVariantMap row{{u"at"_s, c.at.toString(Qt::ISODate)}, {u"who"_s, who(c.who)}, {u"kind"_s, c.kind}};
        if (!c.key.isEmpty())
            row.insert(u"key"_s, c.key);
        if (c.before.isValid())
            row.insert(u"before"_s, c.before);
        if (c.after.isValid())
            row.insert(u"after"_s, c.after);
        out << row;
    }
    return out;
}

void AppScripting::noteLive(const QString &whoChanged, const QString &kind, const QString &key,
                            std::function<void()> undo)
{
    m_changes.append({QDateTime::currentDateTime(), whoChanged, kind, key, {}, {}, std::move(undo)});
    ++m_liveRevision;
    Q_EMIT liveChanged();
}

int AppScripting::forgetLive(const QString &kind, const QString &key)
{
    const qsizetype forgotten = m_changes.removeIf([&](const Change &c) { return c.kind == kind && c.key == key; });
    if (forgotten > 0) {
        ++m_liveRevision;
        Q_EMIT liveChanged();
    }
    return int(forgotten);
}

int AppScripting::revert(const QString &whoReverts)
{
    int undone = 0;
    bool themeChanged = false;
    for (qsizetype i = m_changes.size() - 1; i >= 0; --i) {
        Change c = m_changes[i];
        if (!whoReverts.isEmpty() && c.who != whoReverts && who(c.who) != whoReverts)
            continue;
        m_changes.removeAt(i);
        ++undone;
        if (c.kind == u"setting") {
            if (const SettingSpec *spec = settingSpec(c.key))
                write(*spec, c.before);
        } else if (c.kind == u"theme") {
            const qsizetype removed = m_liveThemes.removeIf([&](const auto &entry) { return entry.first == c.who; });
            themeChanged = themeChanged || removed > 0;
        } else if (c.undo) {
            c.undo();
        }
    }
    if (themeChanged)
        applyTheme();
    if (undone > 0) {
        ++m_liveRevision;
        Q_EMIT liveChanged();
    }
    return undone;
}

} // namespace brulion::shell
