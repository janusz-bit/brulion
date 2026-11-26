#include "codefiles/fuzzy.h"

#include "text/text.h"

#include <algorithm>
#include <array>
#include <optional>
#include <vector>

namespace brulion::codefiles {

namespace {

constexpr int ScoreMatch = 16;
constexpr int GapStart = -3;
constexpr int GapExtension = -1;
constexpr int BonusSeparator = 10; // at the start, after "/"
constexpr int BonusBoundary = 8;   // after "_", "-", ".", " " and other punctuation
constexpr int BonusCamel = 7;      // a camelCase hump, letters turning to digits and back
constexpr int BonusConsecutive = 4;
constexpr int FirstCharMultiplier = 2;
constexpr int BonusCase = 2;
constexpr int NameBonus = 48;
constexpr int NamePrefixBonus = 32;
constexpr int NameExactBonus = 64;
/// Scores are offset by this so that a match with long gaps stays above 0.
constexpr int ScoreBase = 1000;
constexpr int NegInf = -1000000;

/// foldChar() for U+0000 … U+024F (ASCII, Latin-1, Latin Extended-A and -B), made once.
constexpr char16_t TableEnd = 0x250;

const std::array<char16_t, TableEnd> &foldTable()
{
    static const std::array<char16_t, TableEnd> table = [] {
        std::array<char16_t, TableEnd> t{};
        for (char16_t c = 0; c < TableEnd; ++c) {
            if (c < 0x80) {
                t[c] = (c >= u'A' && c <= u'Z') ? char16_t(c + 32) : c;
                continue;
            }
            const QChar ch(c);
            const QString folded = text::foldKey(QStringView(&ch, 1));
            t[c] = folded.size() == 1 ? folded.at(0).unicode() : ch.toLower().unicode();
        }
        return t;
    }();
    return table;
}

enum class CharClass : quint8 { Separator, Delimiter, Lower, Upper, Digit, Letter, Other };

CharClass classOf(char16_t c)
{
    if (c == u'/' || c == u'\\')
        return CharClass::Separator;
    if (c < 0x80) {
        if (c >= u'a' && c <= u'z')
            return CharClass::Lower;
        if (c >= u'A' && c <= u'Z')
            return CharClass::Upper;
        if (c >= u'0' && c <= u'9')
            return CharClass::Digit;
        if (c == u'_' || c == u'-' || c == u'.' || c == u' ' || c == u':' || c == u',' || c == u';' || c == u'+')
            return CharClass::Delimiter;
        return CharClass::Other;
    }
    const QChar ch(c);
    if (ch.isLower())
        return CharClass::Lower;
    if (ch.isUpper())
        return CharClass::Upper;
    if (ch.isDigit())
        return CharClass::Digit;
    if (ch.isLetter() || ch.isMark())
        return CharClass::Letter;
    return ch.isSpace() ? CharClass::Delimiter : CharClass::Other;
}

bool isWordClass(CharClass c)
{
    return c == CharClass::Lower || c == CharClass::Upper || c == CharClass::Digit || c == CharClass::Letter;
}

int bonusFor(CharClass prev, CharClass cur)
{
    if (!isWordClass(cur))
        return 0;
    switch (prev) {
    case CharClass::Separator: return BonusSeparator;
    case CharClass::Delimiter:
    case CharClass::Other: return BonusBoundary;
    case CharClass::Lower: return cur == CharClass::Upper || cur == CharClass::Digit ? BonusCamel : 0;
    case CharClass::Upper:
    case CharClass::Letter: return cur == CharClass::Digit ? BonusCamel : 0;
    case CharClass::Digit: return cur != CharClass::Digit ? BonusCamel : 0;
    }
    return 0;
}

/// The scratch buffers of one thread's matching.
struct Scratch {
    std::vector<int> score;    // m × w
    std::vector<qint16> from;  // m × w: the column of the previous character's match
    std::vector<int> bonus;    // w
    std::vector<char16_t> folded; // w
};

Scratch &scratch()
{
    thread_local Scratch s;
    return s;
}

/// Is `q` a subsequence of `path` folded, from `start`? The first and the last column a match can
/// use ([lo, hi]) when it is.
bool window(QStringView q, QStringView path, qsizetype start, qsizetype *lo, qsizetype *hi)
{
    const qsizetype n = path.size();
    const qsizetype m = q.size();
    qsizetype i = 0;
    qsizetype first = -1;
    for (qsizetype j = start; j < n && i < m; ++j) {
        if (foldChar(path.at(j).unicode()) == q.at(i).unicode()) {
            if (i == 0)
                first = j;
            ++i;
        }
    }
    if (i < m)
        return false;
    // (The last column the query's last character can take: its last occurrence.)
    qsizetype last = n - 1;
    while (last > first && foldChar(path.at(last).unicode()) != q.at(m - 1).unicode())
        --last;
    *lo = first;
    *hi = last;
    return true;
}

/// The best score of `q` matched in path[lo … hi] (nullopt: none), its positions when asked.
std::optional<int> best(const FuzzyQuery &query, QStringView path, qsizetype lo, qsizetype hi, QList<int> *positions)
{
    const QStringView q = query.folded;
    const qsizetype m = q.size();
    const qsizetype w = hi - lo + 1;
    if (w < m)
        return std::nullopt;
    Scratch &s = scratch();
    s.score.assign(size_t(m * w), NegInf);
    if (positions)
        s.from.assign(size_t(m * w), qint16(-1));
    s.bonus.resize(size_t(w));
    s.folded.resize(size_t(w));
    CharClass prev = lo > 0 ? classOf(path.at(lo - 1).unicode()) : CharClass::Separator;
    for (qsizetype j = 0; j < w; ++j) {
        const char16_t c = path.at(lo + j).unicode();
        const CharClass cls = classOf(c);
        s.bonus[size_t(j)] = bonusFor(prev, cls);
        s.folded[size_t(j)] = foldChar(c);
        prev = cls;
    }
    const auto at = [w](qsizetype i, qsizetype j) { return size_t(i * w + j); };
    for (qsizetype i = 0; i < m; ++i) {
        const char16_t want = q.at(i).unicode();
        const char16_t typed = query.typed.at(i).unicode();
        const bool caseTyped = QChar(typed).isUpper();
        int carry = NegInf; // the best of row i-1 at k ≤ j-2, with the gap's penalty
        qsizetype carryFrom = -1;
        // (Row i can only use columns that leave room for the characters before and after it.)
        for (qsizetype j = i; j < w - (m - 1 - i); ++j) {
            if (i > 0 && j >= 2) {
                const int prevScore = s.score[at(i - 1, j - 2)];
                const int opened = prevScore == NegInf ? NegInf : prevScore + GapStart;
                const int extended = carry == NegInf ? NegInf : carry + GapExtension;
                if (opened >= extended && opened != NegInf) {
                    carry = opened;
                    carryFrom = j - 2;
                } else {
                    carry = extended;
                }
            }
            if (s.folded[size_t(j)] != want)
                continue;
            const int b = s.bonus[size_t(j)];
            const int caseBonus = caseTyped && path.at(lo + j).unicode() == typed ? BonusCase : 0;
            if (i == 0) {
                s.score[at(0, j)] = ScoreMatch + b * FirstCharMultiplier + caseBonus;
                continue;
            }
            int candidate = NegInf;
            qsizetype k = -1;
            if (j >= 1) {
                const int consecutive = s.score[at(i - 1, j - 1)];
                if (consecutive != NegInf) {
                    candidate = consecutive + BonusConsecutive;
                    k = j - 1;
                }
            }
            if (carry != NegInf && carry > candidate) {
                candidate = carry;
                k = carryFrom;
            }
            if (candidate == NegInf)
                continue;
            s.score[at(i, j)] = candidate + ScoreMatch + b + caseBonus;
            if (positions)
                s.from[at(i, j)] = qint16(k);
        }
    }
    int top = NegInf;
    qsizetype topJ = -1;
    for (qsizetype j = m - 1; j < w; ++j) {
        if (s.score[at(m - 1, j)] > top) {
            top = s.score[at(m - 1, j)];
            topJ = j;
        }
    }
    if (topJ < 0)
        return std::nullopt;
    if (positions) {
        positions->resize(m);
        for (qsizetype i = m - 1, j = topJ; i >= 0 && j >= 0; --i) {
            (*positions)[i] = int(lo + j);
            j = s.from[at(i, j)];
        }
    }
    return top;
}

/// `a` folded equals `b` (folded already).
bool foldedEquals(QStringView a, QStringView folded)
{
    if (a.size() != folded.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (foldChar(a.at(i).unicode()) != folded.at(i).unicode())
            return false;
    }
    return true;
}

int scoreOf(const FuzzyQuery &query, QStringView path, qsizetype nameStart, QList<int> *positions, bool *inName)
{
    if (query.isEmpty())
        return 1;
    const qsizetype n = path.size();
    const qsizetype m = query.folded.size();
    if (m > MaxQueryLength || n > MaxPathLength || m > n)
        return 0;
    qsizetype lo = 0;
    qsizetype hi = 0;
    if (!window(query.folded, path, 0, &lo, &hi))
        return 0;
    nameStart = std::clamp<qsizetype>(nameStart, 0, n);
    std::optional<int> found;
    int extra = 0;
    if (!query.hasSlash && nameStart < n) {
        qsizetype nameLo = 0;
        qsizetype nameHi = 0;
        if (window(query.folded, path, nameStart, &nameLo, &nameHi)) {
            found = best(query, path, nameLo, nameHi, positions);
            if (found) {
                if (inName)
                    *inName = true;
                const QStringView name = path.sliced(nameStart);
                const qsizetype dot = name.lastIndexOf(u'.');
                const QStringView stem = dot > 0 ? name.first(dot) : name;
                extra = NameBonus;
                if (foldedEquals(name, query.folded) || foldedEquals(stem, query.folded))
                    extra += NameExactBonus;
                else if (name.size() >= m && foldedEquals(name.first(m), query.folded))
                    extra += NamePrefixBonus;
            }
        }
    }
    if (!found)
        found = best(query, path, lo, hi, positions);
    if (!found)
        return 0;
    return std::max(1, ScoreBase + *found + extra - int(n / 16));
}

} // namespace

char16_t foldChar(char16_t c)
{
    if (c < TableEnd)
        return foldTable()[c];
    if (QChar::isSurrogate(c))
        return c;
    return QChar(c).toLower().unicode();
}

FuzzyQuery prepareQuery(QStringView query)
{
    FuzzyQuery q;
    q.folded.reserve(query.size());
    q.typed.reserve(query.size());
    for (const QChar c : query) {
        if (c.isSpace())
            continue;
        q.typed.append(c);
        q.folded.append(QChar(foldChar(c.unicode())));
        if (c == u'/')
            q.hasSlash = true;
    }
    return q;
}

FuzzyMatch matchPath(const FuzzyQuery &query, QStringView path, qsizetype nameStart)
{
    FuzzyMatch m;
    m.score = scoreOf(query, path, nameStart, &m.positions, &m.inName);
    if (m.score == 0)
        m.positions.clear();
    return m;
}

int scorePath(const FuzzyQuery &query, QStringView path, qsizetype nameStart)
{
    return scoreOf(query, path, nameStart, nullptr, nullptr);
}

} // namespace brulion::codefiles
