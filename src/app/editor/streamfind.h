// StreamFind — the find bar over the days of the journal stream (docs/UI.md "Find and replace"):
// the bar's NoteFind keeps searching the day being written in (its current match, showing it,
// replacing it), this object spans the days around it. It counts the query in every day of the
// stream — the first `dayLimit` days, newest first, and no more than MaxSearchedChars of text; a
// day attached here (its PageController, while the stream has it on screen or in its cache) by its
// controller's text, any other from its file (EditorHost::read, cached until the host says the file
// changed) — and moves the search from day to day: next() / previous() at the end of a day, a new
// query or a replace with nothing after it in that day go on to the nearest day with a match
// (dayRequested(): the view scrolls there and makes it the day written in; NoteFind::enter() then
// shows that day's first or last match), round the stream to the first day again.
//
// Replace all covers every day searched: the day written in through NoteFind (one undo step in its
// editor), the other attached days through their controllers (an undo step each), the rest written
// through the host — compare-and-swap against what was read, so a file changed meanwhile is left
// alone and named. undo(token) puts back what that replace changed, only where the text is still
// what it wrote.
//
//   StreamFind { id: streamFind; host: Workspace; model: days; find: noteFind }
//   ContentJournalDay: Component.onCompleted: streamFind.attach(page); onDestruction: detach(page)
//
// `model` lists the days top to bottom (role "path": JournalListModel).
#pragma once

#include "editorhost.h"
#include "notefind.h"
#include "pagecontroller.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace brulion::editor {

class StreamFind : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    /// The days top to bottom (role "path").
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged FINAL)
    /// The bar's search in the day written in (its controller is that day's).
    Q_PROPERTY(brulion::editor::NoteFind *find READ find WRITE setFind NOTIFY findChanged FINAL)
    /// How many days (from the top) are searched; 0: all of them.
    Q_PROPERTY(int dayLimit READ dayLimit WRITE setDayLimit NOTIFY dayLimitChanged FINAL)
    /// Matches in every day searched.
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
    /// The match shown, counted over the days from the top; -1: none.
    Q_PROPERTY(int current READ current NOTIFY changed FINAL)
    /// Days with a match.
    Q_PROPERTY(int dayCount READ dayCount NOTIFY changed FINAL)
    /// Days searched.
    Q_PROPERTY(int searchedDays READ searchedDays NOTIFY changed FINAL)
    /// The stream has more days than were searched (dayLimit, MaxSearchedChars).
    Q_PROPERTY(bool limited READ limited NOTIFY changed FINAL)
    /// Of the matches: those inside links and those in the YAML frontmatter (Replace all's warnings).
    Q_PROPERTY(int linkMatches READ linkMatches NOTIFY changed FINAL)
    Q_PROPERTY(int frontmatterMatches READ frontmatterMatches NOTIFY changed FINAL)

public:
    /// Text searched at most (UTF-16 units over every day): an old journal of many years stops there.
    static constexpr qsizetype MaxSearchedChars = 32 * 1024 * 1024;
    static constexpr int DefaultDayLimit = 1000;

    explicit StreamFind(QObject *parent = nullptr);
    ~StreamFind() override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QAbstractItemModel *model() const { return m_model; }
    void setModel(QAbstractItemModel *model);
    NoteFind *find() const { return m_find; }
    void setFind(NoteFind *find);
    int dayLimit() const { return m_dayLimit; }
    void setDayLimit(int limit);
    int count() const;
    int current() const;
    int dayCount() const;
    int searchedDays() const { return int(m_days.size()); }
    bool limited() const { return m_limited; }
    int linkMatches() const;
    int frontmatterMatches() const;

    /// A day's controller while the stream has it (its live text is searched instead of its file).
    Q_INVOKABLE void attach(brulion::editor::PageController *page);
    Q_INVOKABLE void detach(brulion::editor::PageController *page);
    /// Count again now (what is pending goes first).
    Q_INVOKABLE void refresh();
    /// The next / previous match: in the day written in, else in the nearest day after / before it
    /// that has one (round the stream).
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    /// The day written in's current match replaced (NoteFind::replaceCurrent), then the next one
    /// shown — in a later day when it was that day's last.
    Q_INVOKABLE bool replaceCurrent(const QString &replacement);
    /// Every match of every day searched replaced: { count, days, token (for undo(); 0: nothing
    /// replaced), changed (paths left alone: they changed since they were read), failed }.
    Q_INVOKABLE QVariantMap replaceAll(const QString &replacement);
    /// Put back the replace `token`: { restored (days), left (paths that changed since) }.
    Q_INVOKABLE QVariantMap undo(int token);
    /// Matches in the day at `path` (the day written in: NoteFind's count); 0 when not searched.
    Q_INVOKABLE int countIn(const QString &path) const;

Q_SIGNALS:
    void hostChanged();
    void modelChanged();
    void findChanged();
    void dayLimitChanged();
    void changed();
    /// Make the day at `row` (`path`) of the model the one written in: the search continues there
    /// (NoteFind::enter() has been called: its first match when going down, its last going up).
    void dayRequested(int row, const QString &path);

private:
    struct Day {
        QString path;
        int matches = 0;
        int links = 0;
        int frontmatter = 0;
    };
    struct CachedText {
        QString text;
        bool ok = false;
    };
    struct UndoFile {
        QString path;
        QString before;
        QString after;
    };
    struct UndoEdit {
        QPointer<PageController> controller;
        QString path;
        QString token;
    };

    void schedule();
    void recount();
    QString activePath() const;
    PageController *attached(const QString &path) const;
    /// The text of a day not written in: its attached controller's, else its file's (cached).
    QString textOf(const QString &path, bool *ok);
    void countDay(Day &day, const QString &text) const;
    /// Go on to the nearest day after (`forward`) / before the day written in that has a match.
    void moveToDay(bool forward);
    void onFileChanged(const QString &path);

    QPointer<EditorHost> m_host;
    QPointer<QAbstractItemModel> m_model;
    QPointer<NoteFind> m_find;
    QList<QMetaObject::Connection> m_modelConnections;
    QList<QMetaObject::Connection> m_findConnections;
    QMetaObject::Connection m_hostConnection;
    QList<QPointer<PageController>> m_pages;
    QHash<PageController *, QList<QMetaObject::Connection>> m_pageConnections;
    QHash<QString, CachedText> m_cache;
    int m_dayLimit = DefaultDayLimit;
    QList<Day> m_days; ///< the days searched, top to bottom (the day written in: counted by m_find)
    QString m_query;   ///< what m_days were counted for
    text::FindOptions m_options;
    bool m_limited = false;
    bool m_dirty = true;
    QTimer m_timer;

    int m_token = 0;
    QList<UndoEdit> m_undoEdits;
    QList<UndoFile> m_undoFiles;
};

} // namespace brulion::editor
