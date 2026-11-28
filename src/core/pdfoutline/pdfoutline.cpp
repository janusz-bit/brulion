#include "pdfoutline/pdfoutline.h"

#include "io/zip.h"

#include <QByteArray>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>
#include <variant>

namespace brulion::pdfoutline {

namespace {

// ---- objects ------------------------------------------------------------------------------------

struct Object;
using Array = QList<Object>;
using Dict = QMap<QByteArray, Object>;
struct Ref {
    int num = 0;
    int gen = 0;
};
struct Name {
    QByteArray name;
};
struct String {
    QByteArray bytes;
};
struct Null { };

/// A PDF object: null, a boolean, a number, a string, a name, an array, a dictionary or a reference.
/// (A stream is its dictionary; its data is read by Document::streamData.)
struct Object {
    std::variant<Null, bool, double, String, Name, std::shared_ptr<Array>, std::shared_ptr<Dict>, Ref> value;

    /// The dictionary or array held (nullptr: it holds another kind).
    template<typename T>
    const T *held() const
    {
        const auto *p = std::get_if<std::shared_ptr<T>>(&value);
        return p ? p->get() : nullptr;
    }
    const Dict *dict() const { return held<Dict>(); }
    const Array *array() const { return held<Array>(); }
    const Ref *ref() const { return std::get_if<Ref>(&value); }
    std::optional<double> number() const
    {
        const auto *n = std::get_if<double>(&value);
        return n ? std::optional<double>(*n) : std::nullopt;
    }
    const QByteArray *name() const
    {
        const auto *n = std::get_if<Name>(&value);
        return n ? &n->name : nullptr;
    }
    const QByteArray *string() const
    {
        const auto *s = std::get_if<String>(&value);
        return s ? &s->bytes : nullptr;
    }
    bool isNull() const { return std::holds_alternative<Null>(value); }
};

const Object NullObject{};

const Object &get(const Dict *d, const char *key)
{
    if (!d)
        return NullObject;
    const auto it = d->constFind(QByteArray(key));
    return it == d->constEnd() ? NullObject : *it;
}

// ---- the lexer and parser -----------------------------------------------------------------------

constexpr int MaxNesting = 64;

bool oneOf(char c, std::string_view set)
{
    return set.find(c) != std::string_view::npos;
}

bool isSpace(char c)
{
    return oneOf(c, std::string_view(" \n\r\t\f\0", 6));
}

bool isDelimiter(char c)
{
    return oneOf(c, "()<>[]{}/%");
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

/// Reads objects from `data` at `pos`.
class Parser {
public:
    /// (`pos` comes from the file — an offset of its tables, an object stream's header —: one
    /// outside the data is its end.)
    Parser(QByteArrayView data, qsizetype pos) : m_data(data), m_pos(std::clamp<qsizetype>(pos, 0, data.size())) { }

    qsizetype pos() const { return m_pos; }
    bool atEnd() { return skipSpace(), m_pos >= m_data.size(); }

    /// The next object; nullopt at the end, on a malformed or too deeply nested one.
    std::optional<Object> object(int depth = 0)
    {
        if (depth > MaxNesting || atEnd())
            return std::nullopt;
        const char c = m_data[m_pos];
        if (c == '/')
            return Object{Name{name()}};
        if (c == '(')
            return literal();
        if (c == '<')
            return angle(depth);
        if (c == '[')
            return array(depth);
        if (oneOf(c, "+-.") || isDigit(c))
            return numberOrRef();
        return keyword();
    }

    /// "<<": a dictionary, "<": a hexadecimal string.
    std::optional<Object> angle(int depth)
    {
        const bool twice = m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '<';
        return twice ? dictionary(depth) : hexString();
    }

    /// true, false or null; nullopt for any other word.
    std::optional<Object> keyword()
    {
        const QByteArray w = word();
        if (w == "null")
            return Object{};
        if (w != "true" && w != "false")
            return std::nullopt;
        return Object{w == "true"};
    }

    /// The next word (keyword, number) without moving past it when `peek`.
    QByteArray word(bool peek = false)
    {
        skipSpace();
        qsizetype end = m_pos;
        while (end < m_data.size() && !isSpace(m_data[end]) && !isDelimiter(m_data[end]))
            ++end;
        const QByteArray w = m_data.sliced(m_pos, end - m_pos).toByteArray();
        if (!peek)
            m_pos = end;
        return w;
    }

    void skipSpace()
    {
        while (m_pos < m_data.size()) {
            if (isSpace(m_data[m_pos])) {
                ++m_pos;
            } else if (m_data[m_pos] == '%') {
                while (m_pos < m_data.size() && m_data[m_pos] != '\n' && m_data[m_pos] != '\r')
                    ++m_pos;
            } else {
                break;
            }
        }
    }

private:
    QByteArray name()
    {
        ++m_pos;
        QByteArray out;
        while (m_pos < m_data.size() && !isSpace(m_data[m_pos]) && !isDelimiter(m_data[m_pos])) {
            if (m_data[m_pos] == '#' && m_pos + 2 < m_data.size()) {
                bool ok = false;
                const int c = m_data.sliced(m_pos + 1, 2).toByteArray().toInt(&ok, 16);
                if (ok) {
                    out += char(c);
                    m_pos += 3;
                    continue;
                }
            }
            out += m_data[m_pos++];
        }
        return out;
    }

    std::optional<Object> literal()
    {
        ++m_pos;
        QByteArray out;
        int open = 1;
        while (m_pos < m_data.size()) {
            const char c = m_data[m_pos++];
            if (c == '\\') {
                escape(out);
            } else if (c == '(') {
                ++open;
                out += c;
            } else if (c == ')') {
                if (--open == 0)
                    return Object{String{out}};
                out += c;
            } else {
                out += c;
            }
        }
        return std::nullopt; // unterminated
    }

    /// After a backslash of a literal string: its escape into `out`.
    void escape(QByteArray &out)
    {
        if (m_pos >= m_data.size())
            return;
        const char c = m_data[m_pos++];
        static const QHash<char, char> simple{{'n', '\n'}, {'r', '\r'}, {'t', '\t'}, {'b', '\b'},
                                              {'f', '\f'}, {'(', '('},  {')', ')'},  {'\\', '\\'}};
        if (const auto it = simple.constFind(c); it != simple.constEnd())
            out += *it;
        else if (isOctal(c))
            out += octal(c - '0');
        else if (c == '\r')
            skipIf('\n'); // a continued line
        else if (c != '\n')
            out += c; // an unknown escape: the character itself
    }

    static bool isOctal(char c) { return c >= '0' && c <= '7'; }

    /// An octal escape's character, its first digit `v` read (up to two more follow).
    char octal(int v)
    {
        for (int k = 0; k < 2 && m_pos < m_data.size() && isOctal(m_data[m_pos]); ++k)
            v = v * 8 + (m_data[m_pos++] - '0');
        return char(v & 0xff);
    }

    void skipIf(char c)
    {
        if (m_pos < m_data.size() && m_data[m_pos] == c)
            ++m_pos;
    }

    std::optional<Object> hexString()
    {
        ++m_pos;
        QByteArray hex;
        while (m_pos < m_data.size() && m_data[m_pos] != '>') {
            if (!isSpace(m_data[m_pos]))
                hex += m_data[m_pos];
            ++m_pos;
        }
        if (m_pos >= m_data.size())
            return std::nullopt;
        ++m_pos;
        if (hex.size() % 2)
            hex += '0';
        return Object{String{QByteArray::fromHex(hex)}};
    }

    std::optional<Object> array(int depth)
    {
        ++m_pos;
        auto out = std::make_shared<Array>();
        for (;;) {
            if (atEnd())
                return std::nullopt;
            if (m_data[m_pos] == ']') {
                ++m_pos;
                return Object{out};
            }
            auto item = object(depth + 1);
            if (!item)
                return std::nullopt;
            out->append(std::move(*item));
        }
    }

    std::optional<Object> dictionary(int depth)
    {
        m_pos += 2;
        auto out = std::make_shared<Dict>();
        for (;;) {
            if (atEnd())
                return std::nullopt;
            if (m_data[m_pos] == '>') {
                if (m_pos + 1 >= m_data.size() || m_data[m_pos + 1] != '>')
                    return std::nullopt;
                m_pos += 2;
                return Object{out};
            }
            if (m_data[m_pos] != '/')
                return std::nullopt;
            const QByteArray key = name();
            auto value = object(depth + 1);
            if (!value)
                return std::nullopt;
            out->insert(key, std::move(*value));
        }
    }

    std::optional<Object> numberOrRef()
    {
        bool ok = false;
        const double n = word().toDouble(&ok);
        if (!ok)
            return std::nullopt;
        // "n g R": a reference.
        const qsizetype back = m_pos;
        bool genOk = false;
        const QByteArray gen = word();
        const int g = gen.toInt(&genOk);
        if (genOk && n == std::floor(n) && n >= 0 && word() == "R")
            return Object{Ref{int(n), g}};
        m_pos = back;
        return Object{n};
    }

    QByteArrayView m_data;
    qsizetype m_pos = 0;
};

// ---- the document ---------------------------------------------------------------------------------

/// Where an object is: at an offset of the file, or in an object stream (found by its number in
/// the stream's header).
struct Location {
    qsizetype offset = -1;
    int stream = 0;
};

class Document {
public:
    Document(QByteArrayView data, const Limits &limits) : m_data(data), m_limits(limits) { readXref(); }

    const Dict *trailer() const { return m_trailer.dict(); }

    /// Object `num`, references followed (a chain of them at most a few long); null when none.
    Object resolve(const Object &o, int depth = 0)
    {
        const Ref *r = o.ref();
        if (!r || depth > 8)
            return r ? Object{} : o;
        return resolve(objectNumber(r->num), depth + 1);
    }

    Object objectNumber(int num)
    {
        if (const auto it = m_cache.constFind(num); it != m_cache.constEnd())
            return *it;
        if (m_cache.size() >= m_limits.maxObjects || m_loading.contains(num))
            return {};
        m_loading.insert(num);
        Object o = load(num);
        m_loading.remove(num);
        m_cache.insert(num, o);
        return o;
    }

private:
    Object load(int num)
    {
        const Location at = m_xref.value(num);
        if (at.stream > 0)
            return fromObjectStream(at.stream, num);
        if (at.offset >= 0) {
            if (auto o = objectAt(at.offset, num))
                return *o;
        }
        // The table is wrong (or there is none): the file's own "n g obj".
        scanOnce();
        if (const qsizetype offset = m_scanned.value(num, -1); offset >= 0 && offset != at.offset) {
            if (auto o = objectAt(offset, num))
                return *o;
        }
        return {};
    }

    /// The object "num g obj …" at `offset` (nullopt when that is not there); a stream's data
    /// offset remembered for streamData().
    std::optional<Object> objectAt(qsizetype offset, int num)
    {
        Parser p(m_data, offset);
        bool ok = false;
        if (p.word().toInt(&ok) != num || !ok)
            return std::nullopt;
        p.word(); // the generation
        if (p.word() != "obj")
            return std::nullopt;
        auto o = p.object();
        if (o && o->dict() && p.word(true) == "stream") {
            p.word();
            m_streams.insert(num, afterEol(p.pos()));
        }
        return o;
    }

    /// `at` past one end of line (CR LF, LF or CR).
    qsizetype afterEol(qsizetype at) const
    {
        if (at < m_data.size() && m_data[at] == '\r')
            ++at;
        if (at < m_data.size() && m_data[at] == '\n')
            ++at;
        return at;
    }

    void scanOnce()
    {
        if (m_scannedDone)
            return;
        m_scannedDone = true;
        // "n g obj" at the start of a line (or the file): the last one of a number wins.
        for (qsizetype at = m_data.indexOf("obj"); at >= 0 && m_scanned.size() <= m_limits.maxObjects;
             at = m_data.indexOf("obj", at + 3)) {
            if (const auto [num, start] = objectHeaderBefore(at); start >= 0)
                m_scanned.insert(num, start);
        }
        if (!m_trailer.dict())
            scanTrailer();
    }

    /// Back from the "obj" at `at` over "num gen ": the number and where it starts (−1: none).
    std::pair<int, qsizetype> objectHeaderBefore(qsizetype at) const
    {
        const auto back = [this](qsizetype s, bool (*what)(char)) {
            while (s >= 0 && what(m_data[s]))
                --s;
            return s;
        };
        qsizetype s = back(back(back(at - 1, isSpace), isDigit), isSpace); // spaces, the generation, spaces
        const qsizetype numEnd = s + 1;
        s = back(s, isDigit);
        bool ok = false;
        const int num = m_data.sliced(s + 1, numEnd - s - 1).toByteArray().toInt(&ok);
        return ok && (s < 0 || isSpace(m_data[s])) ? std::pair(num, s + 1) : std::pair(0, qsizetype(-1));
    }

    void scanTrailer()
    {
        const qsizetype at = m_data.lastIndexOf("trailer");
        if (at >= 0) {
            Parser p(m_data, at + 7);
            if (auto o = p.object(); o && o->dict())
                m_trailer = *o;
        }
    }

public:
    /// The decoded data of stream object `num` (its /Filter FlateDecode or none, a PNG predictor);
    /// nullopt when it cannot be read.
    std::optional<QByteArray> streamData(int num)
    {
        const Object o = objectNumber(num);
        const Dict *d = o.dict();
        const auto start = m_streams.constFind(num);
        if (!d || start == m_streams.constEnd())
            return std::nullopt;
        const qsizetype length = streamLength(d, *start);
        if (length < 0)
            return std::nullopt;
        const auto raw = decoded(m_data.sliced(*start, length).toByteArray(), resolve(get(d, "Filter")));
        if (!raw)
            return std::nullopt;
        return unpredicted(*raw, resolve(get(d, "DecodeParms")).dict());
    }

private:
    /// A stream's length: its /Length, else up to "endstream" (−1: neither fits the file).
    qsizetype streamLength(const Dict *d, qsizetype start)
    {
        const qsizetype length = qsizetype(resolve(get(d, "Length")).number().value_or(-1));
        if (length >= 0 && start + length <= m_data.size())
            return length;
        const qsizetype end = m_data.indexOf("endstream", start);
        return end < 0 ? -1 : end - start;
    }

    /// `raw` through `filter` (none, or FlateDecode alone); nullopt for any other filter.
    std::optional<QByteArray> decoded(const QByteArray &raw, const Object &filter) const
    {
        const QByteArray *f = filter.name();
        if (const Array *list = filter.array(); list && list->size() == 1)
            f = list->first().name();
        if (f && *f == "FlateDecode")
            return io::gunzip(raw, m_limits.maxStreamBytes);
        return filter.isNull() ? std::optional<QByteArray>(raw) : std::nullopt;
    }

    /// `data` without its PNG predictor (/Predictor 10…15, /Columns).
    static std::optional<QByteArray> unpredicted(const QByteArray &data, const Dict *parms)
    {
        const int predictor = int(get(parms, "Predictor").number().value_or(1));
        if (predictor < 10)
            return data;
        const int columns = std::clamp(int(get(parms, "Columns").number().value_or(1)), 1, 1 << 16);
        QByteArray out;
        QByteArray above(columns, '\0');
        for (qsizetype at = 0; at < data.size(); at += columns + 1) {
            QByteArray row = data.mid(at + 1, columns);
            row.resize(columns, '\0');
            unpredictRow(row, above, quint8(data[at]));
            out += row;
            above = row;
        }
        return out;
    }

    /// One row of PNG filter `type` (0 None, 1 Sub, 2 Up, 3 Average, 4 Paeth) back to its bytes.
    static void unpredictRow(QByteArray &row, const QByteArray &above, int type)
    {
        for (qsizetype i = 0; i < row.size(); ++i) {
            const int left = i > 0 ? quint8(row[i - 1]) : 0;
            const int upLeft = i > 0 ? quint8(above[i - 1]) : 0;
            row[i] = char(quint8(row[i]) + predicted(type, left, quint8(above[i]), upLeft));
        }
    }

    static int predicted(int type, int left, int up, int upLeft)
    {
        switch (type) {
        case 1: return left;
        case 2: return up;
        case 3: return (left + up) / 2;
        case 4: return paeth(left, up, upLeft);
        default: return 0;
        }
    }

    static int paeth(int left, int up, int upLeft)
    {
        const int p = left + up - upLeft;
        const int pa = std::abs(p - left), pb = std::abs(p - up), pc = std::abs(p - upLeft);
        if (pa <= pb && pa <= pc)
            return left;
        return pb <= pc ? up : upLeft;
    }

    Object fromObjectStream(int stream, int num)
    {
        const auto data = streamData(stream);
        const Dict *d = objectNumber(stream).dict();
        if (!data || !d)
            return {};
        const qsizetype at = offsetInObjectStream(*data, int(get(d, "N").number().value_or(0)), num);
        if (at < 0)
            return {};
        Parser p(*data, qsizetype(get(d, "First").number().value_or(0)) + at);
        return p.object().value_or(Object{});
    }

    /// Where object `num` starts among the `count` objects of an object stream's `data` (its header
    /// of "number offset" pairs; −1: not there).
    static qsizetype offsetInObjectStream(const QByteArray &data, int count, int num)
    {
        Parser header(data, 0);
        for (int i = 0; i < count; ++i) {
            bool okNum = false, okAt = false;
            const int n = header.word().toInt(&okNum);
            const qsizetype at = header.word().toLongLong(&okAt);
            if (!okNum || !okAt)
                return -1;
            if (n == num)
                return at;
        }
        return -1;
    }

    void readXref()
    {
        const qsizetype tail = std::max<qsizetype>(0, m_data.size() - 1024);
        const qsizetype at = m_data.lastIndexOf("startxref");
        QSet<qsizetype> seen;
        if (at >= tail && at >= 0) {
            Parser p(m_data, at + 9);
            bool ok = false;
            qsizetype offset = p.word().toLongLong(&ok);
            while (ok && offset >= 0 && offset < m_data.size() && !seen.contains(offset) && seen.size() < 64) {
                seen.insert(offset);
                const auto prev = readSection(offset);
                ok = prev.has_value();
                offset = prev.value_or(-1);
            }
        }
        if (!m_trailer.dict())
            scanOnce();
    }

    /// The table or stream at `offset` into m_xref (entries already known stay: newer first); its
    /// /Prev (nullopt: none or unreadable).
    std::optional<qsizetype> readSection(qsizetype offset)
    {
        Parser p(m_data, offset);
        const auto read = p.word(true) == "xref" ? tableSection(p) : readStreamAt(offset);
        if (!read)
            return std::nullopt;
        const Object &dict = *read;
        if (!m_trailer.dict())
            m_trailer = dict;
        const auto prev = get(dict.dict(), "Prev").number();
        return prev ? std::optional<qsizetype>(qsizetype(*prev)) : std::nullopt;
    }

    /// A classic table and its trailer (whose /XRefStm, a hybrid file's stream, is read too).
    std::optional<Object> tableSection(Parser &p)
    {
        p.word();
        if (!readTable(p) || p.word() != "trailer")
            return std::nullopt;
        auto t = p.object();
        if (!t || !t->dict())
            return std::nullopt;
        if (const auto stm = get(t->dict(), "XRefStm").number())
            readStreamAt(qsizetype(*stm));
        return t;
    }

    bool readTable(Parser &p)
    {
        for (;;) {
            bool okStart = false, okCount = false;
            const int start = p.word(true).toInt(&okStart);
            if (!okStart)
                return true; // "trailer"
            p.word();
            const int count = p.word().toInt(&okCount);
            if (!okCount || count < 0 || count > m_limits.maxObjects || !readTableRows(p, start, count))
                return false;
        }
    }

    /// `count` rows "offset generation n|f" of a table section from object `start` on.
    bool readTableRows(Parser &p, int start, int count)
    {
        for (int i = 0; i < count; ++i) {
            bool okOff = false;
            const qsizetype off = p.word().toLongLong(&okOff);
            p.word(); // generation
            const QByteArray type = p.word();
            if (!okOff)
                return false;
            if (type == "n" && !m_xref.contains(start + i))
                m_xref.insert(start + i, Location{off, 0});
        }
        return true;
    }

    /// The cross-reference stream at `offset`: its entries into m_xref, its dictionary.
    std::optional<Object> readStreamAt(qsizetype offset)
    {
        const auto found = xrefStreamObject(offset);
        if (!found)
            return std::nullopt;
        const auto &[num, o] = *found;
        const auto data = streamData(num);
        const auto widths = xrefWidths(o.dict());
        if (!data || !widths)
            return std::nullopt;
        qsizetype at = 0;
        const QList<int> index = xrefIndex(o.dict());
        for (qsizetype k = 0; k + 1 < index.size(); k += 2) {
            for (int i = 0; i < index.at(k + 1) && at + widths->row <= data->size(); ++i, at += widths->row)
                addXrefRow(index.at(k) + i, *data, at, *widths);
        }
        return o;
    }

    /// The object at `offset` when it is a cross-reference stream (/Type /XRef): its number and
    /// dictionary, cached.
    std::optional<std::pair<int, Object>> xrefStreamObject(qsizetype offset)
    {
        Parser p(m_data, offset);
        bool ok = false;
        const int num = p.word(true).toInt(&ok);
        const auto o = ok ? objectAt(offset, num) : std::nullopt;
        const QByteArray *type = o ? get(o->dict(), "Type").name() : nullptr;
        if (!type || *type != "XRef")
            return std::nullopt;
        m_cache.insert(num, *o);
        return std::pair(num, *o);
    }

    struct XrefWidths {
        int w[3];
        int row;
    };

    /// /W of a cross-reference stream (each field 0…8 bytes); nullopt when it is none.
    static std::optional<XrefWidths> xrefWidths(const Dict *d)
    {
        const Array *w = get(d, "W").array();
        if (!w || w->size() != 3)
            return std::nullopt;
        XrefWidths out{};
        for (int i = 0; i < 3; ++i) {
            out.w[i] = std::clamp(int(w->at(i).number().value_or(0)), 0, 8);
            out.row += out.w[i];
        }
        return out.row > 0 ? std::optional(out) : std::nullopt;
    }

    /// /Index of a cross-reference stream: pairs of first number and count (default: 0 and /Size).
    static QList<int> xrefIndex(const Dict *d)
    {
        QList<int> index;
        const Array *ix = get(d, "Index").array();
        if (!ix)
            return {0, int(get(d, "Size").number().value_or(0))};
        for (const Object &v : *ix)
            index.append(int(v.number().value_or(0)));
        return index;
    }

    /// Field `which` of the row at `at` (big-endian; a field of no bytes: `fallback`).
    static qint64 xrefField(const QByteArray &data, qsizetype at, const XrefWidths &w, int which, qint64 fallback)
    {
        if (w.w[which] == 0)
            return fallback;
        qsizetype from = at;
        for (int j = 0; j < which; ++j)
            from += w.w[j];
        qint64 v = 0;
        for (int j = 0; j < w.w[which]; ++j)
            v = (v << 8) | quint8(data.at(from + j));
        return v;
    }

    /// The row of object `n` at `at` into m_xref (an entry already known stays: newer first).
    void addXrefRow(int n, const QByteArray &data, qsizetype at, const XrefWidths &w)
    {
        const qint64 type = xrefField(data, at, w, 0, 1);
        if (m_xref.contains(n))
            return;
        if (type == 1)
            m_xref.insert(n, Location{qsizetype(xrefField(data, at, w, 1, 0)), 0});
        else if (type == 2)
            m_xref.insert(n, Location{-1, int(xrefField(data, at, w, 1, 0))});
    }

    QByteArrayView m_data;
    Limits m_limits;
    QHash<int, Location> m_xref;
    QHash<int, qsizetype> m_scanned;
    bool m_scannedDone = false;
    QHash<int, Object> m_cache;
    QHash<int, qsizetype> m_streams; ///< stream object → its data's offset
    QSet<int> m_loading;
    Object m_trailer;
};

// ---- titles ---------------------------------------------------------------------------------------

/// PDFDocEncoding's codes 0x80…0xA0 that are not Latin-1 (ISO 32000-1 Annex D), in their order.
constexpr QStringView PdfDocHigh = u"•†‡…—–ƒ⁄‹›−‰„“”‘’‚™ﬁﬂŁŒŠŸŽıłœšž\uFFFD€";

QChar pdfDocChar(quint8 c)
{
    return c >= 0x80 && c - 0x80 < PdfDocHigh.size() ? PdfDocHigh[c - 0x80] : QChar(char16_t(c));
}

QString textString(const QByteArray &bytes)
{
    if (bytes.startsWith("\xFE\xFF")) {
        QStringDecoder decoder(QStringDecoder::Utf16BE);
        return decoder(bytes.mid(2));
    }
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(bytes.mid(3));
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes)
        out += pdfDocChar(quint8(c));
    return out;
}

// ---- the outline ----------------------------------------------------------------------------------

struct PageInfo {
    int index = 0; ///< 1-based
    double height = 0;
};

class Reader {
public:
    Reader(QByteArrayView data, const Limits &limits) : m_doc(data, limits), m_limits(limits) { }

