#include "agentusage/history.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::agentusage {

namespace {

constexpr qint64 SlotMs = 15 * 60 * 1000;
constexpr qint64 DayMs = 24LL * 60 * 60 * 1000;
constexpr qint64 ChunkBytes = 256 * 1024;
constexpr qint64 HeadBytes = 4096;
constexpr qint64 MaxCacheBytes = 256LL * 1024 * 1024;
constexpr int CacheVersion = 2; // 2: Codex's token_usage_record lines
constexpr int MaxDepth = 6;

bool cancelled(const ReadOptions &options)
{
    return options.cancel && options.cancel->load(std::memory_order_relaxed);
}

/// A key's stable hash (FNV-1a, 64 bits: qHash is seeded per process).
quint64 keyHash(const QString &key)
{
    quint64 h = 14695981039346656037ULL;
    for (const char c : key.toUtf8()) {
        h ^= quint8(c);
        h *= 1099511628211ULL;
    }
    return h;
}

qint64 slotOf(qint64 msecs)
{
    return msecs >= 0 ? msecs / SlotMs : -((-msecs + SlotMs - 1) / SlotMs);
}

struct BucketKey {
    qint64 slot = 0;
    QString model;
    QString project;
    quint8 flags = 0;
    friend bool operator==(const BucketKey &, const BucketKey &) = default;
};

size_t qHash(const BucketKey &k, size_t seed = 0)
{
    return qHashMulti(seed, k.slot, k.model, k.project, k.flags);
}

struct Bucket {
    Tokens tokens;
    double loggedCost = 0;
};

/// What the cache knows of one log file.
struct FileState {
    qint64 size = 0;
    qint64 mtime = 0;
    qint64 offset = 0;     ///< read up to here (the start of a line not ended yet)
    qint64 headLength = 0; ///< `head` is the hash of this many first bytes
    QByteArray head;
    bool gone = false;     ///< no longer on the disk: its use is kept
    bool skipping = false; ///< in the middle of a line too long to read
    qint64 skipped = 0;    ///< lines left out (malformed, too long)
    QString project;       ///< Claude: the last cwd of the file
    CodexState codex;
    QSet<quint64> keys;
    QHash<BucketKey, Bucket> buckets;
};

struct Cache {
    QHash<QString, FileState> files; ///< by absolute path
    QSet<quint64> keys;              ///< every file's keys
};

// ---- the cache's JSON ---------------------------------------------------------------------------

QJsonArray tokensJson(const Tokens &t)
{
    return {double(t.input),     double(t.output),    double(t.cacheWrite5m), double(t.cacheWrite1h),
            double(t.cacheRead), double(t.reasoning), double(t.requests)};
}

bool tokensFrom(const QJsonArray &a, qsizetype at, Tokens *t)
{
    if (a.size() < at + 7)
        return false;
    qint64 *fields[] = {&t->input,     &t->output,    &t->cacheWrite5m, &t->cacheWrite1h,
                        &t->cacheRead, &t->reasoning, &t->requests};
    for (qsizetype i = 0; i < 7; ++i) {
        const QJsonValue v = a.at(at + i);
        if (!v.isDouble() || v.toDouble() < 0)
            return false;
        *fields[i] = v.toInteger();
    }
    return true;
}

QJsonObject fileJson(const QString &path, const FileState &s)
{
    QJsonObject o{{u"path"_s, path},
                  {u"size"_s, double(s.size)},
                  {u"mtime"_s, double(s.mtime)},
                  {u"offset"_s, double(s.offset)},
                  {u"headLength"_s, double(s.headLength)},
                  {u"head"_s, QString::fromLatin1(s.head.toHex())},
                  {u"gone"_s, s.gone},
                  {u"skipping"_s, s.skipping},
                  {u"skipped"_s, double(s.skipped)},
                  {u"project"_s, s.project}};
    if (s.codex.hasTotals || !s.codex.model.isEmpty() || !s.codex.project.isEmpty() || s.codex.fast || s.codex.records)
        o.insert(u"codex"_s, QJsonObject{{u"has"_s, s.codex.hasTotals},
                                         {u"totals"_s, tokensJson(s.codex.totals)},
                                         {u"total"_s, double(s.codex.totalTokens)},
                                         {u"model"_s, s.codex.model},
                                         {u"project"_s, s.codex.project},
                                         {u"fast"_s, s.codex.fast},
                                         {u"records"_s, s.codex.records}});
    QByteArray keys;
    keys.reserve(s.keys.size() * 8);
    QList<quint64> sorted(s.keys.cbegin(), s.keys.cend());
    std::ranges::sort(sorted);
    for (const quint64 k : std::as_const(sorted)) {
        char bytes[8];
        qToLittleEndian(k, bytes);
        keys.append(bytes, 8);
    }
    o.insert(u"keys"_s, QString::fromLatin1(keys.toBase64()));
    // The strings once, the buckets by their index.
    QStringList names;
    QHash<QString, int> index;
    const auto nameIndex = [&](const QString &name) {
        const auto it = index.constFind(name);
        if (it != index.constEnd())
            return *it;
        names.append(name);
        index.insert(name, int(names.size() - 1));
        return int(names.size() - 1);
    };
    QJsonArray buckets;
    for (auto it = s.buckets.cbegin(); it != s.buckets.cend(); ++it) {
        QJsonArray b{double(it.key().slot), nameIndex(it.key().model), nameIndex(it.key().project),
                     int(it.key().flags)};
        for (const QJsonValue &v : tokensJson(it.value().tokens))
            b.append(v);
        b.append(it.value().loggedCost);
        buckets.append(b);
    }
    o.insert(u"names"_s, QJsonArray::fromStringList(names));
    o.insert(u"buckets"_s, buckets);
    return o;
}

std::optional<FileState> fileFrom(const QJsonObject &o)
{
    FileState s;
    const QJsonValue numbers[] = {o.value(u"size"), o.value(u"mtime"), o.value(u"offset"), o.value(u"headLength")};
    for (const QJsonValue &v : numbers) {
        if (!v.isDouble() || v.toDouble() < 0)
            return std::nullopt;
    }
    s.size = o.value(u"size").toInteger();
    s.mtime = o.value(u"mtime").toInteger();
    s.offset = o.value(u"offset").toInteger();
    s.headLength = o.value(u"headLength").toInteger();
    s.head = QByteArray::fromHex(o.value(u"head").toString().toLatin1());
    s.gone = o.value(u"gone").toBool();
    s.skipping = o.value(u"skipping").toBool();
    s.skipped = o.value(u"skipped").toInteger();
    s.project = o.value(u"project").toString();
    if (s.offset > s.size || s.headLength > HeadBytes)
        return std::nullopt;
    if (const QJsonObject c = o.value(u"codex").toObject(); !c.isEmpty()) {
        s.codex.hasTotals = c.value(u"has").toBool();
        if (!tokensFrom(c.value(u"totals").toArray(), 0, &s.codex.totals))
            return std::nullopt;
        s.codex.totalTokens = c.value(u"total").toInteger();
        s.codex.model = c.value(u"model").toString();
        s.codex.project = c.value(u"project").toString();
        s.codex.fast = c.value(u"fast").toBool();
        s.codex.records = c.value(u"records").toBool();
    }
    const QByteArray keys = QByteArray::fromBase64(o.value(u"keys").toString().toLatin1());
    if (keys.size() % 8 != 0)
        return std::nullopt;
    for (qsizetype i = 0; i + 8 <= keys.size(); i += 8)
        s.keys.insert(qFromLittleEndian<quint64>(keys.constData() + i));
    const QJsonArray names = o.value(u"names").toArray();
    for (const QJsonValue &value : o.value(u"buckets").toArray()) {
        const QJsonArray b = value.toArray();
        if (b.size() != 12)
            return std::nullopt;
        const qint64 model = b.at(1).toInteger(-1);
        const qint64 project = b.at(2).toInteger(-1);
        if (model < 0 || model >= names.size() || project < 0 || project >= names.size())
            return std::nullopt;
        BucketKey k{b.at(0).toInteger(), names.at(model).toString(), names.at(project).toString(),
                    quint8(b.at(3).toInt())};
        Bucket bucket;
        if (!tokensFrom(b, 4, &bucket.tokens))
            return std::nullopt;
        bucket.loggedCost = b.at(11).toDouble();
        s.buckets.insert(k, bucket);
    }
    return s;
}

/// The cache of `source` (nullopt: none, or not to be trusted — read everything again).
std::optional<Cache> loadCache(const QString &file, const QString &sourceKey, bool *existed)
{
    *existed = false;
    QFile f(file);
    if (!f.exists())
        return std::nullopt;
    *existed = true;
    if (f.size() > MaxCacheBytes || !f.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonObject root = doc.object();
    if (root.value(u"version").toInt() != CacheVersion || root.value(u"prices").toString() != QLatin1String(PricesAsOf)
        || root.value(u"source").toString() != sourceKey)
        return std::nullopt;
    Cache cache;
    for (const QJsonValue &value : root.value(u"files").toArray()) {
        const QJsonObject o = value.toObject();
        const QString path = o.value(u"path").toString();
        std::optional<FileState> s = fileFrom(o);
        if (path.isEmpty() || !s)
            return std::nullopt;
        for (const quint64 k : std::as_const(s->keys))
            cache.keys.insert(k);
        cache.files.insert(path, std::move(*s));
    }
    return cache;
}

bool saveCache(const QString &file, const QString &sourceKey, const Cache &cache)
{
    QJsonArray files;
    QStringList paths = cache.files.keys();
    std::ranges::sort(paths);
    for (const QString &path : std::as_const(paths))
        files.append(fileJson(path, cache.files.value(path)));
    const QJsonObject root{{u"version"_s, CacheVersion},
                           {u"prices"_s, QLatin1String(PricesAsOf)},
                           {u"source"_s, sourceKey},
                           {u"files"_s, files}};
    if (!QDir().mkpath(QFileInfo(file).absolutePath()))
        return false;
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!f.commit())
        return false;
    // (Which folders the user worked in: for the owner only.)
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

// ---- the files ----------------------------------------------------------------------------------

struct Found {
    QString path;
    QFileInfo info;
};

/// The log files of `source`, by path: no symlink out of its folders, at most MaxDepth deep and
/// MaxFiles in all; Codex's rollouts once by name (sessions before archived_sessions).
QList<Found> discover(const Source &source, const ReadOptions &options, bool *found)
{
    QList<Found> out;
    QSet<QString> names;
    for (const QString &folder : source.folders) {
        const QFileInfo rootInfo(folder);
        if (!rootInfo.isDir())
            continue;
        *found = true;
        const QString root = rootInfo.canonicalFilePath();
        if (root.isEmpty())
            continue;
        const QString prefix = root.endsWith(u'/') ? root : root + u'/';
        QList<Found> here;
        QDirIterator it(root, {u"*.jsonl"_s}, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancelled(options) || out.size() + here.size() >= MaxFiles)
                break;
            const QString path = it.next();
            const QFileInfo info = it.fileInfo();
            if (info.isSymLink()) {
                const QString target = info.canonicalFilePath();
                if (target.isEmpty() || !target.startsWith(prefix))
                    continue;
            }
            if (QStringView(path).mid(prefix.size()).count(u'/') >= MaxDepth)
                continue;
            if (source.provider == Provider::Codex && !info.fileName().startsWith(u"rollout-"))
                continue;
            here.append(Found{path, info});
        }
        std::ranges::sort(here, {}, &Found::path);
        for (const Found &f : std::as_const(here)) {
            if (source.provider == Provider::Codex) {
                if (names.contains(f.info.fileName()))
                    continue;
                names.insert(f.info.fileName());
            }
            out.append(f);
        }
    }
    return out;
}

QByteArray headHash(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
}

void forget(FileState &state, Cache &cache)
{
    for (const quint64 k : std::as_const(state.keys))
        cache.keys.remove(k);
    state = FileState{};
}

class FileReader {
public:
    FileReader(Provider provider, Cache &cache, const ReadOptions &options, qint64 since, ReadStats &stats)
        : m_provider(provider), m_cache(cache), m_options(options), m_since(since), m_stats(stats)
    { }

