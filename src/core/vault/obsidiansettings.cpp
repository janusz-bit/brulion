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

using namespace Qt::StringLiterals;

namespace brulion {
namespace {

constexpr auto kJournalsFolder = "journalsFolder"_L1;
constexpr auto kJournalFileFormat = "journalFileFormat"_L1;
constexpr auto kJournalTemplate = "journalTemplate"_L1;
constexpr auto kTemplatesFolder = "templatesFolder"_L1;
constexpr auto kAttachmentsFolder = "attachmentsFolder"_L1;

/// `text` as a quoted Qt format literal (`text` holds no "'").
QString qtLiteral(const QString &text)
{
    return text.isEmpty() ? QString() : u'\'' + text + u'\'';
}

/// The moment → Qt conversion; `names` is set when the format writes month or day names.
std::optional<QString> convertMoment(const QString &moment, bool *names)
{
    *names = false;
    if (moment.isEmpty())
        return std::nullopt;
    // Qt writes a "'" (as "''") but does not read such file names back as dates.
    if (moment.contains(u'\''))
        return std::nullopt;
    QString out;
    for (qsizetype i = 0; i < moment.size();) {
        const QChar c = moment[i];
        if (c == u'[') {
            const qsizetype close = moment.indexOf(u']', i + 1);
            if (close < 0)
                return std::nullopt;
            out += qtLiteral(moment.sliced(i + 1, close - i - 1));
            i = close + 1;
            continue;
        }
        if (c == u'\\') { // moment's escape of one character
            if (i + 1 < moment.size())
                out += qtLiteral(moment.sliced(i + 1, 1));
            i += 2;
            continue;
        }
        if (!c.isLetter()) {
            out += c;
            ++i;
            continue;
        }
        qsizetype n = 1;
        while (i + n < moment.size() && moment[i + n] == c)
            ++n;
        const QString run = moment.sliced(i, n);
        if (c == u'Y' && (n == 4 || n == 2)) {
            out += n == 4 ? u"yyyy"_s : u"yy"_s;
        } else if (c == u'M' && n <= 4) {
            out += run;
            *names = *names || n >= 3;
        } else if (c == u'D' && n <= 2) {
            out += n == 1 ? u"d"_s : u"dd"_s;
        } else if (c == u'd' && (n == 3 || n == 4)) {
            out += run;
            *names = true;
        } else {
            // Do, DDD(D), d/dd/e/E (weekday numbers), w/W/gggg/GGGG (weeks), Q, X/x, times, and
            // letters that are no token at all (moment would copy them, but they are almost
            // always a token Brulion lacks).
            return std::nullopt;
        }
        i += n;
    }
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

/// The string at `key`, trimmed; "" when missing or null, "" with a warning when not a string.
QString stringValue(const QJsonObject &o, QLatin1StringView key, const QString &path, QStringList &warnings)
{
    const QJsonValue v = o.value(key);
    if (v.isUndefined() || v.isNull())
        return {};
    if (!v.isString()) {
        const QByteArray json = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
        warnings.append(u"Invalid value for \"%1\" in %2: %3"_s.arg(key, path, QString::fromUtf8(json.mid(1, json.size() - 2))));
        return {};
    }
    return v.toString().trimmed();
}

/// True when a tracked file (a Markdown file when `markdownOnly`) lies in `folder` or below.
bool holdsFiles(const QStringList &tracked, const QString &folder, bool markdownOnly)
{
    const QString prefix = folder + u'/';
    for (const QString &p : tracked) {
        const bool inside = folder.isEmpty() ? !p.contains(u'/') : p.startsWith(prefix, Qt::CaseInsensitive);
        if (inside && (!markdownOnly || paths::isMarkdown(p)))
            return true;
    }
    return false;
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

} // namespace

std::optional<QString> qtDateFormatFromMoment(const QString &moment)
{
    bool names = false;
    return convertMoment(moment, &names);
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

    // Journals. Days already written into Brulion's journals folder keep it (and its names).
    if (!holdsFiles(trackedFiles, base.journalsFolder, true)) {
        if (files.dailyNotes) {
            const QString path = obsidianDailyNotesPath();
            if (const auto doc = parseSettings(*files.dailyNotes, path, false, w)) {
                const QJsonObject o = doc->object();
                c.journalsFolder = paths::normalize(stringValue(o, "folder"_L1, path, w));
                const QString moment = stringValue(o, "format"_L1, path, w);
                bool names = false;
                const std::optional<QString> format =
                        moment.isEmpty() ? std::optional<QString>(u"yyyy-MM-dd"_s) : convertMoment(moment, &names);
                if (format && isUsableJournalFormat(*format)) {
                    c.journalFileFormat = *format;
                    if (names)
                        w.append(u"Obsidian's daily note format %1 has month or day names: Brulion writes them in English"_s
                                         .arg(moment));
                } else {
                    w.append(u"Obsidian's daily note format %1 has no Brulion equivalent; set the journal file names in the settings"_s
                                     .arg(moment));
                }
                const QString templ = paths::normalize(stringValue(o, "template"_L1, path, w));
                if (!templ.isEmpty())
                    c.journalTemplate = paths::isMarkdown(templ) ? templ : templ + u".md"_s;
                else if (!trackedFiles.contains(base.journalTemplate))
                    c.journalTemplate.clear(); // Obsidian: no template
            }
        } else if (dailyNotesPluginEnabled(files.corePlugins, w)) {
            // Obsidian's defaults (top of the notebook, YYYY-MM-DD), when a day is written there.
            static const QRegularExpression day(u"^\\d{4}-\\d{2}-\\d{2}\\.md$"_s);
            const bool dayAtTop = std::any_of(trackedFiles.cbegin(), trackedFiles.cend(),
                                              [](const QString &p) { return day.match(p).hasMatch(); });
            if (dayAtTop) {
                c.journalsFolder.clear();
                c.journalFileFormat = u"yyyy-MM-dd"_s;
            }
        }
    }

    // Templates folder (the Templates core plugin).
    if (files.templates && !holdsFiles(trackedFiles, base.templatesFolder, false)) {
        const QString path = obsidianTemplatesPath();
        if (const auto doc = parseSettings(*files.templates, path, false, w)) {
            const QString folder = paths::normalize(stringValue(doc->object(), "folder"_L1, path, w));
            if (!folder.isEmpty())
                c.templatesFolder = folder;
        }
    }

    // Attachments folder: "" or "/" = the top of the notebook, "./…" = next to the note.
    if (files.app && !holdsFiles(trackedFiles, base.attachmentsFolder, false)) {
        const QString path = obsidianAppPath();
        if (const auto doc = parseSettings(*files.app, path, false, w)) {
            const QString folder = stringValue(doc->object(), "attachmentFolderPath"_L1, path, w);
            if (folder == u"."_s || folder.startsWith(u"./"_s))
                w.append(u"Obsidian keeps attachments next to the note (%1); Brulion puts them into one folder: %2"_s
                                 .arg(folder, base.attachmentsFolder.isEmpty() ? u"/"_s : base.attachmentsFolder));
            else
                c.attachmentsFolder = paths::normalize(folder);
        }
    }

    if (c.journalsFolder != base.journalsFolder)
        out.adoptedKeys.append(kJournalsFolder);
    if (c.journalFileFormat != base.journalFileFormat)
        out.adoptedKeys.append(kJournalFileFormat);
    if (c.journalTemplate != base.journalTemplate)
        out.adoptedKeys.append(kJournalTemplate);
    if (c.templatesFolder != base.templatesFolder)
        out.adoptedKeys.append(kTemplatesFolder);
    if (c.attachmentsFolder != base.attachmentsFolder)
        out.adoptedKeys.append(kAttachmentsFolder);
    return out;
}

} // namespace brulion
