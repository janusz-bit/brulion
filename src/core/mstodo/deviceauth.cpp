#include "mstodo/deviceauth.h"

#include "log/redact.h"
#include "mstodo/messages_p.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>

#include <algorithm>

namespace brulion::mstodo {

using namespace Qt::StringLiterals;
using detail::MsTodoMessages;

namespace {

const QRegularExpression &guidPattern()
{
    static const QRegularExpression re(
            u"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"_s);
    return re;
}

/// Form encoding of one value (application/x-www-form-urlencoded).
QByteArray form(const QList<std::pair<QString, QString>> &fields)
{
    QByteArray out;
    for (const auto &[key, value] : fields) {
        if (!out.isEmpty())
            out += '&';
        out += QUrl::toPercentEncoding(key) + '=' + QUrl::toPercentEncoding(value);
    }
    return out;
}

/// The first AADSTS number in `codes` or `description`, 0 when none.
int aadstsCode(const QString &description, const QList<int> &codes)
{
    if (!codes.isEmpty())
        return codes.first();
    static const QRegularExpression re(u"AADSTS(\\d+)"_s);
    const QRegularExpressionMatch m = re.match(description);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

QList<int> errorCodes(const QJsonObject &o)
{
    QList<int> out;
    for (const QJsonValue &v : o.value(u"error_codes"_s).toArray())
        out.append(v.toInt());
    return out;
}

/// The address the user opens: Microsoft's own, else the well-known one.
QUrl verificationUrl(const QString &value)
{
    const QUrl url(value.trimmed());
    const QString host = url.host().toLower();
    const bool microsoft = host == u"microsoft.com" || host.endsWith(u".microsoft.com") || host == u"live.com"
                           || host.endsWith(u".live.com") || host.endsWith(u".microsoftonline.com");
    if (url.isValid() && url.scheme() == u"https" && microsoft && url.userInfo().isEmpty())
        return url;
    return QUrl(u"https://microsoft.com/devicelogin"_s);
}

} // namespace

QString normalizedClientId(const QString &clientId)
{
    QString id = clientId.trimmed();
    if (id.startsWith(u'{') && id.endsWith(u'}'))
        id = id.mid(1, id.size() - 2).trimmed();
    return id.toLower();
}

QString validateClientId(const QString &clientId)
{
    const QString id = normalizedClientId(clientId);
    if (id.isEmpty())
        return MsTodoMessages::tr("Enter the Application (client) ID of your app registration.");
    if (!guidPattern().match(id).hasMatch())
        return MsTodoMessages::tr("An Application (client) ID looks like 3f2504e0-4f89-41d3-9a0c-0305e82c3301 "
                                  "(32 hexadecimal digits with dashes).");
    return {};
}

QString normalizedAuthority(const QString &authority)
{
    const QString a = authority.trimmed().toLower();
    return a.isEmpty() ? DefaultAuthority.toString() : a;
}

QString validateAuthority(const QString &authority)
{
    const QString a = normalizedAuthority(authority);
    if (a == u"consumers" || a == u"organizations" || a == u"common" || guidPattern().match(a).hasMatch())
        return {};
    static const QRegularExpression domain(u"^(?=.{4,253}$)([a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+[a-z]{2,63}$"_s);
    if (domain.match(a).hasMatch())
        return {};
    return MsTodoMessages::tr("Use consumers (personal Microsoft accounts), organizations (work or school "
                              "accounts), common (both), or your directory's tenant ID or domain.");
}

AuthError describeAuthError(const QString &error, const QString &description, const QList<int> &codes)
{
    const int aadsts = aadstsCode(description, codes);
    const QString tail = aadsts ? u" (AADSTS%1)"_s.arg(aadsts) : QString();
    if (aadsts == 7000218 || aadsts == 7000215)
        return {u"public-client-disabled"_s,
                MsTodoMessages::tr("The app registration does not allow this sign-in. In the Microsoft Entra admin "
                                   "center open the app → Authentication and turn on “Allow public client flows”.")
                        + tail};
    if (aadsts == 700016 || aadsts == 90002)
        return {u"unknown-client"_s,
                MsTodoMessages::tr("Microsoft does not know this Application (client) ID here. Check the ID; an "
                                   "app registered for personal accounts needs the authority “consumers”.")
                        + tail};
    if (aadsts == 50059 || aadsts == 9002331 || aadsts == 50020 || aadsts == 500200 || aadsts == 700054)
        return {u"wrong-account-type"_s,
                MsTodoMessages::tr("The app registration does not accept this kind of account. For a personal "
                                   "account set the authority to “consumers” and register the app for personal "
                                   "accounts; for a work account use “organizations” or your tenant.")
                        + tail};
    if (aadsts == 65001 || aadsts == 90094 || aadsts == 90008 || error == u"consent_required"
        || error == u"interaction_required")
        return {u"consent"_s,
                MsTodoMessages::tr("Permission to read your tasks was not granted. Add the delegated permission "
                                   "Tasks.Read to the app registration; at work an administrator may have to "
                                   "approve it.")
                        + tail};
    if (error == u"authorization_declined" || error == u"access_denied")
        return {u"declined"_s, MsTodoMessages::tr("The sign-in was declined. Nothing was imported.")};
    if (error == u"expired_token" || error == u"code_expired")
        return {u"expired"_s, MsTodoMessages::tr("The code expired before the sign-in finished. Sign in again.")};
    if (error == u"bad_verification_code")
        return {u"bad-code"_s, MsTodoMessages::tr("Microsoft did not accept the sign-in code. Sign in again.")};
    // Something else: the first sentence of Microsoft's own explanation (never a token).
    QString detail = description.section(u"\r\n"_s, 0, 0).section(u'\n', 0, 0).trimmed();
    detail = logging::redact(detail.left(300));
    if (detail.isEmpty())
        detail = error;
    return {u"server"_s, detail.isEmpty() ? MsTodoMessages::tr("Microsoft refused the sign-in.")
                                          : MsTodoMessages::tr("Microsoft refused the sign-in: %1").arg(detail)};
}

DeviceCodeLogin::DeviceCodeLogin(sync::HttpTransport *transport, QObject *parent)
    : QObject(parent), m_transport(transport)
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &DeviceCodeLogin::poll);
}

