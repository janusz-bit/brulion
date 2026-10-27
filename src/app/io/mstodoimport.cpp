#include "mstodoimport.h"

#include "io/files.h"
#include "mstodo/mstodo.h"
#include "sync/http.h"

#include <QClipboard>
#include <QGuiApplication>

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

namespace {

const QString ClientIdKey = u"import/mstodo/clientId"_s;
const QString AuthorityKey = u"import/mstodo/authority"_s;

} // namespace

MsTodoImport::MsTodoImport(QObject *parent) : QObject(parent), m_authority(mstodo::DefaultAuthority.toString())
{
    m_countdown.setInterval(1000);
    connect(&m_countdown, &QTimer::timeout, this, [this] {
        if (m_secondsLeft <= 0) {
            m_countdown.stop();
            return;
        }
        --m_secondsLeft;
        Q_EMIT secondsLeftChanged();
    });
}

MsTodoImport::~MsTodoImport() = default;

void MsTodoImport::setSettings(shell::AppSettings *settings)
{
    if (m_settings == settings)
        return;
    m_settings = settings;
    if (m_settings) {
        const QString id = m_settings->value(ClientIdKey).toString();
        const QString authority = m_settings->value(AuthorityKey).toString();
        if (id != m_clientId) {
            m_clientId = id;
            Q_EMIT clientIdChanged();
        }
        const QString a = authority.isEmpty() ? defaultAuthority() : authority;
        if (a != m_authority) {
            m_authority = a;
            Q_EMIT authorityChanged();
        }
        Q_EMIT validationChanged();
    }
    Q_EMIT settingsChanged();
}

void MsTodoImport::setImporter(ImportController *importer)
{
    if (m_importer == importer)
        return;
    m_importer = importer;
    Q_EMIT importerChanged();
}

void MsTodoImport::setTransport(sync::HttpTransport *transport)
{
    cancel();
    m_login.reset();
    m_download.reset();
    m_transport = transport;
}

void MsTodoImport::setClientId(const QString &id)
{
    const QString trimmed = id.trimmed();
    if (trimmed == m_clientId)
        return;
    m_clientId = trimmed;
    if (m_settings)
        m_settings->setValue(ClientIdKey, m_clientId);
    Q_EMIT clientIdChanged();
    Q_EMIT validationChanged();
}

void MsTodoImport::setAuthority(const QString &authority)
{
    const QString trimmed = authority.trimmed();
    if (trimmed == m_authority)
        return;
    m_authority = trimmed;
    if (m_settings)
        m_settings->setValue(AuthorityKey, m_authority);
    Q_EMIT authorityChanged();
    Q_EMIT validationChanged();
}

QString MsTodoImport::clientIdError() const
{
    return m_showErrors ? mstodo::validateClientId(m_clientId) : QString();
}

QString MsTodoImport::authorityError() const
{
    return m_showErrors ? mstodo::validateAuthority(m_authority) : QString();
}

void MsTodoImport::setShowErrors(bool on)
{
    if (m_showErrors == on)
        return;
    m_showErrors = on;
    Q_EMIT validationChanged();
}

QString MsTodoImport::defaultAuthority() const
{
    return mstodo::DefaultAuthority.toString();
}

bool MsTodoImport::busy() const
{
    return m_state == u"starting" || m_state == u"waiting" || m_state == u"downloading";
}

QString MsTodoImport::verificationText() const
{
    QString text = m_verificationUrl;
    if (text.startsWith(u"https://"))
        text.remove(0, 8);
    return text;
}

void MsTodoImport::setState(const QString &state, const QString &errorCode, const QString &errorText)
{
    if (state == m_state && errorCode == m_errorCode && errorText == m_errorText)
        return;
    m_state = state;
    m_errorCode = errorCode;
    m_errorText = errorText;
    Q_EMIT stateChanged();
}

void MsTodoImport::setProgress(const QString &text)
{
    if (text == m_progressText)
        return;
    m_progressText = text;
    Q_EMIT progressChanged();
}

void MsTodoImport::setCode(const QString &code, const QString &url, int seconds)
{
    m_userCode = code;
    m_verificationUrl = url;
    Q_EMIT codeChanged();
    m_secondsLeft = seconds;
    Q_EMIT secondsLeftChanged();
    if (seconds > 0)
        m_countdown.start();
    else
        m_countdown.stop();
}

void MsTodoImport::ensureClients()
{
    if (!m_transport) {
        m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
        m_transport = m_ownTransport.get();
    }
    if (!m_login) {
        m_login = std::make_unique<mstodo::DeviceCodeLogin>(m_transport);
        connect(m_login.get(), &mstodo::DeviceCodeLogin::codeReady, this,
                [this](const QString &code, const QUrl &url, int seconds) {
                    setCode(code, url.toString(), seconds);
                    setProgress(tr("Waiting for the sign-in…"));
                    setState(u"waiting"_s);
                });
        connect(m_login.get(), &mstodo::DeviceCodeLogin::signedIn, this, [this](const QString &token) {
            setCode({}, {}, 0);
            setProgress(tr("Signed in. Downloading the lists…"));
            setState(u"downloading"_s);
            m_download->start(token);
        });
        connect(m_login.get(), &mstodo::DeviceCodeLogin::failed, this, [this](const QString &code, const QString &message) {
            setCode({}, {}, 0);
            setState(u"error"_s, code, message);
        });
    }
    m_login->setTimeUnitMs(m_unitMs);
    if (!m_download) {
        m_download = std::make_unique<mstodo::TodoDownload>(m_transport);
        connect(m_download.get(), &mstodo::TodoDownload::progress, this, [this](int done, int lists, int tasks) {
            setProgress(tr("Downloading the lists… %1 of %2").arg(done).arg(lists) + u" · "_s
                        + tr("%n task(s)", nullptr, tasks));
        });
        connect(m_download.get(), &mstodo::TodoDownload::finished, this,
                [this](const QByteArray &bundle, const QStringList &warnings) {
                    setProgress({});
                    setState(u"idle"_s);
                    if (m_importer) {
                        m_importer->setImporterId(u"mstodo"_s);
                        m_importer->setInput({io::InputFile{u"Microsoft To Do.json"_s, bundle}}, u"Microsoft To Do"_s,
                                             warnings);
                    }
                    Q_EMIT downloaded();
                });
        connect(m_download.get(), &mstodo::TodoDownload::failed, this,
                [this](const QString &code, const QString &message) {
                    setProgress({});
                    setState(u"error"_s, code, message);
                });
    }
    m_download->setTimeUnitMs(m_unitMs);
}

void MsTodoImport::signIn()
{
    if (busy())
        return;
    if (!mstodo::validateClientId(m_clientId).isEmpty() || !mstodo::validateAuthority(m_authority).isEmpty()) {
        setShowErrors(true);
        return;
    }
    setShowErrors(false);
    ensureClients();
    setProgress(tr("Asking Microsoft for a sign-in code…"));
    setState(u"starting"_s);
    m_login->start(m_clientId, m_authority);
}

void MsTodoImport::cancel()
{
    if (m_login)
        m_login->cancel();
    if (m_download)
        m_download->cancel();
    setCode({}, {}, 0);
    setProgress({});
    setState(u"idle"_s);
}

bool MsTodoImport::copyCode()
{
    if (m_userCode.isEmpty())
        return false;
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard)
        return false;
    clipboard->setText(m_userCode);
    return true;
}

} // namespace brulion::ioviews
