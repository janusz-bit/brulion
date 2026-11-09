#include "vault/obsidiansettings.h"

#include "util/paths.h"
#include "vault/vaultconfig.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion {
namespace {

/// A moment.js date format converted to a Qt QDate format.
struct QtDateFormat {
    QString format;
    bool names = false; ///< it writes month or day names
};

/// `text` as a quoted Qt format literal (`text` holds no "'").
QString qtLiteral(const QString &text)
{
    return text.isEmpty() ? QString() : u'\'' + text + u'\'';
}

/// The moment → Qt conversion of qtDateFormatFromMoment().
std::optional<QtDateFormat> convertMoment(const QString &moment)
{
    // Qt writes a "'" (as "''") but does not read such file names back as dates.
    if (moment.isEmpty() || moment.contains(u'\''))
        return std::nullopt;
    QtDateFormat out;
    // Adjacent literals ([a][b], \a\b) form one quoted run: "'a''b'" would write a "'".
    QString literal;
    const auto flush = [&] {
        out.format += qtLiteral(literal);
        literal.clear();
    };
    for (qsizetype i = 0; i < moment.size();) {
        const QChar c = moment[i];
        if (c == u'[') {
            const qsizetype close = moment.indexOf(u']', i + 1);
            if (close < 0)
                return std::nullopt;
            literal += moment.sliced(i + 1, close - i - 1);
            i = close + 1;
            continue;
        }
        if (c == u'\\') { // moment's escape of one character
            if (i + 1 < moment.size())
                literal += moment[i + 1];
            i += 2;
            continue;
        }
        flush();
        if (!c.isLetter()) {
            out.format += c;
            ++i;
            continue;
        }
        qsizetype n = 1;
        while (i + n < moment.size() && moment[i + n] == c)
            ++n;
        const QStringView run = QStringView(moment).sliced(i, n);
        if (c == u'Y' && (n == 4 || n == 2)) {
            out.format += n == 4 ? u"yyyy"_s : u"yy"_s;
        } else if (c == u'M' && n <= 4) {
            out.format += run;
            out.names = out.names || n >= 3;
        } else if (c == u'D' && n <= 2) {
            out.format += n == 1 ? u"d"_s : u"dd"_s;
        } else if (c == u'd' && (n == 3 || n == 4)) {
            out.format += run;
            out.names = true;
        } else {
            // Do, DDD(D), d/dd/e/E (weekday numbers), w/W/gggg/GGGG (weeks), Q, X/x, times, and
            // letters that are no token at all (moment would copy them, but they are almost
            // always a token Brulion lacks).
            return std::nullopt;
        }
        i += n;
    }
    flush();
    return out;
}

/// The JSON object (or array) of a settings file; nullopt with a warning when invalid.
std::optional<QJsonDocument> parseSettings(QByteArray bytes, const QString &path, bool arrayAllowed,
                                           QStringList &warnings)
{
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        warnings.append(u"Invalid JSON in %1: %2 (offset %3)"_s.arg(path, error.errorString()).arg(error.offset));
        return std::nullopt;
    }
    if (!doc.isObject() && !(arrayAllowed && doc.isArray())) {
        warnings.append(u"%1 must contain a JSON object"_s.arg(path));
        return std::nullopt;
    }
    return doc;
}

/// The string at `key`, trimmed; "" when missing or null (Obsidian's default), nullopt with a
/// warning when not a string (the value is skipped).
std::optional<QString> stringValue(const QJsonObject &o, QLatin1StringView key, const QString &path,
                                   QStringList &warnings)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
        return QString();
    if (!v.isString()) {
        const QByteArray json = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
        warnings.append(u"Invalid value for \"%1\" in %2: %3"_s.arg(key, path, QString::fromUtf8(json.mid(1, json.size() - 2))));
        return std::nullopt;
    }
    return v.toString().trimmed();
}

/// The settings object of `bytes` (nullopt when absent or invalid, with a warning).
std::optional<QJsonObject> settingsObject(const std::optional<QByteArray> &bytes, const QString &path,
                                          QStringList &warnings)
{
    if (!bytes)
        return std::nullopt;
    const auto doc = parseSettings(*bytes, path, false, warnings);
    return doc ? std::optional<QJsonObject>(doc->object()) : std::nullopt;
}

