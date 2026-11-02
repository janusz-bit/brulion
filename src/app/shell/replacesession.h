// ReplaceSession — find and replace in the whole notebook (Workspace.replacing; qml/views/ReplaceView.qml,
// docs/UI.md "Find and replace"): the scan behind the view (notebook::ReplaceScan on the index's
// threads, in slices on WebAssembly; editors save first, the scan starts a moment after typing
// stops), the preview as a list model (a row per file, then its hits, then how many more; capped by
// notebook::ReplaceLimits, the count stays exact), what is ticked (everything at first), applying in
// slices and undoing. The session outlives the view: a toast's "Cofnij" works from any view, and
// only for the replace it told about (undo(token)).
#pragma once

#include "notebook/replace.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <memory>

namespace brulion::shell {

class Workspace;

class ReplaceSession : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY parametersChanged FINAL)
    Q_PROPERTY(QString replacement READ replacement WRITE setReplacement NOTIFY parametersChanged FINAL)
    Q_PROPERTY(bool matchCase READ matchCase WRITE setMatchCase NOTIFY parametersChanged FINAL)
    Q_PROPERTY(bool matchDiacritics READ matchDiacritics WRITE setMatchDiacritics NOTIFY parametersChanged FINAL)
    Q_PROPERTY(bool wholeWord READ wholeWord WRITE setWholeWord NOTIFY parametersChanged FINAL)
    /// Notes are being looked through.
    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged FINAL)
    /// Of the notes to look through, 0…1.
    Q_PROPERTY(qreal scanProgress READ scanProgress NOTIFY progressChanged FINAL)
    /// A replace or its undo is being written.
    Q_PROPERTY(bool applying READ applying NOTIFY stateChanged FINAL)
    Q_PROPERTY(qreal applyProgress READ applyProgress NOTIFY progressChanged FINAL)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int hitCount READ hitCount NOTIFY resultsChanged FINAL)
    /// Hits the preview does not show (they are replaced with their file unless it is unticked).
    Q_PROPERTY(int hiddenHits READ hiddenHits NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int tickedFiles READ tickedFiles NOTIFY resultsChanged FINAL)
    Q_PROPERTY(int tickedHits READ tickedHits NOTIFY resultsChanged FINAL)
    /// Notes left out: not valid UTF-8, too large, unreadable.
    Q_PROPERTY(int skippedNotes READ skippedNotes NOTIFY resultsChanged FINAL)

public:
    enum Roles {
        KindRole = Qt::UserRole + 1, ///< "file" | "hit" | "more"
        PathRole,
        TitleRole,
        FolderRole,
        HitsRole,     ///< file: its hits; more: how many are not shown
        TickRole,     ///< file: 0 none, 1 some, 2 all; hit: 0 or 2
        HitRole,      ///< hit: its number in the file
        LineRole,     ///< hit: 1-based line
        BeforeRole,
        MatchedRole,
        AfterRole,
        InLinkRole,
    };

    explicit ReplaceSession(Workspace *workspace);
    ~ReplaceSession() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QString replacement() const { return m_replacement; }
    void setReplacement(const QString &replacement);
    bool matchCase() const { return m_options.matchCase; }
    void setMatchCase(bool on);
    bool matchDiacritics() const { return m_options.matchDiacritics; }
    void setMatchDiacritics(bool on);
    bool wholeWord() const { return m_options.wholeWord; }
    void setWholeWord(bool on);
    bool scanning() const { return m_scan != nullptr; }
    qreal scanProgress() const;
    bool applying() const { return m_applyMode != ApplyMode::None; }
    qreal applyProgress() const;
    int fileCount() const { return int(m_plan.files.size()); }
    int hitCount() const { return m_plan.totalHits; }
    int hiddenHits() const { return m_plan.totalHits - m_plan.previewHits; }
    int tickedFiles() const;
    int tickedHits() const;
    int skippedNotes() const { return int(m_plan.skipped.size()); }

    /// The scan now, not after the debounce (Enter in the query field).
    Q_INVOKABLE void scanNow();
    /// The view went: a scan running stops; the results are forgotten. Undo stays.
    Q_INVOKABLE void reset();
    Q_INVOKABLE void setFileTicked(const QString &path, bool ticked);
    Q_INVOKABLE void setHitTicked(const QString &path, int hit, bool ticked);
    Q_INVOKABLE void tickAll(bool ticked);
    /// Replace what is ticked, in slices: applied() when done. False when there is nothing ticked, or
    /// something is running.
    Q_INVOKABLE bool apply();
    /// Put the replace `token` (applied()) back, in slices: undone() when done. Only the last replace
    /// (a later one takes the place), each file only while it holds what was written.
    Q_INVOKABLE bool undo(int token);
    /// `token`'s replace can still be undone.
    Q_INVOKABLE bool canUndo(int token) const;

Q_SIGNALS:
    void parametersChanged();
    void stateChanged();
    void progressChanged();
    void resultsChanged();
    /// A replace finished: `hits` in `files` files; `changed`: paths left alone as they changed since
    /// the preview; `failed`: paths that could not be written; `token` for undo().
    void applied(int token, int hits, int files, const QStringList &changed, const QStringList &failed);
    /// An undo finished: `restored` files; `left`: paths that changed since and stay as they are.
    void undone(int restored, const QStringList &left);

private:
    enum class ApplyMode { None, Applying, Undoing };
    struct Row {
        int file = 0; ///< in m_plan.files
        int hit = -1; ///< in that file's hits; -1: the file's own row
        bool more = false;
    };
    struct Ticks {
        bool file = true;
        QSet<int> excluded;
    };

    void forgetResults();
    void scheduleScan();
    void startScan();
    void stopScan();
    void scanStep();
    void applyStep();
    void undoStep();
    void rebuildRows();
    int fileRow(int file) const;
    void fileTickChanged(int file);
    int tickedInFile(const notebook::ReplaceFile &file) const;

    Workspace *m_workspace = nullptr;
    QString m_query;
    QString m_replacement;
    text::FindOptions m_options;
    QTimer m_debounce;
    QTimer m_slices;
    std::unique_ptr<notebook::ReplaceScan> m_scan;
    notebook::ReplacePlan m_plan;
    QList<Row> m_rows;
    QHash<QString, Ticks> m_ticks;

    ApplyMode m_applyMode = ApplyMode::None;
    qsizetype m_applyAt = 0;
    QList<notebook::ReplaceChoice> m_choices;
    notebook::ReplacePlan m_applyPlan; ///< the plan being applied: the view may go meanwhile
    QString m_applyReplacement;
    QList<notebook::ReplacedFile> m_written; ///< the last replace, for undo
    QStringList m_changed;
    QStringList m_failed;
    int m_appliedHits = 0;
    int m_undoRestored = 0;
    int m_token = 0;
};

} // namespace brulion::shell
