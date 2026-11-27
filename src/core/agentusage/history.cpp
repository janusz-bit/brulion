#include "agentusage/history.h"

#include "agentusage/logstream_p.h"

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
#include <memory>
#include <optional>
#include <tuple>

using namespace Qt::StringLiterals;

namespace brulion::agentusage {

namespace {

constexpr qint64 SlotMs = 15 * 60 * 1000;
constexpr qint64 DayMs = 24LL * 60 * 60 * 1000;
constexpr qint64 ChunkBytes = 256 * 1024;
constexpr qint64 HeadBytes = 4096;
constexpr qint64 MaxCacheBytes = 256LL * 1024 * 1024;
constexpr int CacheVersion = 3; // 2: Codex's token_usage_record lines; 3: rollouts by name, compressed ones
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
    bool compressed = false; ///< read last as a compressed rollout ("….jsonl.zst")
    bool skipping = false; ///< in the middle of a line too long to read
    qint64 skipped = 0;    ///< lines left out (malformed, too long)
    QString project;       ///< Claude: the last cwd of the file
    CodexState codex;
    QSet<quint64> keys;
    QHash<BucketKey, Bucket> buckets;
};

struct Cache {
    /// By the file's key (Found::key): its absolute path, a Codex rollout's name.
    QHash<QString, FileState> files;
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

/// The keys of the messages counted, 8 bytes each (little endian), sorted, in base64.
QString keysJson(const QSet<quint64> &keys)
{
    QList<quint64> sorted(keys.cbegin(), keys.cend());
    std::ranges::sort(sorted);
    QByteArray bytes;
    bytes.reserve(sorted.size() * 8);
    for (const quint64 k : std::as_const(sorted)) {
        char b[8];
        qToLittleEndian(k, b);
        bytes.append(b, 8);
    }
    return QString::fromLatin1(bytes.toBase64());
}

/// The buckets as arrays, their strings once in `names` and each bucket by their index.
QJsonArray bucketsJson(const QHash<BucketKey, Bucket> &buckets, QStringList &names)
{
    QHash<QString, int> index;
    const auto nameIndex = [&](const QString &name) {
        const auto it = index.constFind(name);
        if (it != index.constEnd())
            return *it;
        names.append(name);
        index.insert(name, int(names.size() - 1));
        return int(names.size() - 1);
    };
    QJsonArray out;
    for (auto it = buckets.cbegin(); it != buckets.cend(); ++it) {
        QJsonArray b{double(it.key().slot), nameIndex(it.key().model), nameIndex(it.key().project),
                     int(it.key().flags)};
        for (const QJsonValue &v : tokensJson(it.value().tokens))
            b.append(v);
        b.append(it.value().loggedCost);
        out.append(b);
    }
    return out;
}

QJsonObject fileJson(const QString &key, const FileState &s)
{
    QStringList names;
    const QJsonArray buckets = bucketsJson(s.buckets, names);
    QJsonObject o{{u"key"_s, key},
                  {u"size"_s, double(s.size)},
                  {u"mtime"_s, double(s.mtime)},
                  {u"offset"_s, double(s.offset)},
                  {u"headLength"_s, double(s.headLength)},
                  {u"head"_s, QString::fromLatin1(s.head.toHex())},
                  {u"gone"_s, s.gone},
                  {u"compressed"_s, s.compressed},
                  {u"skipping"_s, s.skipping},
                  {u"skipped"_s, double(s.skipped)},
                  {u"project"_s, s.project},
                  {u"keys"_s, keysJson(s.keys)},
                  {u"names"_s, QJsonArray::fromStringList(names)},
                  {u"buckets"_s, buckets}};
    // (What a Codex rollout carries from line to line, when it carries something.)
    if (s.codex != CodexState{})
        o.insert(u"codex"_s, QJsonObject{{u"has"_s, s.codex.hasTotals},
                                         {u"totals"_s, tokensJson(s.codex.totals)},
                                         {u"total"_s, double(s.codex.totalTokens)},
                                         {u"model"_s, s.codex.model},
                                         {u"project"_s, s.codex.project},
                                         {u"fast"_s, s.codex.fast},
                                         {u"records"_s, s.codex.records}});
    return o;
}

/// A whole number of the cache that is not negative (nullopt: anything else).
std::optional<qint64> countFrom(const QJsonValue &v)
{
    if (!v.isDouble() || v.toDouble() < 0)
        return std::nullopt;
    return v.toInteger();
}

/// A Codex rollout's state from the cache ({}: none); false when malformed.
bool codexFrom(const QJsonObject &c, CodexState &state)
{
    if (c.isEmpty())
        return true;
    state.hasTotals = c.value(u"has").toBool();
    state.totalTokens = c.value(u"total").toInteger();
    state.model = c.value(u"model").toString();
    state.project = c.value(u"project").toString();
    state.fast = c.value(u"fast").toBool();
    state.records = c.value(u"records").toBool();
    return tokensFrom(c.value(u"totals").toArray(), 0, &state.totals);
}

/// keysJson() read back; false when malformed.
bool keysFrom(const QJsonValue &text, QSet<quint64> &keys)
{
    const QByteArray bytes = QByteArray::fromBase64(text.toString().toLatin1());
    if (bytes.size() % 8 != 0)
        return false;
    for (qsizetype i = 0; i + 8 <= bytes.size(); i += 8)
        keys.insert(qFromLittleEndian<quint64>(bytes.constData() + i));
    return true;
}

/// One bucket of bucketsJson() read back with its `names` (nullopt: malformed).
std::optional<std::pair<BucketKey, Bucket>> bucketFrom(const QJsonArray &b, const QJsonArray &names)
{
    if (b.size() != 12)
        return std::nullopt;
    const qint64 model = b.at(1).toInteger(-1);
    const qint64 project = b.at(2).toInteger(-1);
    const auto named = [&names](qint64 i) { return i >= 0 && i < names.size(); };
    if (!named(model) || !named(project))
        return std::nullopt;
    std::pair<BucketKey, Bucket> out{BucketKey{b.at(0).toInteger(), names.at(model).toString(),
                                               names.at(project).toString(), quint8(b.at(3).toInt())},
                                     Bucket{}};
    if (!tokensFrom(b, 4, &out.second.tokens))
        return std::nullopt;
    out.second.loggedCost = b.at(11).toDouble();
    return out;
}

/// bucketsJson() read back; false when a bucket is malformed.
bool bucketsFrom(const QJsonObject &o, QHash<BucketKey, Bucket> &buckets)
{
    const QJsonArray names = o.value(u"names").toArray();
    for (const QJsonValue &value : o.value(u"buckets").toArray()) {
        std::optional<std::pair<BucketKey, Bucket>> b = bucketFrom(value.toArray(), names);
        if (!b)
            return false;
        buckets.insert(b->first, b->second);
    }
    return true;
}

/// A file's size, time, offset and head's length from the cache; false when one is malformed.
bool numbersFrom(const QJsonObject &o, FileState &s)
{
    const std::pair<const char16_t *, qint64 *> numbers[] = {
            {u"size", &s.size}, {u"mtime", &s.mtime}, {u"offset", &s.offset}, {u"headLength", &s.headLength}};
    for (const auto &[name, field] : numbers) {
        const std::optional<qint64> n = countFrom(o.value(QStringView(name)));
        if (!n)
            return false;
        *field = *n;
    }
    return true;
}

std::optional<FileState> fileFrom(const QJsonObject &o)
{
    FileState s;
    if (!numbersFrom(o, s))
        return std::nullopt;
    s.head = QByteArray::fromHex(o.value(u"head").toString().toLatin1());
    s.gone = o.value(u"gone").toBool();
    s.compressed = o.value(u"compressed").toBool();
    s.skipping = o.value(u"skipping").toBool();
    s.skipped = o.value(u"skipped").toInteger();
    s.project = o.value(u"project").toString();
    // (A compressed rollout's offset is in its text, which is longer than the file.)
    const bool consistent = (s.compressed || s.offset <= s.size) && s.headLength <= HeadBytes;
    if (!consistent || !codexFrom(o.value(u"codex").toObject(), s.codex) || !keysFrom(o.value(u"keys"), s.keys)
        || !bucketsFrom(o, s.buckets))
        return std::nullopt;
    return s;
}

/// The cache file's object when it is of this version, price table and source (nullopt: none,
/// unreadable, malformed or another's; `existed`: there was a file).
std::optional<QJsonObject> cacheRoot(const QString &file, const QString &sourceKey, bool *existed)
{
    QFile f(file);
    *existed = f.exists();
    if (!*existed || f.size() > MaxCacheBytes || !f.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &error);
    const QJsonObject root = doc.object();
    const bool ours = root.value(u"version").toInt() == CacheVersion
                      && root.value(u"prices").toString() == QLatin1String(PricesAsOf)
                      && root.value(u"source").toString() == sourceKey;
    if (error.error != QJsonParseError::NoError || !ours)
        return std::nullopt;
    return root;
}

/// The cache of `source` (nullopt: none, or not to be trusted — read everything again).
std::optional<Cache> loadCache(const QString &file, const QString &sourceKey, bool *existed)
{
    const std::optional<QJsonObject> root = cacheRoot(file, sourceKey, existed);
    if (!root)
        return std::nullopt;
    Cache cache;
    for (const QJsonValue &value : root->value(u"files").toArray()) {
        const QJsonObject o = value.toObject();
        const QString key = o.value(u"key").toString();
        std::optional<FileState> s = fileFrom(o);
        if (key.isEmpty() || !s)
            return std::nullopt;
        for (const quint64 k : std::as_const(s->keys))
            cache.keys.insert(k);
        cache.files.insert(key, std::move(*s));
    }
    return cache;
}

bool saveCache(const QString &file, const QString &sourceKey, const Cache &cache)
{
    QJsonArray files;
    QStringList keys = cache.files.keys();
    std::ranges::sort(keys);
    for (const QString &key : std::as_const(keys))
        files.append(fileJson(key, cache.files.value(key)));
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
    /// What the cache knows it by: its absolute path; a Codex rollout by its name ("rollout-….jsonl",
    /// also when compressed), wherever it is — sessions/ or archived_sessions/ — and however it is kept.
    QString key;
    bool compressed = false; ///< a compressed rollout ("rollout-….jsonl.zst")
    QFileInfo info;
};

/// A Codex rollout's name as the cache knows it: "rollout-….jsonl", its compressed file's too.
QString rolloutName(const QString &fileName)
{
    return fileName.endsWith(u".zst") ? fileName.chopped(4) : fileName;
}

/// A file a folder's walk found that is a log of `codex` or Claude Code: no symlink out of the
/// folder (`prefix`), at most MaxDepth deep, a Codex file a rollout.
bool isLog(const QFileInfo &info, const QString &path, const QString &prefix, bool codex)
{
    if (info.isSymLink()) {
        const QString target = info.canonicalFilePath();
        if (target.isEmpty() || !target.startsWith(prefix))
            return false;
    }
    return QStringView(path).mid(prefix.size()).count(u'/') < MaxDepth
           && (!codex || info.fileName().startsWith(u"rollout-"));
}

/// The log files below `root` (canonical), by path; at most `room` of them.
QList<Found> logsIn(const QString &root, bool codex, const ReadOptions &options, qsizetype room)
{
    const QString prefix = root.endsWith(u'/') ? root : root + u'/';
    const QStringList patterns = codex ? QStringList{u"*.jsonl"_s, u"*.jsonl.zst"_s} : QStringList{u"*.jsonl"_s};
    QList<Found> out;
    QDirIterator it(root, patterns, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && !cancelled(options) && out.size() < room) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        if (!isLog(info, path, prefix, codex))
            continue;
        if (codex)
            out.append(Found{path, rolloutName(info.fileName()), info.fileName().endsWith(u".zst"), info});
        else
            out.append(Found{path, path, false, info});
    }
    std::ranges::sort(out, {}, &Found::path);
    return out;
}

/// The log files of `source`, by path: no symlink out of its folders, at most MaxDepth deep and
/// MaxFiles in all; Codex's rollouts once by name (sessions before archived_sessions, a plain file
/// before its compressed copy).
QList<Found> discover(const Source &source, const ReadOptions &options, bool *found)
{
    QList<Found> out;
    QSet<QString> keys;
    for (const QString &folder : source.folders) {
        const QFileInfo rootInfo(folder);
        const QString root = rootInfo.canonicalFilePath();
        *found = *found || rootInfo.isDir();
        if (!rootInfo.isDir() || root.isEmpty())
            continue;
        for (const Found &f : logsIn(root, source.provider == Provider::Codex, options, MaxFiles - out.size())) {
            if (!keys.contains(f.key))
                out.append(f);
            keys.insert(f.key);
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
        if (readAlready(found, state, size, mtime))
            return true;
        // Gone and back, or shorter than what was read: read again from its start.
        if (state.gone || (!found.compressed && size < state.offset))
            forget(state, m_cache);
        std::optional<Resumed> text = resume(found, state);
        if (!text)
            return true; // (another time)
        ++m_stats.filesRead;
        if (!readRest(*text, state))
            return false;
        state.size = size;
        state.mtime = mtime;
        state.compressed = found.compressed;
        state.gone = false;
        return true;
    }

private:
    /// Read at this size and time, as it is kept now, already — an empty text too (a compressed
    /// file broken at once).
    static bool readAlready(const Found &found, const FileState &state, qint64 size, qint64 mtime)
    {
        return !state.gone && state.size == size && state.mtime == mtime && state.compressed == found.compressed;
    }

    struct Resumed {
        std::unique_ptr<LogStream> stream;
        QByteArray carry; ///< what of the head lies past where the last reading stopped
    };

    /// The text of `found` from where `state` stopped reading it — the same first bytes as before,
    /// or (other ones, or a text shorter than what was read) from its start again —, `state`'s head
    /// set anew; nullopt when it cannot be opened now.
    std::optional<Resumed> resume(const Found &found, FileState &state)
    {
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto stream = std::make_unique<LogStream>(found.path, found.compressed, m_options.maxUnpackedBytes);
            if (!stream->open())
                return std::nullopt;
            const QByteArray head = stream->read(HeadBytes);
            if (state.headLength > 0
                && (head.size() < state.headLength || headHash(head.first(state.headLength)) != state.head))
                forget(state, m_cache);
            state.headLength = head.size();
            state.head = headHash(head);
            if (state.offset <= head.size())
                return Resumed{std::move(stream), head.mid(state.offset)};
            if (stream->skipTo(state.offset))
                return Resumed{std::move(stream), {}};
            forget(state, m_cache);
        }
        return std::nullopt;
    }

    /// The text from where `text` stands into `state`, the line not ended yet left for the next
    /// time; false when cancelled.
    bool readRest(Resumed &text, FileState &state)
    {
        m_consumed = state.offset;
        m_skipping = state.skipping;
        m_pending.clear();
        feed(text.carry, state);
        for (QByteArray chunk; !(chunk = text.stream->read(ChunkBytes)).isEmpty();) {
            if (cancelled(m_options))
                return false;
            feed(chunk, state);
        }
        // (The rest of a compressed rollout that could not be read: one line left out.)
        if (text.stream->failed())
            ++state.skipped;
        state.offset = m_consumed;
        state.skipping = m_skipping;
        return true;
    }

    /// The next piece of the text: its whole lines parsed, a line too long to read skipped, the
    /// rest kept for the next piece.
    void feed(const QByteArray &chunk, FileState &state)
    {
        m_stats.bytesRead += chunk.size();
        const qint64 position = m_consumed + m_pending.size(); // (of the chunk's first byte)
        qsizetype start = 0;
        while (start < chunk.size()) {
            const qsizetype end = chunk.indexOf('\n', start);
            if (end < 0) {
                const qsizetype rest = chunk.size() - start;
                if (m_skipping) {
                    m_consumed = position + chunk.size();
                } else if (m_pending.size() + rest > MaxLineBytes) {
                    m_pending.clear();
                    m_skipping = true;
                    m_consumed = position + chunk.size();
                } else {
                    m_pending.append(chunk.constData() + start, rest);
                }
                return;
            }
            const qsizetype length = end - start;
            if (m_skipping) {
                m_skipping = false;
                ++state.skipped;
            } else if (m_pending.size() + length > MaxLineBytes) {
                ++state.skipped;
            } else {
                m_pending.append(chunk.constData() + start, length);
                line(m_pending, state);
            }
            m_pending.clear();
            m_consumed = position + end + 1;
            start = end + 1;
        }
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
    // The file being read: after its last line that ended (or was skipped), whether a line too long
    // is being skipped, the start of a line not ended yet.
    qint64 m_consumed = 0;
    bool m_skipping = false;
    QByteArray m_pending;
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

namespace {

/// Gone files keep their use (marked so); what is older than `since` is forgotten — and a gone
/// file with nothing left.
void prune(Cache &cache, const QSet<QString> &present, qint64 since)
{
    const qint64 oldestSlot = slotOf(since);
    for (auto it = cache.files.begin(); it != cache.files.end();) {
        FileState &s = *it;
        s.gone = s.gone || !present.contains(it.key());
        s.buckets.removeIf(
                [oldestSlot](const QHash<BucketKey, Bucket>::iterator &b) { return b.key().slot < oldestSlot; });
        if (!s.gone || !s.buckets.isEmpty()) {
            ++it;
            continue;
        }
        for (const quint64 k : std::as_const(s.keys))
            cache.keys.remove(k);
        it = cache.files.erase(it);
    }
}

bool earlier(const DayUse &a, const DayUse &b)
{
    return std::tie(a.date, a.model, a.project, a.flags) < std::tie(b.date, b.model, b.project, b.flags);
}

/// The cache's use per day in `zone` (oldest first), its files on the disk and lines left out into
/// `out`.
void collectDays(const Cache &cache, const QTimeZone &zone, History &out)
{
    QHash<DayKey, DayUse> days;
    for (const FileState &s : std::as_const(cache.files)) {
        out.files += s.gone ? 0 : 1;
        out.skippedLines += s.skipped;
        for (auto b = s.buckets.cbegin(); b != s.buckets.cend(); ++b) {
            const QDate date = QDateTime::fromMSecsSinceEpoch(b.key().slot * SlotMs, zone).date();
            DayUse &d = days[DayKey{date, b.key().model, b.key().project, b.key().flags}];
            d.date = date;
            d.model = b.key().model;
            d.project = b.key().project;
            d.flags = b.key().flags;
            d.tokens += b.value().tokens;
            d.loggedCost += b.value().loggedCost;
        }
    }
    out.days = days.values();
    std::ranges::sort(out.days, earlier);
}

/// The files `found` read into `cache` (a new one only when it changed since `since`); the keys of
/// those found. Stops when cancelled.
QSet<QString> readFiles(const QList<Found> &found, Cache &cache, FileReader &reader, qint64 since,
                        const ReadOptions &options)
{
    QSet<QString> present;
    for (const Found &f : found) {
        if (cancelled(options))
            break;
        present.insert(f.key);
        auto it = cache.files.find(f.key);
        // (Untouched for longer than the history is kept: nothing in it counts.)
        if (it == cache.files.end() && f.info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch() < since)
            continue;
        if (it == cache.files.end())
            it = cache.files.insert(f.key, FileState{});
        if (!reader.read(f, *it))
            break;
    }
    return present;
}

/// The cache of `source` as `options` name it (empty: none, or not to be trusted).
Cache cacheOf(const Source &source, const ReadOptions &options, ReadStats &stats)
{
    if (options.cacheFile.isEmpty())
        return {};
    bool existed = false;
    std::optional<Cache> loaded = loadCache(options.cacheFile, source.key(), &existed);
    stats.cacheRebuilt = !loaded && existed;
    return loaded ? std::move(*loaded) : Cache{};
}

} // namespace

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
    Cache cache = cacheOf(source, options, stats);
    FileReader reader(source.provider, cache, options, since, stats);
    const QSet<QString> present = readFiles(discover(source, options, &out.found), cache, reader, since, options);
    if (cancelled(options)) {
        stats.cancelled = true;
        out.error = u"cancelled"_s;
        return out;
    }
    prune(cache, present, since);
    if (!options.cacheFile.isEmpty() && !saveCache(options.cacheFile, source.key(), cache))
        out.error = u"cache_not_written"_s;
    collectDays(cache, options.timeZone, out);
    out.readAt = now;
    return out;
}

} // namespace brulion::agentusage