    /// Reads what is new in `found` into `state`; false when cancelled (the state then half read:
    /// the caller writes no cache).
    bool read(const Found &found, FileState &state)
    {
        const qint64 size = found.info.size();
        const qint64 mtime = found.info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
        if (!state.gone && state.size == size && state.mtime == mtime && (state.headLength > 0 || size == 0))
            return true;
        QFile f(found.path);
        if (!f.open(QIODevice::ReadOnly))
            return true; // (another time)
        // Rewritten (shorter, or other first bytes): read again from the start.
        bool again = state.gone || size < state.offset;
        if (!again && state.headLength > 0) {
            const QByteArray head = f.read(state.headLength);
            again = head.size() < state.headLength || headHash(head) != state.head;
        }
        if (again)
            forget(state, m_cache);
        if (!f.seek(0))
            return true;
        const QByteArray head = f.read(std::min(HeadBytes, size));
        state.headLength = head.size();
        state.head = headHash(head);
        if (!f.seek(state.offset))
            return true;
        ++m_stats.filesRead;
        qint64 position = state.offset; // of the chunk's first byte
        qint64 consumed = state.offset; // after the last line ended (or skipped)
        bool skipping = state.skipping;
        QByteArray pending;
        while (true) {
            if (cancelled(m_options))
                return false;
            const QByteArray chunk = f.read(ChunkBytes);
            if (chunk.isEmpty())
                break;
            m_stats.bytesRead += chunk.size();
            qsizetype start = 0;
            while (start < chunk.size()) {
                const qsizetype end = chunk.indexOf('\n', start);
                if (end < 0) {
                    const qsizetype rest = chunk.size() - start;
                    if (skipping) {
                        consumed = position + chunk.size();
                    } else if (pending.size() + rest > MaxLineBytes) {
                        pending.clear();
                        skipping = true;
                        consumed = position + chunk.size();
                    } else {
                        pending.append(chunk.constData() + start, rest);
                    }
                    break;
                }
                const qsizetype length = end - start;
                if (skipping) {
                    skipping = false;
                    ++state.skipped;
                } else if (pending.size() + length > MaxLineBytes) {
                    ++state.skipped;
                } else {
                    pending.append(chunk.constData() + start, length);
                    line(pending, state);
                }
                pending.clear();
                consumed = position + end + 1;
                start = end + 1;
            }
            position += chunk.size();
        }
        // (A line not ended yet is being written: read it the next time.)
        state.offset = consumed;
        state.skipping = skipping;
        state.size = size;
        state.mtime = mtime;
        state.gone = false;
        return true;
    }

private:
    void line(QByteArray &text, FileState &state)
    {
        if (text.endsWith('\r'))
            text.chop(1);
        if (text.trimmed().isEmpty() || !mayCarryUsage(m_provider, text))
            return;
        bool malformed = false;
        std::optional<Event> e = m_provider == Provider::Claude ? claudeEvent(text, state.project, &malformed)
                                                                : codexEvent(text, state.codex, &malformed);
        if (malformed)
            ++state.skipped;
        if (!e)
            return;
        if (m_provider == Provider::Claude && !e->project.isEmpty())
            state.project = e->project;
        if (e->msecs < m_since)
            return;
        if (!e->key.isEmpty()) {
            const quint64 h = keyHash(e->key);
            if (m_cache.keys.contains(h)) {
                ++m_stats.duplicates;
                return;
            }
            m_cache.keys.insert(h);
            state.keys.insert(h);
        }
        Bucket &b = state.buckets[BucketKey{slotOf(e->msecs), e->model, e->project, e->flags}];
        b.tokens += e->tokens;
        b.loggedCost += e->loggedCost;
        ++m_stats.events;
    }

