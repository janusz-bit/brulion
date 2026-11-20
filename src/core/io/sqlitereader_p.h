// A minimal read-only reader of SQLite database files (https://www.sqlite.org/fileformat2.html)
// for the Apple Notes import (NoteStore.sqlite): the schema (sqlite_master), and full scans of
// table B-trees with their records, overflow pages and a write-ahead log (the "-wal" file next to
// the database, whose committed frames replace pages of the main file). No SQL, no indexes, no
// writes. The input is hostile: every page number, offset and length is checked, cycles and
// depths are bounded, and a damaged file gives an error or fewer rows — never a crash.
// Internal to the io module.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>

namespace brulion::io::detail {

class SqliteReader {
public:
    struct Table {
        QString name;
        quint32 rootPage = 0;
        QStringList columns;
        int rowidColumn = -1; ///< the INTEGER PRIMARY KEY column (its value is the rowid), -1: none
    };

    /// Open `database` (and its write-ahead log `wal`, may be empty). nullopt with `error` when the
    /// data is no SQLite database or its first page is damaged.
    static std::optional<SqliteReader> open(QByteArray database, QByteArray wal = {}, QString *error = nullptr);

    /// The tables of the schema (names as written).
    QStringList tables() const;
    /// A table by name (case-insensitive), nullptr when there is none.
    const Table *table(const QString &name) const;

    /// One row: its rowid and its values in column order — NULL as an invalid QVariant, integers as
    /// qint64, reals as double, texts as QString, blobs as QByteArray. `columns` limits what is
    /// decoded (indexes into Table::columns; empty: all) — other values stay invalid.
    using RowCallback = std::function<bool(qint64 rowid, const QList<QVariant> &values)>;
    /// Every row of `table` in rowid order; the callback returns false to stop. False when the
    /// B-tree is damaged (the rows before the damage were delivered).
    bool scan(const Table &table, const RowCallback &callback, const QList<int> &columns = {}) const;

    /// Pages of the database (with the log's).
    quint32 pageCount() const { return m_pageCount; }
    /// Frames of the log that were applied (committed and valid).
    int walFrames() const { return m_walFrames; }

    /// Bounds: the largest record read (bytes) and the most pages visited by one scan.
    static constexpr qint64 MaxRecordBytes = 64LL * 1024 * 1024;
    static constexpr int MaxTreeDepth = 40;

private:
    SqliteReader() = default;
    /// The bytes of page `number` (1-based), from the log when it has the page, else the file;
    /// empty when out of range.
    QByteArrayView page(quint32 number) const;
    bool readPayload(QByteArrayView page, qsizetype cellOffset, bool leaf, qint64 *rowid, QByteArray *payload) const;
    bool walk(quint32 page, int depth, QSet<quint32> &visited,
              const std::function<bool(qint64, const QByteArray &)> &leafCell, bool *stop) const;
    void applyWal(const QByteArray &wal);
    bool readSchema(QString *error);

    QByteArray m_data;
    QByteArray m_wal;
    QHash<quint32, qsizetype> m_walPages; ///< page -> offset of its data in m_wal
    quint32 m_pageSize = 0;
    quint32 m_usable = 0;
    quint32 m_pageCount = 0;
    int m_textEncoding = 1; ///< 1 UTF-8, 2 UTF-16le, 3 UTF-16be
    int m_walFrames = 0;
    QList<Table> m_tables;
};

/// Decode one record (https://www.sqlite.org/fileformat2.html#record_format) into `values`
/// (wanted: indexes to decode, empty = all). False when the header is malformed.
bool decodeSqliteRecord(QByteArrayView payload, int textEncoding, int columnCount, const QList<int> &wanted,
                        QList<QVariant> *values);

/// The column names of a CREATE TABLE statement, and which one is the INTEGER PRIMARY KEY.
QStringList sqliteColumns(const QString &createSql, int *rowidColumn);

} // namespace brulion::io::detail
