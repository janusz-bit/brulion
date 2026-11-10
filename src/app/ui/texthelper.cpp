#include "texthelper.h"

#include "markdown/highlight.h"
#include "markdown/inlinerenderer.h"
#include "markdown/pagelook.h"
#include "text/text.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

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

/// A key cap's text: the arrow keys by their arrows, as keyboards print them ("Alt+Up" is
/// written "Alt ↑", the same in every language; the sequence itself keeps Qt's names).
QString keyCap(const QString &key)
{
    if (key == u"Up")
        return u"↑"_s;
    if (key == u"Down")
        return u"↓"_s;
    if (key == u"Left")
        return u"←"_s;
    if (key == u"Right")
        return u"→"_s;
    return key;
}

/// Whether `needle` appears in the first `n` characters of `haystack`, in order.
bool isSubsequence(const QString &needle, const QString &haystack, qsizetype n)
{
    qsizetype i = 0;
    for (qsizetype j = 0; j < n && i < needle.size(); ++j) {
        if (haystack.at(j) == needle.at(i))
            ++i;
    }
    return i == needle.size();
}

/// Per-position bonus of the first `n` folded characters, judged on the original text (camelCase
/// needs the case).
QList<int> positionBonuses(const text::FoldedText &folded, QStringView text, qsizetype n)
{
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
    return bonus;
}

QVariantList toVariantList(const QList<int> &values)
{
    QVariantList out;
    out.reserve(values.size());
    for (const int v : values)
        out.append(v);
    return out;
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
    if (!isSubsequence(q, f, n))
        return result;

    const QList<int> bonus = positionBonuses(folded, text, n);

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
        if (result.positions.isEmpty() || result.positions.constLast() != o)
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
    return {{u"score"_s, match.score}, {u"positions"_s, toVariantList(match.positions)}};
}

QStringList TextHelper::shortcutKeys(const QString &sequence) const
{
    QStringList out;
    if (sequence.isEmpty())
        return out;
    const QStringList parts = sequence.split(u'+');
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (parts.at(i).isEmpty() && i + 1 < parts.size() && parts.at(i + 1).isEmpty()) {
            out.append(u"+"_s);
            ++i;
        } else if (!parts.at(i).isEmpty()) {
            out.append(keyCap(parts.at(i)));
        }
    }
    return out;
}

QList<FuzzyRanked> fuzzyRankList(const QString &query, const QStringList &texts, int limit,
                                 const QStringList &secondary)
{
    struct Ranked {
        int index = 0;
        int score = 0;
        qsizetype length = 0;
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
            ranked.append({i, match.score, texts.at(i).size(), std::move(match.positions)});
    }
    if (!emptyQuery) {
        std::ranges::stable_sort(ranked, [](const Ranked &a, const Ranked &b) {
            if (a.score != b.score)
                return a.score > b.score;
            return a.length < b.length;
        });
    }
    if (limit > 0 && ranked.size() > limit)
        ranked.resize(limit);
    QList<FuzzyRanked> out;
    out.reserve(ranked.size());
    for (Ranked &r : ranked)
        out.append({r.index, r.score, std::move(r.positions)});
    return out;
}

QVariantList TextHelper::fuzzyRank(const QString &query, const QStringList &texts, int limit,
                                   const QStringList &secondary) const
{
    QVariantList out;
    const QList<FuzzyRanked> ranked = fuzzyRankList(query, texts, limit, secondary);
    out.reserve(ranked.size());
    for (const FuzzyRanked &r : ranked)
        out.append(QVariantMap{
                {u"index"_s, r.index}, {u"score"_s, r.score}, {u"positions"_s, toVariantList(r.positions)}});
    return out;
}

QStringList TextHelper::fuzzyRankTexts(const QString &query, const QStringList &texts, int limit) const
{
    QStringList out;
    const QList<FuzzyRanked> ranked = fuzzyRankList(query, texts, limit, {});
    out.reserve(ranked.size());
    for (const FuzzyRanked &r : ranked)
        out.append(texts.at(r.index));
    return out;
}

QString TextHelper::markMatches(const QString &text, const QVariantList &positions, const QString &color) const
{
    QList<int> list;
    list.reserve(positions.size());
    for (const QVariant &p : positions)
        list.append(p.toInt());
    return markMatchesHtml(text, list, color);
}

QString markMatchesHtml(const QString &text, const QList<int> &positions, const QString &color)
{
    QList<bool> marked(text.size(), false);
    for (const int i : positions) {
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

QString TextHelper::findHighlightedHtml(const QString &html, const QString &query, bool matchCase, bool matchDiacritics,
                                        bool wholeWord, const QString &color) const
{
    if (query.isEmpty())
        return html;
    return markdown::InlineRenderer::withFindHighlight(html, query, matchCase, matchDiacritics, wholeWord, color);
}

QVariantMap TextHelper::setHighlight(const QString &text, int from, int to, const QString &color) const
{
    std::optional<markdown::HighlightColor> c;
    if (!color.isEmpty()) {
        c = markdown::parseHighlightColorName(color);
        if (!c)
            return {{u"text"_s, text}, {u"from"_s, from}, {u"to"_s, to}, {u"changed"_s, false}};
    }
    const markdown::HighlightEdit edit = markdown::setHighlight(text, from, to, c);
    return {{u"text"_s, edit.text}, {u"from"_s, int(edit.from)}, {u"to"_s, int(edit.to)}, {u"changed"_s, edit.changed}};
}

QVariantMap TextHelper::pageIcon(const QString &text) const
{
    const markdown::PageIcon icon = markdown::parsePageIcon(text);
    const QString kind = icon.kind == markdown::PageIcon::Kind::Emoji ? u"emoji"_s
                         : icon.kind == markdown::PageIcon::Kind::App ? u"app"_s
                                                                      : QString();
    return {{u"kind"_s, kind}, {u"name"_s, icon.name}};
}

QVariantList TextHelper::highlightColors() const
{
    QVariantList out;
    for (int i = 0; i < markdown::HighlightColorCount; ++i) {
        const auto color = markdown::HighlightColor(i);
        out.append(QVariantMap{{u"name"_s, markdown::highlightColorName(color)},
                               {u"tint"_s, markdown::highlightTint(color)},
                               {u"written"_s, markdown::highlightWrittenColor(color)}});
    }
    return out;
}

QString TextHelper::hoveredLinkHtml(const QString &html, const QString &href) const
{
    return href.isEmpty() ? html : markdown::InlineRenderer::withHoveredLink(html, href);
}

} // namespace brulion::ui
