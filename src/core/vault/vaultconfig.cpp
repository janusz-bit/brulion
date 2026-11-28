#include "vault/vaultconfig.h"

#include "periodic/periodic.h"
#include "util/paths.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>
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
constexpr auto kThemePage = "themePage"_L1;

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
constexpr auto kFcReviewTags = "reviewTags"_L1;
constexpr auto kFcLoadBalance = "loadBalance"_L1;

/// A number of the note-review algorithm: its key, range and whether it is a whole number, read
/// from and written to its field (an int or a double of FlashcardsConfig).
struct AlgorithmNumber {
    QLatin1StringView key;
    double min;
    double max;
    int FlashcardsConfig::*whole = nullptr;
    double FlashcardsConfig::*real = nullptr;

    double get(const FlashcardsConfig &c) const { return whole ? double(c.*whole) : c.*real; }
    void set(FlashcardsConfig &c, double v) const
    {
        if (whole)
            c.*whole = int(v);
        else
            c.*real = v;
    }
    /// `v` as this number: a JSON number in the range (whole when it must be), else nullopt.
    std::optional<double> parse(const QJsonValue &v) const
    {
        if (!v.isDouble())
            return std::nullopt;
        const double n = v.toDouble();
        if (n < min || n > max || (whole && n != std::floor(n)))
            return std::nullopt;
        return n;
    }
};

const AlgorithmNumber AlgorithmNumbers[] = {
        {"baseEase"_L1, 130, 1000, &FlashcardsConfig::baseEase, nullptr},
        {"easyBonus"_L1, 1, 5, nullptr, &FlashcardsConfig::easyBonus},
        {"lapsesIntervalChange"_L1, 0.1, 1, nullptr, &FlashcardsConfig::lapsesIntervalChange},
        {"maxNoteInterval"_L1, 1, 36525, &FlashcardsConfig::maxNoteInterval, nullptr},
};

/// The flashcard settings of each kind, by their keys: one place for reading, writing and
/// applying them.
struct TagKey {
    QLatin1StringView key;
    QStringList FlashcardsConfig::*field;
};
const TagKey TagKeys[] = {{kFcTags, &FlashcardsConfig::tags}, {kFcReviewTags, &FlashcardsConfig::reviewTags}};
struct TextKey {
    QLatin1StringView key;
    QString FlashcardsConfig::*field;
    bool trim;
};
const TextKey TextKeys[] = {
        {kFcSingle, &FlashcardsConfig::singleLineSeparator, false},
        {kFcSingleReversed, &FlashcardsConfig::singleLineReversedSeparator, false},
        {kFcMulti, &FlashcardsConfig::multilineSeparator, true},
        {kFcMultiReversed, &FlashcardsConfig::multilineReversedSeparator, true},
        {kFcEndMarker, &FlashcardsConfig::multilineEndMarker, true},
};
struct FlagKey {
    QLatin1StringView key;
    bool FlashcardsConfig::*field;
};
const FlagKey FlagKeys[] = {
        {kFcHighlights, &FlashcardsConfig::clozeHighlights}, {kFcBold, &FlashcardsConfig::clozeBold},
        {kFcCurly, &FlashcardsConfig::clozeCurly},           {kFcFolders, &FlashcardsConfig::foldersAsDecks},
        {kFcLoadBalance, &FlashcardsConfig::loadBalance},
};

/// The entry of `table` for `key`, or nullptr.
template<typename Table>
auto entryOf(const Table &table, QStringView key) -> decltype(&table[0])
{
    for (const auto &e : table) {
        if (e.key == key)
            return &e;
    }
    return nullptr;
}

std::optional<QString> pathValue(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    return paths::normalize(v.toString());
}

