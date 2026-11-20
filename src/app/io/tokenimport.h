// TokenImport — Todoist and Airtable on their import cards (QML: `TokenImport { service: "todoist";
// importer: importController }`): the user's personal token (Todoist: Settings → Integrations →
// Developer → API token; Airtable: a personal access token with data.records:read and
// schema.bases:read), kept in memory only — never in the settings, the notebook or a log —, then
// the download (cloudimport::TodoistDownload / AirtableDownload) handed to the ImportController,
// whose preview and report do the rest. Input that reaches the ImportController another way
// cancels it.
//
//   state      "idle" | "connecting" (Airtable: listing the bases / tables) | "choosing"
//              (Airtable: `bases`, then `tables` of `baseId` with `checked`) | "downloading"
//              (progressText) | "error" (errorText, errorCode)
//   connect()  Todoist: download everything; Airtable: list the bases
//   chooseBase(id), setTableChecked(id, on), download(), cancel()
//
// ImportAttachments — "Download the attachments" of the CSV mapping card (QML: `ImportAttachments {
// importer: importController }`): the files of the attachment cells the analysis found
// (ImportController::pendingAttachments(); Airtable's "photo.jpg (https://…)") downloaded
// without credentials (cloudimport::UrlFilesDownload) and added to the input
// (ImportController::addInputFiles), so the import embeds them.
//
// Not offered in the browser (WebAssembly): the services' APIs and file hosts do not answer web
// pages of other origins (CORS) — the QML shows the file import there.
#pragma once

#include "importcontroller.h"
#include "log/redact.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>

#include <memory>

namespace brulion::sync {
class HttpTransport;
class NetworkHttpTransport;
} // namespace brulion::sync

namespace brulion::cloudimport {
class TodoistDownload;
class AirtableDownload;
class UrlFilesDownload;
} // namespace brulion::cloudimport

namespace brulion::ioviews {

class TokenImport : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString service READ service WRITE setService NOTIFY serviceChanged FINAL)
    Q_PROPERTY(
            brulion::ioviews::ImportController *importer READ importer WRITE setImporter NOTIFY importerChanged FINAL)
    /// Never stored; cleared after a download and by cancel().
    Q_PROPERTY(QString token READ token WRITE setToken NOTIFY tokenChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged FINAL)
    /// Airtable: [{ id, name }] and the chosen one.
    Q_PROPERTY(QVariantList bases READ bases NOTIFY choicesChanged FINAL)
    Q_PROPERTY(QString baseId READ baseId NOTIFY choicesChanged FINAL)
    /// Airtable: the chosen base's tables [{ id, name, fields, checked }].
    Q_PROPERTY(QVariantList tables READ tables NOTIFY choicesChanged FINAL)
    Q_PROPERTY(int checkedTables READ checkedTables NOTIFY choicesChanged FINAL)

public:
    explicit TokenImport(QObject *parent = nullptr);
    ~TokenImport() override;

    QString service() const { return m_service; }
    void setService(const QString &service);
    ImportController *importer() const { return m_importer; }
    void setImporter(ImportController *importer);
    /// HTTP (tests: a fake server). Not owned; set before connect(). Default: an own
    /// NetworkHttpTransport.
    void setTransport(sync::HttpTransport *transport);
    void setTimeUnitMs(int ms);

    QString token() const { return m_token; }
    void setToken(const QString &token);
    QString state() const { return m_state; }
    bool busy() const;
    QString progressText() const { return m_progressText; }
    QString errorText() const { return m_errorText; }
    QString errorCode() const { return m_errorCode; }
    QVariantList bases() const { return m_bases; }
    QString baseId() const { return m_baseId; }
    QVariantList tables() const { return m_tables; }
    int checkedTables() const;

    Q_INVOKABLE void connectService();
    Q_INVOKABLE void chooseBase(const QString &id);
    Q_INVOKABLE void setTableChecked(const QString &id, bool checked);
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void serviceChanged();
    void importerChanged();
    void tokenChanged();
    void stateChanged();
    void progressChanged();
    void choicesChanged();
    /// The data was downloaded and handed to the importer.
    void downloaded();

private:
    void setState(const QString &state, const QString &code = {}, const QString &text = {});
    void setProgress(const QString &text);
    void ensureClients();
    void handOver(const QList<io::FileEntry> &files, const QStringList &warnings, const QString &importerId,
                  const QString &sourceName);

    QString m_service = QStringLiteral("todoist");
    QPointer<ImportController> m_importer;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::NetworkHttpTransport> m_ownTransport;
    std::unique_ptr<cloudimport::TodoistDownload> m_todoist;
    std::unique_ptr<cloudimport::AirtableDownload> m_airtable;
    QString m_token;
    logging::ScopedSecret m_secret;
    QString m_state = QStringLiteral("idle");
    QString m_progressText;
    QString m_errorText;
    QString m_errorCode;
    QVariantList m_bases;
    QString m_baseId;
    QVariantList m_tables;
    int m_unitMs = 1000;
};

class ImportAttachments : public QObject {
    Q_OBJECT

    Q_PROPERTY(
            brulion::ioviews::ImportController *importer READ importer WRITE setImporter NOTIFY importerChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged FINAL)

public:
    explicit ImportAttachments(QObject *parent = nullptr);
    ~ImportAttachments() override;

    ImportController *importer() const { return m_importer; }
    void setImporter(ImportController *importer);
    void setTransport(sync::HttpTransport *transport);
    QString state() const { return m_state; }
    bool busy() const { return m_state == u"downloading"; }
    QString progressText() const { return m_progressText; }

    /// Download the attachment cells' files not yet in the input.
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();

Q_SIGNALS:
    void importerChanged();
    void stateChanged();
    void progressChanged();
    void downloaded(int files);

private:
    void setState(const QString &state);
    void setProgress(const QString &text);

    QPointer<ImportController> m_importer;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::NetworkHttpTransport> m_ownTransport;
    std::unique_ptr<cloudimport::UrlFilesDownload> m_download;
    QString m_state = QStringLiteral("idle");
    QString m_progressText;
};

} // namespace brulion::ioviews