    QList<Entry> run()
    {
        const Dict *trailer = m_doc.trailer();
        if (!trailer || !get(trailer, "Encrypt").isNull())
            return {};
        m_catalog = m_doc.resolve(get(trailer, "Root"));
        if (!m_catalog.dict())
            return {};
        readPages(get(m_catalog.dict(), "Pages"), 0);
        const Object outlines = m_doc.resolve(get(m_catalog.dict(), "Outlines"));
        walk(get(outlines.dict(), "First"), 0);
        return m_out;
    }

private:
    void readPages(const Object &node, double inheritedHeight)
    {
        const Ref *r = node.ref();
        if (!r || m_seenPages.contains(r->num) || m_pages.size() >= m_limits.maxPages)
            return;
        m_seenPages.insert(r->num);
        const Object o = m_doc.resolve(node);
        const Dict *d = o.dict();
        if (!d)
            return;
        double height = inheritedHeight;
        if (const Array *box = m_doc.resolve(get(d, "MediaBox")).array(); box && box->size() == 4)
            height = std::abs(box->at(3).number().value_or(0) - box->at(1).number().value_or(0));
        if (const Array *kids = m_doc.resolve(get(d, "Kids")).array()) {
            for (const Object &kid : *kids)
                readPages(kid, height);
        } else {
            m_pages.insert(r->num, PageInfo{int(m_pages.size()) + 1, height});
            m_order.append(r->num);
        }
    }

