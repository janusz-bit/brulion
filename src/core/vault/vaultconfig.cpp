#include "vault/vaultconfig.h"

#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion {
namespace {

constexpr auto kVersion = "version"_L1;
constexpr auto kJournalsFolder = "journalsFolder"_L1;
constexpr auto kJournalFileFormat = "journalFileFormat"_L1;
constexpr auto kNewPagesFolder = "newPagesFolder"_L1;
constexpr auto kAttachmentsFolder = "attachmentsFolder"_L1;
constexpr auto kTemplatesFolder = "templatesFolder"_L1;
constexpr auto kJournalTemplate = "journalTemplate"_L1;
constexpr auto kDefaultReminderTime = "defaultReminderTime"_L1;
constexpr auto kWeekStartsOn = "weekStartsOn"_L1;
constexpr auto kJournalMode = "journalMode"_L1;
constexpr auto kPageMode = "pageMode"_L1;

std::optional<QString> pathValue(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    return paths::normalize(v.toString());
}

std::optional<QString> formatValue(const QJsonValue &v)
{
    if (!v.isString() || !isUsableJournalFormat(v.toString().trimmed()))
        return std::nullopt;
    return v.toString().trimmed();
}

std::optional<QTime> timeValue(const QJsonValue &v)
{
    static const QRegularExpression re(u"^(\\d{1,2}):(\\d{2})$"_s);
    if (!v.isString())
        return std::nullopt;
    const auto m = re.match(v.toString().trimmed());
    if (!m.hasMatch())
        return std::nullopt;
    const QTime t(m.captured(1).toInt(), m.captured(2).toInt());
    return t.isValid() ? std::optional<QTime>(t) : std::nullopt;
}

std::optional<Qt::DayOfWeek> weekStartValue(const QJsonValue &v)
{
    if (!v.isDouble())
        return std::nullopt;
    const double d = v.toDouble();
    if (d != std::floor(d) || d < 0 || d > 7)
        return std::nullopt;
    const int n = static_cast<int>(d);
    return n == 0 ? Qt::Sunday : static_cast<Qt::DayOfWeek>(n);
}

/// "outline" / "document" (as markdown::noteModeName / parseNoteMode, which this module does not
/// depend on): case-insensitive, surrounding whitespace ignored.
std::optional<NoteMode> noteModeValue(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    const QString name = v.toString().trimmed();
    if (name.compare("outline"_L1, Qt::CaseInsensitive) == 0)
        return NoteMode::Outline;
    if (name.compare("document"_L1, Qt::CaseInsensitive) == 0)
        return NoteMode::Document;
    return std::nullopt;
}

QString noteModeName(NoteMode mode, NoteMode fallback)
{
    if (mode != NoteMode::Outline && mode != NoteMode::Document)
        mode = fallback;
    return mode == NoteMode::Outline ? u"outline"_s : u"document"_s;
}

QString compactJson(const QJsonValue &v)
{
    // QJsonDocument serialises only objects/arrays: wrap the value and strip the brackets.
    const QByteArray json = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
    return QString::fromUtf8(json.mid(1, json.size() - 2));
}

/// Applies one key of `raw` to `config` through `parse`, or records a warning.
template<typename T, typename Parse, typename Apply>
void mergeKey(const QJsonObject &raw, QLatin1StringView key, Parse parse, Apply apply, QStringList &warnings)
{
    const auto it = raw.constFind(key);
    if (it == raw.constEnd())
        return;
    const std::optional<T> value = parse(*it);
    if (value)
        apply(*value);
    else
        warnings.append(u"Invalid value for \"%1\": %2"_s.arg(key, compactJson(*it)));
}

} // namespace

QString vaultConfigPath()
{
    return u".brulion/config.json"_s;
}

bool isUsableJournalFormat(const QString &format)
{
    if (format.trimmed().isEmpty())
        return false;
    const QDate base(2026, 1, 2);
    const QString name = base.toString(format);
    return name != base.addDays(1).toString(format) && name != base.addMonths(1).toString(format)
           && name != base.addYears(1).toString(format);
}

