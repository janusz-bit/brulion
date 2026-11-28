#include "caldav/client.h"

#include "caldav/davxml_p.h"
#include "caldav/events.h"
#include "log/categories.h"
#include "log/redact.h"
#include "text/text.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <algorithm>

namespace brulion::caldav {

using namespace Qt::StringLiterals;
using namespace detail;

namespace {

/// "W/\"abc\"" / "\"abc\"" / "abc" -> "abc".
QString normalizeEtag(QString etag)
{
    etag = etag.trimmed();
    if (etag.startsWith(u"W/"_s))
        etag = etag.sliced(2);
    if (etag.size() >= 2 && etag.startsWith(u'"') && etag.endsWith(u'"'))
        etag = etag.sliced(1, etag.size() - 2);
    etag.replace(u"&quot;"_s, u"\""_s);
    return etag;
}

QByteArray quotedEtag(const QString &etag)
{
    return '"' + etag.toUtf8() + '"';
}

/// The registrable part of a host name: the last two labels, three when the second-to-last is a
/// short one of a country domain ("example.co.uk", "example.com.pl").
QString siteOf(const QString &host)
{
    const QStringList labels = host.toLower().split(u'.', Qt::SkipEmptyParts);
    if (labels.size() <= 2)
        return labels.join(u'.');
    const int keep = labels.last().size() == 2 && labels.at(labels.size() - 2).size() <= 3 ? 3 : 2;
    return labels.mid(labels.size() - keep).join(u'.');
}

QString withSlash(QString path)
{
    if (!path.endsWith(u'/'))
        path += u'/';
    return path;
}

/// "Refused to follow a redirect to <url>" (sync::NetworkHttpTransport refuses other servers):
/// the target, to follow by hand when it may carry the password.
QUrl refusedRedirect(const QString &networkError)
{
    static const QRegularExpression re(u"^Refused to follow a redirect to (\\S+)$"_s);
    const auto m = re.match(networkError);
    return m.hasMatch() ? QUrl(m.captured(1)) : QUrl();
}

/// Digest challenge parameters (RFC 7616), names lower-cased.
QHash<QString, QString> digestParams(const QByteArray &header)
{
    QHash<QString, QString> out;
    const QString text = QString::fromUtf8(header);
    const qsizetype at = text.indexOf(u"Digest"_s, 0, Qt::CaseInsensitive);
    if (at < 0)
        return out;
    static const QRegularExpression param(u"([A-Za-z0-9_-]+)\\s*=\\s*(\"((?:[^\"\\\\]|\\\\.)*)\"|[^,\\s]+)"_s);
    auto it = param.globalMatch(text, at + 6);
    while (it.hasNext()) {
        const auto m = it.next();
        // A Basic challenge after the Digest one ends it.
        const QString value = m.capturedLength(3) || m.captured(2).startsWith(u'"') ? m.captured(3) : m.captured(2);
        out.insert(m.captured(1).toLower(), value);
    }
    return out;
}

sync::WebDavError errorFor(const sync::HttpResponse &r, const QString &what, const QUrl &url)
{
    const QString shown = url.toString(QUrl::RemoveUserInfo);
    if (r.tooLarge)
        return sync::WebDavError::make(sync::WebDavErrorCode::Oversized, u"%1: the answer is too large"_s.arg(what), shown);
    if (r.status == 0)
        return sync::WebDavError::make(r.timedOut ? sync::WebDavErrorCode::Timeout : sync::WebDavErrorCode::Network,
                                       logging::redact(u"%1: %2"_s.arg(what, r.networkError)), shown);
    return sync::WebDavError::fromStatus(r.status, u"%1 answered %2"_s.arg(what).arg(r.status), shown);
}

sync::WebDavError parseError(const QString &what, const QString &why, const QUrl &url)
{
    return sync::WebDavError::make(sync::WebDavErrorCode::Parse, u"%1: %2"_s.arg(what, why),
                                   url.toString(QUrl::RemoveUserInfo));
}

CalendarInfo calendarFrom(const DavResponse &resp, const QUrl &base)
{
    CalendarInfo info;
    info.url = base.resolved(QUrl(resp.href)).toString(QUrl::FullyEncoded);
    if (!info.url.endsWith(u'/'))
        info.url += u'/';
    info.name = resp.text(NsDav, u"displayname").simplified();
    info.color = normalizedColor(resp.text(NsApple, u"calendar-color"));
    info.description = resp.text(NsCalDav, u"calendar-description").trimmed();
    if (const PropValue *comps = resp.prop(NsCalDav, u"supported-calendar-component-set"))
        info.components = comps->components;
    if (const PropValue *privs = resp.prop(NsDav, u"current-user-privilege-set")) {
        const QStringList &c = privs->children;
        info.readOnly = !(c.contains(u"write"_s) || c.contains(u"write-content"_s) || c.contains(u"all"_s)
                          || c.contains(u"bind"_s));
    }
    info.ctag = resp.text(NsCalendarServer, u"getctag").trimmed();
    info.syncToken = resp.text(NsDav, u"sync-token").trimmed();
    info.order = resp.text(NsApple, u"calendar-order").trimmed().toInt();
    return info;
}

/// The calendar-user-address-set of a principal's answer, participantAddress() form, without
/// repeats.
QStringList addressesOf(const Multistatus &ms)
{
    QStringList out;
    for (const DavResponse &resp : ms.responses) {
        const PropValue *set = resp.prop(NsCalDav, u"calendar-user-address-set");
        for (const QString &href : set ? set->hrefs : QStringList()) {
            const QString address = participantAddress(QUrl::fromPercentEncoding(href.toUtf8()));
            if (!address.isEmpty() && !out.contains(address))
                out.append(address);
        }
    }
    return out;
}

bool isCalendar(const DavResponse &resp)
{
    const PropValue *type = resp.prop(NsDav, u"resourcetype");
    return type && type->children.contains(u"calendar"_s);
}

} // namespace

bool mayCarryCredentials(const QUrl &server, const QUrl &target)
{
    if (!server.isValid() || !target.isValid())
        return false;
    const QString host = target.host().toLower();
    const QString serverHost = server.host().toLower();
    if (target.scheme() == server.scheme() && host == serverHost
        && target.port(target.scheme() == u"https" ? 443 : 80) == server.port(server.scheme() == u"https" ? 443 : 80))
        return true;
    if (target.scheme() != u"https" || server.scheme() != u"https"
        || target.port(443) != server.port(443))
        return false;
    const QString site = siteOf(serverHost);
    return site.contains(u'.') && (host == site || host.endsWith(u'.' + site));
}

struct CalDavClient::Private {
    sync::HttpTransport *transport = nullptr;
    QUrl server;
    QString user;
    QString password;
    logging::ScopedSecret secret;
    int timeoutMs = DefaultTimeoutMs;
    bool browser = false;
    int requests = 0;
    // Digest (RFC 7616), once the server asked for it.
    bool digest = false;
    QHash<QString, QString> challenge;
    int nonceCount = 0;