    void walk(Object item, int level)
    {
        while (const Ref *r = item.ref()) {
            if (m_seen.contains(r->num) || int(m_out.size()) >= m_limits.maxItems || level >= m_limits.maxDepth)
                return;
            m_seen.insert(r->num);
            const Object o = m_doc.resolve(item);
            const Dict *d = o.dict();
            if (!d)
                return;
            Entry e;
            const QByteArray *title = m_doc.resolve(get(d, "Title")).string();
            e.title = title ? textString(*title) : QString();
            e.level = level;
            place(e, destinationOf(d));
            m_out.append(e);
            walk(get(d, "First"), level + 1);
            item = get(d, "Next");
        }
    }

    /// The explicit destination an entry leads to (an array), or null.
    Object destinationOf(const Dict *d)
    {
        Object dest = m_doc.resolve(get(d, "Dest"));
        if (dest.isNull()) {
            const Object action = m_doc.resolve(get(d, "A"));
            const QByteArray *kind = get(action.dict(), "S").name();
            if (!kind || *kind != "GoTo")
                return {};
            dest = m_doc.resolve(get(action.dict(), "D"));
        }
        if (dest.name() || dest.string())
            dest = named(dest.name() ? *dest.name() : *dest.string());
        if (const Dict *wrapped = dest.dict())
            dest = m_doc.resolve(get(wrapped, "D"));
        return dest;
    }

