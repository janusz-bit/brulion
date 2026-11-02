#include "appupdate.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>

using namespace Qt::StringLiterals;

namespace brulion::appupdate {

QUrl latestReleaseApiUrl()
{
    return QUrl(u"https://api.github.com/repos/janusz-bit/brulion/releases/latest"_s);
}

QUrl releasePageUrl(const QString &version)
{
    return QUrl(u"https://github.com/janusz-bit/brulion/releases/tag/v"_s + version);
}

QString normalizedVersion(const QString &text)
{
    // At most 9 digits: every part fits an int.
    static const QRegularExpression plain(uR"(\Av?(\d{1,9}\.\d{1,9}\.\d{1,9})\z)"_s);
    const QRegularExpressionMatch match = plain.match(text);
    return match.hasMatch() ? QVersionNumber::fromString(match.captured(1)).toString() : QString();
}

bool isNewerVersion(const QString &candidate, const QString &current)
{
    const QString newer = normalizedVersion(candidate);
    const QString base = normalizedVersion(current);
    if (newer.isEmpty() || base.isEmpty())
        return false;
    return QVersionNumber::fromString(newer) > QVersionNumber::fromString(base);
}

QString versionFromRelease(const QByteArray &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject())
        return {};
    const QJsonObject release = document.object();
    if (release.value("draft"_L1).toBool() || release.value("prerelease"_L1).toBool())
        return {};
    return normalizedVersion(release.value("tag_name"_L1).toString());
}

UpdateChecker::UpdateChecker(const QUrl &apiUrl, QObject *parent) : QObject(parent), m_apiUrl(apiUrl) { }

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::check()
{
    if (m_reply)
        return;
    if (!m_manager)
        m_manager = new QNetworkAccessManager(this);
    QNetworkRequest request(m_apiUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    // GitHub refuses requests without an agent.
    request.setHeader(QNetworkRequest::UserAgentHeader, u"Brulion"_s);
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("Accept-Language", "*");
    request.setTransferTimeout(TimeoutMs);
    m_tooLarge = false;
    QNetworkReply *reply = m_manager->get(request);
    m_reply = reply;
    const auto limit = [this, reply](qint64 bytes) {
        if (bytes > m_maxBytes && !m_tooLarge) {
            m_tooLarge = true;
            reply->abort();
        }
    };
    connect(reply, &QNetworkReply::metaDataChanged, this,
            [reply, limit] { limit(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [limit](qint64 received, qint64) { limit(received); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply); });
}

void UpdateChecker::finish(QNetworkReply *reply)
{
    reply->deleteLater();
    m_reply = nullptr;
    if (m_tooLarge) {
        Q_EMIT failed(u"more than %1 bytes"_s.arg(m_maxBytes));
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status != 0 && status != 200) {
        Q_EMIT failed(u"HTTP %1"_s.arg(status));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        Q_EMIT failed(reply->errorString());
        return;
    }
    const QString version = versionFromRelease(reply->readAll());
    if (version.isEmpty())
        Q_EMIT failed(u"no version in the answer"_s);
    else
        Q_EMIT checked(version);
}

} // namespace brulion::appupdate
