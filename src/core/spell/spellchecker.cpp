#include "spell/spellchecker.h"

#include "spell/spellwords.h"

#include <algorithm>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace brulion::spell {

namespace {

/// "kot" → "Kot" (the first letter in capitals, a surrogate pair kept whole).
QString capitalised(const QString &word)
{
    if (word.isEmpty())
        return word;
    const qsizetype first = word.front().isHighSurrogate() && word.size() > 1 ? 2 : 1;
    return word.left(first).toUpper() + word.mid(first);
}

bool hasLower(const QString &word)
{
    return std::any_of(word.cbegin(), word.cend(), [](QChar c) { return c.isLower(); });
}

/// An acronym: two or more letters, none of them small ("PDF", "NASA").
bool isAcronym(const QString &word)
{
    const auto letters = std::count_if(word.cbegin(), word.cend(), [](QChar c) { return c.isLetter(); });
    return letters >= 2 && !hasLower(word);
}

QString withoutDot(const QString &word)
{
    return word.endsWith(u'.') ? word.chopped(1) : word;
}

} // namespace

SpellBackend::~SpellBackend() = default;

QList<bool> SpellBackend::checkAll(const QStringList &words)
{
    QList<bool> result;
    result.reserve(words.size());
    for (const QString &w : words)
        result.append(check(w));
    return result;
}

bool matchesWordList(const QSet<QString> &list, const QString &word)
{
    if (word.isEmpty() || list.isEmpty())
        return false;
    if (list.contains(word))
        return true;
    // "Kot" at the start of a sentence for "kot".
    const QString lowerFirst = word.left(1).toLower() + word.mid(1);
    if (lowerFirst != word && !hasLower(word.left(1)) && list.contains(lowerFirst))
        return true;
    // "KOT" for "kot" and "Kot".
    if (!hasLower(word)) {
        const QString lower = word.toLower();
        if (list.contains(lower) || list.contains(capitalised(lower)))
            return true;
    }
    return false;
}

int editDistance(const QString &a, const QString &b, int limit)
{
    const qsizetype n = a.size();
    const qsizetype m = b.size();
    if (std::abs(n - m) > limit)
        return limit + 1;
    // Optimal string alignment: three rows.
    QList<int> before(m + 1), previous(m + 1), current(m + 1);
    for (qsizetype j = 0; j <= m; ++j)
        previous[j] = int(j);
    for (qsizetype i = 1; i <= n; ++i) {
        current[0] = int(i);
        int best = current[0];
        for (qsizetype j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int d = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                d = std::min(d, before[j - 2] + 1);
            current[j] = d;
            best = std::min(best, d);
        }
        if (best > limit)
            return limit + 1;
        std::swap(before, previous);
        std::swap(previous, current);
    }
    return std::min(previous[m], limit + 1);
}

WordListBackend::WordListBackend(QString language, const QStringList &words) : m_language(std::move(language))
{
    for (const QString &w : words) {
        const QString n = normalizedWord(w);
        if (!n.isEmpty() && !m_words.contains(n)) {
            m_words.insert(n);
            m_ordered.append(n);
        }
    }
}

bool WordListBackend::check(const QString &word)
{
    m_asked.append(word);
    return matchesWordList(m_words, word);
}

QStringList WordListBackend::suggest(const QString &word, int max)
{
    m_asked.append(word);
    struct Candidate {
        int distance;
        qsizetype order;
        QString word;
    };
    QList<Candidate> found;
    const QString lower = word.toLower();
    for (qsizetype i = 0; i < m_ordered.size(); ++i) {
        const int d = editDistance(lower, m_ordered[i].toLower(), 2);
        if (d <= 2)
            found.append({d, i, m_ordered[i]});
    }
    std::stable_sort(found.begin(), found.end(), [](const Candidate &x, const Candidate &y) {
        return x.distance != y.distance ? x.distance < y.distance : x.order < y.order;
    });
    QStringList out;
    const bool capital = !word.isEmpty() && word.front().isUpper();
    for (const Candidate &c : std::as_const(found)) {
        if (out.size() >= max)
            break;
        // Hunspell's habit: a capitalised word gets capitalised suggestions.
        out.append(capital ? capitalised(c.word) : c.word);
    }
    return out;
}

SpellChecker::SpellChecker() = default;
SpellChecker::~SpellChecker() = default;

