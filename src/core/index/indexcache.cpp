#include "index/indexcache.h"
#include "index/indexcache_p.h"

#include "index/vaultindex.h"
#include "index/vaultindex_p.h"
#include "markdown/journal.h"
#include "markdown/parse.h"
#include "text/text.h"
#include "util/paths.h"

#include <QCoreApplication>
#include <QElapsedTimer>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <optional>

// The codec writes every field of these types: a new field must be added to encodeNote() /
// decodeNote(), to tests/common/notedump.h and FormatVersion bumped. (Checked with the 64-bit
// Itanium layout of GCC and Clang — Linux, Android, macOS; MSVC lays some types out otherwise.)
#if !defined(_MSC_VER)
static_assert(sizeof(void *) != 8 || sizeof(brulion::Block) == 232,
              "a field was added to Block: update the index cache codec, tests/common/notedump.h and bump "
              "indexcache::FormatVersion");
static_assert(sizeof(void *) != 8 || sizeof(brulion::ParsedNote) == 256,
              "a field was added to ParsedNote: update the index cache codec, tests/common/notedump.h and bump "
              "indexcache::FormatVersion");
static_assert(sizeof(void *) != 8 || sizeof(brulion::Task) == 216,
              "a field was added to Task: update the index cache codec, tests/common/notedump.h and bump "
              "indexcache::FormatVersion");
static_assert(sizeof(void *) != 8 || sizeof(brulion::Reminder) == 80,
              "a field was added to Reminder: update the index cache codec, tests/common/notedump.h and bump "
              "indexcache::FormatVersion");
static_assert(sizeof(void *) != 8 || sizeof(brulion::Link) == 120,
              "a field was added to Link: update the index cache codec, tests/common/notedump.h and bump "
              "indexcache::FormatVersion");
#endif

namespace brulion {

/// The index cache's access to SearchIndex internals (befriended by SearchIndex).
struct SearchIndexAccess {
    using Doc = SearchIndex::Doc;
    using Posting = SearchIndex::Posting;
    using PostingList = SearchIndex::PostingList;
    static const std::map<QString, PostingList> &postings(const SearchIndex &s) { return s.m_postings; }
    static std::map<QString, PostingList> &postings(SearchIndex &s) { return s.m_postings; }
    static const QList<Doc> &docs(const SearchIndex &s) { return s.m_docs; }
    static QList<Doc> &docs(SearchIndex &s) { return s.m_docs; }
    static QHash<QString, int> &byId(SearchIndex &s) { return s.m_byId; }
    static std::array<quint64, SearchIndex::FieldCount> &totals(SearchIndex &s) { return s.m_totalLength; }
};

namespace indexcache {

using namespace Qt::StringLiterals;

namespace {

constexpr char HeaderMagic[] = "BRULIDX1";
constexpr char FooterMagic[] = "BRULEND1";
constexpr quint32 ByteOrderMark = 0x01020304;
constexpr qint64 FooterSize = 48;
constexpr int MaxVariantDepth = 32;

// ---- writing ------------------------------------------------------------------------------------

class Out {
public:
    explicit Out(QByteArray &buffer) : m_b(buffer) { }
    void raw(const char *data, qsizetype n) { m_b.append(data, n); }
    void u8(quint8 v) { m_b.append(char(v)); }
    void u32(quint32 v)
    {
        for (int i = 0; i < 4; ++i)
            u8(quint8(v >> (8 * i)));
    }
    void u64(quint64 v)
    {
        for (int i = 0; i < 8; ++i)
            u8(quint8(v >> (8 * i)));
    }
    void varint(quint64 v)
    {
        while (v >= 0x80) {
            u8(quint8(v | 0x80));
            v >>= 7;
        }
        u8(quint8(v));
    }
    void zigzag(qint64 v) { varint((quint64(v) << 1) ^ quint64(v >> 63)); }
    void boolean(bool v) { u8(v ? 1 : 0); }
    void str(const QString &s)
    {
        if (s.isNull()) {
            varint(0);
            return;
        }
        if (needsUtf16(s)) {
            const qsizetype bytes = s.size() * 2;
            varint((quint64(bytes) << 1 | 1) + 1);
            for (const QChar c : s) {
                u8(quint8(c.unicode() & 0xff));
                u8(quint8(c.unicode() >> 8));
            }
            return;
        }
        const QByteArray utf8 = s.toUtf8();
        varint((quint64(utf8.size()) << 1) + 1);
        raw(utf8.constData(), utf8.size());
    }
    qsizetype size() const { return m_b.size(); }

    /// Does `s` hold a lone surrogate (UTF-8 cannot carry it)?
    static bool needsUtf16(QStringView s)
    {
        for (qsizetype i = 0; i < s.size(); ++i) {
            const char16_t c = s[i].unicode();
            if (c < 0xd800 || c > 0xdfff)
                continue;
            if (c <= 0xdbff && i + 1 < s.size() && s[i + 1].unicode() >= 0xdc00 && s[i + 1].unicode() <= 0xdfff) {
                ++i;
                continue;
            }
            return true;
        }
        return false;
    }

private:
    QByteArray &m_b;
};

// ---- reading (bounds-checked, sticky failure) ----------------------------------------------------

class In {
public:
    explicit In(QByteArrayView data) : m_p(data.data()), m_end(data.data() + data.size()) { }

