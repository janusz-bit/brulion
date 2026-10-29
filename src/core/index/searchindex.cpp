#include "index/searchindex.h"

#include "index/suggest_p.h"
#include "index/textutil_p.h"
#include "text/text.h"

#include <algorithm>
#include <cmath>

namespace brulion {

using namespace index_detail;

namespace {

constexpr double K1 = 1.2;
constexpr double B = 0.7;
constexpr double Delta = 0.5; // BM25+
constexpr std::array<double, 3> Boost = {3.0, 2.0, 1.0};
constexpr double PrefixWeight = 0.375;
constexpr double FuzzyWeight = 0.45;

/// Allowed edit distance for a query term of `length` characters.
int fuzzyDistance(qsizetype length)
{
    return length >= 8 ? 2 : (length >= 4 ? 1 : 0);
}

/// Visit the tokens of already folded text.
template<typename Visit>
void forEachToken(QStringView folded, Visit &&visit)
{
    qsizetype start = -1;
    auto flush = [&](qsizetype end) {
        if (start >= 0 && end - start <= SearchIndex::MaxTokenLength)
            visit(folded.sliced(start, end - start));
        start = -1;
    };
    for (qsizetype i = 0; i < folded.size();) {
        const auto [cp, len] = codePointAt(folded, i);
        if (isTokenChar(cp)) {
            if (start < 0)
                start = i;
        } else {
            flush(i);
        }
        i += len;
    }
    flush(folded.size());
}

} // namespace

struct SearchIndex::Expansion {
    const QString *term = nullptr;
    const PostingList *postings = nullptr;
    double weight = 0;
    bool fuzzy = false; ///< reached only by a typo-tolerant match (not exactly / by prefix)
};

QStringList SearchIndex::tokenize(QStringView text)
{
    QStringList out;
    const QString folded = text::foldKey(text);
    forEachToken(folded, [&](QStringView token) { out.append(token.toString()); });
    return out;
}

void SearchIndex::set(const QString &id, QStringView title, const QStringList &aliases, QStringView content)
{
    remove(id);
    int doc = 0;
    if (!m_free.isEmpty()) {
        doc = m_free.takeLast();
    } else {
        doc = int(m_docs.size());
        m_docs.append(Doc());
    }
    Doc &d = m_docs[doc];
    d.id = id;
    d.used = true;

    QHash<QString, FieldCounts> counts;
    auto addField = [&](QStringView fieldText, Field field) {
        const QString folded = text::foldKey(fieldText);
        forEachToken(folded, [&](QStringView token) {
            ++counts[token.toString()][field];
            ++d.length[field];
        });
    };
    addField(title, Title);
    addField(aliases.join(u'\n'), Aliases);
    addField(content, Content);

    d.terms.reserve(counts.size());
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        const auto entry = m_postings.try_emplace(it.key()).first;
        PostingList &list = entry->second;
        const Posting posting{doc, it.value()};
        if (list.isEmpty() || list.constLast().doc < doc) {
            list.append(posting);
        } else {
            const auto pos = std::lower_bound(list.begin(), list.end(), doc,
                                              [](const Posting &p, int value) { return p.doc < value; });
            list.insert(pos, posting);
        }
        d.terms.append(&entry->first);
    }
    for (int f = 0; f < FieldCount; ++f)
        m_totalLength[f] += d.length[f];
    m_byId.insert(id, doc);
}

void SearchIndex::remove(const QString &id)
{
    const auto found = m_byId.constFind(id);
    if (found == m_byId.constEnd())
        return;
    const int doc = *found;
    m_byId.erase(found);
    Doc &d = m_docs[doc];
    for (const QString *term : std::as_const(d.terms)) {
        const auto it = m_postings.find(*term);
        if (it == m_postings.end())
            continue;
        PostingList &list = it->second;
        const auto pos = std::lower_bound(list.begin(), list.end(), doc,
                                          [](const Posting &p, int value) { return p.doc < value; });
        if (pos != list.end() && pos->doc == doc)
            list.erase(pos);
        if (list.isEmpty())
            m_postings.erase(it);
    }
    for (int f = 0; f < FieldCount; ++f)
        m_totalLength[f] -= std::min<quint64>(m_totalLength[f], d.length[f]);
    d = Doc();
    m_free.append(doc);
}

void SearchIndex::clear()
{
    m_postings.clear();
    m_docs.clear();
    m_free.clear();
    m_byId.clear();
    m_totalLength = {};
}

void SearchIndex::squeeze()
{
    for (auto &[term, list] : m_postings) {
        if (list.capacity() > list.size())
            list.squeeze();
    }
    m_docs.squeeze();
}

