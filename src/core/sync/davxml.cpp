#include "sync/davxml.h"

#include "sync/url.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::sync {

namespace {

/// Element name without namespace prefix (namespace processing is off: prefixes may even be
/// undeclared, as some servers send them).
QStringView localName(const QXmlStreamReader &reader)
{
    const QStringView name = reader.qualifiedName();
    const qsizetype colon = name.lastIndexOf(u':');
    return colon < 0 ? name : name.mid(colon + 1);
}

bool isSuccess(const QString &status)
{
    if (status.trimmed().isEmpty())
        return true;
    const std::optional<int> code = parseStatusLine(status);
    return !code || (*code >= 200 && *code < 300);
}

/// Properties of one <prop>; a property element present but empty counts as present.
struct Props {
    std::optional<QString> etag, lastModified, contentLength;
    std::optional<bool> collection;

    void mergeFrom(const Props &other)
    {
        if (other.etag)
            etag = other.etag;
        if (other.lastModified)
            lastModified = other.lastModified;
        if (other.contentLength)
            contentLength = other.contentLength;
        if (other.collection)
            collection = other.collection;
    }
};

QString elementText(QXmlStreamReader &reader)
{
    return reader.readElementText(QXmlStreamReader::IncludeChildElements);
}

bool readResourceType(QXmlStreamReader &reader)
{
    bool collection = false;
    while (reader.readNextStartElement()) {
        if (localName(reader) == u"collection"_s)
            collection = true;
        reader.skipCurrentElement();
    }
    return collection;
}

Props readProp(QXmlStreamReader &reader)
{
    Props props;
    while (reader.readNextStartElement()) {
        const QStringView name = localName(reader);
        if (name == u"getetag"_s)
            props.etag = elementText(reader);
        else if (name == u"getlastmodified"_s)
            props.lastModified = elementText(reader);
        else if (name == u"getcontentlength"_s)
            props.contentLength = elementText(reader);
        else if (name == u"resourcetype"_s)
            props.collection = readResourceType(reader);
        else
            reader.skipCurrentElement();
    }
    return props;
}

/// One <propstat>: its properties count only when its status is 2xx (or absent).
std::optional<Props> readPropstat(QXmlStreamReader &reader)
{
    Props props;
    QString status;
    while (reader.readNextStartElement()) {
        const QStringView name = localName(reader);
        if (name == u"prop"_s)
            props.mergeFrom(readProp(reader));
        else if (name == u"status"_s)
            status = elementText(reader);
        else
            reader.skipCurrentElement();
    }
    if (!isSuccess(status))
        return std::nullopt;
    return props;
}

/// Byte count of <getcontentlength>: integers exactly (also beyond 2^53), fractions rounded
/// down; negative, non-finite and out-of-range values (>= 2^63, where a cast to qint64 would be
/// undefined) are invalid -> 0.
qint64 parseSize(const std::optional<QString> &text)
{
    if (!text)
        return 0;
    const QString trimmed = text->trimmed();
    bool ok = false;
    const qint64 exact = trimmed.toLongLong(&ok);
    if (ok)
        return qMax<qint64>(0, exact);
    const double n = trimmed.toDouble(&ok);
    constexpr double kBeyondQint64 = 9223372036854775808.0; // 2^63
    return ok && std::isfinite(n) && n >= 0 && n < kBeyondQint64 ? static_cast<qint64>(std::floor(n)) : 0;
}

std::optional<DavResource> readResponse(QXmlStreamReader &reader)
{
    QString href;
    QString status;
    Props props;
    while (reader.readNextStartElement()) {
        const QStringView name = localName(reader);
        if (name == u"href"_s) {
            href = elementText(reader).trimmed();
        } else if (name == u"status"_s) {
            status = elementText(reader);
        } else if (name == u"propstat"_s) {
            if (const std::optional<Props> ok = readPropstat(reader))
                props.mergeFrom(*ok);
        } else {
            reader.skipCurrentElement();
        }
    }
    if (href.isEmpty() || !isSuccess(status))
        return std::nullopt;
    DavResource res;
    res.href = href;
    res.isCollection = props.collection.value_or(false);
    res.etag = normalizeEtag(props.etag.value_or(QString()));
    res.mtime = props.lastModified ? parseHttpDate(*props.lastModified) : 0;
    res.size = parseSize(props.contentLength);
    return res;
}

int monthNumber(QStringView name)
{
    static const QStringList months = {u"jan"_s, u"feb"_s, u"mar"_s, u"apr"_s, u"may"_s, u"jun"_s,
                                       u"jul"_s, u"aug"_s, u"sep"_s, u"oct"_s, u"nov"_s, u"dec"_s};
    return static_cast<int>(months.indexOf(name.left(3).toString().toLower())) + 1;
}

/// Seconds east of UTC of "GMT", "UTC", "Z", "+0200", "-05:30"; std::nullopt if malformed.
std::optional<int> zoneOffset(const QString &zone)
{
    if (zone.isEmpty() || zone.compare(u"GMT"_s, Qt::CaseInsensitive) == 0
        || zone.compare(u"UTC"_s, Qt::CaseInsensitive) == 0 || zone.compare(u"UT"_s, Qt::CaseInsensitive) == 0
        || zone.compare(u"Z"_s, Qt::CaseInsensitive) == 0)
        return 0;
    QString digits = zone.mid(1);
    digits.remove(u':');
    if (digits.size() != 4)
        return std::nullopt;
    const int hours = digits.left(2).toInt();
    const int minutes = digits.mid(2).toInt();
    const int sign = zone.startsWith(u'-') ? -1 : 1;
    return sign * (hours * 3600 + minutes * 60);
}

qint64 utcMsecs(int year, int month, int day, int hour, int minute, int second, int offsetSeconds)
{
    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid())
        return 0;
    return QDateTime(date, time, QTimeZone::UTC).toMSecsSinceEpoch() - qint64(offsetSeconds) * 1000;
}