    bool failed() const { return m_failed; }
    bool atEnd() const { return m_p == m_end; }
    qint64 remaining() const { return m_failed ? 0 : qint64(m_end - m_p); }
    bool fail()
    {
        m_failed = true;
        m_p = m_end;
        return false;
    }

    quint8 u8()
    {
        if (m_failed || m_p >= m_end)
            return fail(), 0;
        return quint8(*m_p++);
    }
    quint32 u32()
    {
        quint32 v = 0;
        for (int i = 0; i < 4; ++i)
            v |= quint32(u8()) << (8 * i);
        return v;
    }
    quint64 u64()
    {
        quint64 v = 0;
        for (int i = 0; i < 8; ++i)
            v |= quint64(u8()) << (8 * i);
        return v;
    }
    quint64 varint()
    {
        quint64 v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const quint8 b = u8();
            if (m_failed)
                return 0;
            v |= quint64(b & 0x7f) << shift;
            if (!(b & 0x80))
                return v;
        }
        fail(); // longer than 10 bytes
        return 0;
    }
    qint64 zigzag()
    {
        const quint64 v = varint();
        return qint64(v >> 1) ^ -qint64(v & 1);
    }
    /// A count of elements of at least `minBytes` bytes each: never more than the rest can hold.
    qsizetype count(qint64 minBytes = 1)
    {
        const quint64 n = varint();
        if (m_failed || n > quint64(remaining() / std::max<qint64>(1, minBytes)))
            return fail(), 0;
        return qsizetype(n);
    }
    int small(int maxValue)
    {
        const quint64 v = varint();
        if (v > quint64(maxValue))
            return fail(), 0;
        return int(v);
    }
    int intValue()
    {
        const qint64 v = zigzag();
        if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max())
            return fail(), 0;
        return int(v);
    }
    bool boolean()
    {
        const quint8 v = u8();
        if (v > 1)
            fail();
        return v == 1;
    }
    QString str()
    {
        const quint64 tag = varint();
        if (m_failed || tag == 0)
            return QString();
        const quint64 bytes = (tag - 1) >> 1;
        const bool utf16 = ((tag - 1) & 1) != 0;
        if (bytes > quint64(remaining()) || (utf16 && bytes % 2 != 0))
            return fail(), QString();
        const char *p = m_p;
        m_p += bytes;
        if (!utf16)
            return QString::fromUtf8(p, qsizetype(bytes));
        QString out(qsizetype(bytes / 2), Qt::Uninitialized);
        for (qsizetype i = 0; i < out.size(); ++i)
            out[i] = QChar(char16_t(quint8(p[2 * i]) | (quint8(p[2 * i + 1]) << 8)));
        return out;
    }
    /// The bytes [here, here + n).
    QByteArrayView take(qint64 n)
    {
        if (n < 0 || n > remaining())
            return fail(), QByteArrayView();
        const QByteArrayView v(m_p, n);
        m_p += n;
        return v;
    }

private:
    const char *m_p;
    const char *m_end;
    bool m_failed = false;
};

// ---- values ---------------------------------------------------------------------------------------

enum VariantTag : quint8 { VInvalid, VBool, VLongLong, VDouble, VString, VStringList, VList, VMap };

/// false when `v` (or something in it) has a type the format cannot hold.
bool writeVariant(Out &o, const QVariant &v, int depth = 0)
{
    if (depth > MaxVariantDepth)
        return false;
    switch (v.typeId()) {
    case QMetaType::UnknownType: o.u8(VInvalid); return true;
    case QMetaType::Bool:
        o.u8(VBool);
        o.boolean(v.toBool());
        return true;
    case QMetaType::LongLong:
        o.u8(VLongLong);
        o.zigzag(v.toLongLong());
        return true;
    case QMetaType::Double: {
        o.u8(VDouble);
        const double d = v.toDouble();
        quint64 bits = 0;
        std::memcpy(&bits, &d, sizeof bits);
        o.u64(bits);
        return true;
    }
    case QMetaType::QString:
        o.u8(VString);
        o.str(v.toString());
        return true;
    case QMetaType::QStringList: {
        o.u8(VStringList);
        const QStringList l = v.toStringList();
        o.varint(quint64(l.size()));
        for (const QString &s : l)
            o.str(s);
        return true;
    }
    case QMetaType::QVariantList: {
        o.u8(VList);
        const QVariantList l = v.toList();
        o.varint(quint64(l.size()));
        for (const QVariant &e : l) {
            if (!writeVariant(o, e, depth + 1))
                return false;
        }
        return true;
    }
    case QMetaType::QVariantMap: {
        o.u8(VMap);
        const QVariantMap m = v.toMap();
        o.varint(quint64(m.size()));
        for (auto it = m.cbegin(); it != m.cend(); ++it) {
            o.str(it.key());
            if (!writeVariant(o, it.value(), depth + 1))
                return false;
        }
        return true;
    }
    default: return false;
    }
}

QVariant readVariant(In &in, int depth = 0)
{
    if (depth > MaxVariantDepth)
        return in.fail(), QVariant();
    switch (in.u8()) {
    case VInvalid: return QVariant();
    case VBool: return QVariant(in.boolean());
    case VLongLong: return QVariant(qlonglong(in.zigzag()));
    case VDouble: {
        const quint64 bits = in.u64();
        double d = 0;
        std::memcpy(&d, &bits, sizeof d);
        return QVariant(d);
    }
    case VString: return QVariant(in.str());
    case VStringList: {
        const qsizetype n = in.count();
        QStringList l;
        l.reserve(n);
        for (qsizetype i = 0; i < n && !in.failed(); ++i)
            l.append(in.str());
        return QVariant(l);
    }
    case VList: {
        const qsizetype n = in.count();
        QVariantList l;
        l.reserve(n);
        for (qsizetype i = 0; i < n && !in.failed(); ++i)
            l.append(readVariant(in, depth + 1));
        return QVariant(l);
    }
    case VMap: {
        const qsizetype n = in.count(2);
        QVariantMap m;
        for (qsizetype i = 0; i < n && !in.failed(); ++i) {
            const QString key = in.str();
            m.insert(key, readVariant(in, depth + 1));
        }
        return QVariant(m);
    }
    default: return in.fail(), QVariant();
    }
}

bool writeMap(Out &o, const QVariantMap &m)
{
    o.varint(quint64(m.size()));
    for (auto it = m.cbegin(); it != m.cend(); ++it) {
        o.str(it.key());
        if (!writeVariant(o, it.value(), 1))
            return false;
    }
    return true;
}

QVariantMap readMap(In &in)
{
    const qsizetype n = in.count(2);
    QVariantMap m;
    for (qsizetype i = 0; i < n && !in.failed(); ++i) {
        const QString key = in.str();
        m.insert(key, readVariant(in, 1));
    }
    return m;
}

void writeStrings(Out &o, const QStringList &l)
{
    o.varint(quint64(l.size()));
    for (const QString &s : l)
        o.str(s);
}

QStringList readStrings(In &in)
{
    const qsizetype n = in.count();
    QStringList l;
    l.reserve(n);
    for (qsizetype i = 0; i < n && !in.failed(); ++i)
        l.append(in.str());
    return l;
}

void writeDate(Out &o, const QDate &d)
{
    if (!d.isValid()) {
        o.u8(0);
        return;
    }
    o.u8(1);
    o.zigzag(d.toJulianDay());
}

QDate readDate(In &in)
{
    switch (in.u8()) {
    case 0: return QDate();
    case 1: {
        const qint64 jd = in.zigzag();
        const QDate d = QDate::fromJulianDay(jd);
        if (!d.isValid())
            in.fail();
        return d;
    }
    default: return in.fail(), QDate();
    }
}

void writeTime(Out &o, const std::optional<QTime> &t)
{
    if (!t) {
        o.u8(0);
    } else if (!t->isValid()) {
        o.u8(2);
    } else {
        o.u8(1);
        o.varint(quint64(t->msecsSinceStartOfDay()));
    }
}

std::optional<QTime> readTime(In &in)
{
    switch (in.u8()) {
    case 0: return std::nullopt;
    case 1: {
        const quint64 ms = in.varint();
        if (ms >= 24ULL * 3600 * 1000)
            return in.fail(), std::nullopt;
        return QTime::fromMSecsSinceStartOfDay(int(ms));
    }
    case 2: return QTime();
    default: return in.fail(), std::nullopt;
    }
}

void writeSpec(Out &o, const DateSpec &s)
{
    writeDate(o, s.date);
    writeTime(o, s.time);
}

DateSpec readSpec(In &in)
{
    DateSpec s;
    s.date = readDate(in);
    s.time = readTime(in);
    return s;
}

void writeOptSpec(Out &o, const std::optional<DateSpec> &s)
{
    o.boolean(s.has_value());
    if (s)
        writeSpec(o, *s);
}

std::optional<DateSpec> readOptSpec(In &in)
{
    if (!in.boolean())
        return std::nullopt;
    return readSpec(in);
}

void writeOptDate(Out &o, const std::optional<QDate> &d)
{
    o.boolean(d.has_value());
    if (d)
        writeDate(o, *d);
}

std::optional<QDate> readOptDate(In &in)
{
    if (!in.boolean())
        return std::nullopt;
    return readDate(in);
}

void writeTask(Out &o, const Task &t)
{
    o.varint(quint64(t.status));
    o.str(t.marker);
    o.str(t.text);
    o.zigzag(t.line);
    writeOptSpec(o, t.due);
    writeOptSpec(o, t.scheduled);
    writeOptSpec(o, t.start);
    writeOptDate(o, t.created);
    writeOptDate(o, t.doneDate);
    writeOptDate(o, t.cancelledDate);
    o.str(t.recurrence);
    o.varint(quint64(t.priority));
}

Task readTask(In &in)
{
    Task t;
    t.status = TaskStatus(in.small(int(TaskStatus::Deferred)));
    t.marker = in.str();
    t.text = in.str();
    t.line = in.intValue();
    t.due = readOptSpec(in);
    t.scheduled = readOptSpec(in);
    t.start = readOptSpec(in);
    t.created = readOptDate(in);
    t.doneDate = readOptDate(in);
    t.cancelledDate = readOptDate(in);
    t.recurrence = in.str();
    t.priority = Priority(in.small(int(Priority::Highest)));
    return t;
}

// ---- notes ----------------------------------------------------------------------------------------

/// A block's text: 0 = its own string (str), 1 = found in the note's text at the block's raw
/// lines (varint offset from the raw's start, varint length) — most texts are, so the cache
/// holds them as two small numbers and decoding copies them out of the note.
void writeText(Out &o, const QString &text, const BlockSource &raw, const QString &content)
{
    if (!text.isEmpty() && raw.isSlice() && raw.to() <= content.size()) {
        const qsizetype at = QStringView(content).sliced(raw.from(), raw.to() - raw.from()).indexOf(text);
        if (at >= 0) {
            o.u8(1);
            o.varint(quint64(at));
            o.varint(quint64(text.size()));
            return;
        }
    }
    o.u8(0);
    o.str(text);
}

/// A block's raw text: 0 = a slice of the note's text (from, to), 1 = its own string.
void writeRaw(Out &o, const BlockSource &raw)
{
    if (raw.isSlice()) {
        o.u8(0);
        o.varint(quint64(raw.from()));
        o.varint(quint64(raw.to()));
    } else {
        o.u8(1);
        o.str(raw.toString());
    }
}

/// The search section in steps (the index must not change in between: the Writer aborts first).
class SearchEncoder {
public:
    explicit SearchEncoder(const SearchIndex &search) : m_search(search) { }

