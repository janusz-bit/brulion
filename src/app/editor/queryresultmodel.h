// QueryResultModel — the live results of a ```query block (docs/SYNTAX.md §10) for the editor's
// QueryBlockView: runs the query through the page's host when it is a QueryHost (queryhost.h;
// the shell's Workspace, the playground's FakeEditorHost), flattens `group:` results into group
// header rows followed by their items, renders each item's Markdown with the page's renderer
// colours (links, tags, date chips; the task marker is drawn by the delegate), and runs the
// query again — debounced — when the index or the query changes. Tasks are checked (and get a
// status from their menu) through PageController::setTaskStatusAt (this page: an undoable edit;
// other files: the host), with the new status shown at once until the index catches up.
//
//   QueryResultModel { id: results; controller: block.controller; source: block.model.editText }
//   Repeater { model: results; delegate: … }   // roles below
//
// Rows (roles): kind ("group" | "item"), label (group label), count (group size), path, title
// (page title), folder, line (0-based), text (plain text), html (rendered), status ("" or the
// task status), isTask, blockId, due ("YYYY-MM-DD[ HH:mm]" or ""), overdue, reminder ("" or
// "YYYY-MM-DD HH:mm"), alarm, showPage (show the page next to the item: it is another page and
// not the group header), journal ("YYYY-MM-DD" when the page is a daily note, else "").
// Only the first `maxItems` items get rows (hiddenCount: the others; showMore() adds rows).
// A task's status menu open on a row holds the rows (hold() / release(): new rows would take the
// menu away with the old ones): runs that come due meanwhile (index changes from sync or other
// notes, the query edited) wait and run once when the last holder lets go; a status chosen on
// held rows goes to the task where the index has it now (lines may have moved meanwhile).
// Created from QML, it runs the query the first time when it is completed (with its controller
// and source), so the block is created with its results — a block whose results came a moment
// later grew after the editor's list had placed it (docs/UI.md "Scrolling performance").
#pragma once

#include "types.h"

#include <QAbstractListModel>
#include <QHash>
#include <QPointer>
#include <QQmlParserStatus>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace brulion {
class EditorHost;
}

