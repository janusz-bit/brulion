#include "cloudimport/downloads.h"

#include "cloudimport/messages_p.h"
#include "io/csvimport.h"

#include <QJsonDocument>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QUrlQuery>

namespace brulion::cloudimport {

using namespace Qt::StringLiterals;
using detail::CloudMessages;

namespace {

constexpr QStringView GraphHost = u"graph.microsoft.com";
constexpr QStringView TodoistApi = u"api.todoist.com";
constexpr QStringView AirtableApi = u"api.airtable.com";
constexpr int MaxGroupDepth = 8;
constexpr int MaxPages = 20000;

QString segment(const QString &id)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(id));
}

QUrl graph(const QString &pathAndQuery)
{
    return QUrl::fromEncoded(QByteArray("https://graph.microsoft.com/v1.0" + pathAndQuery.toUtf8()), QUrl::StrictMode);
}

/// A file extension for a MIME type (".png"), "" when unknown.
QString extensionFor(const QString &mime)
{
    const QString m = mime.section(u';', 0, 0).trimmed().toLower();
    if (m.isEmpty())
        return {};
    const QMimeType type = QMimeDatabase().mimeTypeForName(m);
    const QString suffix = type.isValid() ? type.preferredSuffix() : QString();
    return suffix.isEmpty() ? QString() : u'.' + suffix;
}

