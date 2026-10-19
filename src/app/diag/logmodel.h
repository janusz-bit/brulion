// LogModel — the event log's entries for LogsView (QML: `LogModel { group: "sync" }`), filtered
// by group (log/categories.h: "sync", "net", …; "" = all), lowest level, a search text (case-
// and diacritic-insensitive: "zazolc" finds "Zażółć") and a time window. Follows the Logger:
// new entries are appended (Logger::appended, coalesced ≤ 4 per second), a repeat of the
// newest entry updates its row. Exists only while the page is open — nothing is kept or
// computed otherwise.
//
// Roles: seq, time (ms since epoch), timeText ("14:03:22.517"), day ("2026-10-18"), level
// ("debug" | "info" | "warning" | "error" | "fatal"), letter ("D" …), group, category,
// message (plain text — show it with Text.PlainText), repeat, earlier (from an earlier session).
#pragma once

#include "log/entry.h"

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QString>

namespace brulion::logging {
class Logger;
}

namespace brulion::diag {

class LogModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString group READ group WRITE setGroup NOTIFY filterChanged FINAL)
    /// "debug" (everything) | "info" | "warning" | "error".
    Q_PROPERTY(QString minLevel READ minLevel WRITE setMinLevel NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY filterChanged FINAL)
    /// Time window in ms since the epoch; 0 = no bound.
    Q_PROPERTY(qint64 fromMs READ fromMs WRITE setFromMs NOTIFY filterChanged FINAL)
    Q_PROPERTY(qint64 toMs READ toMs WRITE setToMs NOTIFY filterChanged FINAL)
    /// Also the entries of earlier sessions (from the files).
    Q_PROPERTY(bool includeEarlier READ includeEarlier WRITE setIncludeEarlier NOTIFY filterChanged FINAL)
    /// Rows shown / entries known.
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countChanged FINAL)
    /// A filter is set (group, level, search or time window).
    Q_PROPERTY(bool filtered READ filtered NOTIFY filterChanged FINAL)

public:
    enum Role {
        SeqRole = Qt::UserRole + 1,
        TimeRole,
        TimeTextRole,
        DayRole,
        LevelRole,
        LetterRole,
        GroupRole,
        CategoryRole,
        MessageRole,
        RepeatRole,
        EarlierRole,
    };

    explicit LogModel(QObject *parent = nullptr);
    ~LogModel() override;

    /// The logger to show (default: Logger::instance()); tests pass their own.
    void setLogger(logging::Logger *logger);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString group() const { return m_group; }
    void setGroup(const QString &group);
    QString minLevel() const;
    void setMinLevel(const QString &level);
    QString search() const { return m_search; }
    void setSearch(const QString &search);
    qint64 fromMs() const { return m_fromMs; }
    void setFromMs(qint64 ms);
    qint64 toMs() const { return m_toMs; }
    void setToMs(qint64 ms);
    bool includeEarlier() const { return m_includeEarlier; }
    void setIncludeEarlier(bool on);
    int count() const { return int(m_rows.size()); }
    int totalCount() const { return int(m_all.size()); }
    bool filtered() const;

    /// One formatted line (with its continuation lines) for "copy line".
    Q_INVOKABLE QString lineAt(int row) const;
    /// The shown entries as log text (copy what you see).
    Q_INVOKABLE QString visibleText() const;
    /// Read everything again from the logger.
    Q_INVOKABLE void reload();

Q_SIGNALS:
    void filterChanged();
    void countChanged();

private:
    struct Item {
        logging::Entry entry;
        QString group;
        QString folded; ///< text::foldKey of category + message (search)
        bool earlier = false;
    };

    static Item makeItem(const logging::Entry &entry, bool earlier);
    bool matches(const Item &item) const;
    void rebuild();
    void onAppended();
    void onCleared();

    QPointer<logging::Logger> m_logger;
    QList<Item> m_all; ///< earlier entries (when included) then this session's, oldest first
    QList<int> m_rows; ///< indexes into m_all, shown order
    quint64 m_lastSeq = 0;
    QString m_group;
    logging::Level m_minLevel = logging::Level::Debug;
    QString m_search;
    QString m_foldedSearch;
    qint64 m_fromMs = 0;
    qint64 m_toMs = 0;
    bool m_includeEarlier = false;
    QMetaObject::Connection m_appended;
    QMetaObject::Connection m_cleared;
};

} // namespace brulion::diag
