#include "drawing/lzstring.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace brulion::drawing::lzstring {

namespace {

constexpr char Base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

/// The phrase dictionary of the compressor: (code of a phrase, next code unit) → the code of the
/// longer phrase. Open addressing over two flat arrays (a scene of a few MB makes a few million
/// phrases: 12 bytes each, no node per entry).
class PhraseTable {
public:
    explicit PhraseTable(qsizetype expected)
    {
        std::size_t capacity = 1024;
        while (capacity < std::size_t(expected) * 2)
            capacity *= 2;
        m_keys.assign(capacity, Empty);
        m_values.assign(capacity, 0);
    }

    /// The code stored for `key`, or −1.
    int find(quint64 key) const
    {
        const std::size_t mask = m_keys.size() - 1;
        for (std::size_t i = slot(key) & mask;; i = (i + 1) & mask) {
            if (m_keys[i] == key)
                return m_values[i];
            if (m_keys[i] == Empty)
                return -1;
        }
    }

    void insert(quint64 key, int value)
    {
        if ((m_size + 1) * 10 > m_keys.size() * 7)
            grow();
        put(key, value);
        ++m_size;
    }

private:
    static constexpr quint64 Empty = ~quint64(0);

    static std::size_t slot(quint64 key)
    {
        // (A multiplicative hash: the codes rise one by one.)
        return std::size_t((key * 0x9E3779B97F4A7C15ULL) >> 20);
    }

    void put(quint64 key, int value)
    {
        const std::size_t mask = m_keys.size() - 1;
        std::size_t i = slot(key) & mask;
        while (m_keys[i] != Empty)
            i = (i + 1) & mask;
        m_keys[i] = key;
        m_values[i] = value;
    }

    void grow()
    {
        std::vector<quint64> keys;
        std::vector<int> values;
        keys.swap(m_keys);
        values.swap(m_values);
        m_keys.assign(keys.size() * 2, Empty);
        m_values.assign(keys.size() * 2, 0);
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] != Empty)
                put(keys[i], values[i]);
        }
    }

    std::vector<quint64> m_keys;
    std::vector<int> m_values;
    std::size_t m_size = 0;
};

/// lz-string's bit writer: values least significant bit first, six bits a base64 character.
class BitWriter {
public:
    explicit BitWriter(qsizetype expected) { m_out.reserve(expected); }

    void write(int value, int bits)
    {
        for (int i = 0; i < bits; ++i) {
            m_value = (m_value << 1) | (value & 1);
            if (m_position == 5) {
                m_position = 0;
                m_out.append(QLatin1Char(Base64Chars[m_value]));
                m_value = 0;
            } else {
                ++m_position;
            }
            value >>= 1;
        }
    }

    /// The last character (lz-string writes one more even when the bits end on a character).
    QString finish()
    {
        for (;;) {
            m_value <<= 1;
            if (m_position == 5) {
                m_out.append(QLatin1Char(Base64Chars[m_value]));
                break;
            }
            ++m_position;
        }
        return std::move(m_out);
    }

private:
    QString m_out;
    int m_value = 0;
    int m_position = 0;
};

/// The value of a base64 character ('=' is 64: its sixth bit is never read), −1 for any other.
int base64Value(char16_t c)
{
    if (c >= u'A' && c <= u'Z')
        return c - u'A';
    if (c >= u'a' && c <= u'z')
        return c - u'a' + 26;
    if (c >= u'0' && c <= u'9')
        return c - u'0' + 52;
    if (c == u'+')
        return 62;
    if (c == u'/')
        return 63;
    if (c == u'=')
        return 64;
    return -1;
}

/// lz-string's bit reader over the base64 values: bits from the most significant of each value's
/// six; past the end every bit is 0 (as JavaScript reads "" there).
class BitReader {
public:
    explicit BitReader(const std::vector<quint8> &values) : m_values(values) { m_value = valueAt(0); }

    int read(int bits)
    {
        int out = 0;
        for (int i = 0; i < bits; ++i) {
            const int bit = (m_value & m_position) ? 1 : 0;
            m_position >>= 1;
            if (m_position == 0) {
                m_position = Reset;
                m_value = valueAt(m_index++);
            }
            out |= bit << i;
        }
        return out;
    }

    /// How many values were taken (JavaScript's data.index).
    qsizetype index() const { return m_index; }

private:
    static constexpr int Reset = 32;

    int valueAt(qsizetype i) const { return i < qsizetype(m_values.size()) ? m_values[std::size_t(i)] : 0; }

    const std::vector<quint8> &m_values;
    int m_value = 0;
    int m_position = Reset;
    qsizetype m_index = 1;
};

} // namespace