    /// Append to `body` until `timer` passed `budgetMs` (nullptr: all); true when done.
    bool step(QByteArray &body, const QElapsedTimer *timer, int budgetMs)
    {
        using A = SearchIndexAccess;
        Out o(body);
        if (!m_started) {
            m_started = true;
            const QList<A::Doc> &docs = A::docs(m_search);
            m_renumbered = QList<int>(docs.size(), -1);
            qsizetype used = 0;
            for (qsizetype i = 0; i < docs.size(); ++i) {
                if (docs[i].used)
                    m_renumbered[i] = int(used++);
            }
            o.varint(quint64(used));
            for (const A::Doc &d : docs) {
                if (!d.used)
                    continue;
                o.str(d.id);
                o.u64(d.contentKey);
                for (const quint32 n : d.length)
                    o.varint(n);
            }
            o.varint(quint64(A::postings(m_search).size()));
            if (timer && timer->elapsed() >= budgetMs)
                return false;
        }
        const auto &postings = A::postings(m_search);
        auto it = m_last.isNull() ? postings.begin() : postings.upper_bound(m_last);
        for (; it != postings.end(); ++it) {
            const auto &[term, list] = *it;
            o.str(term);
            o.varint(quint64(list.size()));
            int previous = -1;
            for (const A::Posting &p : list) {
                const int doc = m_renumbered.value(p.doc, -1);
                o.varint(quint64(doc - previous)); // >= 1: sorted by doc
                previous = doc;
                for (const quint32 n : p.tf)
                    o.varint(n);
            }
            if (timer && timer->elapsed() >= budgetMs) {
                m_last = term;
                return std::next(it) == postings.end();
            }
        }
        return true;
    }

private:
    const SearchIndex &m_search;
    bool m_started = false;
    QList<int> m_renumbered;
    QString m_last; ///< the last term written (resume after it)
};

} // namespace