    /// A named destination: the catalog's /Dests, else its /Names /Dests tree.
    Object named(const QByteArray &name)
    {
        const Object old = m_doc.resolve(get(m_catalog.dict(), "Dests"));
        if (const Dict *d = old.dict(); d && d->contains(name))
            return m_doc.resolve(d->value(name));
        const Object names = m_doc.resolve(get(m_catalog.dict(), "Names"));
        QSet<int> seen;
        return inTree(get(names.dict(), "Dests"), name, seen, 0);
    }

    Object inTree(const Object &node, const QByteArray &name, QSet<int> &seen, int depth)
    {
        if (const Ref *r = node.ref()) {
            if (seen.contains(r->num))
                return {};
            seen.insert(r->num);
        }
        const Object o = m_doc.resolve(node);
        const Dict *d = o.dict();
        if (!d || depth > m_limits.maxDepth)
            return {};
        if (const Object found = inNames(m_doc.resolve(get(d, "Names")).array(), name); !found.isNull())
            return found;
        if (const Array *kids = m_doc.resolve(get(d, "Kids")).array()) {
            for (const Object &kid : *kids) {
                const Object found = inTree(kid, name, seen, depth + 1);
                if (!found.isNull())
                    return found;
            }
        }
        return {};
    }

    /// The value of `name` in a name tree node's /Names [key value …] (null: not there).
    Object inNames(const Array *pairs, const QByteArray &name)
    {
        for (qsizetype i = 0; pairs && i + 1 < pairs->size(); i += 2) {
            const QByteArray *key = pairs->at(i).string();
            if (key && *key == name)
                return m_doc.resolve(pairs->at(i + 1));
        }
        return {};
    }

