// The import downloads of the cloudimport module (cloudimport.h): Microsoft OneNote (Graph, with an
// access token of the mstodo module's device-code sign-in, scope Notes.Read), Todoist (the Sync
// API with a personal API token), Airtable (the REST API with a personal access token) and the
// files of a CSV's attachment cells. Each turns what it downloads into input files for the io
// module's importers (the same shapes they read from disk) and emits exactly one of finished /
// failed per start — none after cancel(). Tokens stay in memory (log::redact while in use) and
// are never written or put into a message.
#pragma once

#include "cloudimport/apiclient.h"
#include "io/files.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>

#include <functional>
#include <memory>

namespace brulion::cloudimport {

/// Size budgets of the downloaded files.
struct FileBudget {
    qint64 maxFileBytes = 50LL * 1024 * 1024;
    qint64 maxTotalBytes = 300LL * 1024 * 1024;
    int maxFiles = 5000;
};

/// Common state of a download: the client, warnings, the files, the budget.
class Download : public QObject {
    Q_OBJECT
public:
    /// `transport` is not owned and must outlive the download.
    explicit Download(sync::HttpTransport *transport, QObject *parent = nullptr);

    ApiClient *client() { return &m_client; }
    void setBudget(const FileBudget &budget) { m_budget = budget; }
    void cancel();
    bool active() const { return m_active; }

Q_SIGNALS:
    /// `done` of `total` steps (pages, tables, files); `what` says what is being downloaded.
    void progress(int done, int total, const QString &what);
    /// The input files for the importer and what was left out (translated).
    void finished(const QList<brulion::io::FileEntry> &files, const QStringList &warnings);
    /// "unauthorized", "forbidden", "network", "throttled", "server", "not-found"; translated.
    void failed(const QString &code, const QString &message);

protected:
    void begin(const QString &token);
    void warn(const QString &message);
    void fail(const QString &code, const QString &message);
    /// The general failure of an answer (`service` names the service in the message).
    void failWith(const sync::HttpResponse &response, const QString &service);
    void finish();
    /// Whether a file of `bytes` fits the budget (a warning when not).
    bool fits(qint64 bytes);
    void addFile(const QString &path, const QByteArray &data);
    /// GET `url` as JSON: `ok` gets the object; other answers `failure` (default failWith).
    void getJson(const QUrl &url, std::function<void(const QJsonObject &)> ok,
                 std::function<void(const sync::HttpResponse &)> failure = {});
    /// Is this answer still wanted (not cancelled meanwhile)?
    bool current(int generation) const { return m_active && generation == m_generation; }
    int generation() const { return m_generation; }

    ApiClient m_client;
    FileBudget m_budget;
    QList<io::FileEntry> m_files;
    QStringList m_warnings;
    qint64 m_bytes = 0;
    QString m_service;
    bool m_active = false;
    int m_generation = 0;
};

// ---- OneNote ---------------------------------------------------------------------------------

struct OneNoteNotebook {
    QString id;
    QString name;
};

/// Microsoft Graph's OneNote API: GET /me/onenote/notebooks; per chosen notebook its sections and
/// section groups (recursively), each section's pages (pagelevel=true: level and order, @odata
/// paging), each page's content (HTML) and the resources it shows (pictures in full resolution,
/// attached files). The files: OneNote/onenote.json (the io module's "onenote" manifest),
/// OneNote/pages/<n>.html, OneNote/resources/<n>.<ext>.
class OneNoteDownload : public Download {
    Q_OBJECT
public:
    explicit OneNoteDownload(sync::HttpTransport *transport, QObject *parent = nullptr);