namespace detail {

std::optional<QByteArray> encodeNote(const ParsedNote &note, const QString &content)
{
    QByteArray body;
    Out o(body);
    if (!writeMap(o, note.frontmatter))
        return std::nullopt;
    if (!writeMap(o, note.properties))
        return std::nullopt;
    writeStrings(o, note.aliases);
    writeStrings(o, note.tags);
    o.varint(quint64(note.blocks.size()));
    for (const Block &b : note.blocks) {
        o.str(b.id);
        o.varint(quint64(b.kind));
        o.zigzag(b.line);
        o.zigzag(b.endLine);
        o.zigzag(b.ownEndLine);
        o.zigzag(b.depth);
        writeRaw(o, b.raw);
        writeText(o, b.text, b.raw, content);
        o.zigzag(b.headingLevel);
        o.str(b.lang);
        // The YAML frontmatter block shares the page's properties.
        const bool pageProperties =
                b.kind == BlockKind::Frontmatter && !b.properties.isEmpty() && b.properties == note.properties;
        o.boolean(pageProperties);
        if (!pageProperties && !writeMap(o, b.properties))
            return std::nullopt;
        o.boolean(b.task.has_value());
        if (b.task)
            writeTask(o, *b.task);
        o.varint(quint64(b.reminders.size()));
        for (const Reminder &r : b.reminders) {
            writeSpec(o, r.at);
            o.boolean(r.alarm);
            o.str(r.recurrence);
            const bool blockText = r.text.isNull() == b.text.isNull() && r.text == b.text;
            o.boolean(blockText);
            if (!blockText)
                o.str(r.text);
            o.zigzag(r.line);
        }
        writeStrings(o, b.tags);
        o.zigzag(b.parent);
    }
    o.varint(quint64(note.links.size()));
    for (const Link &l : note.links) {
        o.varint(quint64(l.kind));
        o.str(l.target);
        o.str(l.display);
        o.str(l.heading);
        o.str(l.blockId);
        o.zigzag(l.line);
        o.zigzag(l.from);
        o.zigzag(l.to);
    }
    return body;
}

std::optional<ParsedNote> decodeNote(QByteArrayView body, const VaultPath &path, const QString &content,
                                     const VaultConfig &config)
{
    In in(body);
    ParsedNote note;
    note.path = path;
    note.title = paths::pageTitle(path);
    note.frontmatter = readMap(in);
    note.properties = readMap(in);
    note.aliases = readStrings(in);
    note.tags = readStrings(in);
    const qsizetype blockCount = in.count(10);
    note.blocks.reserve(blockCount);
    for (qsizetype i = 0; i < blockCount && !in.failed(); ++i) {
        Block b;
        b.id = in.str();
        b.kind = BlockKind(in.small(int(BlockKind::Frontmatter)));
        b.line = in.intValue();
        b.endLine = in.intValue();
        b.ownEndLine = in.intValue();
        b.depth = in.intValue();
        if (b.line < 0 || b.endLine < b.line || b.ownEndLine < b.line || b.depth < 0)
            in.fail();
        switch (in.u8()) {
        case 0: {
            const quint64 from = in.varint();
            const quint64 to = in.varint();
            if (from > to || to > quint64(content.size()))
                return std::nullopt;
            b.raw = BlockSource(content, qsizetype(from), qsizetype(to));
            break;
        }
        case 1: b.raw = BlockSource(in.str()); break;
        default: return std::nullopt;
        }
        switch (in.u8()) {
        case 0: b.text = in.str(); break;
        case 1: {
            const quint64 at = in.varint();
            const quint64 length = in.varint();
            if (!b.raw.isSlice() || length == 0 || at > quint64(b.raw.size()) || length > quint64(b.raw.size()) - at)
                return std::nullopt;
            b.text = b.raw.view().sliced(qsizetype(at), qsizetype(length)).toString();
            break;
        }
        default: return std::nullopt;
        }
        b.headingLevel = in.intValue();
        b.lang = in.str();
        if (in.boolean())
            b.properties = note.properties;
        else
            b.properties = readMap(in);
        if (in.boolean())
            b.task = readTask(in);
        const qsizetype reminders = in.count(5);
        b.reminders.reserve(reminders);
        for (qsizetype r = 0; r < reminders && !in.failed(); ++r) {
            Reminder rem;
            rem.at = readSpec(in);
            rem.alarm = in.boolean();
            rem.recurrence = in.str();
            rem.text = in.boolean() ? b.text : in.str();
            rem.line = in.intValue();
            b.reminders.append(std::move(rem));
        }
        b.tags = readStrings(in);
        b.parent = in.intValue();
        if (b.parent < -1 || b.parent >= i)
            return std::nullopt; // parents precede their children
        const int index = int(note.blocks.size());
        if (b.kind == BlockKind::Heading && b.parent < 0 && b.headingLevel > 0)
            note.headings.append(Heading{b.headingLevel, b.text, b.line});
        if (b.task)
            note.tasks.append(*b.task);
        note.reminders.append(b.reminders);
        if (!b.id.isEmpty() && !note.blockIds.contains(b.id))
            note.blockIds.insert(b.id, index);
        if (b.parent >= 0)
            note.blocks[b.parent].children.append(index);
        note.blocks.append(std::move(b));
    }
    const qsizetype linkCount = in.count(8);
    note.links.reserve(linkCount);
    for (qsizetype i = 0; i < linkCount && !in.failed(); ++i) {
        Link l;
        l.kind = LinkKind(in.small(int(LinkKind::Url)));
        l.target = in.str();
        l.display = in.str();
        l.heading = in.str();
        l.blockId = in.str();
        l.line = in.intValue();
        l.from = in.intValue();
        l.to = in.intValue();
        if (l.line < 0 || l.from < 0 || l.from > l.to || l.to > content.size())
            return std::nullopt;
        note.links.append(std::move(l));
    }
    if (in.failed() || !in.atEnd())
        return std::nullopt;
    note.journalDate = markdown::journalDateFromPath(path, config);
    // As parseNote() leaves it: no growth slack.
    note.headings.squeeze();
    note.tasks.squeeze();
    note.reminders.squeeze();
    for (Block &b : note.blocks) {
        if (b.children.capacity() > b.children.size())
            b.children.squeeze();
    }
    return note;
}

QByteArray encodeSearch(const SearchIndex &search)
{
    QByteArray body;
    SearchEncoder encoder(search);
    while (!encoder.step(body, nullptr, 0)) { }
    return body;
}

} // namespace detail