    using Done = std::function<void(const sync::HttpResponse &)>;

    QByteArray authorization(const QByteArray &method, const QUrl &url)
    {
        if (!digest) {
            return "Basic " + (user + u':' + password).toUtf8().toBase64();
        }
        const QString algorithm = challenge.value(u"algorithm"_s, u"MD5"_s);
        const bool sha256 = algorithm.startsWith(u"SHA-256"_s, Qt::CaseInsensitive);
        const bool sess = algorithm.endsWith(u"-sess"_s, Qt::CaseInsensitive);
        const auto hash = [sha256](const QByteArray &data) {
            return QCryptographicHash::hash(data, sha256 ? QCryptographicHash::Sha256 : QCryptographicHash::Md5).toHex();
        };
        const QByteArray realm = challenge.value(u"realm"_s).toUtf8();
        const QByteArray nonce = challenge.value(u"nonce"_s).toUtf8();
        const QByteArray uri = url.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority | QUrl::FullyEncoded).toUtf8();
        const bool qopAuth = challenge.value(u"qop"_s).split(u',').contains(u"auth"_s)
                             || challenge.value(u"qop"_s).split(u',').contains(u" auth"_s);
        const QByteArray cnonce = QByteArray::number(QRandomGenerator::global()->generate64(), 16);
        const QByteArray nc = QByteArray::number(++nonceCount, 16).rightJustified(8, '0');
        QByteArray ha1 = hash(user.toUtf8() + ':' + realm + ':' + password.toUtf8());
        if (sess)
            ha1 = hash(ha1 + ':' + nonce + ':' + cnonce);
        const QByteArray ha2 = hash(method + ':' + uri);
        const QByteArray response = qopAuth ? hash(ha1 + ':' + nonce + ':' + nc + ':' + cnonce + ":auth:" + ha2)
                                            : hash(ha1 + ':' + nonce + ':' + ha2);
        QByteArray h = "Digest username=\"" + user.toUtf8() + "\", realm=\"" + realm + "\", nonce=\"" + nonce
                       + "\", uri=\"" + uri + "\", response=\"" + response + "\", algorithm=" + algorithm.toUtf8();
        if (qopAuth)
            h += ", qop=auth, nc=" + nc + ", cnonce=\"" + cnonce + '"';
        if (challenge.contains(u"opaque"_s))
            h += ", opaque=\"" + challenge.value(u"opaque"_s).toUtf8() + '"';
        return h;
    }

