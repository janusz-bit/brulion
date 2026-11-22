// SearchResultModel — full-text search for SearchView (VaultIndex::search: folded words, prefix
// for the last word, fuzzy, title x3, aliases x2) and of the notebook's PDFs and pictures
// (Workspace.attachments: their text layers and OCR, one row per file at its best page — docs/UI.md
// "Searching attachments"), best first — what was typed before typo-tolerant matches, then by score,
// notes and attachments merged —, with the positions of the matched words for highlighting
// (TextHelper.markMatches) and simple filters.
//
//   SearchResultModel { workspace: Workspace; query: field.text; scope: "pages" }
//
// scope: "all" (default) | "pages" (no daily notes) | "journals" (only daily notes) |
// "attachments" (PDFs and pictures); the query's own filters narrow it further (core
// attachments/searchfilters.h: in:pdf, in:pictures, in:attachments, in:notes, in:pages, in:journals,
// also type: and the Polish w: / typ:); folder: only results inside this folder (and below).
// Results follow vault changes (re-run 300 ms after the index or the attachments' text changed
// while a query is set).
//
// Search by meaning (Workspace.semantic active: on, its model on this device, the notebook's
// vectors there — docs/UI.md "Search by meaning"): the notes' and attachments' full-text hits are
// fused with the notes closest in meaning (core semantic::hybridSearch: 0.8 meaning + 0.2 words,
// never meaning alone; an attachment or a note not embedded yet ranks by its words), and up to
// semantic::MeaningAloneHits notes without the query's words join them (byMeaning). The query's
// vector comes from another thread: the list waits for it a moment (MeaningWaitMs), then shows the
// full-text results and is re-sorted when it arrives. Without it: exactly the full-text search.
//
// Roles: path, title, folder, line (0-based first matching body line; -1 when only the title or
// an alias matched), excerpt (plain text around the match), excerptMatches / titleMatches
// (UTF-16 positions of matched characters), journalDate ("YYYY-MM-DD" for daily notes, else ""),
// score; kind ("note" | "pdf" | "picture"), page (an attachment's best page, 1-based; 0 for a note),
// pageCount, matchedPages (an attachment's pages that match; 0: only its name), ocr (that page's
// text came from OCR), find (what the PDF viewer or the picture view searches for to show the hit),
// byMeaning (a note found by meaning alone: none of the query's words in it).
#pragma once

#include "workspacemodel.h"

#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace brulion::content {

class SearchResultModel : public WorkspaceListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(QString scope READ scope WRITE setScope NOTIFY scopeChanged FINAL)
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged FINAL)
    /// Most results listed (default 100).
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged FINAL)
    /// Results before the scope / folder filters (for "3 of 12" and empty-filter hints).
    Q_PROPERTY(int totalCount READ totalCount NOTIFY resultsChanged FINAL)
    /// Folded words of the query that are highlighted.
    Q_PROPERTY(QStringList terms READ terms NOTIFY resultsChanged FINAL)
    /// [{ folder, count }] of the results' top-level folders ("" = the vault root), most first.
    Q_PROPERTY(QVariantList folders READ folders NOTIFY resultsChanged FINAL)
    /// The query has filters of its own (in:pdf …): the view says what they keep.
    Q_PROPERTY(bool queryFiltered READ queryFiltered NOTIFY resultsChanged FINAL)
    /// Attachments among the results before the filters (the scope's "Attachments" count).
    Q_PROPERTY(int attachmentCount READ attachmentCount NOTIFY resultsChanged FINAL)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        FolderRole,
        LineRole,
        ExcerptRole,
        ExcerptMatchesRole,
        TitleMatchesRole,
        JournalDateRole,
        ScoreRole,
        KindRole,
        PageRole,
        PageCountRole,
        MatchedPagesRole,
        OcrRole,
        FindRole,
        ByMeaningRole,
    };
    Q_ENUM(Role)

    /// How long a new query's results wait for its vector (search by meaning) before the
    /// full-text ones show.
    static constexpr int MeaningWaitMs = 150;

    explicit SearchResultModel(QObject *parent = nullptr);

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString scope() const { return m_scope; }
    void setScope(const QString &scope);
    QString folder() const { return m_folder; }
    void setFolder(const QString &folder);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int totalCount() const { return m_total; }
    QStringList terms() const { return m_terms; }
    QVariantList folders() const { return m_folders; }
    bool queryFiltered() const { return m_queryFiltered; }
    int attachmentCount() const { return m_attachmentCount; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void queryChanged();
    void scopeChanged();
    void folderChanged();
    void limitChanged();
    void resultsChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    struct Hit {
        QString path;
        QString title;
        QString folder;
        int line = -1;
        QString excerpt;
        QVariantList excerptMatches;
        QVariantList titleMatches;
        QString journalDate;
        double score = 0;
        QString kind = QStringLiteral("note");
        int page = 0;
        int pageCount = 0;
        int matchedPages = 0;
        bool ocr = false;
        QString find;
        bool byMeaning = false;
        /// (Merging notes and attachments: SearchIndex::Hit::literal.)
        int literal = 0;

        friend bool operator==(const Hit &, const Hit &) = default;
    };
    void connectAttachments();
    QList<Hit> m_rows;
    /// The query whose vector the list waits for (MeaningWaitMs), and that wait.
    QString m_meaningWait;
    QTimer m_meaningTimer;
    QMetaObject::Connection m_semanticContent;
    QMetaObject::Connection m_semanticQuery;
    QMetaObject::Connection m_semanticState;
    QString m_query;
    QString m_scope = QStringLiteral("all");
    QString m_folder;
    int m_limit = 100;
    int m_total = 0;
    QStringList m_terms;
    QVariantList m_folders;
    bool m_queryFiltered = false;
    int m_attachmentCount = 0;
    QMetaObject::Connection m_attachmentsConnection;
};

} // namespace brulion::content
