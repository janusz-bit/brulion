#include "drawing/lzstring.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <utility>
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
        : m_keys(std::bit_ceil(std::max<std::size_t>(1024, std::size_t(expected) * 2)), Empty)
        , m_values(m_keys.size(), 0)
    { }

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
        const std::vector<quint64> keys = std::exchange(m_keys, std::vector<quint64>(m_keys.size() * 2, Empty));
        const std::vector<int> values = std::exchange(m_values, std::vector<int>(m_values.size() * 2, 0));
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] != Empty)
                put(keys[i], values[i]);
        }
    }

    std::vector<quint64> m_keys;
    std::vector<int> m_values;
    std::size_t m_size = 0;
};

/// lz-string's code width: `numBits` a code, one bit more once `enlargeIn` more entries were counted
/// (the dictionary then reaches the next power of two).
struct CodeWidth {
    int enlargeIn = 0;
    int numBits = 0;

    /// An entry was added to the dictionary.
    void count()
    {
        if (--enlargeIn == 0) {
            enlargeIn = 1 << numBits;
            ++numBits;
        }
    }
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
    static constexpr std::array<qint8, 128> values = [] {
        std::array<qint8, 128> out{};
        out.fill(-1);
        for (int i = 0; Base64Chars[i] != '\0'; ++i)
            out[std::size_t(Base64Chars[i])] = qint8(i);
        return out;
    }();
    return std::size_t(c) < values.size() ? values[c] : -1;
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
    CodeWidth width{.enlargeIn = 2, .numBits = 2}; // (the first entry does not count)
    int dictSize = 3;
    // The phrase w being extended: its code; a single unit also by itself.
    int w = -1;
    bool wSingle = false;
    char16_t wUnit = 0;

    const auto writePhrase = [&] {
        if (wSingle && toCreate[wUnit]) {
            const bool wide = wUnit >= 256;
            out.write(wide ? 1 : 0, width.numBits);
            out.write(wUnit, wide ? 16 : 8);
            width.count();
            toCreate[wUnit] = false;
        } else {
            out.write(w, width.numBits);
        }
        width.count();
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
    out.write(2, width.numBits); // the end mark
    QString result = out.finish();
    result.resize((result.size() + 3) / 4 * 4, u'='); // padded to a multiple of four
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
    CodeWidth width{.enlargeIn = 4, .numBits = 3};

    const auto literal = [&](int bits) -> bool {
        if (qsizetype(out.size()) + 1 > maxUnits)
            return false;
        dictionary.push_back(Run{qsizetype(out.size()), 1});
        out.push_back(char16_t(in.read(bits)));
        return true;
    };

    const int first = in.read(2);
    if (first == 2)
        return QString(); // an empty string
    if (first == 3 || !literal(first == 0 ? 8 : 16))
        return std::nullopt;
    Run w = dictionary.back();
    for (;;) {
        if (in.index() > length)
            return std::nullopt; // the stream ended without its end mark (JavaScript: "")
        const int c = in.read(width.numBits);
        if (c == 2)
            return QStringView(out).toString();
        Run entry;
        if (c < 2) {
            // A literal, written where it is read, is the entry.
            if (!literal(c == 0 ? 8 : 16))
                return std::nullopt;
            entry = dictionary.back();
            width.count();
        } else {
            // A phrase of the dictionary, or w + w's first unit (the phrase being defined right now).
            const qsizetype known = qsizetype(dictionary.size());
            if (c > known)
                return std::nullopt; // a code not defined yet (JavaScript: null)
            const Run phrase = c < known ? dictionary[std::size_t(c)] : w;
            const qsizetype end = qsizetype(out.size());
            entry = Run{end, phrase.length + (c == known ? 1 : 0)};
            if (end + entry.length > maxUnits)
                return std::nullopt;
            out.resize(std::size_t(end + entry.length));
            std::memcpy(out.data() + end, out.data() + phrase.offset, std::size_t(phrase.length) * sizeof(char16_t));
            if (c == known)
                out.back() = out[std::size_t(w.offset)];
        }
        // w + entry's first unit: w lies right before entry in the output.
        dictionary.push_back(Run{w.offset, w.length + 1});
        width.count();
        w = entry;
    }
}

} // namespace brulion::drawing::lzstring