namespace brulion::editor {

class PageController;

class QueryResultModel : public QAbstractListModel, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    /// The page the query block is on (host, current path for `page:this`, renderer colours,
    /// language, clock).
    Q_PROPERTY(brulion::editor::PageController *controller READ controller WRITE setController NOTIFY controllerChanged FINAL)
    /// The query text (the block's edit text without the fences).
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    /// The host can run queries; false: the view shows the query's filters only.
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    /// The last run succeeded (false with `error` for an invalid query).
    Q_PROPERTY(bool ok READ ok NOTIFY resultChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged FINAL)
    /// "tasks" | "blocks" | "pages" | "reminders".
    Q_PROPERTY(QString type READ type NOTIFY resultChanged FINAL)
    /// Matches before the query's `limit`.
    Q_PROPERTY(int total READ total NOTIFY resultChanged FINAL)
    /// Items the query returned (after `limit`).
    Q_PROPERTY(int itemCount READ itemCount NOTIFY resultChanged FINAL)
    /// Returned items without a row yet (see maxItems / showMore()).
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY resultChanged FINAL)
    /// False while the host is still indexing the vault (results may grow).
    Q_PROPERTY(bool complete READ complete NOTIFY resultChanged FINAL)
    /// The query has run at least once since the source or the host changed.
    Q_PROPERTY(bool ready READ ready NOTIFY resultChanged FINAL)
    /// The results are grouped (`group:`): group header rows come first in each group.
    Q_PROPERTY(bool grouped READ grouped NOTIFY resultChanged FINAL)
    /// Items shown at most (default 50).
    Q_PROPERTY(int maxItems READ maxItems WRITE setMaxItems NOTIFY maxItemsChanged FINAL)
    /// Debounce of re-runs after index / source changes in ms (default 150).
    Q_PROPERTY(int refreshDelay READ refreshDelay WRITE setRefreshDelay NOTIFY refreshDelayChanged FINAL)
    /// Something holds the rows (see hold()).
    Q_PROPERTY(bool held READ held NOTIFY heldChanged FINAL)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        LabelRole,
        CountRole,
        PathRole,
        TitleRole,
        FolderRole,
        LineRole,
        TextRole,
        HtmlRole,
        StatusRole,
        IsTaskRole,
        BlockIdRole,
        DueRole,
        OverdueRole,
        ReminderRole,
        AlarmRole,
        ShowPageRole,
        JournalRole,
    };
    Q_ENUM(Role)

    explicit QueryResultModel(QObject *parent = nullptr);
    ~QueryResultModel() override;

    void classBegin() override;
    void componentComplete() override;

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    PageController *controller() const { return m_controller; }
    void setController(PageController *controller);
    QString source() const { return m_source; }
    void setSource(const QString &source);
    bool available() const;
    bool ok() const { return m_ok; }
    QString error() const { return m_error; }
    QString type() const { return m_type; }
    int total() const { return m_total; }
    int itemCount() const { return int(m_items.size()); }
    int hiddenCount() const;
    bool complete() const { return m_complete; }
    bool ready() const { return m_ready; }
    bool grouped() const { return !m_groups.isEmpty(); }
    int maxItems() const { return m_maxItems; }
    void setMaxItems(int max);
    int refreshDelay() const { return m_timer.interval(); }
    void setRefreshDelay(int ms);
    bool held() const { return !m_holders.isEmpty(); }

    /// Run the query now.
    Q_INVOKABLE void refresh();
    /// Keep the rows as they are while `holder` shows something on one (a task's status menu):
    /// runs due meanwhile wait until every holder let go (release(), or the holder destroyed —
    /// a view destroyed with its menu open holds nothing). Holding twice is holding once.
    Q_INVOKABLE void hold(QObject *holder);
    /// `holder` lets go; the last one runs what waited (debounced, like an index change).
    Q_INVOKABLE void release(QObject *holder);
    /// Show 50 more items.
    Q_INVOKABLE void showMore();
    /// A row's roles by name (tests, QML helpers); empty for an invalid row.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Checkbox click on an item: todo/doing/deferred -> done, done/cancelled -> todo. False when
    /// the row is no task or the file could not be changed.
    Q_INVOKABLE bool toggleTask(int row);
    /// The status menu on an item: set its task's status ("todo", "doing", "done", "deferred",
    /// "cancelled"; "" = no status, the row stops being a task). False when the row is no task,
    /// has that status already, or the file could not be changed.
    Q_INVOKABLE bool setTaskStatus(int row, const QString &status);
    /// Open the item's page at its block: { action: "scrollTo", row } when it is on this page
    /// (for BlockEditor.revealRow), else navigated through the host.
    Q_INVOKABLE QVariantMap open(int row, bool newPane = false);
    /// A link in an item's text was clicked: page:/tag:/block:/ext: go through the controller (a
    /// same-page link of another page's item to that page); task:/reminder: open the item.
    Q_INVOKABLE QVariantMap activateLink(int row, const QString &url, bool newPane = false);

Q_SIGNALS:
    void controllerChanged();
    void sourceChanged();
    void availableChanged();
    void resultChanged();
    void maxItemsChanged();
    void refreshDelayChanged();
    void heldChanged();

private:
    struct Row {
        bool group = false;
        QVariantMap item; ///< the host's item map (items) / { key, label } (groups)
        int count = 0;
        QString status;   ///< task status shown (follows toggles before the index does)
        QString html;
        bool htmlValid = false;
    };

    void schedule();
    void timerFired();
    int currentLine(const Row &row) const;
    bool writeStatus(Row &row, const QString &status);
    void rebuildRows();
    void invalidateHtml();
    QString renderItem(const QVariantMap &item) const;
    void connectHost();

    QPointer<PageController> m_controller;
    QPointer<EditorHost> m_host;
    QString m_source;
    QTimer m_timer;
    bool m_ok = false;
    bool m_ready = false;
    bool m_completing = false; ///< created from QML, not completed yet (the first run waits for it)
    bool m_complete = true;
    QString m_error;
    QString m_type;
    int m_total = 0;
    QVariantList m_items;
    QVariantList m_groups;
    QList<Row> m_rows;
    int m_maxItems = 50;
    bool m_groupedByPage = false;
    VaultConfig m_config;
    QHash<QObject *, QMetaObject::Connection> m_holders;
    bool m_pending = false; ///< a run waited for the holders: the rows may be behind the index
};

} // namespace brulion::editor