ParsedVaultConfig parseVaultConfig(QByteArrayView json)
{
    ParsedVaultConfig out;
    if (json.startsWith("\xEF\xBB\xBF"))
        json = json.sliced(3);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toByteArray(), &error);
    if (error.error != QJsonParseError::NoError) {
        out.warnings.append(
                u"Invalid JSON in %1: %2 (offset %3)"_s.arg(vaultConfigPath(), error.errorString()).arg(error.offset));
        return out;
    }
    if (!doc.isObject()) {
        out.warnings.append(u"%1 must contain a JSON object"_s.arg(vaultConfigPath()));
        return out;
    }
    out.raw = doc.object();
    VaultConfig &c = out.config;
    QStringList &w = out.warnings;
    const QJsonObject &raw = out.raw;
    mergeKey<int>(
            raw, kVersion,
            [](const QJsonValue &v) {
                return v.isDouble() && v.toDouble() == 1 ? std::optional<int>(1) : std::nullopt;
            },
            [](int) {}, w);
    mergeKey<QString>(raw, kJournalsFolder, pathValue, [&](const QString &v) { c.journalsFolder = v; }, w);
    mergeKey<QString>(raw, kJournalFileFormat, formatValue, [&](const QString &v) { c.journalFileFormat = v; }, w);
    mergeKey<QString>(raw, kNewPagesFolder, pathValue, [&](const QString &v) { c.newPagesFolder = v; }, w);
    mergeKey<QString>(raw, kAttachmentsFolder, pathValue, [&](const QString &v) { c.attachmentsFolder = v; }, w);
    mergeKey<QString>(raw, kTemplatesFolder, pathValue, [&](const QString &v) { c.templatesFolder = v; }, w);
    mergeKey<QString>(raw, kJournalTemplate, pathValue, [&](const QString &v) { c.journalTemplate = v; }, w);
    mergeKey<QTime>(raw, kDefaultReminderTime, timeValue, [&](QTime v) { c.defaultReminderTime = v; }, w);
    mergeKey<Qt::DayOfWeek>(raw, kWeekStartsOn, weekStartValue, [&](Qt::DayOfWeek v) { c.weekStartsOn = v; }, w);
    mergeKey<NoteMode>(raw, kJournalMode, noteModeValue, [&](NoteMode v) { c.journalMode = v; }, w);
    mergeKey<NoteMode>(raw, kPageMode, noteModeValue, [&](NoteMode v) { c.pageMode = v; }, w);
    return out;
}

QJsonObject vaultConfigToJson(const VaultConfig &config)
{
    const VaultConfig defaults;
    const QString format = config.journalFileFormat.trimmed();
    const QTime time = config.defaultReminderTime.isValid() ? config.defaultReminderTime : defaults.defaultReminderTime;
    const int weekStart = config.weekStartsOn >= Qt::Monday && config.weekStartsOn <= Qt::Sunday
                                  ? static_cast<int>(config.weekStartsOn)
                                  : static_cast<int>(defaults.weekStartsOn);
    QJsonObject o;
    o.insert(kVersion, 1);
    o.insert(kJournalsFolder, paths::normalize(config.journalsFolder));
    o.insert(kJournalFileFormat, isUsableJournalFormat(format) ? format : defaults.journalFileFormat);
    o.insert(kNewPagesFolder, paths::normalize(config.newPagesFolder));
    o.insert(kAttachmentsFolder, paths::normalize(config.attachmentsFolder));
    o.insert(kTemplatesFolder, paths::normalize(config.templatesFolder));
    o.insert(kJournalTemplate, paths::normalize(config.journalTemplate));
    o.insert(kDefaultReminderTime, time.toString(u"HH:mm"_s));
    o.insert(kWeekStartsOn, weekStart == Qt::Sunday ? 0 : weekStart);
    o.insert(kJournalMode, noteModeName(config.journalMode, defaults.journalMode));
    o.insert(kPageMode, noteModeName(config.pageMode, defaults.pageMode));
    return o;
}

QByteArray serializeVaultConfig(const VaultConfig &config, const QJsonObject &raw)
{
    QJsonObject out = raw;
    const QJsonObject known = vaultConfigToJson(config);
    for (auto it = known.constBegin(); it != known.constEnd(); ++it)
        out.insert(it.key(), it.value());
    return QJsonDocument(out).toJson(QJsonDocument::Indented);
}

bool sameVaultConfig(const VaultConfig &a, const VaultConfig &b)
{
    return a.journalsFolder == b.journalsFolder && a.journalFileFormat == b.journalFileFormat
           && a.newPagesFolder == b.newPagesFolder && a.attachmentsFolder == b.attachmentsFolder
           && a.templatesFolder == b.templatesFolder && a.journalTemplate == b.journalTemplate
           && a.defaultReminderTime == b.defaultReminderTime && a.weekStartsOn == b.weekStartsOn
           && a.journalMode == b.journalMode && a.pageMode == b.pageMode;
}

} // namespace brulion
