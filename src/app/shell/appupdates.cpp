#include "appupdates.h"

#include "appsettings.h"

#include "appupdate/appupdate.h"

#include <QLoggingCategory>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcAppUpdates, "brulion.updates")

#ifndef BRULION_UPDATE_CHECK
#error "BRULION_UPDATE_CHECK (0 or 1) comes from the CMake option of that name"
#endif

namespace brulion::shell {

namespace {

constexpr auto KeyCheck = "updates/check"_L1;
constexpr auto KeyLastCheck = "updates/lastCheck"_L1; // milliseconds since the epoch
constexpr auto KeyLatest = "updates/latest"_L1;
constexpr auto KeyDismissed = "updates/dismissed"_L1;

constexpr qint64 CheckEveryMs = 24 * 60 * 60 * 1000;
constexpr int LookEveryMs = 6 * 60 * 60 * 1000;

} // namespace

AppUpdates::AppUpdates(const Options &options, QObject *parent)
    : QObject(parent)
    , m_settings(options.settings)
    , m_current(options.currentVersion)
    , m_supported(options.supported && checkBuiltIn())
    , m_firstCheckDelayMs(options.firstCheckDelayMs)
    , m_clock(options.clock)
{
    m_latest = m_settings->value(KeyLatest).toString();
    m_dismissed = m_settings->value(KeyDismissed).toString();
    if (m_supported && options.network) {
        const QUrl apiUrl = options.apiUrl.isEmpty() ? appupdate::latestReleaseApiUrl() : options.apiUrl;
        m_checker = new appupdate::UpdateChecker(apiUrl, this);
        connect(m_checker, &appupdate::UpdateChecker::checked, this, &AppUpdates::onChecked);
        connect(m_checker, &appupdate::UpdateChecker::failed, this,
                [](const QString &detail) { qCInfo(lcAppUpdates) << "Checking for a new version failed:" << detail; });
    }
    m_timer.setInterval(LookEveryMs);
    connect(&m_timer, &QTimer::timeout, this, &AppUpdates::checkIfDue);
}

AppUpdates::~AppUpdates() = default;

bool AppUpdates::checkBuiltIn()
{
    return BRULION_UPDATE_CHECK != 0;
}

void AppUpdates::start()
{
    if (!m_checker)
        return;
    QTimer::singleShot(m_firstCheckDelayMs, this, &AppUpdates::checkIfDue);
    m_timer.start();
}

bool AppUpdates::enabled() const
{
    return m_settings->value(KeyCheck, true).toBool();
}

void AppUpdates::setEnabled(bool enabled)
{
    if (enabled == this->enabled())
        return;
    m_settings->setValue(KeyCheck, enabled);
    Q_EMIT enabledChanged();
    Q_EMIT availableChanged();
    checkIfDue();
}

QString AppUpdates::availableVersion() const
{
    if (!m_supported || !enabled() || m_latest == m_dismissed || !appupdate::isNewerVersion(m_latest, m_current))
        return {};
    return m_latest;
}

QUrl AppUpdates::releaseUrl() const
{
    const QString version = availableVersion();
    return version.isEmpty() ? QUrl() : appupdate::releasePageUrl(version);
}

void AppUpdates::dismiss()
{
    const QString version = availableVersion();
    if (version.isEmpty())
        return;
    m_dismissed = version;
    m_settings->setValue(KeyDismissed, version);
    Q_EMIT availableChanged();
}

void AppUpdates::checkIfDue()
{
    if (!m_checker || !enabled())
        return;
    const qint64 now = m_clock().toMSecsSinceEpoch();
    const qint64 last = m_settings->value(KeyLastCheck, 0).toLongLong();
    // A last check in the future: the device's clock was wrong then.
    if (last > 0 && last <= now && now - last < CheckEveryMs)
        return;
    m_checker->check();
}

void AppUpdates::setLatestForThisRun(const QString &version)
{
    m_latest = version;
    Q_EMIT availableChanged();
}

void AppUpdates::onChecked(const QString &version)
{
    m_settings->setValue(KeyLastCheck, m_clock().toMSecsSinceEpoch());
    m_settings->setValue(KeyLatest, version);
    m_latest = version;
    Q_EMIT availableChanged();
}

} // namespace brulion::shell