/// True when a tracked file (a Markdown file when `markdownOnly`) lies in `folder` or below,
/// not counting those in Obsidian's own folder `obsidianFolder` or below when it is not the
/// top of the notebook: the guards of adoptObsidianSettings() stand for files Brulion wrote
/// with its defaults, and Obsidian's days in "journals/" (or "Journals/" — Brulion's folders
/// match in any letter case) are no such files.
bool holdsFiles(const QStringList &tracked, const QString &folder, bool markdownOnly, const QString &obsidianFolder)
{
    const QString prefix = folder + u'/';
    const QString obsidianPrefix = obsidianFolder + u'/';
    return std::ranges::any_of(tracked, [&](const QString &p) {
        const bool inside = folder.isEmpty() ? !p.contains(u'/') : p.startsWith(prefix, Qt::CaseInsensitive);
        return inside && (!markdownOnly || paths::isMarkdown(p))
               && (obsidianFolder.isEmpty() || !p.startsWith(obsidianPrefix, Qt::CaseInsensitive));
    });
}

/// True unless .obsidian/core-plugins.json switches the daily notes plugin off (object form
/// {"daily-notes": false}; array form: the enabled plugins).
bool dailyNotesPluginEnabled(const std::optional<QByteArray> &bytes, QStringList &warnings)
{
    if (!bytes)
        return true; // Obsidian's default: on
    const auto doc = parseSettings(*bytes, obsidianCorePluginsPath(), true, warnings);
    if (!doc)
        return true;
    if (doc->isArray())
        return doc->array().contains(QJsonValue(u"daily-notes"_s));
    const QJsonValue v = doc->object().value("daily-notes"_L1);
    return !(v.isBool() && !v.toBool());
}

/// The journal settings of daily-notes.json (or Obsidian's defaults) into `out`. Days already
/// written into Brulion's journals folder keep it (and its names).
void adoptJournals(const ObsidianVaultFiles &files, const QStringList &trackedFiles, const VaultConfig &base,
                   ObsidianAdoption &out)
{
    const QString path = obsidianDailyNotesPath();
    QStringList found; // warnings, reported unless the guard keeps Brulion's settings
    const std::optional<QJsonObject> daily = settingsObject(files.dailyNotes, path, found);
    const std::optional<QString> folder = daily ? stringValue(*daily, "folder"_L1, path, found) : std::nullopt;
    const QString obsidianFolder = folder ? paths::normalize(*folder) : QString();
    if (holdsFiles(trackedFiles, base.journalsFolder, true, obsidianFolder))
        return;
    VaultConfig &c = out.config;
    QStringList &w = out.warnings;
    w += found;
    if (!daily) {
        // Obsidian's defaults (top of the notebook, YYYY-MM-DD), when a day is written there.
        static const QRegularExpression day(u"^\\d{4}-\\d{2}-\\d{2}\\.md$"_s);
        if (!files.dailyNotes && dailyNotesPluginEnabled(files.corePlugins, w)
            && std::ranges::any_of(trackedFiles, [](const QString &p) { return day.match(p).hasMatch(); })) {
            c.journalsFolder.clear();
            c.journalFileFormat = u"yyyy-MM-dd"_s;
        }
        return;
    }
    if (folder)
        c.journalsFolder = obsidianFolder;
    if (const std::optional<QString> moment = stringValue(*daily, "format"_L1, path, w)) {
        const std::optional<QtDateFormat> format =
                moment->isEmpty() ? QtDateFormat{u"yyyy-MM-dd"_s} : convertMoment(*moment);
        if (format && isUsableJournalFormat(format->format)) {
            c.journalFileFormat = format->format;
            if (format->names)
                w.append(
                        u"Obsidian's daily note format %1 has month or day names: Brulion writes them in English"_s.arg(
                                *moment));
        } else {
            w.append(
                    u"Obsidian's daily note format %1 has no Brulion equivalent; set the journal file names in the settings"_s
                            .arg(*moment));
        }
    }
    if (const std::optional<QString> value = stringValue(*daily, "template"_L1, path, w)) {
        const QString templ = paths::normalize(*value);
        if (!templ.isEmpty())
            c.journalTemplate = paths::isMarkdown(templ) ? templ : templ + u".md"_s;
        else if (!trackedFiles.contains(base.journalTemplate))
            c.journalTemplate.clear(); // Obsidian: no template
    }
}

} // namespace

