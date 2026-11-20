#include "tokenimport.h"

#include "cloudimport/downloads.h"
#include "io/files.h"
#include "sync/http.h"

using namespace Qt::StringLiterals;

namespace brulion::ioviews {

// ---- TokenImport ----------------------------------------------------------------------------------

TokenImport::TokenImport(QObject *parent) : QObject(parent) { }

TokenImport::~TokenImport() = default;

void TokenImport::setService(const QString &service)
{
    const QString s = service == u"airtable" ? service : u"todoist"_s;
    if (s == m_service)
        return;
    cancel();
    m_service = s;
    Q_EMIT serviceChanged();
}

void TokenImport::setImporter(ImportController *importer)
{
    if (m_importer == importer)
        return;
    if (m_importer)
        disconnect(m_importer, nullptr, this, nullptr);
    m_importer = importer;
    if (m_importer) {
        // Files picked or a path typed meanwhile: that input wins.
        connect(m_importer, &ImportController::stateChanged, this, [this] {
            if (busy() && m_importer->state() != u"empty")
                cancel();
        });
    }
    Q_EMIT importerChanged();
}

void TokenImport::setTransport(sync::HttpTransport *transport)
{
    cancel();
    m_todoist.reset();
    m_airtable.reset();
    m_transport = transport;
}

void TokenImport::setTimeUnitMs(int ms)
{
    m_unitMs = std::max(1, ms);
}

void TokenImport::setToken(const QString &token)
{
    if (token == m_token)
        return;
    m_token = token;
    m_secret.reset(token.trimmed());
    Q_EMIT tokenChanged();
}

bool TokenImport::busy() const
{
    return m_state == u"connecting" || m_state == u"choosing" || m_state == u"downloading";
}

int TokenImport::checkedTables() const
{
    int n = 0;
    for (const QVariant &t : m_tables)
        n += t.toMap().value(u"checked"_s).toBool();
    return n;
}

void TokenImport::setState(const QString &state, const QString &code, const QString &text)
{
    if (state == m_state && code == m_errorCode && text == m_errorText)
        return;
    m_state = state;
    m_errorCode = code;
    m_errorText = text;
    Q_EMIT stateChanged();
}

void TokenImport::setProgress(const QString &text)
{
    if (text == m_progressText)
        return;
    m_progressText = text;
    Q_EMIT progressChanged();
}

void TokenImport::ensureClients()
{
    if (!m_transport) {
        m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
        m_transport = m_ownTransport.get();
    }
    const auto failed = [this](const QString &code, const QString &message) {
        setProgress({});
        setState(u"error"_s, code, message);
    };
    const auto progress = [this](int done, int total, const QString &what) {
        QString text = tr("Downloading… %1 of %2").arg(done + 1).arg(std::max(total, 1));
        if (!what.isEmpty())
            text += u" · "_s + what;
        setProgress(text);
    };
    if (!m_todoist) {
        m_todoist = std::make_unique<cloudimport::TodoistDownload>(m_transport);
        connect(m_todoist.get(), &cloudimport::Download::progress, this, progress);
        connect(m_todoist.get(), &cloudimport::Download::failed, this, failed);
        connect(m_todoist.get(), &cloudimport::Download::finished, this,
                [this](const QList<io::FileEntry> &files, const QStringList &warnings) {
                    handOver(files, warnings, u"todoist"_s, u"Todoist"_s);
                });
    }
    if (!m_airtable) {
        m_airtable = std::make_unique<cloudimport::AirtableDownload>(m_transport);
        connect(m_airtable.get(), &cloudimport::Download::progress, this, progress);
        connect(m_airtable.get(), &cloudimport::Download::failed, this, failed);
        connect(m_airtable.get(), &cloudimport::AirtableDownload::basesListed, this,
                [this](const QList<cloudimport::AirtableBase> &bases) {
                    m_bases.clear();
                    for (const cloudimport::AirtableBase &b : bases)
                        m_bases.append(QVariantMap{
                                {u"id"_s, b.id}, {u"name"_s, b.name}, {u"value"_s, b.id}, {u"text"_s, b.name}});
                    m_tables.clear();
                    m_baseId.clear();
                    Q_EMIT choicesChanged();
                    setProgress({});
                    if (m_bases.isEmpty()) {
                        setState(u"error"_s, u"no-bases"_s,
                                 tr("The token sees no base: add the bases to its access in Airtable's builder hub."));
                        return;
                    }
                    setState(u"choosing"_s);
                    if (m_bases.size() == 1)
                        chooseBase(m_bases.first().toMap().value(u"id"_s).toString());
                });
        connect(m_airtable.get(), &cloudimport::AirtableDownload::tablesListed, this,
                [this](const QString &base, const QList<cloudimport::AirtableTable> &tables) {
                    if (base != m_baseId)
                        return;
                    m_tables.clear();
                    for (const cloudimport::AirtableTable &t : tables)
                        m_tables.append(QVariantMap{
                                {u"id"_s, t.id}, {u"name"_s, t.name}, {u"fields"_s, t.fields}, {u"checked"_s, true}});
                    Q_EMIT choicesChanged();
                    setProgress({});
                    setState(u"choosing"_s);
                });
        connect(m_airtable.get(), &cloudimport::Download::finished, this,
                [this](const QList<io::FileEntry> &files, const QStringList &warnings) {
                    handOver(files, warnings, u"airtable"_s, u"Airtable"_s);
                });
    }
    m_todoist->client()->setTimeUnitMs(m_unitMs);
    m_airtable->client()->setTimeUnitMs(m_unitMs);
}

void TokenImport::connectService()
{
    if (m_state == u"connecting" || m_state == u"downloading")
        return;
    if (m_token.trimmed().isEmpty()) {
        setState(u"error"_s, u"no-token"_s, tr("Paste the token first."));
        return;
    }
    ensureClients();
    if (m_service == u"todoist") {
        setProgress(tr("Downloading the projects…"));
        setState(u"downloading"_s);
        m_todoist->start(m_token.trimmed());
        return;
    }
    setProgress(tr("Listing the bases…"));
    setState(u"connecting"_s);
    m_airtable->listBases(m_token.trimmed());
}

void TokenImport::chooseBase(const QString &id)
{
    if (!m_airtable || id.isEmpty())
        return;
    m_baseId = id;
    m_tables.clear();
    Q_EMIT choicesChanged();
    setProgress(tr("Listing the tables…"));
    setState(u"connecting"_s);
    m_airtable->listTables(m_token.trimmed(), id);
}

void TokenImport::setTableChecked(const QString &id, bool checked)
{
    for (QVariant &t : m_tables) {
        QVariantMap m = t.toMap();
        if (m.value(u"id"_s).toString() == id && m.value(u"checked"_s).toBool() != checked) {
            m.insert(u"checked"_s, checked);
            t = m;
            Q_EMIT choicesChanged();
            return;
        }
    }
}

void TokenImport::download()
{
    if (m_service != u"airtable" || m_state != u"choosing" || m_baseId.isEmpty() || !m_airtable)
        return;
    QStringList ids;
    for (const QVariant &t : std::as_const(m_tables)) {
        if (t.toMap().value(u"checked"_s).toBool())
            ids.append(t.toMap().value(u"id"_s).toString());
    }
    if (ids.isEmpty())
        return;
    QString name;
    for (const QVariant &b : std::as_const(m_bases)) {
        if (b.toMap().value(u"id"_s).toString() == m_baseId)
            name = b.toMap().value(u"name"_s).toString();
    }
    setProgress(tr("Downloading the tables…"));
    setState(u"downloading"_s);
    m_airtable->start(m_token.trimmed(), m_baseId, name, ids);
}

void TokenImport::cancel()
{
    if (m_todoist)
        m_todoist->cancel();
    if (m_airtable)
        m_airtable->cancel();
    m_bases.clear();
    m_tables.clear();
    m_baseId.clear();
    Q_EMIT choicesChanged();
    setProgress({});
    setState(u"idle"_s);
}

void TokenImport::handOver(const QList<io::FileEntry> &files, const QStringList &warnings, const QString &importerId,
                           const QString &sourceName)
{
    setProgress({});
    setState(u"idle"_s);
    // The token is not needed any more.
    setToken({});
    m_bases.clear();
    m_tables.clear();
    m_baseId.clear();
    Q_EMIT choicesChanged();
    if (m_importer) {
        m_importer->setImporterId(importerId);
        m_importer->setInput(files, sourceName, warnings);
    }
    Q_EMIT downloaded();
}

// ---- ImportAttachments ------------------------------------------------------------------------------

ImportAttachments::ImportAttachments(QObject *parent) : QObject(parent) { }

ImportAttachments::~ImportAttachments() = default;

void ImportAttachments::setImporter(ImportController *importer)
{
    if (m_importer == importer)
        return;
    if (m_importer)
        disconnect(m_importer, nullptr, this, nullptr);
    m_importer = importer;
    if (m_importer) {
        connect(m_importer, &ImportController::inputChanged, this, [this] {
            // Other files were picked: a running download is for the old input.
            if (busy() && m_importer->state() == u"reading")
                cancel();
        });
    }
    Q_EMIT importerChanged();
}

void ImportAttachments::setTransport(sync::HttpTransport *transport)
{
    cancel();
    m_download.reset();
    m_transport = transport;
}

void ImportAttachments::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT stateChanged();
}