// ---- the search decoder (steps) ----------------------------------------------------------------------

struct Reader::SearchDecoder::Private {
    QByteArray file; ///< keeps the bytes alive
    In in{QByteArrayView()};
    SearchIndex out;
    qsizetype docCount = 0;
    qsizetype termsLeft = -1; ///< -1: the documents are not read yet
    bool done = false;
    bool failed = false;

    bool fail()
    {
        failed = true;
        done = true;
        out = SearchIndex();
        return true;
    }

    /// Read the documents (one go: a few hundred KB at most).
    bool readDocs()
    {
        using A = SearchIndexAccess;
        docCount = in.count(11);
        QList<A::Doc> &docs = A::docs(out);
        QHash<QString, int> &byId = A::byId(out);
        auto &totals = A::totals(out);
        docs.reserve(docCount);
        for (qsizetype i = 0; i < docCount && !in.failed(); ++i) {
            A::Doc d;
            d.id = in.str();
            d.contentKey = in.u64();
            for (quint32 &n : d.length) {
                const quint64 v = in.varint();
                if (v > std::numeric_limits<quint32>::max())
                    return fail();
                n = quint32(v);
            }
            d.used = true;
            if (d.id.isNull() || byId.contains(d.id))
                return fail();
            for (int f = 0; f < SearchIndex::FieldCount; ++f)
                totals[f] += d.length[f];
            byId.insert(d.id, int(docs.size()));
            docs.append(std::move(d));
        }
        termsLeft = in.count(3);
        return in.failed() ? fail() : false;
    }

