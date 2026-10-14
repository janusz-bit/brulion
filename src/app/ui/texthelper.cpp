#include "texthelper.h"

#include "text/text.h"

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

// Scoring (fzf-like, scaled to small integers).
constexpr int ScoreMatch = 16;
constexpr int GapStart = -3;
constexpr int GapExtension = -1;
constexpr int BonusBoundary = 8;          // first character of a word
constexpr int BonusCamel = 7;             // lower→Upper hump ("pageView" -> V)
constexpr int BonusConsecutive = 5;       // character right after the previous match
constexpr int FirstCharMultiplier = 2;    // the query's first character on a boundary counts double
constexpr qsizetype MaxTextLength = 1024; // longer texts are matched on their beginning only
constexpr int NegInf = std::numeric_limits<int>::min() / 4;

bool isSeparator(QChar c)
{
    return !text::isWordChar(c);
}

} // namespace

FuzzyMatch fuzzyMatch(QStringView query, QStringView text)
{
    FuzzyMatch result;
    QString q = text::foldKey(query);
    q.removeIf([](QChar c) { return c.isSpace(); });
    if (q.isEmpty()) {
        result.score = 1;
        return result;
    }
    const text::FoldedText folded = text::foldKeyWithMap(text);
    const QString &f = folded.text;
    const qsizetype n = std::min(f.size(), MaxTextLength);
    const qsizetype m = q.size();
    if (m > n)
        return result;

    // Quick reject: is q a subsequence of f at all?
    {
        qsizetype i = 0;
        for (qsizetype j = 0; j < n && i < m; ++j) {
            if (f.at(j) == q.at(i))
                ++i;
        }
        if (i < m)
            return result;
    }

    // Per-position bonus, judged on the original text (camelCase needs the case).
    QList<int> bonus(n, 0);
    for (qsizetype j = 0; j < n; ++j) {
        const qsizetype o = folded.toOriginal(j);
        if (j == 0) {
            bonus[j] = BonusBoundary;
            continue;
        }
        const qsizetype po = folded.toOriginal(j - 1);
        const QChar cur = o < text.size() ? text.at(o) : QChar();
        const QChar prev = po < text.size() ? text.at(po) : QChar();
        if (po == o)
            bonus[j] = bonus[j - 1]; // same original character (expanded by folding)
        else if (isSeparator(prev) && !isSeparator(cur))
            bonus[j] = BonusBoundary;
        else if (prev.isLower() && cur.isUpper())
            bonus[j] = BonusCamel;
        else if (!prev.isDigit() && cur.isDigit())
            bonus[j] = BonusCamel / 2;
    }

    // score[i][j]: best score with q[i] matched at f[j]; from[i][j]: position of q[i-1].
    QList<int> score(m * n, NegInf);
    QList<int> from(m * n, -1);
    auto at = [n](qsizetype i, qsizetype j) { return i * n + j; };

    for (qsizetype j = 0; j < n; ++j) {
        if (f.at(j) == q.at(0))
            score[at(0, j)] = ScoreMatch + bonus[j] * FirstCharMultiplier;
    }
    for (qsizetype i = 1; i < m; ++i) {
        int best = NegInf; // max over k <= j-2 of score[i-1][k] + gap penalty for (j-k-1) skipped
        int bestK = -1;
        for (qsizetype j = i; j < n; ++j) {
            if (j >= 2) {
                const int extended = best == NegInf ? NegInf : best + GapExtension;
                const int prevScore = score[at(i - 1, j - 2)];
                const int opened = prevScore == NegInf ? NegInf : prevScore + GapStart;
                if (opened >= extended) {
                    best = opened;
                    bestK = int(j - 2);
                } else {
                    best = extended;
                }
            }
            if (f.at(j) != q.at(i))
                continue;
            int candidate = NegInf;
            int k = -1;
            const int consecutive = score[at(i - 1, j - 1)];
            if (consecutive != NegInf) {
                candidate = consecutive + BonusConsecutive;
                k = int(j - 1);
            }
            if (best != NegInf && best > candidate) {
                candidate = best;
                k = bestK;
            }
            if (candidate == NegInf)
                continue;
            score[at(i, j)] = candidate + ScoreMatch + bonus[j];
            from[at(i, j)] = k;
        }
    }

    int bestScore = NegInf;
    qsizetype bestJ = -1;
    for (qsizetype j = m - 1; j < n; ++j) {
        if (score[at(m - 1, j)] > bestScore) {
            bestScore = score[at(m - 1, j)];
            bestJ = j;
        }
    }
    if (bestJ < 0)
        return result;

    QList<int> foldedPositions(m);
    for (qsizetype i = m - 1, j = bestJ; i >= 0; --i) {
        foldedPositions[i] = int(j);
        j = from[at(i, j)];
    }
    for (int p : std::as_const(foldedPositions)) {
        const int o = int(folded.toOriginal(p));
        if (result.positions.isEmpty() || result.positions.last() != o)
            result.positions.append(o);
    }
    // Exact (folded) equality and prefix matches are what people mean most of the time.
    if (f == q)
        bestScore += ScoreMatch;
    else if (f.startsWith(q))
        bestScore += ScoreMatch / 2;
    result.score = std::max(1, bestScore);
    return result;
}

