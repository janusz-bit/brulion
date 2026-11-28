#include "fakecaldav.h"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace caldavtest {

using namespace Qt::StringLiterals;
using brulion::sync::HttpRequest;
using brulion::sync::HttpResponse;

namespace {

QString escape(const QString &text)
{
    return text.toHtmlEscaped();
}

HttpResponse status(int code)
{
    HttpResponse r;
    r.status = code;
    return r;
}

QString normalizeEtag(QByteArray value)
{
    QString v = QString::fromUtf8(value).trimmed();
    if (v.startsWith(u"W/"_s))
        v = v.sliced(2);
    v.remove(u'"');
    return v;
}

QHash<QString, QString> authParams(const QString &header)
{
    QHash<QString, QString> out;
    static const QRegularExpression re(u"([a-zA-Z]+)=(\"([^\"]*)\"|[^,\\s]+)"_s);
    auto it = re.globalMatch(header);
    while (it.hasNext()) {
        const auto m = it.next();
        out.insert(m.captured(1).toLower(), m.capturedLength(3) || m.captured(2).startsWith(u'"') ? m.captured(3) : m.captured(2));
    }
    return out;
}

QByteArray md5(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex();
}

} // namespace

FakeCalDav::FakeCalDav() : FakeCalDav(Options{}) { }

FakeCalDav::FakeCalDav(Options options, QObject *parent) : QObject(parent), m_options(std::move(options)) { }

FakeCalDav::~FakeCalDav() = default;

QString FakeCalDav::principalPath() const
{
    return m_options.root + u"principals/"_s + m_options.user + u'/';
}

QString FakeCalDav::homePath() const
{
    return m_options.root + u"calendars/"_s + m_options.user + u'/';
}

QString FakeCalDav::url(const QString &path) const
{
    return m_options.origin + path;
}

QString FakeCalDav::calendarUrl(const QString &slug) const
{
    QUrl u(m_options.origin);
    u.setPath(homePath() + slug + u'/');
    return u.toString(QUrl::FullyEncoded);
}

void FakeCalDav::addCalendar(const QString &slug, const Calendar &calendar)
{
    m_calendars.insert(slug, calendar);
}

void FakeCalDav::removeCalendar(const QString &slug)
{
    m_calendars.remove(slug);
}

QString FakeCalDav::newEtag()
{
    return u"e"_s + QString::number(++m_etag);
}

void FakeCalDav::logChange(const QString &slug, const QString &name)
{
    m_changes.append({++m_token, slug, name});
    if (auto it = m_calendars.find(slug); it != m_calendars.end())
        ++it->ctag;
}

QString FakeCalDav::putMember(const QString &slug, const QString &name, const QByteArray &data)
{
    Calendar &c = m_calendars[slug];
    const QString etag = newEtag();
    c.members.insert(name, Member{data, etag});
    logChange(slug, name);
    return etag;
}

void FakeCalDav::removeMember(const QString &slug, const QString &name)
{
    if (auto it = m_calendars.find(slug); it != m_calendars.end() && it->members.remove(name))
        logChange(slug, name);
}

std::optional<FakeCalDav::Member> FakeCalDav::member(const QString &slug, const QString &name) const
{
    const auto c = m_calendars.constFind(slug);
    if (c == m_calendars.constEnd())
        return std::nullopt;
    const auto m = c->members.constFind(name);
    if (m == c->members.constEnd())
        return std::nullopt;
    return *m;
}

QStringList FakeCalDav::memberNames(const QString &slug) const
{
    const auto c = m_calendars.constFind(slug);
    return c == m_calendars.constEnd() ? QStringList() : c->members.keys();
}

const FakeCalDav::Calendar *FakeCalDav::calendar(const QString &slug) const
{
    const auto c = m_calendars.constFind(slug);
    return c == m_calendars.constEnd() ? nullptr : &*c;
}

void FakeCalDav::expireTokens()
{
    m_changes.clear();
    m_firstToken = ++m_token;
}

int FakeCalDav::count(const QByteArray &method) const
{
    int n = 0;
    for (const LoggedRequest &r : requests)
        n += r.method == method ? 1 : 0;
    return n;
}

QString FakeCalDav::tag(const QString &name) const
{
    return m_options.prefix.isEmpty() ? name : m_options.prefix + u':' + name;
}