    /// One term with its postings.
    bool readTerm(const QString *previous)
    {
        using A = SearchIndexAccess;
        const QString term = in.str();
        if (in.failed() || term.isEmpty() || (previous && !(*previous < term)))
            return fail(); // terms are sorted and distinct
        const qsizetype n = in.count(4);
        if (in.failed() || n == 0)
            return fail();
        auto &postings = A::postings(out);
        const auto entry = postings.emplace_hint(postings.end(), term, A::PostingList());
        A::PostingList &list = entry->second;
        list.reserve(n);
        QList<A::Doc> &docs = A::docs(out);
        qint64 doc = -1;
        for (qsizetype i = 0; i < n; ++i) {
            const quint64 delta = in.varint();
            if (in.failed() || delta == 0 || delta > quint64(docCount))
                return fail();
            doc += qint64(delta);
            if (doc >= docCount)
                return fail();
            A::Posting p;
            p.doc = int(doc);
            for (quint32 &tf : p.tf) {
                const quint64 v = in.varint();
                if (v > std::numeric_limits<quint32>::max())
                    return fail();
                tf = quint32(v);
            }
            list.append(p);
            docs[doc].terms.append(&entry->first);
        }
        return in.failed() ? fail() : false;
    }

    bool step(int budgetMs)
    {
        if (done)
            return true;
        QElapsedTimer timer;
        timer.start();
        if (termsLeft < 0 && readDocs())
            return true;
        const auto &postings = SearchIndexAccess::postings(out);
        while (termsLeft > 0) {
            const QString *previous = postings.empty() ? nullptr : &postings.rbegin()->first;
            if (readTerm(previous))
                return true;
            --termsLeft;
            if (timer.elapsed() >= budgetMs)
                break;
        }
        if (termsLeft == 0) {
            done = true;
            if (!in.atEnd())
                return fail();
            for (SearchIndexAccess::Doc &d : SearchIndexAccess::docs(out))
                d.terms.squeeze();
        }
        return done;
    }
};

Reader::SearchDecoder::SearchDecoder(std::unique_ptr<Private> p) : d(std::move(p)) { }
Reader::SearchDecoder::SearchDecoder(SearchDecoder &&) noexcept = default;
Reader::SearchDecoder &Reader::SearchDecoder::operator=(SearchDecoder &&) noexcept = default;
Reader::SearchDecoder::~SearchDecoder() = default;

bool Reader::SearchDecoder::step(int budgetMs)
{
    return d->step(budgetMs);
}

bool Reader::SearchDecoder::failed() const
{
    return d->failed;
}

SearchIndex Reader::SearchDecoder::take()
{
    if (d->failed || !d->done)
        return SearchIndex();
    return std::move(d->out);
}

namespace detail {

std::optional<SearchIndex> decodeSearch(QByteArrayView body)
{
    auto p = std::make_unique<Reader::SearchDecoder::Private>();
    p->in = In(body);
    while (!p->step(std::numeric_limits<int>::max())) { }
    if (p->failed)
        return std::nullopt;
    return std::move(p->out);
}

} // namespace detail

// ---- Identity, Reader -------------------------------------------------------------------------------

Identity Identity::current(const VaultConfig &config, const QString &vaultId)
{
    Identity id;
    id.format = FormatVersion;
    id.parser = quint32(markdown::ParserVersion);
    id.search = SearchIndex::FormatVersion;
    id.qt = QString::fromLatin1(qVersion());
    id.app = QCoreApplication::applicationVersion();
    id.parse = VaultIndex::parseFingerprint(config);
    id.vaultId = vaultId;
    return id;
}

namespace {

QByteArray encodeHeader(const Identity &id)
{
    QByteArray h;
    Out o(h);
    o.raw(HeaderMagic, 8);
    o.u32(ByteOrderMark);
    o.varint(id.format);
    o.varint(id.parser);
    o.varint(id.search);
    o.str(id.qt);
    o.str(id.app);
    o.str(id.parse);
    o.str(id.vaultId);
    o.u64(text::fnv1a64(h));
    return h;
}

} // namespace

Reader::~Reader() = default;

