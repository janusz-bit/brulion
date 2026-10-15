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
// (set/remove one document at a time).
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
    /// One search result.
    struct Hit {
        QString id;
        double score = 0;
        /// Matched document terms (folded): the best match of every query term, for excerpts.
        QStringList terms;
    };

    /// Tokens longer than this (base64 blobs, hashes) are not indexed.
    static constexpr qsizetype MaxTokenLength = 64;
    /// Query terms beyond this many (a pasted paragraph) are ignored.
    static constexpr qsizetype MaxQueryTerms = 32;

    /// Add or replace the document `id`.
    void set(const QString &id, QStringView title, const QStringList &aliases, QStringView content);
    /// Forget the document `id` (no-op when unknown).
    void remove(const QString &id);
    void clear();

    bool contains(const QString &id) const { return m_byId.contains(id); }
    /// Number of indexed documents.
    qsizetype size() const { return m_byId.size(); }
    /// Number of distinct terms in the dictionary.
    qsizetype termCount() const { return qsizetype(m_postings.size()); }

    /// Best `limit` documents for `query`, highest score first (ties by id).
    QList<Hit> search(QStringView query, int limit) const;

    /// Folded tokens of `text` in order (duplicates kept): "Kupić farbę (2 l)" -> kupic, farbe, 2, l.
    static QStringList tokenize(QStringView text);

private:
    enum Field { Title = 0, Aliases = 1, Content = 2, FieldCount = 3 };
    using FieldCounts = std::array<quint32, FieldCount>;

    struct Posting {
        int doc = 0;
        FieldCounts tf{};
    };
    using PostingList = QList<Posting>; ///< sorted by doc

    struct Doc {
        QString id;
        FieldCounts length{};
        QStringList terms; ///< distinct terms of the document
        bool used = false;
    };

    struct Expansion;
    QList<Expansion> expand(const QString &term, bool prefix) const;
    double fieldScore(const Posting &posting, qsizetype df) const;

    std::map<QString, PostingList> m_postings; ///< ordered: prefix ranges via lower_bound
    QList<Doc> m_docs;
    QList<int> m_free;
    QHash<QString, int> m_byId;
    std::array<quint64, FieldCount> m_totalLength{};
};

} // namespace brulion