QString FakeCalDav::nsDecl() const
{
    const QString dav = m_options.prefix.isEmpty() ? u"xmlns=\"DAV:\""_s : u"xmlns:%1=\"DAV:\""_s.arg(m_options.prefix);
    return dav + u" xmlns:c=\"urn:ietf:params:xml:ns:caldav\" xmlns:cs=\"http://calendarserver.org/ns/\" "
                 u"xmlns:a=\"http://apple.com/ns/ical/\""_s;
}

QByteArray FakeCalDav::multistatus(const QString &inner, const QString &extra) const
{
    return (u"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<"_s + tag(u"multistatus"_s) + u' ' + nsDecl() + u'>' + inner
            + extra + u"</"_s + tag(u"multistatus"_s) + u'>')
            .toUtf8();
}

QString FakeCalDav::calendarProps(const QString &slug, const Calendar &c) const
{
    QString props = u"<%1><%2/><c:calendar/></%1>"_s.arg(tag(u"resourcetype"_s), tag(u"collection"_s));
    props += u"<%1>%2</%1>"_s.arg(tag(u"displayname"_s), escape(c.name));
    if (!c.color.isEmpty())
        props += u"<a:calendar-color>%1FF</a:calendar-color>"_s.arg(c.color.toUpper());
    props += u"<c:supported-calendar-component-set>"_s;
    for (const QString &comp : c.components)
        props += u"<c:comp name=\"%1\"/>"_s.arg(comp);
    props += u"</c:supported-calendar-component-set>"_s;
    props += u"<cs:getctag>ctag-%1-%2</cs:getctag>"_s.arg(slug).arg(c.ctag);
    if (m_options.syncCollection)
        props += u"<%1>http://dav.example.pl/sync/%2</%1>"_s.arg(tag(u"sync-token"_s)).arg(m_token);
    if (m_options.privileges) {
        props += u"<%1><%2><%3/></%2>"_s.arg(tag(u"current-user-privilege-set"_s), tag(u"privilege"_s), tag(u"read"_s));
        if (!c.readOnly)
            props += u"<%1><%2/></%1>"_s.arg(tag(u"privilege"_s), tag(u"write"_s));
        props += u"</%1>"_s.arg(tag(u"current-user-privilege-set"_s));
    }
    return props;
}

bool FakeCalDav::authorized(const HttpRequest &request)
{
    const QString header = QString::fromUtf8(request.header("authorization"));
    if (!m_options.digest) {
        if (!header.startsWith(u"Basic "_s))
            return false;
        const QString decoded = QString::fromUtf8(QByteArray::fromBase64(header.sliced(6).toLatin1()));
        return decoded == m_options.user + u':' + m_options.password;
    }
    if (!header.startsWith(u"Digest "_s))
        return false;
    const QHash<QString, QString> p = authParams(header.sliced(7));
    if (p.value(u"username"_s) != m_options.user || p.value(u"nonce"_s) != m_nonce)
        return false;
    const QByteArray ha1 = md5((m_options.user + u":Fake:"_s + m_options.password).toUtf8());
    const QByteArray ha2 = md5(request.method + ':' + p.value(u"uri"_s).toUtf8());
    const QByteArray expected = md5(ha1 + ':' + m_nonce.toUtf8() + ':' + p.value(u"nc"_s).toUtf8() + ':'
                                    + p.value(u"cnonce"_s).toUtf8() + ":auth:" + ha2);
    return p.value(u"response"_s).toUtf8() == expected;
}

HttpResponse FakeCalDav::unauthorized() const
{
    HttpResponse r = status(401);
    r.headers.insert("www-authenticate",
                     m_options.digest ? (u"Digest realm=\"Fake\", nonce=\"%1\", qop=\"auth\", algorithm=MD5"_s.arg(m_nonce)).toUtf8()
                                      : QByteArray("Basic realm=\"Fake\""));
    return r;
}

void FakeCalDav::send(const HttpRequest &request, brulion::sync::HttpCallback done)
{
    LoggedRequest log;
    log.method = request.method;
    log.path = request.url.path(QUrl::FullyDecoded);
    log.depth = request.header("depth");
    log.ifMatch = request.header("if-match");
    log.ifNoneMatch = request.header("if-none-match");
    log.body = request.body;
    HttpResponse response;
    if (QUrl(m_options.origin).host() != request.url.host()) {
        response.networkError = u"Host %1 not found"_s.arg(request.url.host());
    } else if (hook) {
        if (auto r = hook(request, log.path))
            response = *r;
        else
            response = handle(request, log.path, log);
    } else {
        response = handle(request, log.path, log);
    }
    requests.append(log);
    QTimer::singleShot(0, this, [response, done = std::move(done)] { done(response); });
}