std::shared_ptr<const Reader> Reader::open(QByteArray file, const Identity &expected, QString *why)
{
    auto reject = [why](const QString &reason) -> std::shared_ptr<const Reader> {
        if (why)
            *why = reason;
        return nullptr;
    };
    const qint64 size = file.size();
    if (size < 12 + FooterSize)
        return reject(u"too short"_s);
    In header{QByteArrayView(file)};
    if (header.take(8) != QByteArrayView(HeaderMagic, 8))
        return reject(u"not an index cache"_s);
    if (header.u32() != ByteOrderMark)
        return reject(u"byte order"_s);
    Identity found;
    found.format = quint32(header.varint());
    if (header.failed() || found.format != expected.format)
        return reject(u"format %1 (expected %2)"_s.arg(found.format).arg(expected.format));
    found.parser = quint32(header.varint());
    found.search = quint32(header.varint());
    found.qt = header.str();
    found.app = header.str();
    found.parse = header.str();
    found.vaultId = header.str();
    const qint64 headerEnd = size - header.remaining();
    const quint64 headerSum = header.u64();
    if (header.failed() || headerSum != text::fnv1a64(QByteArrayView(file).first(headerEnd)))
        return reject(u"header checksum"_s);
    if (found.parser != expected.parser)
        return reject(u"parser version %1 (expected %2)"_s.arg(found.parser).arg(expected.parser));
    if (found.search != expected.search)
        return reject(u"search format %1 (expected %2)"_s.arg(found.search).arg(expected.search));
    if (found.qt != expected.qt || found.app != expected.app)
        return reject(u"written by Qt %1 / Brulion %2"_s.arg(found.qt, found.app));
    if (found.parse != expected.parse)
        return reject(u"another notebook configuration"_s);
    if (found.vaultId != expected.vaultId)
        return reject(u"another notebook"_s);
    const qint64 recordsStart = headerEnd + 8;

    In footer(QByteArrayView(file).last(FooterSize));
    const quint64 dirOffset = footer.u64();
    const quint64 dirLength = footer.u64();
    const quint64 searchOffset = footer.u64();
    const quint64 searchLength = footer.u64();
    const quint32 noteCount = footer.u32();
    footer.u32(); // flags (none yet)
    if (footer.take(8) != QByteArrayView(FooterMagic, 8))
        return reject(u"footer"_s);
    const quint64 dataEnd = quint64(size - FooterSize);
    if (dirOffset < quint64(recordsStart) || dirLength > dataEnd || dirOffset > dataEnd - dirLength
        || dataEnd - dirOffset - dirLength != 8)
        return reject(u"directory bounds"_s);
    const QByteArrayView dirBytes = QByteArrayView(file).sliced(qsizetype(dirOffset), qsizetype(dirLength));
    In sum(QByteArrayView(file).sliced(qsizetype(dirOffset + dirLength), 8));
    if (sum.u64() != text::fnv1a64(dirBytes))
        return reject(u"directory checksum"_s);
    const quint64 recordsEnd = searchLength > 0 ? searchOffset : dirOffset;
    if (searchLength > 0
        && (searchOffset < quint64(recordsStart) || searchLength > dirOffset || searchOffset > dirOffset - searchLength
            || dirOffset - searchOffset - searchLength != 8))
        return reject(u"search bounds"_s);

    std::shared_ptr<Reader> reader(new Reader);
    In dir(dirBytes);
    const qsizetype count = dir.count(12);
    if (dir.failed() || quint64(count) != noteCount)
        return reject(u"directory"_s);
    reader->m_records.reserve(count);
    QList<std::pair<qint64, qint64>> spans;
    spans.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        Record r;
        r.path = dir.str();
        r.fileSize = qint64(dir.varint());
        r.mtime = dir.zigzag();
        r.contentKey = dir.u64();
        const quint64 offset = dir.varint();
        const quint64 length = dir.varint();
        if (dir.failed() || r.path.isEmpty() || r.fileSize < 0)
            return reject(u"directory entry"_s);
        if (length > 0) {
            // The body and its checksum inside the records.
            if (offset < quint64(recordsStart) || length > recordsEnd || offset > recordsEnd - length
                || recordsEnd - offset - length < 8)
                return reject(u"record bounds"_s);
            spans.append({qint64(offset), qint64(offset + length + 8)});
        }
        r.offset = qint64(offset);
        r.length = qint64(length);
        if (reader->m_records.contains(r.path))
            return reject(u"duplicate path"_s);
        reader->m_records.insert(r.path, r);
    }
    if (!dir.atEnd())
        return reject(u"directory"_s);
    std::sort(spans.begin(), spans.end());
    for (qsizetype i = 1; i < spans.size(); ++i) {
        if (spans[i].first < spans[i - 1].second)
            return reject(u"overlapping records"_s);
    }
    reader->m_searchOffset = qint64(searchOffset);
    reader->m_searchLength = qint64(searchLength);
    reader->m_file = std::move(file);
    return reader;
}

const Record *Reader::find(const VaultPath &path) const
{
    const auto it = m_records.constFind(path);
    return it == m_records.cend() ? nullptr : &*it;
}

qsizetype Reader::noteCount() const
{
    return m_records.size();
}