    Provider m_provider;
    Cache &m_cache;
    const ReadOptions &m_options;
    qint64 m_since;
    ReadStats &m_stats;
};

struct DayKey {
    QDate date;
    QString model;
    QString project;
    quint8 flags = 0;
    friend bool operator==(const DayKey &, const DayKey &) = default;
};

size_t qHash(const DayKey &k, size_t seed = 0)
{
    return qHashMulti(seed, k.date.toJulianDay(), k.model, k.project, k.flags);
}

} // namespace

Source sourceOf(Provider provider, const QStringList &command, const QHash<QString, QString> &environment,
                const QString &home)
{
    const QString credentials = agentlimits::credentialsFile(provider, command, environment, home);
    if (credentials.isEmpty())
        return {};
    const QString dir = QFileInfo(credentials).path();
    Source s;
    s.provider = provider;
    if (provider == Provider::Claude) {
        s.folders.append(QDir(dir).filePath(u"projects"_s));
        // Claude Code without CLAUDE_CONFIG_DIR also keeps them in the XDG folder.
        if (QDir::cleanPath(dir) == QDir::cleanPath(QDir(home).filePath(u".claude"_s)))
            s.folders.append(QDir(home).filePath(u".config/claude/projects"_s));
    } else {
        s.folders = {QDir(dir).filePath(u"sessions"_s), QDir(dir).filePath(u"archived_sessions"_s)};
    }
    for (QString &f : s.folders)
        f = QDir::cleanPath(f);
    return s;
}

