#include "vault/vaultconfig.h"

#include "periodic/periodic.h"
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
constexpr auto kWeeklyFolder = "weeklyFolder"_L1;
constexpr auto kWeeklyFileFormat = "weeklyFileFormat"_L1;
constexpr auto kWeeklyTemplate = "weeklyTemplate"_L1;
constexpr auto kMonthlyFolder = "monthlyFolder"_L1;
constexpr auto kMonthlyFileFormat = "monthlyFileFormat"_L1;
constexpr auto kMonthlyTemplate = "monthlyTemplate"_L1;
constexpr auto kFlashcards = "flashcards"_L1;

// The keys of the "flashcards" object.
constexpr auto kFcTags = "tags"_L1;
constexpr auto kFcSingle = "singleLineSeparator"_L1;
constexpr auto kFcSingleReversed = "singleLineReversedSeparator"_L1;
constexpr auto kFcMulti = "multilineSeparator"_L1;
constexpr auto kFcMultiReversed = "multilineReversedSeparator"_L1;
constexpr auto kFcEndMarker = "multilineEndMarker"_L1;
constexpr auto kFcHighlights = "clozeHighlights"_L1;
constexpr auto kFcBold = "clozeBold"_L1;
constexpr auto kFcCurly = "clozeCurly"_L1;
constexpr auto kFcFolders = "foldersAsDecks"_L1;

std::optional<QString> pathValue(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    return paths::normalize(v.toString());
}

std::optional<QString> formatValue(const QJsonValue &v)
{
    const QString format = v.toString().trimmed();
    if (!v.isString() || !isUsableJournalFormat(format))
        return std::nullopt;
    return format;
}