    static void send(const std::shared_ptr<Private> &self, const QByteArray &method, const QUrl &url,
                     const QByteArray &body, const QList<std::pair<QByteArray, QByteArray>> &headers, Done done,
                     int attempt = 0)
    {
        sync::HttpRequest request;
        request.method = method;
        request.url = url;
        request.body = body;
        request.headers = headers;
        request.timeoutMs = self->timeoutMs;
        request.timeoutWhileIdle = true;
        request.maxBodyBytes = MaxAnswerBytes;
        if (!body.isEmpty() && !request.hasHeader("Content-Type"))
            request.headers.emplaceBack(QByteArrayLiteral("Content-Type"), "application/xml; charset=utf-8");
        if (!self->browser)
            request.headers.emplaceBack(QByteArrayLiteral("User-Agent"), "Brulion");
        if (mayCarryCredentials(self->server, url))
            request.headers.emplaceBack(QByteArrayLiteral("Authorization"), self->authorization(method, url));
        ++self->requests;
        const std::weak_ptr<Private> weak = self;
        qCDebug(brulion::logging::lcNet) << method << url.toString(QUrl::RemoveUserInfo | QUrl::RemoveQuery);
        self->transport->send(request, [weak, method, url, body, headers, attempt,
                                        done = std::move(done)](const sync::HttpResponse &response) {
            const std::shared_ptr<Private> me = weak.lock();
            if (!me)
                return;
            if (response.status == 401 && attempt < 2) {
                const QHash<QString, QString> params = digestParams(response.header("www-authenticate"));
                if (params.contains(u"nonce"_s)) {
                    const bool stale = params.value(u"stale"_s).compare(u"true"_s, Qt::CaseInsensitive) == 0;
                    const bool fresh = !me->digest || stale || params.value(u"nonce"_s) != me->challenge.value(u"nonce"_s);
                    if (fresh) {
                        me->digest = true;
                        me->challenge = params;
                        me->nonceCount = 0;
                        send(me, method, url, body, headers, done, attempt + 1);
                        return;
                    }
                }
            }
            // A redirect the transport refused (another server): followed when the password may go there.
            if (response.status == 0 && attempt < 5) {
                const QUrl target = refusedRedirect(response.networkError);
                if (target.isValid() && mayCarryCredentials(me->server, target)) {
                    send(me, method, target, body, headers, done, attempt + 1);
                    return;
                }
            }
            if ((response.status == 301 || response.status == 302 || response.status == 307 || response.status == 308)
                && attempt < 5) {
                const QUrl target = url.resolved(QUrl(QString::fromUtf8(response.header("location"))));
                if (target.isValid() && mayCarryCredentials(me->server, target)) {
                    send(me, method, target, body, headers, done, attempt + 1);
                    return;
                }
            }
            done(response);
        });
    }

    static void propfind(const std::shared_ptr<Private> &self, const QUrl &url, const QByteArray &depth,
                         const QByteArray &body, std::function<void(const Multistatus &, const sync::HttpResponse &)> done)
    {
        send(self, "PROPFIND", url, body, {{"Depth", depth}},
             [done = std::move(done)](const sync::HttpResponse &r) {
                 Multistatus ms;
                 if (r.status == 207)
                     ms = parseMultistatus(r.body);
                 else
                     ms.error = u"no multistatus"_s;
                 done(ms, r);
             });
    }
};