QString cacheFileFor(const QString &cacheDir, const Source &source)
{
    const QByteArray hash = QCryptographicHash::hash(source.key().toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QDir(cacheDir).filePath(
            u"agent-usage/%1-%2.json"_s.arg(agentlimits::providerName(source.provider), QString::fromLatin1(hash)));
}

History readHistory(const Source &source, const ReadOptions &options, ReadStats *statsOut)
{
    ReadStats localStats;
    ReadStats &stats = statsOut ? *statsOut : localStats;
    stats = ReadStats{};
    History out;
    out.provider = source.provider;
    if (!source.valid()) {
        out.error = u"no_source"_s;
        return out;
    }
    const QDateTime now = options.now.isValid() ? options.now.toUTC() : QDateTime::currentDateTimeUtc();
    const qint64 since = now.toMSecsSinceEpoch() - qint64(RetentionDays) * DayMs;
    Cache cache;
    if (!options.cacheFile.isEmpty()) {
        bool existed = false;
        if (std::optional<Cache> loaded = loadCache(options.cacheFile, source.key(), &existed))
            cache = std::move(*loaded);
        else
            stats.cacheRebuilt = existed;
    }
    const QList<Found> found = discover(source, options, &out.found);
    FileReader reader(source.provider, cache, options, since, stats);
    QSet<QString> present;
    for (const Found &f : found) {
        if (cancelled(options))
            break;
        present.insert(f.path);
        auto it = cache.files.find(f.path);
        if (it == cache.files.end()) {
            // (Untouched for longer than the history is kept: nothing in it counts.)
            if (f.info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch() < since)
                continue;
            it = cache.files.insert(f.path, FileState{});
        }
        if (!reader.read(f, *it))
            break;
    }
    if (cancelled(options)) {
        stats.cancelled = true;
        out.error = u"cancelled"_s;
        return out;
    }
    // Gone files keep their use; what is older than the history is forgotten.
    const qint64 oldestSlot = slotOf(since);
    for (auto it = cache.files.begin(); it != cache.files.end();) {
        FileState &s = *it;
        if (!present.contains(it.key()))
            s.gone = true;
        s.buckets.removeIf(
                [oldestSlot](const QHash<BucketKey, Bucket>::iterator &b) { return b.key().slot < oldestSlot; });
        if (s.gone && s.buckets.isEmpty()) {
            for (const quint64 k : std::as_const(s.keys))
                cache.keys.remove(k);
            it = cache.files.erase(it);
        } else {
            ++it;
        }
    }
    if (!options.cacheFile.isEmpty() && !saveCache(options.cacheFile, source.key(), cache))
        out.error = u"cache_not_written"_s;
    // The days in the time zone asked for.
    QHash<DayKey, DayUse> days;
    for (const FileState &s : std::as_const(cache.files)) {
        if (!s.gone)
            ++out.files;
        out.skippedLines += s.skipped;
        for (auto b = s.buckets.cbegin(); b != s.buckets.cend(); ++b) {
            const QDate date = QDateTime::fromMSecsSinceEpoch(b.key().slot * SlotMs, options.timeZone).date();
            const DayKey key{date, b.key().model, b.key().project, b.key().flags};
            DayUse &d = days[key];
            d.date = date;
            d.model = key.model;
            d.project = key.project;
            d.flags = key.flags;
            d.tokens += b.value().tokens;
            d.loggedCost += b.value().loggedCost;
        }
    }
    out.days = days.values();
    std::ranges::sort(out.days, [](const DayUse &a, const DayUse &b) {
        if (a.date != b.date)
            return a.date < b.date;
        if (a.model != b.model)
            return a.model < b.model;
        if (a.project != b.project)
            return a.project < b.project;
        return a.flags < b.flags;
    });
    out.readAt = now;
    return out;
}

} // namespace brulion::agentusage