std::optional<ParsedNote> Reader::decodeNote(const Record &record, const VaultPath &path, const QString &content,
                                             const VaultConfig &config) const
{
    if (record.length <= 0 || record.path != path)
        return std::nullopt;
    const QByteArrayView body = QByteArrayView(m_file).sliced(record.offset, record.length);
    In sum(QByteArrayView(m_file).sliced(record.offset + record.length, 8));
    if (sum.u64() != text::fnv1a64(body))
        return std::nullopt;
    return detail::decodeNote(body, path, content, config);
}

bool Reader::hasSearch() const
{
    return m_searchLength > 0;
}

Reader::SearchDecoder Reader::searchDecoder() const
{
    auto p = std::make_unique<SearchDecoder::Private>();
    p->file = m_file;
    if (m_searchLength <= 0) {
        p->fail();
        return SearchDecoder(std::move(p));
    }
    const QByteArrayView body = QByteArrayView(p->file).sliced(m_searchOffset, m_searchLength);
    In sum(QByteArrayView(p->file).sliced(m_searchOffset + m_searchLength, 8));
    if (sum.u64() != text::fnv1a64(body)) {
        p->fail();
        return SearchDecoder(std::move(p));
    }
    p->in = In(body);
    return SearchDecoder(std::move(p));
}

// ---- Writer ------------------------------------------------------------------------------------------

struct Writer::Private {
    const VaultIndex &index;
    Identity identity;
    StatFn stat;
    int version = 0;
    bool started = false;
    bool withSearch = false;
    QList<VaultPath> paths;
    qsizetype next = 0;
    QByteArray directory;
    qsizetype written = 0;
    quint64 searchOffset = 0;
    quint64 searchLength = 0;
    bool searchDone = false;
    std::optional<SearchEncoder> searchEncoder;
    QByteArray searchBody;

    Private(const VaultIndex &i, Identity id, StatFn s) : index(i), identity(std::move(id)), stat(std::move(s)) { }
};

Writer::Writer(const VaultIndex &index, Identity identity, StatFn stat)
    : d(std::make_unique<Private>(index, std::move(identity), std::move(stat)))
{ }

Writer::~Writer() = default;

qsizetype Writer::notesWritten() const
{
    return d->written;
}

bool Writer::hasSearch() const
{
    return d->withSearch;
}

Writer::State Writer::step(int budgetMs, QByteArray *out)
{
    QElapsedTimer timer;
    timer.start();
    if (!d->started) {
        d->started = true;
        d->version = d->index.version();
        d->withSearch = index_detail::IndexAccess::searchComplete(d->index);
        d->paths = d->index.paths();
        out->clear();
        out->append(encodeHeader(d->identity));
        Out dir(d->directory);
        dir.varint(quint64(d->paths.size()));
    }
    if (d->index.version() != d->version)
        return State::Aborted;
    Out o(*out);
    Out dir(d->directory);
    while (d->next < d->paths.size()) {
        const VaultPath &path = d->paths[d->next++];
        const index_detail::Entry *e = index_detail::IndexAccess::entry(d->index, path);
        const std::optional<FileStat> st = d->stat ? d->stat(path) : std::nullopt;
        const std::optional<QByteArray> body = e ? detail::encodeNote(e->note, e->content) : std::nullopt;
        dir.str(path);
        dir.varint(quint64(st ? std::max<qint64>(0, st->size) : 0));
        dir.zigzag(st ? st->mtime : 0);
        dir.u64(e ? e->contentKey : 0);
        if (body && st) {
            dir.varint(quint64(out->size()));
            dir.varint(quint64(body->size()));
            o.raw(body->constData(), body->size());
            o.u64(text::fnv1a64(*body));
            ++d->written;
        } else {
            dir.varint(0); // parsed on load
            dir.varint(0);
        }
        if (timer.elapsed() >= budgetMs)
            return State::More;
    }
    if (d->withSearch && !d->searchDone) {
        if (!d->searchEncoder)
            d->searchEncoder.emplace(index_detail::IndexAccess::search(d->index));
        if (!d->searchEncoder->step(d->searchBody, &timer, budgetMs))
            return State::More;
        d->searchDone = true;
        d->searchOffset = quint64(out->size());
        d->searchLength = quint64(d->searchBody.size());
        o.raw(d->searchBody.constData(), d->searchBody.size());
        o.u64(text::fnv1a64(d->searchBody));
        d->searchBody = QByteArray();
        if (timer.elapsed() >= budgetMs)
            return State::More;
    }
    const quint64 dirOffset = quint64(out->size());
    o.raw(d->directory.constData(), d->directory.size());
    o.u64(text::fnv1a64(d->directory));
    o.u64(dirOffset);
    o.u64(quint64(d->directory.size()));
    o.u64(d->withSearch ? d->searchOffset : 0);
    o.u64(d->withSearch ? d->searchLength : 0);
    o.u32(quint32(d->paths.size()));
    o.u32(0);
    o.raw(FooterMagic, 8);
    return State::Done;
}

QByteArray encode(const VaultIndex &index, const Identity &identity, const StatFn &stat)
{
    Writer writer(index, identity, stat);
    QByteArray out;
    while (writer.step(std::numeric_limits<int>::max(), &out) == Writer::State::More) { }
    return out;
}

} // namespace indexcache
} // namespace brulion