int fullYear(int year, qsizetype digits)
{
    if (digits > 2)
        return year;
    return year < 70 ? 2000 + year : 1900 + year;
}

} // namespace

QByteArray propfindBody()
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                             "<d:propfind xmlns:d=\"DAV:\">\n"
                             "  <d:prop>\n"
                             "    <d:getetag/>\n"
                             "    <d:getlastmodified/>\n"
                             "    <d:getcontentlength/>\n"
                             "    <d:resourcetype/>\n"
                             "  </d:prop>\n"
                             "</d:propfind>\n");
}

MultistatusResult parseMultistatus(QByteArrayView xml)
{
    MultistatusResult result;
    QXmlStreamReader reader(xml);
    reader.setNamespaceProcessing(false);
    if (!reader.readNextStartElement()) {
        result.error = reader.hasError() ? u"Invalid WebDAV XML: %1"_s.arg(reader.errorString())
                                         : u"Invalid WebDAV response: empty document"_s;
        return result;
    }
    if (localName(reader) != u"multistatus"_s) {
        result.error = u"Invalid WebDAV response: no <multistatus> element"_s;
        return result;
    }
    while (reader.readNextStartElement()) {
        if (localName(reader) == u"response"_s) {
            if (std::optional<DavResource> res = readResponse(reader))
                result.resources.append(std::move(*res));
        } else {
            reader.skipCurrentElement();
        }
    }
    // Consume the rest so that malformed trailing content is noticed too.
    while (!reader.atEnd())
        reader.readNext();
    if (reader.hasError()) {
        result.resources.clear();
        result.error = u"Invalid WebDAV XML: %1"_s.arg(reader.errorString());
    }
    return result;
}

std::optional<int> parseStatusLine(QStringView line)
{
    static const QRegularExpression re(u"\\b(\\d{3})\\b"_s);
    const QRegularExpressionMatch m = re.matchView(line);
    if (!m.hasMatch())
        return std::nullopt;
    return m.captured(1).toInt();
}

qint64 parseHttpDate(QStringView text)
{
    const QString s = text.trimmed().toString();
    if (s.isEmpty())
        return 0;
    // IMF-fixdate "Tue, 14 Oct 2026 08:00:00 GMT" and RFC 850 "Tuesday, 14-Oct-26 08:00:00 GMT".
    static const QRegularExpression imf(
        u"^(?:[A-Za-z]{3,9},?\\s*)?(\\d{1,2})[\\s-]+([A-Za-z]{3})[A-Za-z]*[\\s-]+(\\d{2,4})\\s+"
        u"(\\d{1,2}):(\\d{2})(?::(\\d{2}))?\\s*(GMT|UTC|UT|Z|[+-]\\d{2}:?\\d{2})?$"_s,
        QRegularExpression::CaseInsensitiveOption);
    if (const QRegularExpressionMatch m = imf.match(s); m.hasMatch()) {
        const int month = monthNumber(m.capturedView(2));
        const std::optional<int> offset = zoneOffset(m.captured(7));
        if (month == 0 || !offset)
            return 0;
        return utcMsecs(fullYear(m.captured(3).toInt(), m.capturedLength(3)), month, m.captured(1).toInt(),
                        m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt(), *offset);
    }
    // asctime "Tue Oct 14 08:00:00 2026" (always UTC).
    static const QRegularExpression asctime(
        u"^[A-Za-z]{3}\\s+([A-Za-z]{3})\\s+(\\d{1,2})\\s+(\\d{1,2}):(\\d{2}):(\\d{2})\\s+(\\d{4})$"_s);
    if (const QRegularExpressionMatch m = asctime.match(s); m.hasMatch()) {
        const int month = monthNumber(m.capturedView(1));
        if (month == 0)
            return 0;
        return utcMsecs(m.captured(6).toInt(), month, m.captured(2).toInt(), m.captured(3).toInt(),
                        m.captured(4).toInt(), m.captured(5).toInt(), 0);
    }
    // ISO 8601 (some servers use it); without a zone it is taken as UTC.
    static const QRegularExpression isoZone(u"(?:Z|[+-]\\d{2}:?\\d{2})$"_s, QRegularExpression::CaseInsensitiveOption);
    const QDateTime iso = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!iso.isValid())
        return 0;
    if (isoZone.match(s).hasMatch())
        return iso.toMSecsSinceEpoch();
    return QDateTime(iso.date(), iso.time(), QTimeZone::UTC).toMSecsSinceEpoch();
}

} // namespace brulion::sync
