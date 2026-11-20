#include "mstodoimport.h"

#include "cloudimport/downloads.h"
#include "io/files.h"
#include "mstodo/mstodo.h"
#include "sync/http.h"

#include <QClipboard>
#include <QGuiApplication>

#include <chrono>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::ioviews {

namespace {

const QString ClientIdKey = u"import/mstodo/clientId"_s;
const QString AuthorityKey = u"import/mstodo/authority"_s;

} // namespace

MsTodoImport::MsTodoImport(QObject *parent) : QObject(parent), m_authority(mstodo::DefaultAuthority.toString())
{
    m_countdown.setInterval(1s);
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
    if (m_importer)
        disconnect(m_importer, nullptr, this, nullptr);
    m_importer = importer;
    if (m_importer) {
        // The user went the file way meanwhile (picked files, typed a path): the sign-in stops,
        // so its download can never replace that input later or be dropped during its import.
        connect(m_importer, &ImportController::stateChanged, this, [this] {
            if (busy() && m_importer->state() != u"empty")
                cancel();
        });
    }
    Q_EMIT importerChanged();
}

void MsTodoImport::setTransport(sync::HttpTransport *transport)
{
    cancel();
    m_login.reset();
    m_download.reset();
    m_oneNote.reset();
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
    return m_state == u"starting" || m_state == u"waiting" || m_state == u"listing" || m_state == u"choosing"
           || m_state == u"downloading";
}

void MsTodoImport::setService(const QString &service)
{
    const QString s = service == u"onenote" ? service : u"mstodo"_s;
    if (s == m_service)
        return;
    cancel();
    m_service = s;
    Q_EMIT serviceChanged();
}

int MsTodoImport::checkedNotebooks() const
{
    int n = 0;
    for (const QVariant &v : m_notebooks)
        n += v.toMap().value(u"checked"_s).toBool();
    return n;
}

void MsTodoImport::setNotebookChecked(const QString &id, bool checked)
{
    for (QVariant &v : m_notebooks) {
        QVariantMap m = v.toMap();
        if (m.value(u"id"_s).toString() == id && m.value(u"checked"_s).toBool() != checked) {
            m.insert(u"checked"_s, checked);
            v = m;
            Q_EMIT notebooksChanged();
            return;
        }
    }
}

void MsTodoImport::downloadNotebooks()
{
    if (m_state != u"choosing" || !m_oneNote)
        return;
    QStringList ids;
    for (const QVariant &v : std::as_const(m_notebooks)) {
        if (v.toMap().value(u"checked"_s).toBool())
            ids.append(v.toMap().value(u"id"_s).toString());
    }
    if (ids.isEmpty())
        return;
    setProgress(tr("Downloading the notebooks…"));
    setState(u"downloading"_s);
    const QString token = m_token;
    m_token.clear();
    m_secret.reset();
    m_oneNote->start(token, ids);
}

QString MsTodoImport::verificationText() const
{
    constexpr QStringView https = u"https://";
    return m_verificationUrl.startsWith(https) ? m_verificationUrl.sliced(https.size()) : m_verificationUrl;
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
            if (m_service == u"onenote") {
                // The token waits in memory while the user chooses the notebooks.
                m_token = token;
                m_secret.reset(token);
                setProgress(tr("Signed in. Listing the notebooks…"));
                setState(u"listing"_s);
                m_oneNote->listNotebooks(token);
                return;
            }
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
    if (!m_oneNote) {
        m_oneNote = std::make_unique<cloudimport::OneNoteDownload>(m_transport);
        connect(m_oneNote.get(), &cloudimport::OneNoteDownload::notebooksListed, this,
                [this](const QList<cloudimport::OneNoteNotebook> &notebooks) {
                    m_notebooks.clear();
                    for (const cloudimport::OneNoteNotebook &n : notebooks)
                        m_notebooks.append(QVariantMap{{u"id"_s, n.id}, {u"name"_s, n.name}, {u"checked"_s, true}});
                    Q_EMIT notebooksChanged();
                    setProgress({});
                    if (m_notebooks.isEmpty()) {
                        m_token.clear();
                        m_secret.reset();
                        setState(u"error"_s, u"no-notebooks"_s, tr("This account has no OneNote notebooks."));
                        return;
                    }
                    setState(u"choosing"_s);
                });
        connect(m_oneNote.get(), &cloudimport::Download::progress, this,
                [this](int done, int total, const QString &what) {
                    setProgress(tr("Downloading the notebooks… %1 of %2").arg(done + 1).arg(std::max(total, 1))
                                + (what.isEmpty() ? QString() : u" · "_s + what));
                });
        connect(m_oneNote.get(), &cloudimport::Download::finished, this,
                [this](const QList<io::FileEntry> &files, const QStringList &warnings) {
                    setProgress({});
                    setState(u"idle"_s);
                    m_notebooks.clear();
                    Q_EMIT notebooksChanged();
                    if (m_importer) {
                        m_importer->setImporterId(u"onenote"_s);
                        m_importer->setInput(files, u"OneNote"_s, warnings);
                    }
                    Q_EMIT downloaded();
                });
        connect(m_oneNote.get(), &cloudimport::Download::failed, this,
                [this](const QString &code, const QString &message) {
                    m_token.clear();
                    m_secret.reset();
                    setProgress({});
                    setState(u"error"_s, code, message);
                });
    }
    m_oneNote->client()->setTimeUnitMs(m_unitMs);
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
    m_login->setScope(m_service == u"onenote" ? mstodo::NotesScope.toString() : mstodo::Scope.toString());
    m_login->start(m_clientId, m_authority);
}

void MsTodoImport::cancel()
{
    if (m_login)
        m_login->cancel();
    if (m_download)
        m_download->cancel();
    if (m_oneNote)
        m_oneNote->cancel();
    m_token.clear();
    m_secret.reset();
    if (!m_notebooks.isEmpty()) {
        m_notebooks.clear();
        Q_EMIT notebooksChanged();
    }
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