    /// The notebooks of the account (notebooksListed or failed).
    void listNotebooks(const QString &accessToken);
    /// Download the notebooks with these ids (all when empty).
    void start(const QString &accessToken, const QStringList &notebookIds = {});

Q_SIGNALS:
    void notebooksListed(const QList<brulion::cloudimport::OneNoteNotebook> &notebooks);

private:
    /// A notebook, section group or section of the manifest.
    struct Node {
        QString id;
        QString name;
        bool section = false;
        QList<int> children;
        QJsonArray pages;
    };
    struct PageJob {
        int section = 0;
        qsizetype page = 0; ///< index in its section's pages
        QString id;
    };
    void collect(const QUrl &url, QJsonArray items, std::function<void(const QJsonArray &)> done);
    void fetchNotebook(qsizetype index);
    void fetchContainer(int node, const QString &path, int depth, std::function<void()> done);
    void fetchGroups(int node, std::shared_ptr<QList<int>> groups, int depth, std::function<void()> done);
    void fetchSectionPages(qsizetype index);
    void fetchPage(qsizetype index);
    void fetchResources(std::shared_ptr<QStringList> queue, std::function<void()> done);
    QJsonObject nodeJson(int node, int depth) const;
    void writeManifest();

    QList<OneNoteNotebook> m_notebooks;
    QStringList m_chosen;
    QList<Node> m_nodes;
    QList<int> m_roots; ///< notebooks
    QList<int> m_sectionNodes;
    QList<PageJob> m_pages;
    QJsonObject m_resources;
    QSet<QString> m_resourceSeen;
};

// ---- Todoist ---------------------------------------------------------------------------------

/// Todoist's Sync API (POST https://api.todoist.com/api/v1/sync, sync_token=*, resource_types
/// projects, items, sections, labels, notes, project_notes, reminders) with a personal API token
/// (Todoist: Settings → Integrations → Developer), then the files of comments. The files:
/// todoist.json (the answer, marked {"brulion": "todoist-export"}) and
/// _attachments/<hash>/<name> (io::csvAttachmentInputPath()).
class TodoistDownload : public Download {
    Q_OBJECT
public:
    explicit TodoistDownload(sync::HttpTransport *transport, QObject *parent = nullptr);
    void start(const QString &apiToken);

private:
    void fetchFiles(std::shared_ptr<QList<QPair<QString, QString>>> queue);
};

// ---- Airtable --------------------------------------------------------------------------------

struct AirtableBase {
    QString id;
    QString name;
};
struct AirtableTable {
    QString id;
    QString name;
    int fields = 0;
};

/// Airtable's REST API with a personal access token (scopes data.records:read and
/// schema.bases:read): GET /v0/meta/bases, /v0/meta/bases/{base}/tables, the records of the chosen
/// tables (pageSize 100, offset paging; at most 5 requests a second), then the attachments. The
/// files: airtable.json (the io module's "airtable" bundle) and _attachments/<hash>/<name>.
class AirtableDownload : public Download {
    Q_OBJECT
public:
    explicit AirtableDownload(sync::HttpTransport *transport, QObject *parent = nullptr);

    void listBases(const QString &token);
    void listTables(const QString &token, const QString &baseId);
    /// Download the tables (all of the base when empty).
    void start(const QString &token, const QString &baseId, const QString &baseName, const QStringList &tableIds = {});

Q_SIGNALS:
    void basesListed(const QList<brulion::cloudimport::AirtableBase> &bases);
    void tablesListed(const QString &baseId, const QList<brulion::cloudimport::AirtableTable> &tables);

private:
    void fetchRecords(qsizetype table, const QString &offset);
    void fetchAttachments(std::shared_ptr<QList<QPair<QString, QString>>> queue);
    void failAirtable(const sync::HttpResponse &response);

    QString m_baseId;
    QString m_baseName;
    QList<QJsonObject> m_tables;
    QList<AirtableBase> m_bases;
};

// ---- Files of CSV cells -------------------------------------------------------------------------

/// The files of a CSV's attachment cells ("photo.jpg (https://…)", io::CsvFileAnalysis::
/// attachmentUrls): each https address downloaded without credentials, within the budget, as
/// _attachments/<hash>/<name>. Failures are warnings (the cell keeps its address).
class UrlFilesDownload : public Download {
    Q_OBJECT
public:
    explicit UrlFilesDownload(sync::HttpTransport *transport, QObject *parent = nullptr);
    /// `files`: {url, file name}.
    void start(const QList<QPair<QString, QString>> &files);

private:
    void next(std::shared_ptr<QList<QPair<QString, QString>>> queue, int total);
};

} // namespace brulion::cloudimport