DeviceCodeLogin::~DeviceCodeLogin() = default;

QString DeviceCodeLogin::endpoint(QStringView path) const
{
    return u"https://"_s + LoginHost + u'/' + QString::fromLatin1(QUrl::toPercentEncoding(m_authority, ".-"))
           + u"/oauth2/v2.0/"_s + path;
}

void DeviceCodeLogin::start(const QString &clientId, const QString &authority)
{
    cancel();
    if (const QString e = validateClientId(clientId); !e.isEmpty()) {
        Q_EMIT failed(u"invalid-client-id"_s, e);
        return;
    }
    if (const QString e = validateAuthority(authority); !e.isEmpty()) {
        Q_EMIT failed(u"invalid-authority"_s, e);
        return;
    }
    m_clientId = normalizedClientId(clientId);
    m_authority = normalizedAuthority(authority);
    m_active = true;
    m_networkErrors = 0;
    requestCode();
}

void DeviceCodeLogin::cancel()
{
    ++m_generation;
    m_pollTimer.stop();
    m_active = false;
    m_deviceCode.clear();
}

void DeviceCodeLogin::fail(const AuthError &error)
{
    cancel();
    Q_EMIT failed(error.code, error.message);
}

void DeviceCodeLogin::send(const QString &url, const QByteArray &body,
                           std::function<void(const sync::HttpResponse &)> done)
{
    sync::HttpRequest request;
    request.method = "POST";
    request.url = QUrl(url);
    request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
    request.body = body;
    request.timeoutMs = m_timeoutMs;
    const int generation = m_generation;
    QPointer<DeviceCodeLogin> self(this);
    m_transport->send(request, [self, generation, done = std::move(done)](const sync::HttpResponse &response) {
        if (self && self->m_generation == generation && self->m_active)
            done(response);
    });
}

void DeviceCodeLogin::requestCode()
{
    send(endpoint(u"devicecode"), form({{u"client_id"_s, m_clientId}, {u"scope"_s, Scope.toString()}}),
         [this](const sync::HttpResponse &response) {
             if (response.status == 0) {
                 fail({u"network"_s, MsTodoMessages::tr("Microsoft's sign-in page cannot be reached. Check the "
                                                        "internet connection.")});
                 return;
             }
             const QJsonObject o = QJsonDocument::fromJson(response.body).object();
             if (response.status != 200) {
                 fail(describeAuthError(o.value(u"error"_s).toString(), o.value(u"error_description"_s).toString(),
                                        errorCodes(o)));
                 return;
             }
             const QString userCode = o.value(u"user_code"_s).toString().trimmed();
             m_deviceCode = o.value(u"device_code"_s).toString();
             if (userCode.isEmpty() || m_deviceCode.isEmpty()) {
                 fail({u"server"_s, MsTodoMessages::tr("Microsoft sent an answer Brulion does not understand.")});
                 return;
             }
             const int expiresIn = std::clamp(o.value(u"expires_in"_s).toInt(900), 60, 3600);
             m_interval = std::clamp(o.value(u"interval"_s).toInt(5), 1, 60);
             m_expiresMs = qint64(expiresIn) * m_unitMs;
             m_started.start();
             Q_EMIT codeReady(userCode, verificationUrl(o.value(u"verification_uri"_s).toString()), expiresIn);
             if (m_active)
                 m_pollTimer.start(m_interval * m_unitMs);
         });
}

void DeviceCodeLogin::poll()
{
    if (!m_active)
        return;
    if (m_started.elapsed() >= m_expiresMs) {
        fail(describeAuthError(u"expired_token"_s, {}));
        return;
    }
    send(endpoint(u"token"),
         form({{u"grant_type"_s, u"urn:ietf:params:oauth:grant-type:device_code"_s},
               {u"client_id"_s, m_clientId},
               {u"device_code"_s, m_deviceCode}}),
         [this](const sync::HttpResponse &response) {
             const auto again = [this] { m_pollTimer.start(m_interval * m_unitMs); };
             if (response.status == 0 || response.status >= 500) {
                 // A dropped connection while the user signs in on another device: try again a few
                 // times.
                 if (++m_networkErrors > 3) {
                     fail({u"network"_s, MsTodoMessages::tr("Microsoft's sign-in page cannot be reached. Check "
                                                            "the internet connection.")});
                     return;
                 }
                 again();
                 return;
             }
             m_networkErrors = 0;
             const QJsonObject o = QJsonDocument::fromJson(response.body).object();
             if (response.status == 200) {
                 const QString token = o.value(u"access_token"_s).toString();
                 if (token.isEmpty()) {
                     fail({u"server"_s, MsTodoMessages::tr("Microsoft sent an answer Brulion does not understand.")});
                     return;
                 }
                 cancel();
                 Q_EMIT signedIn(token);
                 return;
             }
             const QString error = o.value(u"error"_s).toString();
             if (error == u"authorization_pending") {
                 again();
                 return;
             }
             if (error == u"slow_down") {
                 m_interval = qMin(m_interval + 5, 60);
                 again();
                 return;
             }
             fail(describeAuthError(error, o.value(u"error_description"_s).toString(), errorCodes(o)));
         });
}

} // namespace brulion::mstodo
