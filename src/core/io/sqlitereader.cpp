#include "io/sqlitereader_p.h"

#include "io/messages_p.h"

#include <QRegularExpression>
#include <QStringDecoder>
#include <QtEndian>

#include <algorithm>
#include <bit>
#include <cstring>

namespace brulion::io::detail {

using namespace Qt::StringLiterals;

namespace {

quint32 be32(QByteArrayView d, qsizetype at)
{
    if (at < 0 || at + 4 > d.size())
        return 0;
    const auto *p = reinterpret_cast<const unsigned char *>(d.data() + at);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint32 be16(QByteArrayView d, qsizetype at)
{
    if (at < 0 || at + 2 > d.size())
        return 0;
    const auto *p = reinterpret_cast<const unsigned char *>(d.data() + at);
    return (quint32(p[0]) << 8) | quint32(p[1]);
}

/// A SQLite varint (1–9 bytes, big-endian, 7 bits each but the ninth byte's 8). Returns the bytes
/// used, 0 when it runs past the end.
int varint(QByteArrayView d, qsizetype at, quint64 *value)
{
    quint64 v = 0;
    for (int i = 0; i < 9; ++i) {
        if (at + i >= d.size())
            return 0;
        const auto b = static_cast<unsigned char>(d[at + i]);
        if (i == 8) {
            v = (v << 8) | b;
            *value = v;
            return 9;
        }
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            *value = v;
            return i + 1;
        }
    }
    return 0;
}

QString decodeText(QByteArrayView bytes, int encoding)
{
    if (encoding == 2 || encoding == 3) {
        QStringDecoder d(encoding == 2 ? QStringConverter::Utf16LE : QStringConverter::Utf16BE,
                         QStringConverter::Flag::Stateless);
        return d(bytes);
    }
    return QString::fromUtf8(bytes);
}

/// WAL checksum over `data` (multiples of 8 bytes), continuing s0/s1.
void walChecksum(QByteArrayView data, bool bigEndian, quint32 &s0, quint32 &s1)
{
    for (qsizetype i = 0; i + 8 <= data.size(); i += 8) {
        quint32 x0, x1;
        std::memcpy(&x0, data.data() + i, 4);
        std::memcpy(&x1, data.data() + i + 4, 4);
        if (bigEndian != (std::endian::native == std::endian::big)) {
            x0 = qbswap(x0);
            x1 = qbswap(x1);
        }
        s0 += x0 + s1;
        s1 += x1 + s0;
    }
}

} // namespace

bool decodeSqliteRecord(QByteArrayView payload, int textEncoding, int columnCount, const QList<int> &wanted,
                        QList<QVariant> *values)
{
    values->clear();
    values->resize(columnCount);
    quint64 headerSize = 0;
    int n = varint(payload, 0, &headerSize);
    if (n == 0 || headerSize > quint64(payload.size()) || headerSize < quint64(n))
        return false;
    qsizetype pos = n;
    qsizetype body = qsizetype(headerSize);
    for (int col = 0; pos < qsizetype(headerSize); ++col) {
        quint64 type = 0;
        n = varint(payload, pos, &type);
        if (n == 0)
            return false;
        pos += n;
        qint64 size = 0;
        switch (type) {
        case 0:
        case 8:
        case 9: size = 0; break;
        case 1: size = 1; break;
        case 2: size = 2; break;
        case 3: size = 3; break;
        case 4: size = 4; break;
        case 5: size = 6; break;
        case 6:
        case 7: size = 8; break;
        case 10:
        case 11: return false; // reserved
        default:
            if (type > quint64(SqliteReader::MaxRecordBytes) * 2 + 13)
                return false;
            size = qint64((type - (type % 2 ? 13 : 12)) / 2);
        }
        if (body + size > payload.size())
            return false;
        const bool want = col < columnCount && (wanted.isEmpty() || wanted.contains(col));
        if (want) {
            QVariant v;
            const QByteArrayView bytes = payload.sliced(body, size);
            if (type >= 1 && type <= 6) {
                qint64 i = static_cast<signed char>(bytes[0]);
                for (qsizetype k = 1; k < size; ++k)
                    i = (i << 8) | static_cast<unsigned char>(bytes[k]);
                v = i;
            } else if (type == 7) {
                quint64 bits = 0;
                for (qsizetype k = 0; k < 8; ++k)
                    bits = (bits << 8) | static_cast<unsigned char>(bytes[k]);
                v = std::bit_cast<double>(bits);
            } else if (type == 8) {
                v = qint64(0);
            } else if (type == 9) {
                v = qint64(1);
            } else if (type >= 12 && type % 2 == 0) {
                v = bytes.toByteArray();
            } else if (type >= 13) {
                v = decodeText(bytes, textEncoding);
            }
            (*values)[col] = v;
        }
        body += size;
    }
    return true;
}

QStringList sqliteColumns(const QString &createSql, int *rowidColumn)
{
    *rowidColumn = -1;
    const qsizetype open = createSql.indexOf(u'(');
    const qsizetype close = createSql.lastIndexOf(u')');
    if (open < 0 || close <= open)
        return {};
    const QString inner = createSql.sliced(open + 1, close - open - 1);
    // Split at top-level commas (types like DECIMAL(10,2), quoted names).
    QStringList parts;
    QString current;
    int depth = 0;
    QChar quote;
    for (const QChar c : inner) {
        if (!quote.isNull()) {
            current += c;
            if (c == quote)
                quote = QChar();
            continue;
        }
        if (c == u'"' || c == u'\'' || c == u'`') {
            quote = c;
        } else if (c == u'[') {
            quote = u']';
        } else if (c == u'(') {
            ++depth;
        } else if (c == u')') {
            --depth;
        } else if (c == u',' && depth == 0) {
            parts.append(current.trimmed());
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.trimmed().isEmpty())
        parts.append(current.trimmed());
    static const QRegularExpression constraint(u"^(?:CONSTRAINT|PRIMARY\\s+KEY|UNIQUE|CHECK|FOREIGN\\s+KEY)\\b"_s,
                                               QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression rowidType(u"^\\S+\\s+INTEGER\\s+PRIMARY\\s+KEY\\b"_s,
                                              QRegularExpression::CaseInsensitiveOption);
    QStringList columns;
    for (const QString &part : std::as_const(parts)) {
        if (constraint.match(part).hasMatch())
            continue;
        QString name;
        if (!part.isEmpty() && (part.front() == u'"' || part.front() == u'`' || part.front() == u'[')) {
            const QChar end = part.front() == u'[' ? u']' : part.front();
            const qsizetype e = part.indexOf(end, 1);
            name = e > 0 ? part.sliced(1, e - 1) : part.sliced(1);
        } else {
            name = part.section(QRegularExpression(u"\\s+"_s), 0, 0);
        }
        if (rowidType.match(part).hasMatch() && *rowidColumn < 0)
            *rowidColumn = int(columns.size());
        columns.append(name);
    }
    return columns;
}

std::optional<SqliteReader> SqliteReader::open(QByteArray database, QByteArray wal, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<SqliteReader> {
        if (error)
            *error = message;
        return std::nullopt;
    };
    if (database.size() < 512 || !database.startsWith(QByteArrayView("SQLite format 3\0", 16)))
        return fail(IoMessages::tr("The file is no SQLite database."));
    SqliteReader r;
    r.m_data = std::move(database);
    quint32 pageSize = be16(r.m_data, 16);
    if (pageSize == 1)
        pageSize = 65536;
    if (pageSize < 512 || pageSize > 65536 || (pageSize & (pageSize - 1)) != 0)
        return fail(IoMessages::tr("The database is damaged (page size)."));
    r.m_pageSize = pageSize;
    const auto reserved = static_cast<unsigned char>(r.m_data[20]);
    r.m_usable = pageSize - reserved;
    if (r.m_usable < 480)
        return fail(IoMessages::tr("The database is damaged (page size)."));
    r.m_textEncoding = int(be32(r.m_data, 56));
    if (r.m_textEncoding < 1 || r.m_textEncoding > 3)
        r.m_textEncoding = 1;
    r.m_pageCount = quint32(r.m_data.size() / pageSize);
    if (!wal.isEmpty())
        r.applyWal(wal);
    QString schemaError;
    if (!r.readSchema(&schemaError))
        return fail(schemaError);
    return r;
}

void SqliteReader::applyWal(const QByteArray &wal)
{
    // Header: magic, version, page size, checkpoint, salt1, salt2, checksum1, checksum2.
    const quint32 magic = be32(wal, 0);
    if (wal.size() < 32 || (magic != 0x377f0682 && magic != 0x377f0683) || be32(wal, 8) != m_pageSize)
        return;
    const bool bigEndian = magic == 0x377f0683;
    quint32 s0 = 0, s1 = 0;
    walChecksum(QByteArrayView(wal).first(24), bigEndian, s0, s1);
    if (s0 != be32(wal, 24) || s1 != be32(wal, 28))
        return;
    const quint32 salt1 = be32(wal, 16), salt2 = be32(wal, 20);
    QHash<quint32, qsizetype> pending;
    quint32 committedPages = 0;
    const qsizetype frameSize = 24 + qsizetype(m_pageSize);
    for (qsizetype at = 32; at + frameSize <= wal.size(); at += frameSize) {
        const quint32 pageNumber = be32(wal, at);
        const quint32 commit = be32(wal, at + 4);
        if (be32(wal, at + 8) != salt1 || be32(wal, at + 12) != salt2 || pageNumber == 0)
            break;
        walChecksum(QByteArrayView(wal).sliced(at, 8), bigEndian, s0, s1);
        walChecksum(QByteArrayView(wal).sliced(at + 24, m_pageSize), bigEndian, s0, s1);
        if (s0 != be32(wal, at + 16) || s1 != be32(wal, at + 20))
            break;
        pending.insert(pageNumber, at + 24);
        ++m_walFrames;
        if (commit != 0) {
            // A transaction ends: its pages count.
            for (auto it = pending.constBegin(); it != pending.constEnd(); ++it)
                m_walPages.insert(it.key(), it.value());
            pending.clear();
            committedPages = commit;
        }
    }
    m_walFrames -= int(pending.size()); // an unfinished transaction is not applied
    if (!m_walPages.isEmpty()) {
        m_wal = wal;
        if (committedPages > 0)
            m_pageCount = committedPages;
    }
}

QByteArrayView SqliteReader::page(quint32 number) const
{
    if (number == 0 || number > m_pageCount)
        return {};
    if (const auto it = m_walPages.constFind(number); it != m_walPages.constEnd())
        return QByteArrayView(m_wal).sliced(*it, m_pageSize);
    const qsizetype at = qsizetype(number - 1) * m_pageSize;
    if (at + qsizetype(m_pageSize) > m_data.size())
        return {};
    return QByteArrayView(m_data).sliced(at, m_pageSize);
}

bool SqliteReader::readPayload(QByteArrayView pg, qsizetype cell, bool leaf, qint64 *rowid, QByteArray *payload) const
{
    quint64 size = 0;
    int n = varint(pg, cell, &size);
    if (n == 0)
        return false;
    cell += n;
    quint64 id = 0;
    n = varint(pg, cell, &id);
    if (n == 0)
        return false;
    cell += n;
    *rowid = qint64(id);
    if (!leaf)
        return true;
    if (size > quint64(MaxRecordBytes))
        return false;
    const qint64 u = m_usable;
    const qint64 x = u - 35;
    qint64 local = qint64(size);
    if (local > x) {
        const qint64 m = ((u - 12) * 32 / 255) - 23;
        const qint64 k = m + ((qint64(size) - m) % (u - 4));
        local = k <= x ? k : m;
    }
    if (cell + local > pg.size())
        return false;
    payload->clear();
    payload->reserve(qsizetype(size));
    payload->append(pg.sliced(cell, local));
    if (local == qint64(size))
        return true;
    quint32 next = be32(pg, cell + local);
    QSet<quint32> seen;
    while (payload->size() < qsizetype(size)) {
        if (next == 0 || seen.contains(next))
            return false;
        seen.insert(next);
        const QByteArrayView over = page(next);
        if (over.isEmpty())
            return false;
        const qsizetype take = std::min<qsizetype>(qsizetype(size) - payload->size(), qsizetype(u) - 4);
        payload->append(over.sliced(4, take));
        next = be32(over, 0);
    }
    return true;
}

bool SqliteReader::walk(quint32 number, int depth, QSet<quint32> &visited,
                        const std::function<bool(qint64, const QByteArray &)> &leafCell, bool *stop) const
{
    if (depth > MaxTreeDepth || visited.contains(number))
        return false;
    visited.insert(number);
    const QByteArrayView pg = page(number);
    if (pg.isEmpty())
        return false;
    const qsizetype header = number == 1 ? 100 : 0;
    const auto type = static_cast<unsigned char>(pg[header]);
    const quint32 cells = be16(pg, header + 3);
    const bool leaf = type == 0x0D;
    if (type != 0x0D && type != 0x05)
        return false;
    const qsizetype pointers = header + (leaf ? 8 : 12);
    if (pointers + qsizetype(cells) * 2 > pg.size())
        return false;
    bool ok = true;
    for (quint32 i = 0; i < cells && !*stop; ++i) {
        const qsizetype cell = be16(pg, pointers + qsizetype(i) * 2);
        if (cell < pointers || cell >= pg.size())
            return false;
        if (leaf) {
            qint64 rowid = 0;
            QByteArray payload;
            if (!readPayload(pg, cell, true, &rowid, &payload)) {
                ok = false;
                continue; // one damaged cell: the others still come
            }
            if (!leafCell(rowid, payload))
                *stop = true;
        } else {
            const quint32 child = be32(pg, cell);
            ok = walk(child, depth + 1, visited, leafCell, stop) && ok;
        }
    }
    if (!leaf && !*stop)
        ok = walk(be32(pg, header + 8), depth + 1, visited, leafCell, stop) && ok;
    return ok;
}

bool SqliteReader::readSchema(QString *error)
{
    Table master{u"sqlite_master"_s, 1, {u"type"_s, u"name"_s, u"tbl_name"_s, u"rootpage"_s, u"sql"_s}, -1};
    bool any = false;
    const bool ok = scan(master, [this, &any](qint64, const QList<QVariant> &v) {
        any = true;
        if (v.value(0).toString() != u"table")
            return true;
        Table t;
        t.name = v.value(1).toString();
        t.rootPage = quint32(v.value(3).toLongLong());
        t.columns = sqliteColumns(v.value(4).toString(), &t.rowidColumn);
        if (!t.name.isEmpty() && t.rootPage > 0 && !t.columns.isEmpty())
            m_tables.append(t);
        return true;
    });
    if (!ok && !any) {
        *error = IoMessages::tr("The database is damaged (schema).");
        return false;
    }
    return true;
}

QStringList SqliteReader::tables() const
{
    QStringList out;
    for (const Table &t : m_tables)
        out.append(t.name);
    return out;
}

const SqliteReader::Table *SqliteReader::table(const QString &name) const
{
    for (const Table &t : m_tables) {
        if (t.name.compare(name, Qt::CaseInsensitive) == 0)
            return &t;
    }
    return nullptr;
}

bool SqliteReader::scan(const Table &table, const RowCallback &callback, const QList<int> &columns) const
{
    QSet<quint32> visited;
    bool stop = false;
    QList<QVariant> values;
    const int count = int(table.columns.size());
    return walk(
                   table.rootPage, 0, visited,
                   [&](qint64 rowid, const QByteArray &payload) {
                       if (!decodeSqliteRecord(payload, m_textEncoding, count, columns, &values))
                           return true;
                       if (table.rowidColumn >= 0 && table.rowidColumn < count)
                           values[table.rowidColumn] = rowid;
                       return callback(rowid, values);
                   },
                   &stop)
           || stop;
}

} // namespace brulion::io::detail
