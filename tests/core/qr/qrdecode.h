// A QR code read back for the tests (tests/core/qr; tests/app/qr reads the drawn picture with it):
// an error-free reader of byte-mode codes, written from ISO/IEC 18004 apart from the generator, so
// that the tests check what a scanner would read — the format information (error correction, mask
// and its BCH code, both copies), the version information (from version 7), the dark module, the
// codewords in the zigzag order around the function patterns, the blocks de-interleaved with their
// Reed-Solomon codewords checked (every syndrome zero), the byte segment, its terminator and the
// pad codewords. Not a scanner: the modules come in as booleans, nothing is damaged.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>

namespace qrtest {

struct Decoded {
    int version = 0;
    char ecc = '?'; ///< 'L', 'M', 'Q' or 'H'
    int mask = -1;
    QByteArray bytes;
};

namespace detail {

// ISO/IEC 18004 table 9 by error correction (L, M, Q, H) and version (index 0 unused).
inline constexpr std::array<std::array<int, 41>, 4> EccPerBlock{{
        {-1, 7,  10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28,
         28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
        {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26,
         26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},
        {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30,
         28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
        {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28,
         30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},
}};
inline constexpr std::array<std::array<int, 41>, 4> BlockCount{{
        {-1, 1, 1, 1,  1,  1,  2,  2,  2,  2,  4,  4,  4,  4,  4,  6,  6,  6,  6,  7, 8,
         8,  9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},
        {-1, 1,  1,  1,  2,  2,  4,  4,  4,  5,  5,  5,  8,  9,  9,  10, 10, 11, 13, 14, 16,
         17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},
        {-1, 1,  1,  2,  2,  4,  4,  6,  6,  8,  8,  8,  10, 12, 16, 12, 17, 16, 18, 21, 20,
         23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},
        {-1, 1,  1,  2,  4,  4,  4,  5,  6,  8,  8,  11, 11, 16, 16, 18, 16, 19, 21, 25, 25,
         25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},
}};

/// The centres of the alignment patterns of `version` on either axis (annex E).
inline QList<int> alignmentCentres(int version)
{
    if (version == 1)
        return {};
    const int size = version * 4 + 17;
    const int count = version / 7 + 2;
    const int step = version == 32 ? 26 : (version * 4 + count * 2 + 1) / (count * 2 - 2) * 2;
    QList<int> centres{6};
    for (int i = count - 2; i >= 0; --i)
        centres.append(size - 7 - i * step);
    return centres;
}

/// GF(256) with the polynomial x^8 + x^4 + x^3 + x^2 + 1 (0x11D), generator 2.
struct Field {
    std::array<int, 512> exp{};
    std::array<int, 256> log{};
    Field()
    {
        int x = 1;
        for (int i = 0; i < 255; ++i) {
            exp[std::size_t(i)] = x;
            log[std::size_t(x)] = i;
            x <<= 1;
            if (x & 0x100)
                x ^= 0x11D;
        }
        for (int i = 255; i < 512; ++i)
            exp[std::size_t(i)] = exp[std::size_t(i - 255)];
    }
    int multiply(int a, int b) const
    {
        return a == 0 || b == 0 ? 0 : exp[std::size_t(log[std::size_t(a)] + log[std::size_t(b)])];
    }
};

/// A block's codewords (data, then error correction) are a Reed-Solomon codeword of a generator
/// with the roots 2^0 … 2^(eccCount - 1): the codeword polynomial is zero at each of them.
inline bool syndromesZero(const QByteArray &codewords, int eccCount)
{
    static const Field field;
    for (int i = 0; i < eccCount; ++i) {
        const int root = field.exp[std::size_t(i)];
        int value = 0; // Horner, highest power first
        for (const char c : codewords)
            value = field.multiply(value, root) ^ int(std::uint8_t(c));
        if (value != 0)
            return false;
    }
    return true;
}

inline bool masked(int mask, int x, int y)
{
    switch (mask) {
    case 0: return (x + y) % 2 == 0;
    case 1: return y % 2 == 0;
    case 2: return x % 3 == 0;
    case 3: return (x + y) % 3 == 0;
    case 4: return (x / 3 + y / 2) % 2 == 0;
    case 5: return x * y % 2 + x * y % 3 == 0;
    case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
    default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
    }
}

/// The remainder of `value` × x^`bits` divided by `polynomial` (a BCH code's check bits).
inline int bchRemainder(int value, int bits, int polynomial)
{
    int rem = value;
    for (int i = 0; i < bits; ++i)
        rem = (rem << 1) ^ ((rem >> (bits - 1)) * polynomial);
    return rem & ((1 << bits) - 1);
}

} // namespace detail

/// The code of size × size `modules` (row by row from the top left, true dark, no quiet zone)
/// read; std::nullopt with `why` when it is no well-formed byte-mode code.
inline std::optional<Decoded> decode(int size, const QList<bool> &modules, QString *why = nullptr)
{
    using namespace detail;
    const auto fail = [why](const char *reason) -> std::optional<Decoded> {
        if (why)
            *why = QString::fromLatin1(reason);
        return std::nullopt;
    };
    const int version = (size - 17) / 4;
    if (version < 1 || version > 40 || size != version * 4 + 17 || modules.size() != qsizetype(size) * size)
        return fail("no code's size");
    const auto dark = [&](int x, int y) { return modules.at(qsizetype(y) * size + x); };
    const auto bit = [&](int x, int y) { return dark(x, y) ? 1 : 0; };

    // The finder patterns: a dark 7 × 7 ring, a light ring, a dark 3 × 3 centre, a light separator.
    const auto finderAt = [&](int cx, int cy) {
        for (int dy = -4; dy <= 4; ++dy) {
            for (int dx = -4; dx <= 4; ++dx) {
                const int x = cx + dx;
                const int y = cy + dy;
                if (x < 0 || y < 0 || x >= size || y >= size)
                    continue;
                const int ring = std::max(std::abs(dx), std::abs(dy));
                if (dark(x, y) != (ring != 2 && ring != 4))
                    return false;
            }
        }
        return true;
    };
    if (!finderAt(3, 3) || !finderAt(size - 4, 3) || !finderAt(3, size - 4))
        return fail("finder pattern");
    for (int i = 8; i < size - 8; ++i) {
        if (dark(i, 6) != (i % 2 == 0) || dark(6, i) != (i % 2 == 0))
            return fail("timing pattern");
    }

    // The format information, both copies.
    int format = 0;
    for (int i = 0; i <= 5; ++i)
        format |= bit(8, i) << i;
    format |= bit(8, 7) << 6;
    format |= bit(8, 8) << 7;
    format |= bit(7, 8) << 8;
    for (int i = 9; i < 15; ++i)
        format |= bit(14 - i, 8) << i;
    int copy = 0;
    for (int i = 0; i < 8; ++i)
        copy |= bit(size - 1 - i, 8) << i;
    for (int i = 8; i < 15; ++i)
        copy |= bit(8, size - 15 + i) << i;
    if (format != copy)
        return fail("format copies differ");
    if (!dark(8, size - 8))
        return fail("dark module");
    const int unmasked = format ^ 0x5412;
    const int formatData = unmasked >> 10;
    if (bchRemainder(formatData, 10, 0x537) != (unmasked & 0x3FF))
        return fail("format BCH");
    const int mask = formatData & 7;
    const int eccIndex = std::array<int, 4>{1, 0, 3, 2}[std::size_t(formatData >> 3)]; // M L H Q → L M Q H
    if (version >= 7) {
        int info = 0;
        int other = 0;
        for (int i = 0; i < 18; ++i) {
            info |= bit(size - 11 + i % 3, i / 3) << i;
            other |= bit(i / 3, size - 11 + i % 3) << i;
        }
        if (info != other || info >> 12 != version || bchRemainder(version, 12, 0x1F25) != (info & 0xFFF))
            return fail("version information");
    }

    // The function patterns' modules: finders with separators, timing, alignment, format and
    // version information.
    const QList<int> centres = alignmentCentres(version);
    const auto function = [&](int x, int y) {
        if (x == 6 || y == 6)
            return true;
        const auto near = [](int a, int b, int r) { return std::abs(a - b) <= r; };
        if ((near(x, 3, 4) && near(y, 3, 4)) || (near(x, size - 4, 4) && near(y, 3, 4))
            || (near(x, 3, 4) && near(y, size - 4, 4)))
            return true;
        if ((x == 8 && (y <= 8 || y >= size - 8)) || (y == 8 && (x <= 8 || x >= size - 8)))
            return true;
        if (version >= 7
            && ((x >= size - 11 && x <= size - 9 && y <= 5) || (y >= size - 11 && y <= size - 9 && x <= 5)))
            return true;
        const qsizetype last = centres.size() - 1;
        for (qsizetype i = 0; i < centres.size(); ++i) {
            for (qsizetype j = 0; j < centres.size(); ++j) {
                if ((i == 0 && j == 0) || (i == 0 && j == last) || (i == last && j == 0))
                    continue;
                if (near(x, centres[i], 2) && near(y, centres[j], 2))
                    return true;
            }
        }
        return false;
    };

    // The codewords: two columns at a time from the right, up and down in turn, past the
    // vertical timing pattern.
    QList<int> bits;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6)
            right = 5;
        const bool upward = ((right + 1) & 2) == 0;
        for (int step = 0; step < size; ++step) {
            const int y = upward ? size - 1 - step : step;
            for (int j = 0; j < 2; ++j) {
                const int x = right - j;
                if (!function(x, y))
                    bits.append((dark(x, y) != masked(mask, x, y)) ? 1 : 0);
            }
        }
    }
    const int raw = int(bits.size() / 8); // the remainder bits (0 … 7) carry nothing
    QByteArray codewords(raw, '\0');
    for (int i = 0; i < raw * 8; ++i)
        codewords[i / 8] = char(codewords[i / 8] | (bits[i] << (7 - i % 8)));