CalDavClient::CalDavClient(sync::HttpTransport *transport, const QUrl &server, const QString &user,
                           const QString &password)
    : d(std::make_shared<Private>())
{
    d->transport = transport;
    d->server = server;
    d->user = user;
    d->password = password;
    d->secret.reset(password);
}

CalDavClient::~CalDavClient() = default;

void CalDavClient::setTimeout(int msec)
{
    d->timeoutMs = msec;
}

void CalDavClient::setBrowser(bool browser)
{
    d->browser = browser;
}

QUrl CalDavClient::server() const
{
    return d->server;
}

QString CalDavClient::user() const
{
    return d->user;
}

int CalDavClient::requestCount() const
{
    return d->requests;
}

QUrl CalDavClient::urlOf(const QUrl &calendar, const QString &path) const
{
    QUrl url = calendar;
    url.setPath(path, QUrl::DecodedMode);
    url.setQuery(QString());
    url.setFragment(QString());
    return url;
}

QString CalDavClient::pathOf(const QUrl &base, const QString &href)
{
    return base.resolved(QUrl(href.trimmed())).path(QUrl::FullyDecoded);
}

void CalDavClient::discover(std::function<void(const Discovery &)> done)
{
    const std::shared_ptr<Private> self = d;
    const QUrl server = d->server;
    // The home's calendars, once a principal is known.
    auto fromPrincipal = [self, done](const QUrl &principal) {
        Private::propfind(self, principal, "0", propfindHomeSet(),
                          [self, principal, done](const Multistatus &ms, const sync::HttpResponse &r) {
                              Discovery result;
                              result.principalUrl = principal.toString(QUrl::FullyEncoded);
                              if (r.status != 207) {
                                  result.error = errorFor(r, u"PROPFIND principal"_s, principal);
                                  done(result);
                                  return;
                              }
                              QUrl home = principal;
                              result.addresses = addressesOf(ms);
                              for (const DavResponse &resp : ms.responses) {
                                  if (const PropValue *set = resp.prop(NsCalDav, u"calendar-home-set");
                                      set && !set->hrefs.isEmpty()) {
                                      home = principal.resolved(QUrl(set->hrefs.first()));
                                      break;
                                  }
                              }
                              result.homeUrl = home.toString(QUrl::FullyEncoded);
                              Private::propfind(self, home, "1", propfindCalendars(),
                                                [home, result, done](const Multistatus &list, const sync::HttpResponse &lr) mutable {
                                                    if (lr.status != 207 || !list.ok()) {
                                                        result.error = lr.status == 207
                                                                ? parseError(u"PROPFIND home"_s, list.error, home)
                                                                : errorFor(lr, u"PROPFIND home"_s, home);
                                                        done(result);
                                                        return;
                                                    }
                                                    for (const DavResponse &resp : list.responses) {
                                                        if (isCalendar(resp))
                                                            result.calendars.append(calendarFrom(resp, home));
                                                    }
                                                    std::ranges::stable_sort(result.calendars, [](const CalendarInfo &a, const CalendarInfo &b) {
                                                        if (a.order != b.order)
                                                            return a.order < b.order;
                                                        return text::compareNames(a.name, b.name) < 0;
                                                    });
                                                    done(result);
                                                });
                          });
    };
    // 1. The address itself: a principal, a home or a calendar.
    Private::propfind(
            self, server, "0", propfindPrincipal(),
            [self, server, fromPrincipal, done](const Multistatus &ms, const sync::HttpResponse &r) {
                if (r.status == 401) {
                    Discovery result;
                    result.error = errorFor(r, u"PROPFIND"_s, server);
                    done(result);
                    return;
                }
                if (r.status == 207 && ms.ok()) {
                    for (const DavResponse &resp : ms.responses) {
                        if (isCalendar(resp)) {
                            // A calendar's own address: that calendar alone.
                            Private::propfind(self, server, "0", propfindCalendars(),
                                              [server, done](const Multistatus &one, const sync::HttpResponse &or_) {
                                                  Discovery result;
                                                  result.homeUrl = server.toString(QUrl::FullyEncoded);
                                                  if (or_.status != 207) {
                                                      result.error = errorFor(or_, u"PROPFIND calendar"_s, server);
                                                  } else {
                                                      for (const DavResponse &c : one.responses) {
                                                          if (isCalendar(c))
                                                              result.calendars.append(calendarFrom(c, server));
                                                      }
                                                  }
                                                  done(result);
                                              });
                            return;
                        }
                        if (const PropValue *p = resp.prop(NsDav, u"current-user-principal"); p && !p->hrefs.isEmpty()) {
                            fromPrincipal(server.resolved(QUrl(p->hrefs.first())));
                            return;
                        }
                        if (const PropValue *h = resp.prop(NsCalDav, u"calendar-home-set"); h && !h->hrefs.isEmpty()) {
                            fromPrincipal(server); // the principal's own address
                            return;
                        }
                    }
                }
                // 2. The well-known address (RFC 6764).
                const QUrl wellKnown = server.resolved(QUrl(u"/.well-known/caldav"_s));
                Private::propfind(self, wellKnown, "0", propfindPrincipal(),
                                  [server, wellKnown, fromPrincipal, done](const Multistatus &wk, const sync::HttpResponse &wr) {
                                      if (wr.status == 207 && wk.ok()) {
                                          for (const DavResponse &resp : wk.responses) {
                                              if (const PropValue *p = resp.prop(NsDav, u"current-user-principal");
                                                  p && !p->hrefs.isEmpty()) {
                                                  fromPrincipal(wellKnown.resolved(QUrl(p->hrefs.first())));
                                                  return;
                                              }
                                          }
                                      }
                                      Discovery result;
                                      if (wr.status == 401 || wr.status == 0)
                                          result.error = errorFor(wr, u"PROPFIND /.well-known/caldav"_s, wellKnown);
                                      else
                                          result.error = sync::WebDavError::make(
                                                  sync::WebDavErrorCode::NotFound,
                                                  u"No CalDAV account at %1 (no current-user-principal)"_s.arg(
                                                          server.toString(QUrl::RemoveUserInfo)),
                                                  server.toString(QUrl::RemoveUserInfo));
                                      done(result);
                                  });
            });
}