TextHelper::TextHelper(QObject *parent) : QObject(parent) { }

QString TextHelper::fold(const QString &text) const
{
    return text::foldKey(text);
}

int TextHelper::fuzzyScore(const QString &query, const QString &text) const
{
    return ui::fuzzyMatch(query, text).score;
}

QVariantMap TextHelper::fuzzyMatch(const QString &query, const QString &text) const
{
    const FuzzyMatch match = ui::fuzzyMatch(query, text);
    QVariantList positions;
    for (int p : match.positions)
        positions.append(p);
    return {{u"score"_s, match.score}, {u"positions"_s, positions}};
}

QVariantList TextHelper::fuzzyRank(const QString &query, const QStringList &texts, int limit,
                                   const QStringList &secondary) const
{
    struct Ranked {
        int index;
        int score;
        qsizetype length;
        QList<int> positions;
    };
    QList<Ranked> ranked;
    const bool emptyQuery = query.trimmed().isEmpty();
    for (int i = 0; i < texts.size(); ++i) {
        if (emptyQuery) {
            ranked.append({i, 0, 0, {}});
            continue;
        }
        FuzzyMatch match = ui::fuzzyMatch(query, texts.at(i));
        if (i < secondary.size() && !secondary.at(i).isEmpty()) {
            const FuzzyMatch other = ui::fuzzyMatch(query, secondary.at(i));
            if (other.score / 2 > match.score)
                match = {other.score / 2, {}};
        }
        if (match.score > 0)
            ranked.append({i, match.score, texts.at(i).size(), match.positions});
    }
    if (!emptyQuery) {
        std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.length < b.length;
        });
    }
    if (limit > 0 && ranked.size() > limit)
        ranked.resize(limit);
    QVariantList out;
    out.reserve(ranked.size());
    for (const Ranked &r : std::as_const(ranked)) {
        QVariantList positions;
        for (int p : r.positions)
            positions.append(p);
        out.append(QVariantMap{{u"index"_s, r.index}, {u"score"_s, r.score}, {u"positions"_s, positions}});
    }
    return out;
}

QString TextHelper::markMatches(const QString &text, const QVariantList &positions, const QString &color) const
{
    QList<bool> marked(text.size(), false);
    for (const QVariant &p : positions) {
        const int i = p.toInt();
        if (i >= 0 && i < text.size())
            marked[i] = true;
    }
    // Keep surrogate pairs together.
    for (qsizetype i = 0; i + 1 < text.size(); ++i) {
        if (text.at(i).isHighSurrogate() && text.at(i + 1).isLowSurrogate() && (marked[i] || marked[i + 1]))
            marked[i] = marked[i + 1] = true;
    }
    const QString open = color.isEmpty() ? u"<b>"_s : u"<b><font color=\"%1\">"_s.arg(color.toHtmlEscaped());
    const QString close = color.isEmpty() ? u"</b>"_s : u"</font></b>"_s;
    QString out;
    out.reserve(text.size() * 2);
    qsizetype i = 0;
    while (i < text.size()) {
        qsizetype j = i;
        while (j < text.size() && marked[j] == marked[i])
            ++j;
        const QString chunk = text.mid(i, j - i).toHtmlEscaped();
        out += marked[i] ? open + chunk + close : chunk;
        i = j;
    }
    return out;
}

QString TextHelper::escapeHtml(const QString &text) const
{
    return text.toHtmlEscaped();
}

} // namespace brulion::ui
