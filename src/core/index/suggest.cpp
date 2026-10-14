#include "index/suggest_p.h"

#include "index/textutil_p.h"

#include <QVarLengthArray>

#include <algorithm>

namespace brulion::index_detail {

namespace {

/// Span of the shortest greedy in-order occurrence of `q`'s characters in `name`, or -1.
qsizetype subsequenceSpan(QStringView name, QStringView q)
{
    qsizetype start = -1;
    qsizetype j = 0;
    for (qsizetype i = 0; i < name.size() && j < q.size(); ++i) {
        if (name[i] != q[j])
            continue;
        if (j == 0)
            start = i;
        if (++j == q.size())
            return i - start + 1;
    }
    return -1;
}

/// Does `q` occur in `name` at the start of a word?
bool occursAtWordStart(QStringView name, QStringView q)
{
    for (qsizetype i = name.indexOf(q); i >= 0; i = name.indexOf(q, i + 1)) {
        if (i == 0 || !isTokenChar(codePointBefore(name, i)))
            return true;
    }
    return false;
}

} // namespace

int boundedEditDistance(QStringView a, QStringView b, int max)
{
    const auto n = a.size();
    const auto m = b.size();
    if (std::abs(n - m) > max)
        return max + 1;
    if (a == b)
        return 0;
    // Three rolling rows of the OSA dynamic programme in one buffer.
    QVarLengthArray<int, 3 * 64> buffer(3 * (m + 1), 0);
    int *prev2 = buffer.data();
    int *prev = prev2 + (m + 1);
    int *cur = prev + (m + 1);
    for (qsizetype j = 0; j <= m; ++j)
        prev[j] = int(j);
    for (qsizetype i = 1; i <= n; ++i) {
        cur[0] = int(i);
        int rowMin = cur[0];
        for (qsizetype j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int v = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                v = std::min(v, prev2[j - 2] + 1);
            cur[j] = v;
            rowMin = std::min(rowMin, v);
        }
        if (rowMin > max)
            return max + 1;
        int *const oldest = prev2;
        prev2 = prev;
        prev = cur;
        cur = oldest;
    }
    return std::min(prev[m], max + 1);
}

NameRank rankName(QStringView name, QStringView q)
{
    if (q.isEmpty() || name.isEmpty())
        return NameRank::None;
    if (name == q)
        return NameRank::Exact;
    if (name.startsWith(q))
        return NameRank::Prefix;
    if (name.contains(q))
        return occursAtWordStart(name, q) ? NameRank::WordPrefix : NameRank::Contains;
    const QStringList nameWords = words(name);
    // Multi-word query: every query word starts a word of the name ("dom ja" -> "Dom Jana").
    const QStringList queryWords = words(q);
    if (queryWords.size() > 1) {
        const bool all = std::all_of(queryWords.begin(), queryWords.end(), [&](const QString &qw) {
            return std::any_of(nameWords.begin(), nameWords.end(), [&](const QString &w) { return w.startsWith(qw); });
        });
        if (all)
            return NameRank::WordPrefix;
    }
    if (q.size() >= 4) {
        const int max = q.size() >= 7 ? 2 : 1;
        for (const QString &w : nameWords) {
            if (boundedEditDistance(q, QStringView(w).first(std::min(w.size(), q.size())), max) <= max
                || boundedEditDistance(q, w, max) <= max)
                return NameRank::FuzzyClose;
        }
    }
    if (q.size() >= 2) {
        const qsizetype span = subsequenceSpan(name, q);
        if (span >= 0)
            return span <= q.size() * 2 ? NameRank::FuzzyClose : NameRank::Fuzzy;
    }
    return NameRank::None;
}

} // namespace brulion::index_detail