/// A theme page: a Markdown file's vault path, "" none.
std::optional<QString> themePageValue(const QJsonValue &v)
{
    if (!v.isString())
        return std::nullopt;
    const QString path = paths::normalize(v.toString());
    if (!path.isEmpty() && (!paths::isMarkdown(path) || paths::isIgnored(path)))
        return std::nullopt;
    return path;
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

/// The value `v` of the flashcard setting `key` into `fc`: true, false when it is no such value
/// (the default stays), nullopt for a key this version does not know (kept in the file).
std::optional<bool> readFlashcardKey(FlashcardsConfig &fc, QStringView key, const QJsonValue &v)
{
    if (const TagKey *t = entryOf(TagKeys, key)) {
        const auto tags = tagsValue(v);
        if (tags)
            fc.*(t->field) = *tags;
        return tags.has_value();
    }
    if (const TextKey *t = entryOf(TextKeys, key)) {
        const auto value = separatorValue(v, t->trim);
        if (value)
            fc.*(t->field) = *value;
        return value.has_value();
    }
    if (const FlagKey *f = entryOf(FlagKeys, key)) {
        if (v.isBool())
            fc.*(f->field) = v.toBool();
        return v.isBool();
    }
    if (const AlgorithmNumber *n = entryOf(AlgorithmNumbers, key)) {
        const auto value = n->parse(v);
        if (value)
            n->set(fc, *value);
        return value.has_value();
    }
    return std::nullopt;
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
    for (auto v = o.constBegin(); v != o.constEnd(); ++v) {
        if (readFlashcardKey(out.config.flashcards, v.key(), *v) == false)
            out.warnings.append(u"Invalid value for \"%1.%2\": %3"_s.arg(kFlashcards, v.key(), compactJson(*v)));
    }
}

/// The flags and the algorithm's numbers of `c` that differ from `d` into `o`.
void insertChangedFlagsAndNumbers(QJsonObject &o, const FlashcardsConfig &c, const FlashcardsConfig &d)
{
    for (const FlagKey &f : FlagKeys) {
        if (c.*(f.field) != d.*(f.field))
            o.insert(f.key, c.*(f.field));
    }
    for (const AlgorithmNumber &n : AlgorithmNumbers) {
        if (n.get(c) != n.get(d))
            o.insert(n.key, n.get(c));
    }
}

/// The values of `c` that differ from the defaults, as the "flashcards" object (empty: none).
QJsonObject flashcardsObject(const FlashcardsConfig &c)
{
    const FlashcardsConfig d;
    QJsonObject o;
    for (const TagKey &t : TagKeys) {
        if (const QStringList tags = normalizeFlashcardTags(c.*(t.field)); !tags.isEmpty())
            o.insert(t.key, QJsonArray::fromStringList(tags));
    }
    for (const TextKey &t : TextKeys) {
        const auto ok = separatorValue(c.*(t.field), t.trim);
        if (ok && *ok != d.*(t.field))
            o.insert(t.key, *ok);
    }
    insertChangedFlagsAndNumbers(o, c, d);
    return o;
}

/// `key` is a flashcard setting of this version (one it writes, or leaves out at its default).
bool isFlashcardKey(QStringView key)
{
    return entryOf(TagKeys, key) || entryOf(TextKeys, key) || entryOf(FlagKeys, key) || entryOf(AlgorithmNumbers, key);
}

/// The keys of a file's "flashcards" object this version does not know (a newer one's), kept as
/// they are.
QJsonObject unknownFlashcardKeys(QJsonObject fc)
{
    for (auto key = fc.begin(); key != fc.end();)
        key = isFlashcardKey(key.key()) ? fc.erase(key) : std::next(key);
    return fc;
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

QString vaultDictionaryPath()
{
    return u".brulion/dictionary.txt"_s;
}

const QList<VaultPath> &notebookSettingsFiles()
{
    static const QList<VaultPath> files{vaultConfigPath(), vaultDictionaryPath()};
    return files;
}

bool isNotebookSettingsFile(const VaultPath &path)
{
    return notebookSettingsFiles().contains(path);
}

namespace {

constexpr qsizetype kMaxDictionaryWord = 100;

QStringList cleanWords(QStringList words)
{
    words.sort();
    words.erase(std::unique(words.begin(), words.end()), words.end());
    return words;
}

} // namespace

QStringList parseDictionary(QStringView text)
{
    QStringList words;
    for (const QStringView line : text.tokenize(u'\n')) {
        const QStringView word = line.trimmed();
        if (!word.isEmpty() && word.size() <= kMaxDictionaryWord)
            words.append(word.toString());
    }
    return cleanWords(std::move(words));
}

QString serializeDictionary(const QStringList &words)
{
    QString out;
    for (const QString &word : parseDictionary(words.join(u'\n')))
        out += word + u'\n';
    return out;
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
    mergeKey(out, kThemePage, themePageValue, &VaultConfig::themePage);
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
    if (const QString theme = paths::normalize(config.themePage); !theme.isEmpty() && paths::isMarkdown(theme))
        o.insert(kThemePage, theme);
    return o;
}

QByteArray serializeVaultConfig(const VaultConfig &config, const QJsonObject &raw)
{
    QJsonObject out = raw;
    const QJsonObject known = vaultConfigToJson(config);
    out.remove(kThemePage); // written only while there is one
    for (auto it = known.constBegin(); it != known.constEnd(); ++it)
        out.insert(it.key(), it.value());
    // "flashcards" holds only what differs from the defaults; its keys this version does not know
    // stay, and the file's own object stays when it is none (a file the user broke is theirs).
    const QJsonObject mine = flashcardsObject(config.flashcards);
    const auto there = raw.constFind(kFlashcards);
    if (there == raw.constEnd() || there->isObject()) {
        QJsonObject fc = unknownFlashcardKeys(there == raw.constEnd() ? QJsonObject() : there->toObject());
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
           && a.monthlyTemplate == b.monthlyTemplate && a.flashcards == b.flashcards
           && a.themePage == b.themePage;
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

namespace {

/// `v` as a number: a number, or a text of one as a settings field types it ("1,3" or "1.3").
std::optional<double> numberOf(const QVariant &v)
{
    const int type = v.metaType().id();
    if (type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::Double)
        return v.toDouble();
    if (type != QMetaType::QString)
        return std::nullopt;
    bool ok = false;
    const double n = v.toString().trimmed().replace(u',', u'.').toDouble(&ok);
    return ok ? std::optional<double>(n) : std::nullopt;
}

/// `v` as the tags setting `t` of `c` (a text "a, b" or a list); false when it is neither.
bool applyTags(FlashcardsConfig &c, const TagKey &t, const QVariant &v)
{
    const int type = v.metaType().id();
    if (type != QMetaType::QString && type != QMetaType::QStringList)
        return false;
    const auto tags = tagsValue(type == QMetaType::QString ? QJsonValue(v.toString())
                                                           : QJsonValue(QJsonArray::fromStringList(v.toStringList())));
    if (tags)
        c.*(t.field) = *tags;
    return tags.has_value();
}

/// `v` as the setting `key` of `c` (QML's and the command line's values: texts, flags, numbers or
/// their texts); false when `key` is none or `v` is no value of it.
bool applyFlashcardKey(FlashcardsConfig &c, QStringView key, const QVariant &v)
{
    if (const TagKey *t = entryOf(TagKeys, key))
        return applyTags(c, *t, v);
    if (entryOf(TextKeys, key))
        return v.metaType().id() == QMetaType::QString && readFlashcardKey(c, key, QJsonValue(v.toString())) == true;
    if (entryOf(FlagKeys, key))
        return v.metaType().id() == QMetaType::Bool && readFlashcardKey(c, key, QJsonValue(v.toBool())) == true;
    const std::optional<double> number = numberOf(v);
    return entryOf(AlgorithmNumbers, key) && number && readFlashcardKey(c, key, QJsonValue(*number)) == true;
}

} // namespace

bool applyFlashcardsConfig(FlashcardsConfig &config, const QVariantMap &values)
{
    FlashcardsConfig c = config;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!applyFlashcardKey(c, it.key(), it.value()))
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
                  {u"foldersAsDecks"_s, c.foldersAsDecks},
                  {u"loadBalance"_s, c.loadBalance}};
    for (const AlgorithmNumber &n : AlgorithmNumbers)
        m.insert(n.key, n.get(c));
    if (const QStringList tags = normalizeFlashcardTags(c.tags); !tags.isEmpty())
        m.insert(u"tags"_s, tags.join(u", "_s));
    if (const QStringList reviewTags = normalizeFlashcardTags(c.reviewTags); !reviewTags.isEmpty())
        m.insert(u"reviewTags"_s, reviewTags.join(u", "_s));
    return m;
}

} // namespace brulion