std::optional<QString> qtDateFormatFromMoment(const QString &moment)
{
    if (const std::optional<QtDateFormat> converted = convertMoment(moment))
        return converted->format;
    return std::nullopt;
}

QString obsidianDailyNotesPath()
{
    return u".obsidian/daily-notes.json"_s;
}

QString obsidianAppPath()
{
    return u".obsidian/app.json"_s;
}

QString obsidianTemplatesPath()
{
    return u".obsidian/templates.json"_s;
}

QString obsidianCorePluginsPath()
{
    return u".obsidian/core-plugins.json"_s;
}

ObsidianAdoption adoptObsidianSettings(const ObsidianVaultFiles &files, const QStringList &trackedFiles,
                                       const VaultConfig &base)
{
    ObsidianAdoption out;
    out.config = base;
    VaultConfig &c = out.config;
    QStringList &w = out.warnings;

    adoptJournals(files, trackedFiles, base, out);

    // Weekly and monthly notes stay next to the daily ones (unless they had a folder of their own).
    if (base.weeklyFolder == base.journalsFolder)
        c.weeklyFolder = c.journalsFolder;
    if (base.monthlyFolder == base.journalsFolder)
        c.monthlyFolder = c.journalsFolder;

    // Templates folder (the Templates core plugin).
    {
        const QString path = obsidianTemplatesPath();
        QStringList found;
        const std::optional<QJsonObject> o = settingsObject(files.templates, path, found);
        const std::optional<QString> value = o ? stringValue(*o, "folder"_L1, path, found) : std::nullopt;
        const QString folder = value ? paths::normalize(*value) : QString();
        if (!holdsFiles(trackedFiles, base.templatesFolder, false, folder)) {
            w += found;
            if (!folder.isEmpty())
                c.templatesFolder = folder;
        }
    }

    // Attachments folder: "" or "/" = the top of the notebook, "./…" = next to the note.
    {
        const QString path = obsidianAppPath();
        QStringList found;
        const std::optional<QJsonObject> o = settingsObject(files.app, path, found);
        const std::optional<QString> value =
                o ? stringValue(*o, "attachmentFolderPath"_L1, path, found) : std::nullopt;
        const bool nextToNote = value && (*value == u"."_s || value->startsWith(u"./"_s));
        const QString folder = value && !nextToNote ? paths::normalize(*value) : QString();
        if (!holdsFiles(trackedFiles, base.attachmentsFolder, false, folder)) {
            w += found;
            if (nextToNote)
                w.append(u"Obsidian keeps attachments next to the note (%1); Brulion puts them into one folder: %2"_s
                                 .arg(*value, base.attachmentsFolder.isEmpty() ? u"/"_s : base.attachmentsFolder));
            else if (value)
                c.attachmentsFolder = folder;
        }
    }

    // The config.json keys of the adopted values, in the order of the settings page.
    static constexpr std::pair<QString VaultConfig::*, QLatin1StringView> adoptable[] = {
            {&VaultConfig::journalsFolder, "journalsFolder"_L1},
            {&VaultConfig::journalFileFormat, "journalFileFormat"_L1},
            {&VaultConfig::journalTemplate, "journalTemplate"_L1},
            {&VaultConfig::templatesFolder, "templatesFolder"_L1},
            {&VaultConfig::attachmentsFolder, "attachmentsFolder"_L1},
    };
    for (const auto &[field, key] : adoptable) {
        if (c.*field != base.*field)
            out.adoptedKeys.append(key);
    }
    return out;
}

bool obsidianAdoptionDependsOn(const QString &path, const VaultConfig &base)
{
    if (!path.contains(u'/'))
        return true; // a day at the top of the notebook
    for (const QString *folder : {&base.journalsFolder, &base.templatesFolder, &base.attachmentsFolder}) {
        if (folder->isEmpty() || path.startsWith(*folder + u'/', Qt::CaseInsensitive))
            return true;
    }
    return path == base.journalTemplate;
}

} // namespace brulion
