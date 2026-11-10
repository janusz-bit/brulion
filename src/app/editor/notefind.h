// NoteFind — the find bar's logic for one note (docs/UI.md "Find and replace"): the matches of a
// query in the page's actual Markdown (PageController::text(), in source mode its sourceText), the
// current one, going to it, replacing. The matches are recounted a moment after the text changes
// (longer on big notes), never touching the caret: only next(), previous() and the replaces emit
// targetRequested(). A query or option just changed goes to the nearest match from where the bar
// was opened. An edit leaves no current match (the one shown may have moved or gone): the next one
// counts from its place. Replace only ever replaces the match on show.
//
// A target says where a match is: { row, from, to (in that row's edit text), editable (the block's
// inline editor can select it; false for a table, a list marker or a task marker, a fence line: the
// view shows the whole block instead), sourceFrom, sourceTo (positions in source mode's TextEdit, whose line breaks are one "\n" each) }.
//
// Replacing goes through the PageController: one undo step, other blocks' bytes untouched, the usual
// save and merge. In source mode the text belongs to the TextEdit, with its own undo:
// sourceReplaceRequested() hands it the ranges (EditorInput::replaceRanges, one undo step).
#pragma once

#include "editorhost.h"
#include "pagecontroller.h"
#include "text/find.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::editor {

class NoteFind : public QObject {
    Q_OBJECT
    /// The note searched (a day of the journal stream: the day being written in); null: nothing.
    Q_PROPERTY(brulion::editor::PageController *controller READ controller WRITE setController NOTIFY controllerChanged FINAL)
    /// The bar is open: only then the matches are kept up to date.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    Q_PROPERTY(bool matchCase READ matchCase WRITE setMatchCase NOTIFY optionsChanged FINAL)
    Q_PROPERTY(bool matchDiacritics READ matchDiacritics WRITE setMatchDiacritics NOTIFY optionsChanged FINAL)
    Q_PROPERTY(bool wholeWord READ wholeWord WRITE setWholeWord NOTIFY optionsChanged FINAL)
    /// The query the matches are for (settles after typing stops): what the views highlight.
    Q_PROPERTY(QString appliedQuery READ appliedQuery NOTIFY matchesChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY matchesChanged FINAL)
    /// The match shown, -1: none yet.
    Q_PROPERTY(int current READ current NOTIFY matchesChanged FINAL)
    /// Of the matches: those inside links (text::isInsideLink(); a replace makes the link point
    /// elsewhere) and those in the YAML frontmatter, which the block view hides in its panel.
    Q_PROPERTY(int linkMatches READ linkMatches NOTIFY matchesChanged FINAL)
    Q_PROPERTY(int frontmatterMatches READ frontmatterMatches NOTIFY matchesChanged FINAL)
    /// Next and previous go round the note (default). Off (the journal stream: StreamFind), the
    /// note's end is endReached() instead — also a new query or a replace with nothing after it.
    Q_PROPERTY(bool wrap READ wrap WRITE setWrap NOTIFY wrapChanged FINAL)

public:
    explicit NoteFind(QObject *parent = nullptr);

    PageController *controller() const { return m_controller; }
    void setController(PageController *controller);
    bool active() const { return m_active; }
    void setActive(bool active);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool matchCase() const { return m_options.matchCase; }
    void setMatchCase(bool on) { setOption(&text::FindOptions::matchCase, on); }
    bool matchDiacritics() const { return m_options.matchDiacritics; }
    void setMatchDiacritics(bool on) { setOption(&text::FindOptions::matchDiacritics, on); }
    bool wholeWord() const { return m_options.wholeWord; }
    void setWholeWord(bool on) { setOption(&text::FindOptions::wholeWord, on); }
    QString appliedQuery() const { return m_appliedQuery; }
    int count() const { return int(m_matches.size()); }
    int current() const { return m_current; }
    int linkMatches() const { return m_linkMatches; }
    int frontmatterMatches() const { return m_frontmatterMatches; }
    text::FindOptions options() const { return m_options; }
    bool wrap() const { return m_wrap; }
    void setWrap(bool wrap);
    /// The matches as counted (in the text of sourceOfSearch() at the last recount).
    const QList<text::TextRange> &matches() const { return m_matches; }
    /// A recount is pending (the text, the query or the options changed since).
    bool stale() const;
    /// Show the note's first (`first`) or last match as soon as the note is loaded and counted —
    /// the journal stream moving to another day; the controller may change before that. A note
    /// without a match then: endReached(first).
    Q_INVOKABLE void enter(bool first);

    /// The next match goes from where the caret is: `sourcePosition` in source mode's TextEdit, else
    /// the block being edited or selected (the bar was just opened).
    Q_INVOKABLE void setOriginHere(int sourcePosition = -1);
    /// Count again now (what is pending goes first in next(), previous() and the replaces too).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    /// The current match replaced, the one after it shown. No current match, or a recount still
    /// pending (the text or the options changed): a match is shown first and nothing is replaced —
    /// what is replaced has been seen. The same text as the match's: nothing to replace, the next
    /// one is shown. True when something was replaced.
    Q_INVOKABLE bool replaceCurrent(const QString &replacement);
    /// Every match replaced as one undo step. How many (0: none, or the same text).
    Q_INVOKABLE int replaceAll(const QString &replacement);

Q_SIGNALS:
    void controllerChanged();
    void activeChanged();
    void queryChanged();
    void optionsChanged();
    void matchesChanged();
    /// Show this match (see the file comment).
    void targetRequested(const QVariantMap &target);
    /// Source mode: replace these [{ from, to }] (TextEdit positions, ascending) in the TextEdit.
    void sourceReplaceRequested(const QVariantList &ranges, const QString &replacement);
    void wrapChanged();
    /// `wrap` off: there is no match after (`forward`) / before the one on show in this note.
    void endReached(bool forward);

private:
    void connectController();
    /// One of the options set (`option`: &text::FindOptions::matchCase, …): a new search.
    void setOption(bool text::FindOptions::*option, bool on);
    void schedule(bool jump);
    QString sourceOfSearch() const;
    QVariantMap targetOf(text::TextRange range) const;
    void show(int index);
    /// Shows match `index` going forward: past the last one the first (wrap), else endReached(true).
    void showForward(qsizetype index);
    void recount();
    void countRisks();
    bool sourceMode() const;

    QPointer<PageController> m_controller;
    QList<QMetaObject::Connection> m_connections;
    bool m_active = false;
    QString m_query;
    QString m_appliedQuery;
    text::FindOptions m_options;
    text::FindOptions m_appliedOptions;
    QString m_text; ///< the text m_matches are ranges of
    QList<text::TextRange> m_matches;
    int m_current = -1;
    int m_linkMatches = 0;
    int m_frontmatterMatches = 0;
    qsizetype m_origin = 0;
    bool m_jump = false;
    bool m_wrap = true;
    enum class Enter { None, First, Last } m_enter = Enter::None;
    QTimer m_timer;
};

} // namespace brulion::editor