QList<SearchIndex::Expansion> SearchIndex::expand(const QString &term, bool prefix) const
{
    QList<Expansion> out;
    QHash<const QString *, qsizetype> seen;
    auto add = [&](const std::pair<const QString, PostingList> &entry, double weight, bool fuzzy) {
        const auto it = seen.constFind(&entry.first);
        if (it != seen.constEnd()) {
            Expansion &e = out[*it];
            e.weight = std::max(e.weight, weight);
            e.fuzzy = e.fuzzy && fuzzy;
            return;
        }
        seen.insert(&entry.first, out.size());
        out.append(Expansion{&entry.first, &entry.second, weight, fuzzy});
    };
    const auto len = double(term.size());
    if (const auto exact = m_postings.find(term); exact != m_postings.end())
        add(*exact, 1.0, false);
    if (prefix) {
        for (auto it = m_postings.lower_bound(term); it != m_postings.end() && it->first.startsWith(term); ++it) {
            if (it->first.size() == term.size())
                continue;
            const double extra = double(it->first.size()) - len;
            add(*it, PrefixWeight * len / (len + 0.3 * extra), false);
        }
    }
    const int maxDistance = fuzzyDistance(term.size());
    if (maxDistance > 0) {
        for (const auto &entry : m_postings) {
            if (std::abs(entry.first.size() - term.size()) > maxDistance)
                continue;
            const int d = boundedEditDistance(term, entry.first, maxDistance);
            if (d >= 1 && d <= maxDistance)
                add(entry, FuzzyWeight * len / (len + d), true);
        }
    }
    return out;
}

double SearchIndex::fieldScore(const Posting &posting, qsizetype df) const
{
    const double docs = double(m_byId.size());
    const double idf = std::log(1.0 + (docs - double(df) + 0.5) / (double(df) + 0.5));
    const Doc &d = m_docs[posting.doc];
    double score = 0;
    for (int f = 0; f < FieldCount; ++f) {
        const double tf = posting.tf[f];
        if (tf <= 0)
            continue;
        const double avg = m_totalLength[f] > 0 ? double(m_totalLength[f]) / docs : 1.0;
        const double norm = 1.0 - B + B * double(d.length[f]) / std::max(avg, 1e-9);
        score += Boost[f] * idf * (Delta + tf * (K1 + 1.0) / (tf + K1 * norm));
    }
    return score;
}

QList<SearchIndex::Hit> SearchIndex::search(QStringView query, int limit) const
{
    QList<Hit> hits;
    if (limit <= 0 || m_byId.isEmpty())
        return hits;
    QStringList terms = tokenize(query);
    terms.removeDuplicates();
    if (terms.size() > MaxQueryTerms)
        terms.resize(MaxQueryTerms);
    if (terms.isEmpty())
        return hits;

    const qsizetype docCount = m_docs.size();
    QList<double> total(docCount, 0.0);
    QList<int> matchedTerms(docCount, 0);
    // Query terms a document contains as typed (exactly or, the last one, as a prefix) — not only
    // through a typo-tolerant match.
    QList<int> literalTerms(docCount, 0);
    QList<QList<const QString *>> best(terms.size(), QList<const QString *>(docCount, nullptr));
    for (qsizetype t = 0; t < terms.size(); ++t) {
        const QList<Expansion> expansions = expand(terms[t], t == terms.size() - 1);
        QList<double> termScore(docCount, 0.0);
        QList<double> bestScore(docCount, 0.0);
        QList<bool> literal(docCount, false);
        for (const Expansion &e : expansions) {
            for (const Posting &p : *e.postings) {
                const double s = e.weight * fieldScore(p, e.postings->size());
                termScore[p.doc] += s;
                if (!e.fuzzy)
                    literal[p.doc] = true;
                if (s > bestScore[p.doc]) {
                    bestScore[p.doc] = s;
                    best[t][p.doc] = e.term;
                }
            }
        }
        for (qsizetype doc = 0; doc < docCount; ++doc) {
            if (best[t][doc]) {
                total[doc] += termScore[doc];
                ++matchedTerms[doc];
                if (literal[doc])
                    ++literalTerms[doc];
            }
        }
    }

    auto collect = [&](int required) {
        QList<int> docs;
        for (qsizetype doc = 0; doc < docCount; ++doc) {
            if (m_docs[doc].used && matchedTerms[doc] >= required && matchedTerms[doc] > 0)
                docs.append(int(doc));
        }
        return docs;
    };
    QList<int> docs = collect(int(terms.size()));
    if (docs.isEmpty() && terms.size() > 1)
        docs = collect(1);
    // What the user typed comes first: documents containing more of the query terms as typed
    // (exactly or as a prefix — "zolw" in "żółwia") rank above those found only through typo
    // tolerance ("zolw" ~ "żółć"), whatever the relevance score; then by score.
    std::sort(docs.begin(), docs.end(), [&](int a, int b) {
        if (literalTerms[a] != literalTerms[b])
            return literalTerms[a] > literalTerms[b];
        if (total[a] != total[b])
            return total[a] > total[b];
        return m_docs[a].id < m_docs[b].id;
    });
    if (docs.size() > limit)
        docs.resize(limit);
    hits.reserve(docs.size());
    for (const int doc : std::as_const(docs)) {
        Hit hit;
        hit.id = m_docs[doc].id;
        hit.score = total[doc];
        for (qsizetype t = 0; t < terms.size(); ++t) {
            if (const QString *term = best[t][doc]; term && !hit.terms.contains(*term))
                hit.terms.append(*term);
        }
        hits.append(std::move(hit));
    }
    return hits;
}

} // namespace brulion
