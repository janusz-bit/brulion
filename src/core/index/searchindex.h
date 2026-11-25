// SearchIndex — the full-text inverted index behind VaultIndex::search(). Part of the index
// module — see index/index.h for the module overview.
//
// Documents have three fields: title (boost x3), aliases (x2) and content (x1). Text is folded
// with text::foldKey() before it is split into tokens (runs of Unicode letters/digits), so the
// query "zolw" finds "Żółw". Scoring is BM25+ per field. Each query term matches exactly, by
// prefix (the last term only — search as you type) and fuzzily (optimal string alignment edit
// distance 1 for terms of 4+ characters, 2 for 8+); prefix and fuzzy matches weigh less than
// exact ones, and documents containing more query terms as typed (exactly or by prefix) always
// rank above those found only through typo tolerance. Every term must match (AND); when that
// finds nothing for a multi-term query, any term may match (OR). Not thread-safe; incremental
// (set/remove one document at a time). prepare() is pure and runs on any thread: the index loader
// tokenises documents on worker threads (notebook::IndexLoader) and builds a SearchIndex on one
// of them, which VaultIndex::adoptSearch() takes over.
#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <array>
#include <map>

namespace brulion {

class SearchIndex {
public:
    /// Bump when tokenize(), the folding or the fields change: the index cache (indexcache.h)
    /// stores search sections of this format only. 2: encrypted fragments make no words.
    static constexpr quint32 FormatVersion = 2;

    enum Field { Title = 0, Aliases = 1, Content = 2, FieldCount = 3 };
    /// Per field (title, aliases, content): term frequencies or lengths.
    using FieldCounts = std::array<quint32, FieldCount>;

    /// A tokenised document (prepare()), ready for set().
    struct Document {
        FieldCounts length{};                         ///< tokens per field
        QList<std::pair<QString, FieldCounts>> terms; ///< distinct terms (any order) with frequencies
    };

    /// One search result.
    struct Hit {
        QString id;
        double score = 0;
        /// Matched document terms (folded): the best match of every query term, for excerpts.
        QStringList terms;
        /// Query terms the document contains as typed (exactly or, the last one, as a prefix) — not
        /// only through a typo-tolerant match: results of two indexes merge by it, then by score.
        int literal = 0;
    };

    /// Tokens longer than this (base64 blobs, hashes) are not indexed.
    static constexpr qsizetype MaxTokenLength = 64;
    /// Query terms beyond this many (a pasted paragraph) are ignored.
    static constexpr qsizetype MaxQueryTerms = 32;

    SearchIndex() = default;
    // Move-only: documents point into the dictionary (Doc::terms).
    SearchIndex(const SearchIndex &) = delete;
    SearchIndex &operator=(const SearchIndex &) = delete;
    SearchIndex(SearchIndex &&) noexcept = default;
    SearchIndex &operator=(SearchIndex &&) noexcept = default;

    /// Add or replace the document `id`.
    void set(const QString &id, QStringView title, const QStringList &aliases, QStringView content);
    /// The same with a document prepared elsewhere; `contentKey` (VaultIndex::contentKey() of the
    /// note's text, 0 = unknown) tells VaultIndex::adoptSearch() whether it is still current.
    void set(const QString &id, Document doc, quint64 contentKey = 0);
    /// Tokenise a document (pure: any thread).
    static Document prepare(QStringView title, const QStringList &aliases, QStringView content);
    /// The content key the document `id` was set with; 0 when unknown or not set.
    quint64 contentKey(const QString &id) const;
    /// Ids of every document (any order).
    QStringList ids() const;
    /// Forget the document `id` (no-op when unknown).
    void remove(const QString &id);
    void clear();
    /// Drop the growth slack of the posting lists (after a bulk build).
    void squeeze();

    bool contains(const QString &id) const { return m_byId.contains(id); }
    /// Number of indexed documents.
    qsizetype size() const { return m_byId.size(); }
    /// Number of distinct terms in the dictionary.
    qsizetype termCount() const { return qsizetype(m_postings.size()); }
    /// Changes with every set(), remove(), clear() and squeeze(), unique among all indexes (one
    /// moved in has another): tells the index cache's snapshot whether the index changed between
    /// two of its steps. 0 for an index never changed.
    quint64 revision() const { return m_revision; }

    /// Best `limit` documents for `query`, highest score first (ties by id).
    QList<Hit> search(QStringView query, int limit) const;

    /// Folded tokens of `text` in order (duplicates kept): "Kupić farbę (2 l)" -> kupic, farbe, 2, l.
    static QStringList tokenize(QStringView text);

private:
    friend struct SearchIndexAccess; // the index cache's codec (indexcache.cpp)

    /// A document's term frequencies, packed into 32 bits (postings of 8 bytes, not 16): content
    /// in bits 0-20, title 21-25, aliases 26-30. Counts that do not fit (a word written over two
    /// million times, a title repeating one 32 times) set bit 31; bits 0-30 then index m_overflow.
    struct Posting {
        int doc = 0;
        quint32 tf = 0;
    };
    using PostingList = QList<Posting>; ///< sorted by doc
    struct Term {
        PostingList postings;
        quint32 id = 0; ///< index into m_terms
    };
    using Dictionary = std::map<QString, Term>; ///< ordered: prefix ranges via lower_bound

    struct Doc {
        QString id;
        FieldCounts length{};
        /// Distinct terms of the document by id (m_terms: a term keeps its id while a posting
        /// list of a document refers to it; nodes of a std::map never move).
        QList<quint32> terms;
        quint64 contentKey = 0;
        bool used = false;
    };

    static constexpr quint32 OverflowBit = 0x80000000u;
    /// The counts of `tf` (packed, or an index into `overflow`).
    static FieldCounts unpack(quint32 tf, const QList<FieldCounts> &overflow)
    {
        if (tf & OverflowBit)
            return overflow.at(qsizetype(tf & ~OverflowBit));
        return {(tf >> 21) & 0x1f, (tf >> 26) & 0x1f, tf & 0x1fffff};
    }
    FieldCounts counts(Posting p) const { return unpack(p.tf, m_overflow); }
    Posting makePosting(int doc, const FieldCounts &tf);
    void releasePosting(Posting p);
    /// The id of the new term at `it`.
    quint32 newTermId(Dictionary::iterator it);

    struct Expansion;
    QList<Expansion> expand(const QString &term, bool prefix) const;
    double fieldScore(Posting posting, qsizetype df) const;
    /// A new revision().
    void touch();

    Dictionary m_postings;
    QList<Dictionary::iterator> m_terms; ///< by Term::id (free ids: any value)
    QList<quint32> m_freeTerms;
    QList<FieldCounts> m_overflow; ///< counts that do not fit a Posting
    QList<quint32> m_freeOverflow;
    QList<Doc> m_docs;
    QList<int> m_free;
    QHash<QString, int> m_byId;
    std::array<quint64, FieldCount> m_totalLength{};
    quint64 m_revision = 0;
};

} // namespace brulion