    /// The page and top of the explicit destination `dest` into `e`.
    void place(Entry &e, const Object &dest)
    {
        const Array *a = dest.array();
        const PageInfo *page = a && !a->isEmpty() ? pageOf(a->first()) : nullptr;
        if (!page)
            return;
        e.page = page->index;
        const std::optional<double> top = topOf(*a);
        if (top && page->height > 0)
            e.top = std::clamp(1.0 - *top / page->height, 0.0, 1.0);
    }

    /// The page a destination names: a page object, or its 0-based number.
    const PageInfo *pageOf(const Object &target) const
    {
        if (const Ref *r = target.ref()) {
            const auto it = m_pages.constFind(r->num);
            return it == m_pages.constEnd() ? nullptr : &*it;
        }
        const auto n = target.number();
        if (!n || *n < 0 || *n >= m_order.size())
            return nullptr;
        return &*m_pages.constFind(m_order.at(int(*n)));
    }

    /// The top (in points from the bottom) an explicit destination asks for: /XYZ left top zoom,
    /// /FitH top, /FitBH top, /FitR left bottom right top; nullopt for the others or none.
    static std::optional<double> topOf(const Array &a)
    {
        static const QHash<QByteArray, int> where{{"XYZ", 3}, {"FitH", 2}, {"FitBH", 2}, {"FitR", 5}};
        const QByteArray *kind = a.size() > 1 ? a.at(1).name() : nullptr;
        const int at = kind ? where.value(*kind, -1) : -1;
        return at > 0 && at < a.size() ? a.at(at).number() : std::nullopt;
    }

    Document m_doc;
    Limits m_limits;
    Object m_catalog;
    QHash<int, PageInfo> m_pages;
    QList<int> m_order;
    QSet<int> m_seenPages;
    QSet<int> m_seen;
    QList<Entry> m_out;
};

} // namespace

QList<Entry> readOutline(QByteArrayView pdf, const Limits &limits)
{
    if (!pdf.startsWith("%PDF") && pdf.indexOf("%PDF") < 0)
        return {};
    Reader reader(pdf, limits);
    return reader.run();
}

} // namespace brulion::pdfoutline
