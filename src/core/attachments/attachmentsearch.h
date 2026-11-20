// AttachmentSearch — the full-text index of the notebook's PDFs and pictures (docs/UI.md "Search"):
// a SearchIndex (index/searchindex.h: BM25+, prefix of the last word, fuzzy, diacritic-insensitive —
// "zolw" finds "żółw" —, the same scoring as the notes') with one document per page that has text
// and one for the file's name (its title field: "wykład" finds "Wykład 3.pdf"). Results come one
// per file, at its best page, with how many of its pages match, an excerpt of that page around
// the hit and the text the PDF viewer searches for to show it. Part of the attachments module —
// see attachments/attachments.h.
//
// prepare() is pure (any thread: the app tokenises on its worker); everything else is used from
// one thread. The page texts are not kept here (a notebook of lecture scans has megabytes of
// them): search() asks `pageText` for the pages of the hits it returns (the app reads them from
// the device's cache, record.h).
#pragma once

#include "attachments/record.h"
#include "attachments/searchfilters.h"
#include "index/searchindex.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace brulion::attachments {

class AttachmentSearch {
public:
    /// A page tokenised for set() / setPage().
    struct PreparedPage {
        int page = 0;
        SearchIndex::Document document;
        bool ocr = false;
    };
    /// A whole file tokenised (prepare()).
    struct Prepared {
        QString path;
        Kind kind = Kind::Pdf;
        int pageCount = 0;
        /// The file's name ("Wykład 3" of assets/Wykład 3.pdf) as a document of its own.
        SearchIndex::Document name;
        /// The pages with text, in page order.
        QList<PreparedPage> pages;
    };

    /// One file found.
    struct Hit {
        QString path;
        Kind kind = Kind::Pdf;
        /// The best matching page (1-based; 1 when only the file's name matched).
        int page = 1;
        /// Pages of the file that match (0: only its name).
        int matchedPages = 0;
        int pageCount = 0;
        double score = 0;
        /// Query terms matched as typed (SearchIndex::Hit::literal) — notes' hits merge by it.
        int literal = 0;
        /// The best page's text came from OCR.
        bool ocr = false;
        /// Only the file's name matched.
        bool nameOnly = false;
        /// Folded document terms of the best page that matched.
        QStringList terms;
        /// Plain text around the hit on that page, one line ("" when no page text was given).
        QString excerpt;
        /// What the PDF viewer (or the picture's highlights) searches for to show the hit: the query
        /// when the page holds it as a phrase, else the page's own spelling of its best matched word.
        QString find;
    };

    /// Kinds of files searched (search()).
    static constexpr Scopes AllKinds = AttachmentScopes;

    AttachmentSearch() = default;
    AttachmentSearch(const AttachmentSearch &) = delete;
    AttachmentSearch &operator=(const AttachmentSearch &) = delete;
    AttachmentSearch(AttachmentSearch &&) noexcept = default;
    AttachmentSearch &operator=(AttachmentSearch &&) noexcept = default;

    /// Tokenise the file `path`'s record (pure: any thread).
    static Prepared prepare(const QString &path, const Record &record);
    /// One page (an OCR result coming in).
    static PreparedPage preparePage(int page, const Page &text);

    /// Add or replace a file's documents.
    void set(Prepared prepared);
    /// Add or replace one page of an indexed file (no-op when the file is not indexed).
    void setPage(const QString &path, PreparedPage page);
    void remove(const QString &path);
    /// The file moved: its pages are kept, its name's document follows the new name.
    void rename(const QString &from, const QString &to);
    void clear();

    bool contains(const QString &path) const { return m_files.contains(path); }
    /// Files indexed.
    qsizetype fileCount() const { return m_files.size(); }
    /// Pages with text indexed.
    qsizetype pageCount() const;
    /// Pages of `path` with text indexed (0 when unknown).
    int indexedPages(const QString &path) const;
    QStringList paths() const { return m_files.keys(); }
    /// Grows with every change (a search result's model compares it).
    quint64 revision() const { return m_index.revision(); }

    /// The text of page `page` of `path` (nullopt: unknown — no excerpt then).
    using PageText = std::function<std::optional<Page>(const QString &path, int page)>;
    /// The best `limit` files for `query` (its words: no filters — searchfilters.h), best first:
    /// literal terms, then score, then path. `kinds`: Pdf and / or Pictures.
    QList<Hit> search(QStringView query, int limit, Scopes kinds = AllKinds, const PageText &pageText = {}) const;

    /// The excerpt of `text` around the best place of `terms` (folded document terms): the line
    /// window holding most of them, about `max` characters cut at words with "…", white space
    /// squashed; the text's start when none occurs. Pure.
    static QString excerpt(QStringView text, const QStringList &terms, qsizetype max = 160);
    /// What to search for on a page to show the hit (Hit::find): a query of several words when the
    /// page holds them in a row (folded), else the page's own spelling of the first of `terms` it
    /// holds as a word ("Żółw" for "zolw"), else the first term. Pure.
    static QString findText(QStringView text, QStringView query, const QStringList &terms);

private:
    struct File {
        int id = 0;
        Kind kind = Kind::Pdf;
        int pageCount = 0;
        /// Pages with a document, and those whose text came from OCR.
        QList<int> pages;
        QList<int> ocrPages;
    };
    static QString docId(int file, int page);
    static std::pair<int, int> fromDocId(const QString &id);
    void removeDocs(const File &file);

    SearchIndex m_index;
    QHash<QString, File> m_files;
    QHash<int, QString> m_pathOf;
    int m_nextId = 1;
};

} // namespace brulion::attachments