void ImportAttachments::setProgress(const QString &text)
{
    if (text == m_progressText)
        return;
    m_progressText = text;
    Q_EMIT progressChanged();
}

void ImportAttachments::download()
{
    if (busy() || !m_importer)
        return;
    const QList<QPair<QString, QString>> pending = m_importer->pendingAttachments();
    if (pending.isEmpty())
        return;
    if (!m_transport) {
        m_ownTransport = std::make_unique<sync::NetworkHttpTransport>();
        m_transport = m_ownTransport.get();
    }
    if (!m_download) {
        m_download = std::make_unique<cloudimport::UrlFilesDownload>(m_transport);
        connect(m_download.get(), &cloudimport::Download::progress, this, [this](int done, int total, const QString &) {
            setProgress(tr("Downloading the attachments… %1 of %2").arg(done + 1).arg(total));
        });
        connect(m_download.get(), &cloudimport::Download::finished, this,
                [this](const QList<io::FileEntry> &files, const QStringList &warnings) {
                    setProgress({});
                    setState(u"idle"_s);
                    if (m_importer)
                        m_importer->addInputFiles(files, warnings);
                    Q_EMIT downloaded(int(files.size()));
                });
        connect(m_download.get(), &cloudimport::Download::failed, this,
                [this](const QString &, const QString &message) {
                    setProgress(message);
                    setState(u"error"_s);
                });
    }
    setProgress(tr("Downloading the attachments…"));
    setState(u"downloading"_s);
    m_download->start(pending);
}

void ImportAttachments::cancel()
{
    if (m_download)
        m_download->cancel();
    setProgress({});
    setState(u"idle"_s);
}

} // namespace brulion::ioviews