void SpellChecker::setBackends(std::vector<std::unique_ptr<SpellBackend>> backends)
{
    m_backends = std::move(backends);
    // (A language without a dictionary gives no backend: never one to call.)
    std::erase_if(m_backends, [](const std::unique_ptr<SpellBackend> &b) { return !b; });
}

QStringList SpellChecker::languages() const
{
    QStringList out;
    for (const auto &b : m_backends)
        out.append(b->language());
    return out;
}

void SpellChecker::setPersonalWords(const QStringList &words)
{
    m_personal.clear();
    for (const QString &w : words) {
        const QString n = normalizedWord(w.trimmed());
        if (!n.isEmpty())
            m_personal.insert(n);
    }
}

void SpellChecker::setIgnoredWords(const QStringList &words)
{
    m_ignored.clear();
    for (const QString &w : words) {
        const QString n = normalizedWord(w.trimmed());
        if (!n.isEmpty())
            m_ignored.insert(n);
    }
}

bool SpellChecker::listed(const QString &word) const
{
    return matchesWordList(m_personal, word) || matchesWordList(m_ignored, word);
}

bool SpellChecker::knownByBackends(const QString &word)
{
    for (const auto &b : m_backends) {
        if (b->check(word))
            return true;
    }
    return false;
}

bool SpellChecker::partsCorrect(const QString &word)
{
    const QStringList parts = hyphenParts(word);
    if (parts.size() < 2)
        return false;
    return std::all_of(parts.cbegin(), parts.cend(), [this](const QString &part) {
        return isAcronym(part) || listed(part) || knownByBackends(part);
    });
}

bool SpellChecker::isCorrect(const QString &input)
{
    if (m_backends.empty())
        return true;
    const QString dotted = normalizedWord(input);
    const QString word = withoutDot(dotted);
    if (word.isEmpty())
        return true;
    if (listed(word) || (dotted != word && listed(dotted)))
        return true;
    if (knownByBackends(word) || (dotted != word && knownByBackends(dotted)))
        return true;
    return partsCorrect(word);
}

QList<bool> SpellChecker::checkAll(const QStringList &words)
{
    QList<bool> result(words.size(), true);
    if (m_backends.empty())
        return result;
    // Every word the lists do not decide goes to each backend in one request; what is left
    // (dots, hyphens) is decided word by word.
    QList<qsizetype> open;
    QStringList asked;
    for (qsizetype i = 0; i < words.size(); ++i) {
        const QString dotted = normalizedWord(words[i]);
        const QString word = withoutDot(dotted);
        if (word.isEmpty() || listed(word) || (dotted != word && listed(dotted)))
            continue;
        open.append(i);
        asked.append(word);
    }
    QList<bool> known(asked.size(), false);
    for (const auto &b : m_backends) {
        QStringList still;
        QList<qsizetype> where;
        for (qsizetype k = 0; k < asked.size(); ++k) {
            if (!known[k]) {
                still.append(asked[k]);
                where.append(k);
            }
        }
        if (still.isEmpty())
            break;
        const QList<bool> answers = b->checkAll(still);
        for (qsizetype k = 0; k < where.size() && k < answers.size(); ++k) {
            if (answers[k])
                known[where[k]] = true;
        }
    }
    for (qsizetype k = 0; k < open.size(); ++k) {
        if (known[k])
            continue;
        const QString dotted = normalizedWord(words[open[k]]);
        const QString word = withoutDot(dotted);
        result[open[k]] = (dotted != word && knownByBackends(dotted)) || partsCorrect(word);
    }
    return result;
}

QStringList SpellChecker::suggestions(const QString &input, int max)
{
    const QString word = withoutDot(normalizedWord(input));
    if (word.isEmpty() || max <= 0)
        return {};
    std::vector<QStringList> lists;
    for (const auto &b : m_backends)
        lists.push_back(b->suggest(word, max));
    QStringList out;
    QSet<QString> seen{word};
    for (qsizetype rank = 0; out.size() < max; ++rank) {
        bool any = false;
        for (const QStringList &list : lists) {
            if (rank >= list.size())
                continue;
            any = true;
            const QString s = list[rank];
            if (!s.isEmpty() && !seen.contains(s)) {
                seen.insert(s);
                out.append(s);
                if (out.size() >= max)
                    break;
            }
        }
        if (!any)
            break;
    }
    return out;
}

} // namespace brulion::spell