void CalDavClient::listCalendars(const QUrl &home,
                                 std::function<void(const QList<CalendarInfo> &, const sync::WebDavError &)> done)
{
    Private::propfind(d, home, "1", propfindCalendars(), [home, done](const Multistatus &ms, const sync::HttpResponse &r) {
        if (r.status != 207 || !ms.ok()) {
            done({}, r.status == 207 ? parseError(u"PROPFIND home"_s, ms.error, home) : errorFor(r, u"PROPFIND home"_s, home));
            return;
        }
        QList<CalendarInfo> out;
        for (const DavResponse &resp : ms.responses) {
            if (isCalendar(resp))
                out.append(calendarFrom(resp, home));
        }
        done(out, {});
    });
}

void CalDavClient::calendarState(const QUrl &calendar,
                                 std::function<void(const CalendarInfo &, const sync::WebDavError &)> done)
{
    Private::propfind(d, calendar, "0", propfindCalendarState(),
                      [calendar, done](const Multistatus &ms, const sync::HttpResponse &r) {
                          if (r.status != 207 || !ms.ok() || ms.responses.isEmpty()) {
                              done({}, r.status == 207 ? parseError(u"PROPFIND calendar"_s, ms.error, calendar)
                                                       : errorFor(r, u"PROPFIND calendar"_s, calendar));
                              return;
                          }
                          CalendarInfo info = calendarFrom(ms.responses.first(), calendar);
                          done(info, {});
                      });
}