    // The blocks, interleaved codeword by codeword; short blocks have a data codeword less.
    const int blocks = BlockCount[std::size_t(eccIndex)][std::size_t(version)];
    const int eccCount = EccPerBlock[std::size_t(eccIndex)][std::size_t(version)];
    const int shortBlocks = blocks - raw % blocks;
    const int shortLength = raw / blocks;
    const int shortData = shortLength - eccCount;
    QList<QByteArray> padded(blocks, QByteArray(shortLength + 1, '\0'));
    int k = 0;
    for (int i = 0; i <= shortLength; ++i) {
        for (int j = 0; j < blocks; ++j) {
            if (i == shortData && j < shortBlocks)
                continue;
            padded[j][i] = codewords[k++];
        }
    }
    if (k != raw)
        return fail("block layout");
    QByteArray data;
    for (int j = 0; j < blocks; ++j) {
        const int dataLength = j < shortBlocks ? shortData : shortData + 1;
        const QByteArray block = padded[j].left(dataLength) + padded[j].right(eccCount);
        if (!syndromesZero(block, eccCount))
            return fail("Reed-Solomon codewords");
        data += block.left(dataLength);
    }

    // The data: one byte segment, its terminator, then the pad codewords.
    qsizetype at = 0;
    const qsizetype dataBits = data.size() * 8;
    const auto take = [&](int count) {
        int value = 0;
        for (int i = 0; i < count; ++i, ++at)
            value = (value << 1) | ((std::uint8_t(data[at / 8]) >> (7 - at % 8)) & 1);
        return value;
    };
    if (take(4) != 0b0100)
        return fail("not a byte segment");
    const int countBits = version <= 9 ? 8 : 16;
    const int length = take(countBits);
    if (4 + countBits + length * 8 > dataBits)
        return fail("segment longer than the code");
    Decoded decoded;
    decoded.version = version;
    decoded.ecc = "LMQH"[eccIndex];
    decoded.mask = mask;
    for (int i = 0; i < length; ++i)
        decoded.bytes.append(char(take(8)));
    const int terminator = int(std::min<qsizetype>(4, dataBits - at));
    if (take(terminator) != 0)
        return fail("terminator");
    while (at % 8 != 0) {
        if (take(1) != 0)
            return fail("bits before the padding");
    }
    for (int pad = 0xEC; at < dataBits; pad ^= 0xEC ^ 0x11) {
        if (take(8) != pad)
            return fail("pad codewords");
    }
    return decoded;
}

} // namespace qrtest