template<periodic::Period P>
std::optional<QString> periodFormatValue(const QJsonValue &v)
{
    const QString format = v.toString().trimmed();
    if (!v.isString() || !periodic::isUsableFormat(P, format))
        return std::nullopt;
    return format;
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

/// A separator: a text of at most 40 characters without a line break (empty: switched off).
std::optional<QString> separatorValue(const QJsonValue &v, bool trim)
{
    if (!v.isString())
        return std::nullopt;
    const QString text = v.toString();
    if (text.size() > 40 || text.contains(u'\n') || text.contains(u'\r'))
        return std::nullopt;
    return trim ? text.trimmed() : text;
}

/// Tags as a JSON array of strings or one text ("flashcards, fiszki").
std::optional<QStringList> tagsValue(const QJsonValue &v)
{
    QStringList raw;
    if (v.isString())
        raw = v.toString().split(QRegularExpression(u"[,\\s]+"_s), Qt::SkipEmptyParts);
    else if (v.isArray()) {
        for (const QJsonValue &item : v.toArray()) {
            if (!item.isString())
                return std::nullopt;
            raw << item.toString();
        }
    } else
        return std::nullopt;
    return normalizeFlashcardTags(raw);
}

/// The "flashcards" object into `out.config.flashcards`; an invalid value is a warning and the default.
void mergeFlashcards(ParsedVaultConfig &out)
{
    const auto it = out.raw.constFind(kFlashcards);
    if (it == out.raw.constEnd())
        return;
    if (!it->isObject()) {
        out.warnings.append(u"Invalid value for \"%1\": %2"_s.arg(kFlashcards, compactJson(*it)));
        return;
    }
    const QJsonObject o = it->toObject();
    FlashcardsConfig &fc = out.config.flashcards;
    const auto text = [&](QLatin1StringView key, QString FlashcardsConfig::*field, bool trim) {
        const auto v = o.constFind(key);
        if (v == o.constEnd())
            return;
        if (const auto value = separatorValue(*v, trim))
            fc.*field = *value;
        else
            out.warnings.append(u"Invalid value for \"%1.%2\": %3"_s.arg(kFlashcards, key, compactJson(*v)));
    };
    const auto flag = [&](QLatin1StringView key, bool FlashcardsConfig::*field) {
        const auto v = o.constFind(key);
        if (v == o.constEnd())
            return;
        if (v->isBool())
            fc.*field = v->toBool();
        else
            out.warnings.append(u"Invalid value for \"%1.%2\": %3"_s.arg(kFlashcards, key, compactJson(*v)));
    };
    if (const auto v = o.constFind(kFcTags); v != o.constEnd()) {
        if (const auto tags = tagsValue(*v))
            fc.tags = *tags;
        else
            out.warnings.append(u"Invalid value for \"%1.%2\": %3"_s.arg(kFlashcards, kFcTags, compactJson(*v)));
    }
    text(kFcSingle, &FlashcardsConfig::singleLineSeparator, false);
    text(kFcSingleReversed, &FlashcardsConfig::singleLineReversedSeparator, false);
    text(kFcMulti, &FlashcardsConfig::multilineSeparator, true);
    text(kFcMultiReversed, &FlashcardsConfig::multilineReversedSeparator, true);
    text(kFcEndMarker, &FlashcardsConfig::multilineEndMarker, true);
    flag(kFcHighlights, &FlashcardsConfig::clozeHighlights);
    flag(kFcBold, &FlashcardsConfig::clozeBold);
    flag(kFcCurly, &FlashcardsConfig::clozeCurly);
    flag(kFcFolders, &FlashcardsConfig::foldersAsDecks);
}

/// The values of `c` that differ from the defaults, as the "flashcards" object (empty: none).
QJsonObject flashcardsObject(const FlashcardsConfig &c)
{
    const FlashcardsConfig d;
    QJsonObject o;
    if (const QStringList tags = normalizeFlashcardTags(c.tags); !tags.isEmpty())
        o.insert(kFcTags, QJsonArray::fromStringList(tags));
    const auto text = [&](QLatin1StringView key, const QString &value, const QString &def, bool trim) {
        const auto ok = separatorValue(value, trim);
        if (ok && *ok != def)
            o.insert(key, *ok);
    };
    text(kFcSingle, c.singleLineSeparator, d.singleLineSeparator, false);
    text(kFcSingleReversed, c.singleLineReversedSeparator, d.singleLineReversedSeparator, false);
    text(kFcMulti, c.multilineSeparator, d.multilineSeparator, true);
    text(kFcMultiReversed, c.multilineReversedSeparator, d.multilineReversedSeparator, true);
    text(kFcEndMarker, c.multilineEndMarker, d.multilineEndMarker, true);
    if (c.clozeHighlights != d.clozeHighlights)
        o.insert(kFcHighlights, c.clozeHighlights);
    if (c.clozeBold != d.clozeBold)
        o.insert(kFcBold, c.clozeBold);
    if (c.clozeCurly != d.clozeCurly)
        o.insert(kFcCurly, c.clozeCurly);
    if (c.foldersAsDecks != d.foldersAsDecks)
        o.insert(kFcFolders, c.foldersAsDecks);
    return o;
}

/// 1, the only version so far.
std::optional<int> versionValue(const QJsonValue &v)
{
    return v.isDouble() && v.toDouble() == 1 ? std::optional<int>(1) : std::nullopt;
}

/// Sets `field` of `out.config` from `key` of `out.raw` through `parse` (a null `field`: the value
/// is only checked), or records a warning.
template<typename T, typename Parse>
void mergeKey(ParsedVaultConfig &out, QLatin1StringView key, Parse parse, T VaultConfig::*field)
{
    const auto it = out.raw.constFind(key);
    if (it == out.raw.constEnd())
        return;
    if (const std::optional<T> value = parse(*it); !value)
        out.warnings.append(u"Invalid value for \"%1\": %2"_s.arg(key, compactJson(*it)));
    else if (field)
        out.config.*field = *value;
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
    mergeKey<int>(out, kVersion, versionValue, nullptr);
    mergeKey(out, kJournalsFolder, pathValue, &VaultConfig::journalsFolder);
    mergeKey(out, kJournalFileFormat, formatValue, &VaultConfig::journalFileFormat);
    mergeKey(out, kNewPagesFolder, pathValue, &VaultConfig::newPagesFolder);
    mergeKey(out, kAttachmentsFolder, pathValue, &VaultConfig::attachmentsFolder);
    mergeKey(out, kTemplatesFolder, pathValue, &VaultConfig::templatesFolder);
    mergeKey(out, kJournalTemplate, pathValue, &VaultConfig::journalTemplate);
    mergeKey(out, kDefaultReminderTime, timeValue, &VaultConfig::defaultReminderTime);
    mergeKey(out, kWeekStartsOn, weekStartValue, &VaultConfig::weekStartsOn);
    mergeKey(out, kJournalMode, noteModeValue, &VaultConfig::journalMode);
    mergeKey(out, kPageMode, noteModeValue, &VaultConfig::pageMode);
    // Weekly and monthly notes: next to the daily ones unless their folder is set.
    out.config.weeklyFolder = out.config.journalsFolder;
    out.config.monthlyFolder = out.config.journalsFolder;
    mergeKey(out, kWeeklyFolder, pathValue, &VaultConfig::weeklyFolder);
    mergeKey(out, kWeeklyFileFormat, periodFormatValue<periodic::Period::Week>, &VaultConfig::weeklyFileFormat);
    mergeKey(out, kWeeklyTemplate, pathValue, &VaultConfig::weeklyTemplate);
    mergeKey(out, kMonthlyFolder, pathValue, &VaultConfig::monthlyFolder);
    mergeKey(out, kMonthlyFileFormat, periodFormatValue<periodic::Period::Month>, &VaultConfig::monthlyFileFormat);
    mergeKey(out, kMonthlyTemplate, pathValue, &VaultConfig::monthlyTemplate);
    mergeFlashcards(out);
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
    o.insert(kWeeklyFolder, paths::normalize(config.weeklyFolder));
    o.insert(kWeeklyFileFormat, periodic::fileFormat(periodic::Period::Week, config));
    o.insert(kWeeklyTemplate, paths::normalize(config.weeklyTemplate));
    o.insert(kMonthlyFolder, paths::normalize(config.monthlyFolder));
    o.insert(kMonthlyFileFormat, periodic::fileFormat(periodic::Period::Month, config));
    o.insert(kMonthlyTemplate, paths::normalize(config.monthlyTemplate));
    if (const QJsonObject flashcards = flashcardsObject(config.flashcards); !flashcards.isEmpty())
        o.insert(kFlashcards, flashcards);
    return o;
}

QByteArray serializeVaultConfig(const VaultConfig &config, const QJsonObject &raw)
{
    QJsonObject out = raw;
    const QJsonObject known = vaultConfigToJson(config);
    for (auto it = known.constBegin(); it != known.constEnd(); ++it)
        out.insert(it.key(), it.value());
    // "flashcards" holds only what differs from the defaults; its keys this version does not know
    // stay, and the file's own object stays when it is none (a file the user broke is theirs).
    const QJsonObject mine = flashcardsObject(config.flashcards);
    const auto there = raw.constFind(kFlashcards);
    if (there == raw.constEnd() || there->isObject()) {
        QJsonObject fc = there == raw.constEnd() ? QJsonObject() : there->toObject();
        for (const QLatin1StringView key : {kFcTags, kFcSingle, kFcSingleReversed, kFcMulti, kFcMultiReversed,
                                            kFcEndMarker, kFcHighlights, kFcBold, kFcCurly, kFcFolders})
            fc.remove(key);
        for (auto it = mine.constBegin(); it != mine.constEnd(); ++it)
            fc.insert(it.key(), it.value());
        if (fc.isEmpty())
            out.remove(kFlashcards);
        else
            out.insert(kFlashcards, fc);
    } else if (!mine.isEmpty()) {
        out.insert(kFlashcards, mine);
    }
    return QJsonDocument(out).toJson(QJsonDocument::Indented);
}

bool sameVaultConfig(const VaultConfig &a, const VaultConfig &b)
{
    return a.journalsFolder == b.journalsFolder && a.journalFileFormat == b.journalFileFormat
           && a.newPagesFolder == b.newPagesFolder && a.attachmentsFolder == b.attachmentsFolder
           && a.templatesFolder == b.templatesFolder && a.journalTemplate == b.journalTemplate
           && a.defaultReminderTime == b.defaultReminderTime && a.weekStartsOn == b.weekStartsOn
           && a.journalMode == b.journalMode && a.pageMode == b.pageMode && a.weeklyFolder == b.weeklyFolder
           && a.weeklyFileFormat == b.weeklyFileFormat && a.weeklyTemplate == b.weeklyTemplate
           && a.monthlyFolder == b.monthlyFolder && a.monthlyFileFormat == b.monthlyFileFormat
           && a.monthlyTemplate == b.monthlyTemplate && a.flashcards == b.flashcards;
}

QStringList normalizeFlashcardTags(const QStringList &tags)
{
    QStringList out;
    for (const QString &tag : tags) {
        QString t = tag.trimmed();
        while (t.startsWith(u'#'))
            t.remove(0, 1);
        t = t.trimmed().toLower();
        if (!t.isEmpty() && !t.contains(QRegularExpression(u"\\s"_s)) && !out.contains(t))
            out << t;
    }
    return out;
}

bool applyFlashcardsConfig(FlashcardsConfig &config, const QVariantMap &values)
{
    FlashcardsConfig c = config;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QString &key = it.key();
        const QVariant &v = it.value();
        const auto text = [&](QString FlashcardsConfig::*field, bool trim) {
            if (v.metaType().id() != QMetaType::QString)
                return false;
            const auto value = separatorValue(v.toString(), trim);
            if (value)
                c.*field = *value;
            return value.has_value();
        };
        const auto flag = [&](bool FlashcardsConfig::*field) {
            if (v.metaType().id() != QMetaType::Bool)
                return false;
            c.*field = v.toBool();
            return true;
        };
        bool ok = false;
        if (key == kFcTags) {
            if (v.metaType().id() == QMetaType::QString || v.metaType().id() == QMetaType::QStringList) {
                const auto tags = tagsValue(v.metaType().id() == QMetaType::QString
                                                    ? QJsonValue(v.toString())
                                                    : QJsonValue(QJsonArray::fromStringList(v.toStringList())));
                ok = tags.has_value();
                if (ok)
                    c.tags = *tags;
            }
        } else if (key == kFcSingle)
            ok = text(&FlashcardsConfig::singleLineSeparator, false);
        else if (key == kFcSingleReversed)
            ok = text(&FlashcardsConfig::singleLineReversedSeparator, false);
        else if (key == kFcMulti)
            ok = text(&FlashcardsConfig::multilineSeparator, true);
        else if (key == kFcMultiReversed)
            ok = text(&FlashcardsConfig::multilineReversedSeparator, true);
        else if (key == kFcEndMarker)
            ok = text(&FlashcardsConfig::multilineEndMarker, true);
        else if (key == kFcHighlights)
            ok = flag(&FlashcardsConfig::clozeHighlights);
        else if (key == kFcBold)
            ok = flag(&FlashcardsConfig::clozeBold);
        else if (key == kFcCurly)
            ok = flag(&FlashcardsConfig::clozeCurly);
        else if (key == kFcFolders)
            ok = flag(&FlashcardsConfig::foldersAsDecks);
        if (!ok)
            return false;
    }
    config = c;
    return true;
}

QVariantMap flashcardsSettings(const FlashcardsConfig &c)
{
    QVariantMap m{{u"singleLineSeparator"_s, c.singleLineSeparator},
                  {u"singleLineReversedSeparator"_s, c.singleLineReversedSeparator},
                  {u"multilineSeparator"_s, c.multilineSeparator},
                  {u"multilineReversedSeparator"_s, c.multilineReversedSeparator},
                  {u"multilineEndMarker"_s, c.multilineEndMarker},
                  {u"clozeHighlights"_s, c.clozeHighlights},
                  {u"clozeBold"_s, c.clozeBold},
                  {u"clozeCurly"_s, c.clozeCurly},
                  {u"foldersAsDecks"_s, c.foldersAsDecks}};
    if (const QStringList tags = normalizeFlashcardTags(c.tags); !tags.isEmpty())
        m.insert(u"tags"_s, tags.join(u", "_s));
    return m;
}

} // namespace brulion