QString attributeValue(const QString &attrs, const QString &name)
{
    const QRegularExpression re(u"\\b"_s + QRegularExpression::escape(name) + u"\\s*=\\s*\"([^\"]*)\""_s,
                                QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(attrs);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString decodeEntities(QString s)
{
    s.replace(u"&amp;"_s, u"&"_s)
            .replace(u"&quot;"_s, u"\""_s)
            .replace(u"&#39;"_s, u"'"_s)
            .replace(u"&lt;"_s, u"<"_s)
            .replace(u"&gt;"_s, u">"_s);
    return s;
}

} // namespace

// ---- Download -------------------------------------------------------------------------------------

Download::Download(sync::HttpTransport *transport, QObject *parent) : QObject(parent), m_client(transport, this) { }

void Download::begin(const QString &token)
{
    cancel();
    m_active = true;
    m_client.setToken(token);
    m_files.clear();
    m_warnings.clear();
    m_bytes = 0;
}

void Download::cancel()
{
    ++m_generation;
    m_active = false;
    m_client.cancel();
    m_files.clear();
}

void Download::warn(const QString &message)
{
    if (!m_warnings.contains(message))
        m_warnings.append(message);
}

void Download::fail(const QString &code, const QString &message)
{
    const QString text = logging::redact(message);
    cancel();
    Q_EMIT failed(code, text);
}

void Download::failWith(const sync::HttpResponse &response, const QString &service)
{
    if (response.status == 0) {
        fail(u"network"_s,
             response.timedOut
                     ? CloudMessages::tr("%1 did not answer in time. Try again.").arg(service)
                     : CloudMessages::tr("%1 cannot be reached. Check the internet connection.").arg(service));
    } else if (response.status == 401) {
        fail(u"unauthorized"_s, CloudMessages::tr("%1 did not accept the sign-in or the token (it may be wrong or "
                                                  "expired).")
                                        .arg(service));
    } else if (response.status == 403) {
        fail(u"forbidden"_s, CloudMessages::tr("%1 does not let Brulion read this data: check the token's or the "
                                               "app's permissions.")
                                     .arg(service));
    } else if (response.status == 404) {
        fail(u"not-found"_s, CloudMessages::tr("%1 does not know what Brulion asked for (HTTP 404).").arg(service));
    } else if (response.status == 429 || response.status == 503 || response.status == 504) {
        fail(u"throttled"_s,
             CloudMessages::tr("%1 is limiting requests right now. Try again in a few minutes.").arg(service));
    } else {
        fail(u"server"_s, CloudMessages::tr("%1 answered with an error (HTTP %2).").arg(service).arg(response.status));
    }
}

void Download::finish()
{
    const QList<io::FileEntry> files = m_files;
    const QStringList warnings = m_warnings;
    cancel();
    Q_EMIT finished(files, warnings);
}

bool Download::fits(qint64 bytes)
{
    if (bytes > m_budget.maxFileBytes) {
        warn(CloudMessages::tr("Files larger than %n MB were left out.", nullptr, int(m_budget.maxFileBytes >> 20)));
        return false;
    }
    if (m_bytes + bytes > m_budget.maxTotalBytes || m_files.size() >= m_budget.maxFiles) {
        warn(CloudMessages::tr("Some files were left out: together they are too large to import at once."));
        return false;
    }
    return true;
}

void Download::addFile(const QString &path, const QByteArray &data)
{
    m_bytes += data.size();
    m_files.append(io::FileEntry{path, data});
}

void Download::getJson(const QUrl &url, std::function<void(const QJsonObject &)> ok,
                       std::function<void(const sync::HttpResponse &)> failure)
{
    sync::HttpRequest request;
    request.url = url;
    request.headers = {{"Accept", "application/json"}};
    const int gen = m_generation;
    m_client.send(request, [this, gen, ok = std::move(ok),
                            failure = std::move(failure)](const sync::HttpResponse &response) {
        if (!current(gen))
            return;
        if (response.status >= 200 && response.status < 300) {
            QJsonParseError error{};
            const QJsonDocument doc = QJsonDocument::fromJson(response.body, &error);
            if (error.error != QJsonParseError::NoError || !doc.isObject()) {
                fail(u"server"_s, CloudMessages::tr("%1 sent an answer Brulion does not understand.").arg(m_service));
                return;
            }
            ok(doc.object());
            return;
        }
        if (failure)
            failure(response);
        else
            failWith(response, m_service);
    });
}

// ---- OneNote --------------------------------------------------------------------------------------

OneNoteDownload::OneNoteDownload(sync::HttpTransport *transport, QObject *parent) : Download(transport, parent)
{
    m_service = u"Microsoft Graph"_s;
    const auto isGraph = [](const QUrl &url) { return isHttpsHost(url, GraphHost); };
    m_client.setHosts(isGraph, isGraph);
}

void OneNoteDownload::collect(const QUrl &url, QJsonArray items, std::function<void(const QJsonArray &)> done)
{
    getJson(url, [this, items = std::move(items), done = std::move(done)](const QJsonObject &page) mutable {
        for (const auto &v : page.value(u"value"_s).toArray())
            items.append(v);
        const QUrl next(page.value(u"@odata.nextLink"_s).toString());
        if (next.isEmpty() || !isHttpsHost(next, GraphHost) || items.size() >= MaxPages) {
            if (!next.isEmpty())
                warn(CloudMessages::tr("Not everything could be downloaded."));
            done(items);
            return;
        }
        collect(next, std::move(items), std::move(done));
    });
}

void OneNoteDownload::listNotebooks(const QString &accessToken)
{
    begin(accessToken);
    collect(graph(u"/me/onenote/notebooks?%24select=id,displayName&%24orderby=displayName"_s), {},
            [this](const QJsonArray &items) {
                QList<OneNoteNotebook> notebooks;
                for (const auto &v : items) {
                    const QJsonObject o = v.toObject();
                    if (!o.value(u"id"_s).toString().isEmpty())
                        notebooks.append({o.value(u"id"_s).toString(), o.value(u"displayName"_s).toString()});
                }
                m_active = false;
                m_client.cancel();
                Q_EMIT notebooksListed(notebooks);
            });
}

void OneNoteDownload::start(const QString &accessToken, const QStringList &notebookIds)
{
    begin(accessToken);
    m_chosen = notebookIds;
    m_nodes.clear();
    m_roots.clear();
    m_sectionNodes.clear();
    m_pages.clear();
    m_resources = {};
    m_resourceSeen.clear();
    collect(graph(u"/me/onenote/notebooks?%24select=id,displayName&%24orderby=displayName"_s), {},
            [this](const QJsonArray &items) {
                for (const auto &v : items) {
                    const QJsonObject o = v.toObject();
                    const QString id = o.value(u"id"_s).toString();
                    if (id.isEmpty() || (!m_chosen.isEmpty() && !m_chosen.contains(id)))
                        continue;
                    m_roots.append(int(m_nodes.size()));
                    m_nodes.append(Node{id, o.value(u"displayName"_s).toString(), false, {}, {}});
                }
                if (m_roots.isEmpty()) {
                    fail(u"not-found"_s, CloudMessages::tr("No OneNote notebook was found in this account."));
                    return;
                }
                fetchNotebook(0);
            });
}

void OneNoteDownload::fetchNotebook(qsizetype index)
{
    if (index >= m_roots.size()) {
        fetchSectionPages(0);
        return;
    }
    const int node = m_roots[index];
    Q_EMIT progress(int(index), int(m_roots.size()), m_nodes[node].name);
    fetchContainer(node, u"/me/onenote/notebooks/"_s + segment(m_nodes[node].id), 0,
                   [this, index] { fetchNotebook(index + 1); });
}

void OneNoteDownload::fetchContainer(int node, const QString &path, int depth, std::function<void()> done)
{
    collect(graph(path + u"/sections?%24select=id,displayName"_s), {},
            [this, node, path, depth, done](const QJsonArray &sections) {
                for (const auto &v : sections) {
                    const QJsonObject o = v.toObject();
                    const int child = int(m_nodes.size());
                    m_nodes.append(
                            Node{o.value(u"id"_s).toString(), o.value(u"displayName"_s).toString(), true, {}, {}});
                    m_nodes[node].children.append(child);
                    m_sectionNodes.append(child);
                }
                if (depth >= MaxGroupDepth) {
                    done();
                    return;
                }
                collect(graph(path + u"/sectionGroups?%24select=id,displayName"_s), {},
                        [this, node, depth, done](const QJsonArray &groups) {
                            auto queue = std::make_shared<QList<int>>();
                            for (const auto &v : groups) {
                                const QJsonObject o = v.toObject();
                                const int child = int(m_nodes.size());
                                m_nodes.append(Node{o.value(u"id"_s).toString(),
                                                    o.value(u"displayName"_s).toString(),
                                                    false,
                                                    {},
                                                    {}});
                                m_nodes[node].children.append(child);
                                queue->append(child);
                            }
                            fetchGroups(node, queue, depth, done);
                        });
            });
}

void OneNoteDownload::fetchGroups(int node, std::shared_ptr<QList<int>> groups, int depth, std::function<void()> done)
{
    if (groups->isEmpty()) {
        done();
        return;
    }
    const int group = groups->takeFirst();
    fetchContainer(group, u"/me/onenote/sectionGroups/"_s + segment(m_nodes[group].id), depth + 1,
                   [this, node, groups, depth, done] { fetchGroups(node, groups, depth, done); });
}

void OneNoteDownload::fetchSectionPages(qsizetype index)
{
    if (index >= m_sectionNodes.size()) {
        if (m_pages.isEmpty()) {
            writeManifest();
            finish();
            return;
        }
        fetchPage(0);
        return;
    }
    const int node = m_sectionNodes[index];
    collect(graph(u"/me/onenote/sections/"_s + segment(m_nodes[node].id)
                  + u"/pages?%24select=id,title,createdDateTime,lastModifiedDateTime,level,order,links&pagelevel=true&%24top=100"_s),
            {}, [this, node, index](const QJsonArray &pages) {
                for (const auto &v : pages) {
                    const QJsonObject p = v.toObject();
                    const QString id = p.value(u"id"_s).toString();
                    if (id.isEmpty() || m_pages.size() >= MaxPages)
                        continue;
                    // The page's GUID in OneNote's own links ("…page-id={…}&end").
                    static const QRegularExpression guid(u"page-id=\\{?([0-9a-fA-F-]{36})"_s);
                    const auto m = guid.match(p.value(u"links"_s)
                                                      .toObject()
                                                      .value(u"oneNoteClientUrl"_s)
                                                      .toObject()
                                                      .value(u"href"_s)
                                                      .toString());
                    const int n = int(m_pages.size());
                    QJsonObject page{{u"id"_s, id},
                                     {u"title"_s, p.value(u"title"_s).toString()},
                                     {u"level"_s, p.value(u"level"_s).toInt()},
                                     {u"order"_s, p.value(u"order"_s).toInt()},
                                     {u"created"_s, p.value(u"createdDateTime"_s).toString()},
                                     {u"modified"_s, p.value(u"lastModifiedDateTime"_s).toString()},
                                     {u"file"_s, u"pages/%1.html"_s.arg(n)}};
                    if (m.hasMatch())
                        page.insert(u"guid"_s, m.captured(1).toLower());
                    m_pages.append(PageJob{node, m_nodes[node].pages.size(), id});
                    m_nodes[node].pages.append(page);
                }
                fetchSectionPages(index + 1);
            });
}

void OneNoteDownload::fetchPage(qsizetype index)
{
    if (index >= m_pages.size()) {
        writeManifest();
        finish();
        return;
    }
    const PageJob job = m_pages[index];
    const QJsonObject page = m_nodes[job.section].pages[job.page].toObject();
    Q_EMIT progress(int(index), int(m_pages.size()), page.value(u"title"_s).toString());
    if (!m_active)
        return;
    sync::HttpRequest request;
    request.url = graph(u"/me/onenote/pages/"_s + segment(job.id) + u"/content"_s);
    request.headers = {{"Accept", "text/html"}};
    const int gen = m_generation;
    m_client.send(request, [this, gen, index, job](const sync::HttpResponse &response) {
        if (!current(gen))
            return;
        if (response.status < 200 || response.status >= 300) {
            if (response.status == 401 || response.status == 403 || response.status == 0 || response.status == 429) {
                failWith(response, m_service);
                return;
            }
            warn(CloudMessages::tr("Some pages could not be downloaded."));
            fetchPage(index + 1);
            return;
        }
        const QString html = QString::fromUtf8(response.body);
        addFile(u"OneNote/pages/%1.html"_s.arg(index), response.body);
        // Pictures (the full resolution when there is one) and attached files.
        auto queue = std::make_shared<QStringList>();
        static const QRegularExpression img(u"<img\\b([^>]*)>"_s, QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression object(u"<object\\b([^>]*)>"_s, QRegularExpression::CaseInsensitiveOption);
        for (auto it = img.globalMatch(html); it.hasNext();) {
            const QString attrs = it.next().captured(1);
            QString url = decodeEntities(attributeValue(attrs, u"data-fullres-src"_s));
            QString type = attributeValue(attrs, u"data-fullres-src-type"_s);
            if (url.isEmpty()) {
                url = decodeEntities(attributeValue(attrs, u"src"_s));
                type = attributeValue(attrs, u"data-src-type"_s);
            }
            // Only Graph's own resources (a picture of another site stays its address).
            if (!url.isEmpty() && !m_resourceSeen.contains(url) && isHttpsHost(QUrl(url), GraphHost)) {
                m_resourceSeen.insert(url);
                m_resources.insert(url, QJsonObject{{u"type"_s, type}});
                queue->append(url);
            }
        }
        for (auto it = object.globalMatch(html); it.hasNext();) {
            const QString attrs = it.next().captured(1);
            const QString url = decodeEntities(attributeValue(attrs, u"data"_s));
            if (!url.isEmpty() && !m_resourceSeen.contains(url) && isHttpsHost(QUrl(url), GraphHost)) {
                m_resourceSeen.insert(url);
                m_resources.insert(
                        url, QJsonObject{{u"type"_s, attributeValue(attrs, u"type"_s)},
                                         {u"name"_s, decodeEntities(attributeValue(attrs, u"data-attachment"_s))}});
                queue->append(url);
            }
        }
        fetchResources(queue, [this, index] { fetchPage(index + 1); });
    });
}

void OneNoteDownload::fetchResources(std::shared_ptr<QStringList> queue, std::function<void()> done)
{
    while (!queue->isEmpty()) {
        const QString url = queue->takeFirst();
        const QUrl u(url);
        if (!isHttpsHost(u, GraphHost))
            continue; // never sent anywhere else (the page keeps the picture's address)
        sync::HttpRequest request;
        request.url = u;
        request.headers = {{"Accept", "*/*"}};
        const int gen = m_generation;
        m_client.send(request, [this, gen, url, queue, done](const sync::HttpResponse &response) {
            if (!current(gen))
                return;
            if (response.status == 401) {
                failWith(response, m_service);
                return;
            }
            if (response.status >= 200 && response.status < 300 && fits(response.body.size())) {
                QJsonObject r = m_resources.value(url).toObject();
                QString ext = extensionFor(r.value(u"type"_s).toString());
                if (ext.isEmpty())
                    ext = extensionFor(QString::fromLatin1(response.header("content-type")));
                const QString file = u"resources/%1%2"_s.arg(m_files.size()).arg(ext);
                addFile(u"OneNote/"_s + file, response.body);
                r.insert(u"file"_s, file);
                m_resources.insert(url, r);
            } else if (response.status < 200 || response.status >= 300) {
                warn(CloudMessages::tr("Some pictures or files could not be downloaded."));
            }
            fetchResources(queue, done);
        });
        return;
    }
    done();
}

QJsonObject OneNoteDownload::nodeJson(int node, int depth) const
{
    const Node &n = m_nodes[node];
    QJsonObject o{{u"id"_s, n.id}, {u"name"_s, n.name}};
    if (n.section) {
        o.insert(u"pages"_s, n.pages);
        return o;
    }
    QJsonArray sections, groups;
    for (const int c : n.children) {
        if (m_nodes[c].section)
            sections.append(nodeJson(c, depth + 1));
        else if (depth < MaxGroupDepth + 1)
            groups.append(nodeJson(c, depth + 1));
    }
    o.insert(u"sections"_s, sections);
    o.insert(u"groups"_s, groups);
    return o;
}

void OneNoteDownload::writeManifest()
{
    QJsonArray notebooks;
    for (const int r : std::as_const(m_roots))
        notebooks.append(nodeJson(r, 0));
    const QJsonObject manifest{{u"brulion"_s, u"onenote-export"_s},
                               {u"version"_s, 1},
                               {u"notebooks"_s, notebooks},
                               {u"resources"_s, m_resources}};
    m_files.prepend(io::FileEntry{u"OneNote/onenote.json"_s, QJsonDocument(manifest).toJson(QJsonDocument::Compact)});
}

// ---- Todoist --------------------------------------------------------------------------------------

TodoistDownload::TodoistDownload(sync::HttpTransport *transport, QObject *parent) : Download(transport, parent)
{
    m_service = u"Todoist"_s;
    m_client.setHosts([](const QUrl &url) { return isHttpsDomain(url, u"todoist.com"); },
                      [](const QUrl &url) { return isHttpsDomain(url, u"todoist.com"); });
}

void TodoistDownload::start(const QString &apiToken)
{
    begin(apiToken.trimmed());
    if (apiToken.trimmed().isEmpty()) {
        fail(u"unauthorized"_s, CloudMessages::tr("Enter the Todoist API token first."));
        return;
    }
    Q_EMIT progress(0, 1, u"Todoist"_s);
    sync::HttpRequest request;
    request.method = "POST";
    request.url = QUrl(u"https://"_s + TodoistApi + u"/api/v1/sync"_s);
    QUrlQuery form;
    form.addQueryItem(u"sync_token"_s, u"*"_s);
    form.addQueryItem(u"resource_types"_s,
                      u"[\"projects\",\"items\",\"sections\",\"labels\",\"notes\",\"project_notes\",\"reminders\"]"_s);
    request.body = form.toString(QUrl::FullyEncoded).toUtf8();
    request.headers = {{"Content-Type", "application/x-www-form-urlencoded"}, {"Accept", "application/json"}};
    const int gen = m_generation;
    m_client.send(request, [this, gen](const sync::HttpResponse &response) {
        if (!current(gen))
            return;
        if (response.status < 200 || response.status >= 300) {
            failWith(response, m_service);
            return;
        }
        QJsonParseError error{};
        QJsonObject o = QJsonDocument::fromJson(response.body, &error).object();
        if (error.error != QJsonParseError::NoError || !o.value(u"items"_s).isArray()) {
            fail(u"server"_s, CloudMessages::tr("%1 sent an answer Brulion does not understand.").arg(m_service));
            return;
        }
        o.insert(u"brulion"_s, u"todoist-export"_s);
        o.remove(u"user"_s); // the account's details are not needed
        o.remove(u"sync_token"_s);
        addFile(u"todoist.json"_s, QJsonDocument(o).toJson(QJsonDocument::Compact));
        auto queue = std::make_shared<QList<QPair<QString, QString>>>();
        for (const QString &kind : {u"notes"_s, u"project_notes"_s}) {
            for (const auto &v : o.value(kind).toArray()) {
                const QJsonObject file = v.toObject().value(u"file_attachment"_s).toObject();
                const QString url = file.value(u"file_url"_s).toString().trimmed();
                if (!url.isEmpty())
                    queue->append({url, file.value(u"file_name"_s).toString()});
            }
        }
        fetchFiles(queue);
    });
}

void TodoistDownload::fetchFiles(std::shared_ptr<QList<QPair<QString, QString>>> queue)
{
    while (!queue->isEmpty()) {
        const auto [url, name] = queue->takeFirst();
        if (!m_client.allowed(QUrl(url))) {
            warn(CloudMessages::tr("Files stored outside Todoist were linked, not downloaded."));
            continue;
        }
        sync::HttpRequest request;
        request.url = QUrl(url);
        const int gen = m_generation;
        m_client.send(request, [this, gen, url, name, queue](const sync::HttpResponse &response) {
            if (!current(gen))
                return;
            if (response.status >= 200 && response.status < 300 && fits(response.body.size()))
                addFile(io::csvAttachmentInputPath(url, name), response.body);
            else if (response.status < 200 || response.status >= 300)
                warn(CloudMessages::tr("Some files of comments could not be downloaded."));
            fetchFiles(queue);
        });
        return;
    }
    finish();
}

// ---- Airtable -------------------------------------------------------------------------------------

AirtableDownload::AirtableDownload(sync::HttpTransport *transport, QObject *parent) : Download(transport, parent)
{
    m_service = u"Airtable"_s;
    m_client.setHosts(
            [](const QUrl &url) {
                return isHttpsHost(url, AirtableApi) || isHttpsDomain(url, u"airtableusercontent.com")
                       || isHttpsHost(url, u"dl.airtable.com");
            },
            [](const QUrl &url) { return isHttpsHost(url, AirtableApi); });
    m_client.setSpacingMs(220); // Airtable: 5 requests a second per base
}

void AirtableDownload::failAirtable(const sync::HttpResponse &response)
{
    const QJsonObject error = QJsonDocument::fromJson(response.body).object().value(u"error"_s).toObject();
    const QString type = error.value(u"type"_s).toString();
    if (response.status == 401 || type == u"AUTHENTICATION_REQUIRED") {
        fail(u"unauthorized"_s, CloudMessages::tr("Airtable did not accept the token. Create a personal access token "
                                                  "with the scopes data.records:read and schema.bases:read."));
        return;
    }
    if (response.status == 403 || type == u"INVALID_PERMISSIONS_OR_MODEL_NOT_FOUND") {
        fail(u"forbidden"_s, CloudMessages::tr("The token may not read this base: add the base to the token's access "
                                               "and the scopes data.records:read and schema.bases:read."));
        return;
    }
    failWith(response, m_service);
}

void AirtableDownload::listBases(const QString &token)
{
    begin(token.trimmed());
    m_bases.clear();
    struct Pager {
        static void next(AirtableDownload *d, const QString &offset)
        {
            QString url = u"https://api.airtable.com/v0/meta/bases"_s;
            if (!offset.isEmpty())
                url += u"?offset="_s + segment(offset);
            d->getJson(
                    QUrl(url),
                    [d](const QJsonObject &o) {
                        for (const auto &v : o.value(u"bases"_s).toArray()) {
                            const QJsonObject b = v.toObject();
                            d->m_bases.append({b.value(u"id"_s).toString(), b.value(u"name"_s).toString()});
                        }
                        const QString more = o.value(u"offset"_s).toString();
                        if (!more.isEmpty() && d->m_bases.size() < 2000) {
                            next(d, more);
                            return;
                        }
                        d->m_active = false;
                        d->m_client.cancel();
                        Q_EMIT d->basesListed(d->m_bases);
                    },
                    [d](const sync::HttpResponse &r) { d->failAirtable(r); });
        }
    };
    Pager::next(this, {});
}

void AirtableDownload::listTables(const QString &token, const QString &baseId)
{
    begin(token.trimmed());
    getJson(
            QUrl(u"https://api.airtable.com/v0/meta/bases/"_s + segment(baseId) + u"/tables"_s),
            [this, baseId](const QJsonObject &o) {
                QList<AirtableTable> tables;
                for (const auto &v : o.value(u"tables"_s).toArray()) {
                    const QJsonObject t = v.toObject();
                    tables.append({t.value(u"id"_s).toString(), t.value(u"name"_s).toString(),
                                   int(t.value(u"fields"_s).toArray().size())});
                }
                m_active = false;
                m_client.cancel();
                Q_EMIT tablesListed(baseId, tables);
            },
            [this](const sync::HttpResponse &r) { failAirtable(r); });
}

void AirtableDownload::start(const QString &token, const QString &baseId, const QString &baseName,
                             const QStringList &tableIds)
{
    begin(token.trimmed());
    m_baseId = baseId;
    m_baseName = baseName;
    m_tables.clear();
    getJson(
            QUrl(u"https://api.airtable.com/v0/meta/bases/"_s + segment(baseId) + u"/tables"_s),
            [this, tableIds](const QJsonObject &o) {
                for (const auto &v : o.value(u"tables"_s).toArray()) {
                    const QJsonObject t = v.toObject();
                    if (tableIds.isEmpty() || tableIds.contains(t.value(u"id"_s).toString())) {
                        QJsonObject table{{u"id"_s, t.value(u"id"_s)},
                                          {u"name"_s, t.value(u"name"_s)},
                                          {u"primaryFieldId"_s, t.value(u"primaryFieldId"_s)},
                                          {u"fields"_s, t.value(u"fields"_s)},
                                          {u"records"_s, QJsonArray()}};
                        m_tables.append(table);
                    }
                }
                if (m_tables.isEmpty()) {
                    fail(u"not-found"_s, CloudMessages::tr("The base has no table Brulion could read."));
                    return;
                }
                fetchRecords(0, {});
            },
            [this](const sync::HttpResponse &r) { failAirtable(r); });
}

void AirtableDownload::fetchRecords(qsizetype table, const QString &offset)
{
    if (table >= m_tables.size()) {
        // The bundle, then the attachments of every record.
        QJsonArray tables;
        auto queue = std::make_shared<QList<QPair<QString, QString>>>();
        QSet<QString> seen;
        for (const QJsonObject &t : std::as_const(m_tables)) {
            tables.append(t);
            QSet<QString> attachmentFields;
            for (const auto &f : t.value(u"fields"_s).toArray()) {
                if (f.toObject().value(u"type"_s).toString() == u"multipleAttachments")
                    attachmentFields.insert(f.toObject().value(u"name"_s).toString());
            }
            for (const auto &r : t.value(u"records"_s).toArray()) {
                const QJsonObject fields = r.toObject().value(u"fields"_s).toObject();
                for (const QString &name : std::as_const(attachmentFields)) {
                    for (const auto &a : fields.value(name).toArray()) {
                        const QString url = a.toObject().value(u"url"_s).toString();
                        if (!url.isEmpty() && !seen.contains(url)) {
                            seen.insert(url);
                            QString file = a.toObject().value(u"filename"_s).toString().simplified();
                            file.remove(u'(').remove(u')');
                            queue->append({url, file});
                        }
                    }
                }
            }
        }
        const QJsonObject bundle{
                {u"brulion"_s, u"airtable-export"_s},
                {u"version"_s, 1},
                {u"bases"_s,
                 QJsonArray{QJsonObject{{u"id"_s, m_baseId}, {u"name"_s, m_baseName}, {u"tables"_s, tables}}}}};
        addFile(u"airtable.json"_s, QJsonDocument(bundle).toJson(QJsonDocument::Compact));
        fetchAttachments(queue);
        return;
    }
    Q_EMIT progress(int(table), int(m_tables.size()), m_tables[table].value(u"name"_s).toString());
    QString url = u"https://api.airtable.com/v0/"_s + segment(m_baseId) + u'/'
                  + segment(m_tables[table].value(u"id"_s).toString()) + u"?pageSize=100"_s;
    if (!offset.isEmpty())
        url += u"&offset="_s + segment(offset);
    getJson(
            QUrl(url),
            [this, table](const QJsonObject &o) {
                QJsonArray records = m_tables[table].value(u"records"_s).toArray();
                for (const auto &r : o.value(u"records"_s).toArray())
                    records.append(r);
                m_tables[table].insert(u"records"_s, records);
                const QString more = o.value(u"offset"_s).toString();
                if (!more.isEmpty() && records.size() < 100000)
                    fetchRecords(table, more);
                else
                    fetchRecords(table + 1, {});
            },
            [this](const sync::HttpResponse &r) { failAirtable(r); });
}

void AirtableDownload::fetchAttachments(std::shared_ptr<QList<QPair<QString, QString>>> queue)
{
    while (!queue->isEmpty()) {
        const auto [url, name] = queue->takeFirst();
        if (!m_client.allowed(QUrl(url)))
            continue;
        sync::HttpRequest request;
        request.url = QUrl(url);
        const int gen = m_generation;
        m_client.send(request, [this, gen, url, name, queue](const sync::HttpResponse &response) {
            if (!current(gen))
                return;
            if (response.status >= 200 && response.status < 300 && fits(response.body.size()))
                addFile(io::csvAttachmentInputPath(url, name), response.body);
            else if (response.status < 200 || response.status >= 300)
                warn(CloudMessages::tr("Some attachments could not be downloaded; their addresses were kept."));
            fetchAttachments(queue);
        });
        return;
    }
    finish();
}

// ---- Files of CSV cells -----------------------------------------------------------------------------

UrlFilesDownload::UrlFilesDownload(sync::HttpTransport *transport, QObject *parent) : Download(transport, parent)
{
    m_service = CloudMessages::tr("The server");
    // Any https server; no credentials ever.
    m_client.setHosts(
            [](const QUrl &url) {
                return url.scheme() == u"https" && url.userInfo().isEmpty() && !url.host().isEmpty();
            },
            [](const QUrl &) { return false; });
}

void UrlFilesDownload::start(const QList<QPair<QString, QString>> &files)
{
    begin({});
    auto queue = std::make_shared<QList<QPair<QString, QString>>>(files);
    next(queue, int(files.size()));
}

void UrlFilesDownload::next(std::shared_ptr<QList<QPair<QString, QString>>> queue, int total)
{
    while (!queue->isEmpty()) {
        const auto [url, name] = queue->takeFirst();
        Q_EMIT progress(total - int(queue->size()) - 1, total, name);
        if (!m_active)
            return;
        if (!m_client.allowed(QUrl(url))) {
            warn(CloudMessages::tr("Addresses that are not https were not downloaded."));
            continue;
        }
        sync::HttpRequest request;
        request.url = QUrl(url);
        const int gen = m_generation;
        m_client.send(request, [this, gen, url, name, queue, total](const sync::HttpResponse &response) {
            if (!current(gen))
                return;
            if (response.status >= 200 && response.status < 300 && fits(response.body.size()))
                addFile(io::csvAttachmentInputPath(url, name), response.body);
            else if (response.status < 200 || response.status >= 300)
                warn(CloudMessages::tr("Some attachments could not be downloaded (the links may have expired); their "
                                       "addresses were kept."));
            next(queue, total);
        });
        return;
    }
    finish();
}

} // namespace brulion::cloudimport