void CalDavClient::changes(const QUrl &calendar, const QString &syncToken, std::function<void(const ChangeSet &)> done)
{
    const std::shared_ptr<Private> self = d;
    const QString own = withSlash(calendar.path(QUrl::FullyDecoded));
    // Every member with its ETag (no token, or a server without sync-collection).
    auto listAll = [self, calendar, own, done]() {
        Private::propfind(self, calendar, "1", propfindEtags(),
                          [self, calendar, own, done](const Multistatus &ms, const sync::HttpResponse &r) {
                              ChangeSet set;
                              set.complete = true;
                              if (r.status != 207 || !ms.ok()) {
                                  set.error = r.status == 207 ? parseError(u"PROPFIND calendar"_s, ms.error, calendar)
                                                              : errorFor(r, u"PROPFIND calendar"_s, calendar);
                                  done(set);
                                  return;
                              }
                              for (const DavResponse &resp : ms.responses) {
                                  const QString path = CalDavClient::pathOf(calendar, resp.href);
                                  if (withSlash(path) == own)
                                      continue;
                                  if (const PropValue *type = resp.prop(NsDav, u"resourcetype");
                                      type && type->children.contains(u"collection"_s))
                                      continue;
                                  set.changed.append({path, normalizeEtag(resp.text(NsDav, u"getetag")), {}});
                              }
                              // The token for the next run (none: the ETags are compared again).
                              Private::propfind(self, calendar, "0", propfindCalendarState(),
                                                [set, done](const Multistatus &st, const sync::HttpResponse &sr) mutable {
                                                    if (sr.status == 207 && !st.responses.isEmpty())
                                                        set.syncToken = st.responses.first().text(NsDav, u"sync-token").trimmed();
                                                    done(set);
                                                });
                          });
    };
    if (syncToken.isEmpty()) {
        listAll();
        return;
    }
    // sync-collection, repeated while the server says it cut the answer short (507 on the collection).
    struct Loop {
        ChangeSet set;
        int rounds = 0;
    };
    auto loop = std::make_shared<Loop>();
    auto ask = std::make_shared<std::function<void(const QString &)>>();
    // The function holds itself only weakly; each request's callback holds it while it waits.
    *ask = [self, calendar, own, done, listAll, loop, weakAsk = std::weak_ptr(ask)](const QString &token) {
        const auto again = weakAsk.lock();
        Private::send(self, "REPORT", calendar, syncCollection(token), {},
                      [calendar, own, done, listAll, loop, again](const sync::HttpResponse &r) {
                          if (r.status != 207) {
                              // A refused token (403 / 409 valid-sync-token, 400) or no sync-collection.
                              if (r.status == 400 || r.status == 403 || r.status == 404 || r.status == 405 || r.status == 409
                                  || r.status == 412 || r.status == 415 || r.status == 422 || r.status == 501) {
                                  listAll();
                                  return;
                              }
                              loop->set.error = errorFor(r, u"REPORT sync-collection"_s, calendar);
                              done(loop->set);
                              return;
                          }
                          const Multistatus ms = parseMultistatus(r.body);
                          if (!ms.ok()) {
                              loop->set.error = parseError(u"REPORT sync-collection"_s, ms.error, calendar);
                              done(loop->set);
                              return;
                          }
                          bool more = false;
                          for (const DavResponse &resp : ms.responses) {
                              const QString path = CalDavClient::pathOf(calendar, resp.href);
                              if (withSlash(path) == own) {
                                  more = more || resp.status == 507;
                                  continue;
                              }
                              loop->set.changed.removeIf([&path](const RemoteResource &c) { return c.path == path; });
                              if (resp.status == 404 || resp.status == 410) {
                                  if (!loop->set.removed.contains(path))
                                      loop->set.removed.append(path);
                              } else {
                                  loop->set.removed.removeAll(path);
                                  loop->set.changed.append({path, normalizeEtag(resp.text(NsDav, u"getetag")), {}});
                              }
                          }
                          loop->set.syncToken = ms.syncToken;
                          if (more && !ms.syncToken.isEmpty() && ++loop->rounds < 50 && again) {
                              (*again)(ms.syncToken);
                              return;
                          }
                          done(loop->set);
                      });
    };
    (*ask)(syncToken);
}

