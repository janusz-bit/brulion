#include "caldav/davxml_p.h"

#include <QXmlStreamReader>

namespace brulion::caldav::detail {

using namespace Qt::StringLiterals;

namespace {

QString key(QStringView ns, QStringView name)
{
    return ns.toString() + u'|' + name.toString();
}

QByteArray escapeXml(const QString &text)
{
    QString out = text.toHtmlEscaped();
    return out.toUtf8();
}

int statusOf(QStringView line)
{
    // "HTTP/1.1 404 Not Found"
    const QStringList parts = line.trimmed().toString().split(u' ', Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return 0;
    return parts.at(1).toInt();
}

} // namespace

const PropValue *DavResponse::prop(QStringView ns, QStringView name) const
{
    const auto it = props.constFind(key(ns, name));
    return it == props.constEnd() ? nullptr : &*it;
}

QString DavResponse::text(QStringView ns, QStringView name) const
{
    const PropValue *p = prop(ns, name);
    return p ? p->text : QString();
}

Multistatus parseMultistatus(QByteArrayView xml, int maxResponses)
{
    Multistatus out;
    QXmlStreamReader r(xml);
    r.setNamespaceProcessing(true);
    // Find the root.
    while (!r.atEnd() && !r.isStartElement())
        r.readNext();
    if (r.hasError() || !r.isStartElement()) {
        out.error = u"no XML document"_s;
        return out;
    }
    if (r.name() == u"error") {
        out.errorConditions = parseErrorConditions(xml);
        out.error = u"the server answered an error"_s;
        return out;
    }
    if (r.name() != u"multistatus") {
        out.error = u"not a multistatus (<%1>)"_s.arg(r.name());
        return out;
    }
    // Depth below <multistatus>: 1 = response / sync-token, 2 = href, status, propstat, …
    int depth = 0;
    DavResponse current;
    QHash<QString, PropValue> statProps;
    int statStatus = 0;
    bool inPropstat = false;
    bool inProp = false;
    int propDepth = 0; // depth of the property element being read
    QString propKey;
    PropValue prop;
    QString text;
    while (!r.atEnd()) {
        const QXmlStreamReader::TokenType t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            ++depth;
            text.clear();
            const QStringView name = r.name();
            if (depth == 1 && name == u"response") {
                current = DavResponse{};
            } else if (depth == 2 && name == u"propstat") {
                inPropstat = true;
                statProps.clear();
                statStatus = 0;
            } else if (depth == 3 && inPropstat && name == u"prop") {
                inProp = true;
            } else if (inProp && depth == 4) {
                propDepth = depth;
                propKey = key(r.namespaceUri(), name);
                prop = PropValue{};
            } else if (inProp && depth > 4) {
                if (name == u"comp") {
                    const QString compName = r.attributes().value(u"name"_s).toString().toUpper();
                    if (!compName.isEmpty())
                        prop.components.append(compName);
                } else if (name != u"href") {
                    prop.children.append(name.toString());
                }
            }
        } else if (t == QXmlStreamReader::Characters) {
            text += r.text();
        } else if (t == QXmlStreamReader::EndElement) {
            const QStringView name = r.name();
            if (inProp && depth > propDepth && name == u"href") {
                prop.hrefs.append(text.trimmed());
            } else if (inProp && depth == propDepth) {
                if (prop.text.isEmpty())
                    prop.text = prop.hrefs.isEmpty() ? text : QString();
                statProps.insert(propKey, prop);
                propDepth = 0;
            } else if (depth == 3 && name == u"prop") {
                inProp = false;
            } else if (depth == 3 && inPropstat && name == u"status") {
                statStatus = statusOf(text);
            } else if (depth == 2 && name == u"propstat") {
                inPropstat = false;
                if (statStatus == 0 || (statStatus >= 200 && statStatus < 300)) {
                    for (auto it = statProps.constBegin(); it != statProps.constEnd(); ++it)
                        current.props.insert(it.key(), it.value());
                }
            } else if (depth == 2 && name == u"href" && current.href.isEmpty()) {
                current.href = text.trimmed();
            } else if (depth == 2 && name == u"status") {
                current.status = statusOf(text);
            } else if (depth == 1 && name == u"response") {
                if (!current.href.isEmpty()) {
                    if (out.responses.size() >= maxResponses) {
                        out.error = u"more than %1 responses"_s.arg(maxResponses);
                        return out;
                    }
                    out.responses.append(std::move(current));
                }
                current = DavResponse{};
            } else if (depth == 1 && name == u"sync-token") {
                out.syncToken = text.trimmed();
            }
            // Text of a property: what was read inside it, without its children's own elements.
            if (inProp && depth == propDepth + 1 && name != u"href")
                text.clear();
            --depth;
        }
    }
    if (r.hasError() && r.error() != QXmlStreamReader::PrematureEndOfDocumentError)
        out.error = u"broken XML: %1"_s.arg(r.errorString());
    else if (r.hasError())
        out.error = u"the answer ends early"_s;
    return out;
}

QStringList parseErrorConditions(QByteArrayView xml)
{
    QStringList out;
    QXmlStreamReader r(xml);
    r.setNamespaceProcessing(true);
    int depth = 0;
    while (!r.atEnd()) {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement) {
            ++depth;
            if (depth == 2)
                out.append(r.name().toString());
        } else if (t == QXmlStreamReader::EndElement) {
            --depth;
        }
    }
    return out;
}