HttpResponse FakeCalDav::handle(const HttpRequest &request, const QString &rawPath, LoggedRequest &log)
{
    if (rawPath == u"/.well-known/caldav") {
        if (!m_options.wellKnown)
            return status(404);
        HttpResponse r = status(301);
        r.headers.insert("location", m_options.root.toUtf8());
        return r;
    }
    log.authorized = authorized(request);
    if (!log.authorized)
        return unauthorized();
    const QString path = rawPath.isEmpty() ? u"/"_s : rawPath;
    const QString withSlash = path.endsWith(u'/') ? path : path + u'/';
    const QString ok = u"<%1>HTTP/1.1 200 OK</%1>"_s.arg(tag(u"status"_s));
    const auto response = [&](const QString &href, const QString &props) {
        return u"<%1><%2>%3</%2><%4><%5>%6</%5>%7</%4></%1>"_s.arg(tag(u"response"_s), tag(u"href"_s), escape(href),
                                                                   tag(u"propstat"_s), tag(u"prop"_s), props, ok);
    };
    const QString home = homePath();
    // Which calendar and member the path names.
    QString slug;
    QString name;
    if (path.startsWith(home) && path.size() > home.size()) {
        const QStringList parts = path.sliced(home.size()).split(u'/', Qt::SkipEmptyParts);
        slug = parts.value(0);
        name = parts.size() > 1 ? parts.at(1) : QString();
    }
    const bool calendarExists = !slug.isEmpty() && m_calendars.contains(slug);

    if (request.method == "PROPFIND") {
        if (withSlash == u"/" || withSlash == m_options.root) {
            return [&] {
                HttpResponse r = status(207);
                r.body = multistatus(response(withSlash, u"<%1><%2>%3</%2></%1><%4><%5/></%4>"_s.arg(
                                                                 tag(u"current-user-principal"_s), tag(u"href"_s),
                                                                 principalPath(), tag(u"resourcetype"_s), tag(u"collection"_s))));
                return r;
            }();
        }
        if (withSlash == principalPath()) {
            HttpResponse r = status(207);
            QString addresses;
            for (const QString &a : m_options.addresses + QStringList{principalPath()})
                addresses += u"<%1>%2</%1>"_s.arg(tag(u"href"_s), a);
            r.body = multistatus(response(withSlash, u"<c:calendar-home-set><%1>%2</%1></c:calendar-home-set>"
                                                     u"<%3>%4</%3><%5><%6/><%7/></%5>"
                                                     u"<c:calendar-user-address-set>%8</c:calendar-user-address-set>"_s
                                                             .arg(tag(u"href"_s), home, tag(u"displayname"_s), m_options.user,
                                                                  tag(u"resourcetype"_s), tag(u"collection"_s), tag(u"principal"_s),
                                                                  addresses)));
            return r;
        }
        if (withSlash == home) {
            QString inner = response(home, u"<%1><%2/></%1>"_s.arg(tag(u"resourcetype"_s), tag(u"collection"_s)));
            if (log.depth == "1") {
                for (auto it = m_calendars.constBegin(); it != m_calendars.constEnd(); ++it)
                    inner += response(home + it.key() + u'/', calendarProps(it.key(), *it));
                // A scheduling inbox: no calendar.
                inner += response(home + u"inbox/"_s, u"<%1><%2/><c:schedule-inbox/></%1>"_s.arg(tag(u"resourcetype"_s),
                                                                                                tag(u"collection"_s)));
            }
            HttpResponse r = status(207);
            r.body = multistatus(inner);
            return r;
        }
        if (calendarExists && name.isEmpty()) {
            const Calendar &c = m_calendars[slug];
            QString inner = response(home + slug + u'/', calendarProps(slug, c));
            if (log.depth == "1") {
                for (auto it = c.members.constBegin(); it != c.members.constEnd(); ++it)
                    inner += response(home + slug + u'/' + it.key(),
                                      u"<%1>\"%2\"</%1><%3/><%4>text/calendar; charset=utf-8</%4>"_s.arg(
                                              tag(u"getetag"_s), it->etag, tag(u"resourcetype"_s), tag(u"getcontenttype"_s)));
            }
            HttpResponse r = status(207);
            r.body = multistatus(inner);
            return r;
        }
        return status(404);
    }
    if (request.method == "REPORT") {
        if (!calendarExists || !name.isEmpty())
            return status(404);
        const Calendar &c = m_calendars[slug];
        const QString body = QString::fromUtf8(request.body);
        if (body.contains(u"sync-collection"_s)) {
            if (!m_options.syncCollection)
                return status(501);
            static const QRegularExpression tokenRe(u"<[^>]*sync-token>([^<]*)</[^>]*sync-token>"_s);
            const QString token = tokenRe.match(body).captured(1).trimmed();
            const int since = token.isEmpty() ? -1 : token.section(u'/', -1).toInt();
            if (!token.isEmpty() && since < m_firstToken) {
                HttpResponse r = status(403);
                r.body = "<?xml version=\"1.0\"?><d:error xmlns:d=\"DAV:\"><d:valid-sync-token/></d:error>";
                return r;
            }
            QString inner;
            QStringList seen;
            QStringList names;
            if (since < 0) {
                names = c.members.keys();
            } else {
                for (const Change &ch : std::as_const(m_changes)) {
                    if (ch.token > since && ch.slug == slug && !names.contains(ch.name))
                        names.append(ch.name);
                }
            }
            for (const QString &n : std::as_const(names)) {
                const QString href = home + slug + u'/' + n;
                if (c.members.contains(n))
                    inner += response(href, u"<%1>\"%2\"</%1>"_s.arg(tag(u"getetag"_s), c.members.value(n).etag));
                else
                    inner += u"<%1><%2>%3</%2><%4>HTTP/1.1 404 Not Found</%4></%1>"_s.arg(tag(u"response"_s), tag(u"href"_s),
                                                                                       escape(href), tag(u"status"_s));
            }
            HttpResponse r = status(207);
            r.body = multistatus(inner, u"<%1>http://dav.example.pl/sync/%2</%1>"_s.arg(tag(u"sync-token"_s)).arg(m_token));
            return r;
        }
        if (body.contains(u"calendar-multiget"_s)) {
            if (!m_options.multiget)
                return status(501);
            static const QRegularExpression hrefRe(u"<[^>]*href>([^<]+)</[^>]*href>"_s);
            QString inner;
            auto it = hrefRe.globalMatch(body);
            while (it.hasNext()) {
                const QString href = QUrl(it.next().captured(1)).path(QUrl::FullyDecoded);
                const QString n = href.section(u'/', -1);
                if (c.members.contains(n)) {
                    const Member &m = c.members[n];
                    inner += response(href, u"<%1>\"%2\"</%1><c:calendar-data>%3</c:calendar-data>"_s.arg(
                                                    tag(u"getetag"_s), m.etag, escape(QString::fromUtf8(m.data))));
                } else {
                    inner += u"<%1><%2>%3</%2><%4>HTTP/1.1 404 Not Found</%4></%1>"_s.arg(tag(u"response"_s), tag(u"href"_s),
                                                                                       escape(href), tag(u"status"_s));
                }
            }
            HttpResponse r = status(207);
            r.body = multistatus(inner);
            return r;
        }
        return status(400);
    }
    if (!calendarExists || name.isEmpty())
        return status(request.method == "PUT" ? 409 : 404);
    Calendar &c = m_calendars[slug];
    if (request.method == "GET") {
        if (!c.members.contains(name))
            return status(404);
        HttpResponse r = status(200);
        r.body = c.members[name].data;
        r.headers.insert("etag", '"' + c.members[name].etag.toUtf8() + '"');
        r.headers.insert("content-type", "text/calendar; charset=utf-8");
        return r;
    }
    if (request.method == "PUT") {
        if (c.readOnly)
            return status(403);
        const bool exists = c.members.contains(name);
        if (!log.ifNoneMatch.isEmpty() && exists)
            return status(412);
        if (!log.ifMatch.isEmpty() && (!exists || normalizeEtag(log.ifMatch) != c.members[name].etag))
            return status(412);
        const QString etag = putMember(slug, name, request.body);
        HttpResponse r = status(exists ? 204 : 201);
        if (m_options.putEtag)
            r.headers.insert("etag", '"' + etag.toUtf8() + '"');
        return r;
    }
    if (request.method == "DELETE") {
        if (c.readOnly)
            return status(403);
        if (!c.members.contains(name))
            return status(404);
        if (!log.ifMatch.isEmpty() && normalizeEtag(log.ifMatch) != c.members[name].etag)
            return status(412);
        removeMember(slug, name);
        return status(204);
    }
    return status(405);
}

} // namespace caldavtest