void CalDavClient::fetch(const QUrl &calendar, const QStringList &paths,
                         std::function<void(const QList<RemoteResource> &, const sync::WebDavError &)> done)
{
    struct State {
        QStringList queue;
        QList<RemoteResource> out;
        bool multiget = true;
    };
    auto state = std::make_shared<State>();
    state->queue = paths;
    const std::shared_ptr<Private> self = d;
    auto next = std::make_shared<std::function<void()>>();
    const std::weak_ptr<std::function<void()>> weakNext = next;
    *next = [self, calendar, state, done, weakNext]() {
        const auto again = weakNext.lock();
        if (state->queue.isEmpty() || !again) {
            done(state->out, {});
            return;
        }
        if (!state->multiget) {
            const QString path = state->queue.takeFirst();
            QUrl url = calendar;
            url.setPath(path, QUrl::DecodedMode);
            Private::send(self, "GET", url, {}, {{"Accept", "text/calendar"}},
                          [state, done, again, url](const sync::HttpResponse &r) {
                              if (r.status == 200) {
                                  state->out.append({url.path(QUrl::FullyDecoded), normalizeEtag(QString::fromUtf8(r.header("etag"))),
                                                     r.body});
                              } else if (r.status != 404 && r.status != 410) {
                                  done(state->out, errorFor(r, u"GET"_s, url));
                                  return;
                              }
                              (*again)();
                          });
            return;
        }
        const QStringList batch = state->queue.mid(0, MultigetBatch);
        QStringList hrefs;
        for (const QString &p : batch) {
            QUrl u;
            u.setPath(p, QUrl::DecodedMode);
            hrefs.append(u.toString(QUrl::FullyEncoded));
        }
        Private::send(self, "REPORT", calendar, calendarMultiget(hrefs), {{"Depth", "1"}},
                      [state, done, again, calendar, batch](const sync::HttpResponse &r) {
                          if (r.status == 400 || r.status == 403 || r.status == 405 || r.status == 415 || r.status == 501) {
                              state->multiget = false; // GET them one by one
                              (*again)();
                              return;
                          }
                          if (r.status != 207) {
                              done(state->out, errorFor(r, u"REPORT calendar-multiget"_s, calendar));
                              return;
                          }
                          const Multistatus ms = parseMultistatus(r.body);
                          if (!ms.ok()) {
                              done(state->out, parseError(u"REPORT calendar-multiget"_s, ms.error, calendar));
                              return;
                          }
                          for (const DavResponse &resp : ms.responses) {
                              const PropValue *data = resp.prop(NsCalDav, u"calendar-data");
                              if (!data || resp.status == 404)
                                  continue;
                              state->out.append({CalDavClient::pathOf(calendar, resp.href),
                                                 normalizeEtag(resp.text(NsDav, u"getetag")), data->text.toUtf8()});
                          }
                          state->queue = state->queue.mid(batch.size());
                          (*again)();
                      });
    };
    (*next)();
}

void CalDavClient::put(const QString &path, const QByteArray &data, const QString &ifMatch,
                       std::function<void(const WriteResult &)> done)
{
    const QUrl url = urlOf(d->server, path);
    QList<std::pair<QByteArray, QByteArray>> headers{{"Content-Type", "text/calendar; charset=utf-8"}};
    if (ifMatch.isEmpty())
        headers.emplaceBack(QByteArrayLiteral("If-None-Match"), "*");
    else
        headers.emplaceBack(QByteArrayLiteral("If-Match"), quotedEtag(ifMatch));
    Private::send(d, "PUT", url, data, headers, [url, path, done](const sync::HttpResponse &r) {
        WriteResult result;
        result.path = path;
        if (r.status == 200 || r.status == 201 || r.status == 204) {
            result.etag = normalizeEtag(QString::fromUtf8(r.header("etag")));
            if (const QByteArray location = r.header("location"); !location.isEmpty())
                result.path = CalDavClient::pathOf(url, QString::fromUtf8(location));
        } else {
            result.error = errorFor(r, u"PUT"_s, url);
        }
        done(result);
    });
}

void CalDavClient::remove(const QString &path, const QString &ifMatch, std::function<void(const sync::WebDavError &)> done)
{
    const QUrl url = urlOf(d->server, path);
    QList<std::pair<QByteArray, QByteArray>> headers;
    if (!ifMatch.isEmpty())
        headers.emplaceBack(QByteArrayLiteral("If-Match"), quotedEtag(ifMatch));
    Private::send(d, "DELETE", url, {}, headers, [url, done](const sync::HttpResponse &r) {
        if (r.status == 200 || r.status == 204 || r.status == 404 || r.status == 410)
            done({});
        else
            done(errorFor(r, u"DELETE"_s, url));
    });
}

} // namespace brulion::caldav