QByteArray propfindPrincipal()
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                             "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop>"
                             "<d:current-user-principal/><d:resourcetype/><c:calendar-home-set/></d:prop></d:propfind>");
}

QByteArray propfindHomeSet()
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                             "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\"><d:prop>"
                             "<c:calendar-home-set/><d:displayname/></d:prop></d:propfind>");
}

QByteArray propfindCalendars()
{
    return QByteArrayLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
            "<d:propfind xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" "
            "xmlns:cs=\"http://calendarserver.org/ns/\" xmlns:a=\"http://apple.com/ns/ical/\"><d:prop>"
            "<d:resourcetype/><d:displayname/><a:calendar-color/><a:calendar-order/>"
            "<c:supported-calendar-component-set/><c:calendar-description/><cs:getctag/><d:sync-token/>"
            "<d:current-user-privilege-set/></d:prop></d:propfind>");
}

QByteArray propfindEtags()
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                             "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:getetag/><d:resourcetype/><d:getcontenttype/>"
                             "</d:prop></d:propfind>");
}

QByteArray propfindCalendarState()
{
    return QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                             "<d:propfind xmlns:d=\"DAV:\" xmlns:cs=\"http://calendarserver.org/ns/\"><d:prop>"
                             "<cs:getctag/><d:sync-token/></d:prop></d:propfind>");
}

QByteArray syncCollection(const QString &token)
{
    QByteArray body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                      "<d:sync-collection xmlns:d=\"DAV:\"><d:sync-token>";
    body += escapeXml(token);
    body += "</d:sync-token><d:sync-level>1</d:sync-level><d:prop><d:getetag/></d:prop></d:sync-collection>";
    return body;
}

QByteArray calendarMultiget(const QStringList &hrefs)
{
    QByteArray body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                      "<c:calendar-multiget xmlns:d=\"DAV:\" xmlns:c=\"urn:ietf:params:xml:ns:caldav\">"
                      "<d:prop><d:getetag/><c:calendar-data/></d:prop>";
    for (const QString &href : hrefs) {
        body += "<d:href>";
        body += escapeXml(href);
        body += "</d:href>";
    }
    body += "</c:calendar-multiget>";
    return body;
}

QByteArray proppatchCalendar(const QString &name, const QString &color)
{
    QByteArray body = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                      "<d:propertyupdate xmlns:d=\"DAV:\" xmlns:a=\"http://apple.com/ns/ical/\"><d:set><d:prop>";
    if (!name.isEmpty())
        body += "<d:displayname>" + escapeXml(name) + "</d:displayname>";
    if (!color.isEmpty())
        body += "<a:calendar-color>" + escapeXml(color) + "</a:calendar-color>";
    body += "</d:prop></d:set></d:propertyupdate>";
    return body;
}

} // namespace brulion::caldav::detail