QString compressToBase64(QStringView input)
{
    BitWriter out(input.size() / 2 + 8);
    // A code unit's code (−1 none yet) and whether its literal is still to be written.
    std::vector<int> unitCode(65536, -1);
    std::vector<bool> toCreate(65536, false);
    PhraseTable phrases(std::min<qsizetype>(input.size() / 4, 1 << 20) + 64);
    int enlargeIn = 2; // (the first entry does not count)
    int dictSize = 3;
    int numBits = 2;
    // The phrase w being extended: its code; a single unit also by itself.
    int w = -1;
    bool wSingle = false;
    char16_t wUnit = 0;

    const auto enlarge = [&] {
        if (--enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
    };
    const auto writePhrase = [&] {
        if (wSingle && toCreate[wUnit]) {
            if (wUnit < 256) {
                out.write(0, numBits);
                out.write(wUnit, 8);
            } else {
                out.write(1, numBits);
                out.write(wUnit, 16);
            }
            enlarge();
            toCreate[wUnit] = false;
        } else {
            out.write(w, numBits);
        }
        enlarge();
    };

    for (const QChar qc : input) {
        const char16_t c = qc.unicode();
        if (unitCode[c] < 0) {
            unitCode[c] = dictSize++;
            toCreate[c] = true;
        }
        if (w < 0) {
            w = unitCode[c];
            wSingle = true;
            wUnit = c;
            continue;
        }
        const quint64 key = (quint64(w) << 16) | c;
        const int longer = phrases.find(key);
        if (longer >= 0) {
            w = longer;
            wSingle = false;
            continue;
        }
        writePhrase();
        phrases.insert(key, dictSize++);
        w = unitCode[c];
        wSingle = true;
        wUnit = c;
    }
    if (w >= 0)
        writePhrase();
    out.write(2, numBits); // the end mark
    QString result = out.finish();
    switch (result.size() % 4) {
    case 1: result += QLatin1String("==="); break;
    case 2: result += QLatin1String("=="); break;
    case 3: result += QLatin1Char('='); break;
    default: break;
    }
    return result;
}

std::optional<QString> decompressFromBase64(QStringView input, qsizetype maxUnits)
{
    std::vector<quint8> values;
    values.reserve(std::size_t(input.size()));
    for (const QChar qc : input) {
        const char16_t c = qc.unicode();
        if (c == u' ' || c == u'\n' || c == u'\r' || c == u'\t' || c == u'\f' || c == u'\v')
            continue;
        const int v = base64Value(c);
        if (v < 0)
            return std::nullopt;
        values.push_back(quint8(v));
    }
    if (values.empty())
        return std::nullopt;
    const qsizetype length = qsizetype(values.size());
    BitReader in(values);

    // The dictionary: codes 0–2 are the markers; every phrase is a run of the output already
    // written (offset, length) — a literal where it was written, a phrase extended by a unit the
    // run of the phrase and the unit written after it.
    struct Run {
        qsizetype offset = 0;
        qsizetype length = 0;
    };
    // (Reserved for a usual scene — a few phrases a character, some four units of text a
    // character —, grown beyond: a large or hostile file allocates only what it really fills.)
    std::vector<Run> dictionary(3);
    dictionary.reserve(std::size_t(std::min<qsizetype>(length, 1 << 20)) + 4);
    std::vector<char16_t> out;
    out.reserve(std::size_t(std::min<qsizetype>({maxUnits, length * 4 + 16, qsizetype(1) << 22})));
    int enlargeIn = 4;
    int numBits = 3;

    const auto literal = [&](int bits) -> bool {
        if (qsizetype(out.size()) + 1 > maxUnits)
            return false;
        dictionary.push_back(Run{qsizetype(out.size()), 1});
        out.push_back(char16_t(in.read(bits)));
        return true;
    };

    switch (in.read(2)) {
    case 0:
        if (!literal(8))
            return std::nullopt;
        break;
    case 1:
        if (!literal(16))
            return std::nullopt;
        break;
    case 2: return QString(); // an empty string
    default: return std::nullopt;
    }
    Run w = dictionary.back();
    for (;;) {
        if (in.index() > length)
            return std::nullopt; // the stream ended without its end mark (JavaScript: "")
        int c = in.read(numBits);
        // A literal is written where it is read: it then is the entry below.
        bool written = false;
        switch (c) {
        case 0:
        case 1:
            if (!literal(c == 0 ? 8 : 16))
                return std::nullopt;
            c = int(dictionary.size()) - 1;
            written = true;
            --enlargeIn;
            break;
        case 2: return QString(reinterpret_cast<const QChar *>(out.data()), qsizetype(out.size()));
        default: break;
        }
        if (enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
        const qsizetype end = qsizetype(out.size());
        Run entry;
        if (written) {
            entry = dictionary[std::size_t(c)];
        } else if (c < qsizetype(dictionary.size())) {
            // (Codes 0–2 never get here.)
            const Run found = dictionary[std::size_t(c)];
            if (end + found.length > maxUnits)
                return std::nullopt;
            out.resize(std::size_t(end + found.length));
            std::memcpy(out.data() + end, out.data() + found.offset, std::size_t(found.length) * sizeof(char16_t));
            entry = Run{end, found.length};
        } else if (c == qsizetype(dictionary.size())) {
            // w + w's first unit (the phrase being defined right now).
            if (end + w.length + 1 > maxUnits)
                return std::nullopt;
            out.resize(std::size_t(end + w.length + 1));
            std::memcpy(out.data() + end, out.data() + w.offset, std::size_t(w.length) * sizeof(char16_t));
            out[std::size_t(end + w.length)] = out[std::size_t(w.offset)];
            entry = Run{end, w.length + 1};
        } else {
            return std::nullopt; // a code not defined yet (JavaScript: null)
        }
        // w + entry's first unit: w lies right before entry in the output.
        dictionary.push_back(Run{w.offset, w.length + 1});
        if (--enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
        w = entry;
    }
}

} // namespace brulion::drawing::lzstring
