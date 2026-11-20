// PublishController — the "Publish" tab of ImportExportView (QML: `PublishController { workspace:
// Workspace; settings: AppSettings }`; docs/UI.md "Publishing"): the notebook's publishing targets
// on this device (core module publish: PublishStore in the notebook's device state folder, shared
// with brulion-cli), what each publishes, the dry run, publishing and unpublishing.
//
//   targets     [{ id, name, kind, kindText, icon, address, siteUrl, url (where it is, after a
//                 publish), items [{ kind, value, label, icon }], linkedPages, onlyMarked,
//                 extensionPages, aiConversations, content, published, lastText, lastOk,
//                 lastError, needsPassword, needsSharePassword, needsToken }]
//   current     the chosen target's id ("" none); currentTarget its row ({} none)
//   kinds       [{ value, text, icon, description }] — the folder target on the desktop only
//   state       "idle" → preview() → "preparing" (the chosen pages read in chunks, the site built
//               off the GUI thread) → "checking" (the target looked at) → "previewed" (plan) |
//               publish(force) → … → "publishing" → "published" (plan, resultText, url) |
//               "conflicts" (plan: files changed at the target — publish(true) replaces them) |
//               unpublish(force) → "unpublishing" → "unpublished" | "error" (errorText)
//   plan        { counts {upload, update, unchanged, delete, conflict}, summary, sizeText, pages,
//                 attachments, remoteChecked, remoteText, url, files [{ path, action, actionText,
//                 icon, sizeText }] (what changes first, at most 300), moreFiles, leftOut [{ path,
//                 reasonText }], conflicts [{ path, actionText }], warnings [text],
//                 fragmentsRemoved }
//
// Secrets: a target's WebDAV / Nextcloud password and a Nextcloud link's password in owner-only
// files next to the Nextcloud app password (publish::secretFile), the GitHub token the notebooks
// from GitHub use (<app data>/secrets/github-token) — never in the store, masked in every log line.
#pragma once

#include "types.h"

#include "appsettings.h"

#include "publish/publish.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace brulion::sync {
class HttpTransport;
class NetworkHttpTransport;
} // namespace brulion::sync

namespace brulion::shell {
class Workspace;
} // namespace brulion::shell

namespace brulion::ioviews {

class PublishController : public QObject {
    Q_OBJECT

    Q_PROPERTY(brulion::shell::Workspace *workspace READ workspace WRITE setWorkspace NOTIFY workspaceChanged FINAL)
    Q_PROPERTY(brulion::shell::AppSettings *settings READ settings WRITE setSettings NOTIFY workspaceChanged FINAL)
    Q_PROPERTY(QVariantList targets READ targets NOTIFY targetsChanged FINAL)
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged FINAL)
    Q_PROPERTY(QVariantMap currentTarget READ currentTarget NOTIFY currentChanged FINAL)
    Q_PROPERTY(QVariantList kinds READ kinds NOTIFY languageChanged FINAL)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged FINAL)
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString resultText READ resultText NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString url READ url NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString errorText READ errorText NOTIFY stateChanged FINAL)
    /// The folder target exists here (a desktop's folders).
    Q_PROPERTY(bool folderTargets READ folderTargets CONSTANT FINAL)
    /// The notebook's folders (the folder picker), sorted.
    Q_PROPERTY(QStringList folders READ folders NOTIFY targetsChanged FINAL)

public:
    explicit PublishController(QObject *parent = nullptr);
    ~PublishController() override;

    shell::Workspace *workspace() const { return m_workspace; }
    void setWorkspace(shell::Workspace *workspace);
    shell::AppSettings *settings() const { return m_settings; }
    void setSettings(shell::AppSettings *settings);

    QVariantList targets() const { return m_targets; }
    QString current() const { return m_current; }
    void setCurrent(const QString &id);
    QVariantMap currentTarget() const;
    QVariantList kinds() const;
    QString state() const { return m_state; }
    bool busy() const;
    qreal progress() const { return m_progress; }
    QString progressText() const { return m_progressText; }
    QVariantMap plan() const { return m_plan; }
    QString resultText() const { return m_resultText; }
    QString url() const { return m_url; }
    QString errorText() const { return m_errorText; }
    bool folderTargets() const;
    QStringList folders() const;

    /// The fields of a target for the form: { id, name, kind, repository, branch, folder, url,
    /// server, username, path, siteUrl, title, content, share, sharePassword, shareExpires
    /// ("yyyy-MM-dd" | ""), hasPassword, hasSharePassword } — a new one's defaults for "".
    Q_INVOKABLE QVariantMap targetFields(const QString &id) const;
    /// What is wrong with the form's fields (translated; "" when they make a target).
    Q_INVOKABLE QString checkFields(const QVariantMap &fields) const;
    /// Save a target from the form's fields (a new one when `id` is empty) with its secrets
    /// ("" keeps the one kept; `forget…` removes it): its id, "" when the fields make none.
    Q_INVOKABLE QString saveTarget(const QVariantMap &fields, const QString &password = {},
                                   const QString &sharePassword = {});
    Q_INVOKABLE void removeTarget(const QString &id);
    /// Add an item to the current target ("page" | "folder" | "tag" | "notebook", its value), then
    /// a dry run.
    Q_INVOKABLE void addItem(const QString &kind, const QString &value);
    Q_INVOKABLE void removeItem(int index);
    /// The item a route names ({ add: "page" | "folder" | "tag" | "notebook", path, folder, tag }):
    /// added to the current target (then a dry run); without a target it waits for one
    /// (addPending() once a target was made, clearPending() when none was) — true then: the
    /// caller asks for a new target.
    Q_INVOKABLE bool addFromRoute(const QVariantMap &route);
    Q_INVOKABLE void addPending();
    Q_INVOKABLE void clearPending();
    /// "linkedPages" | "onlyMarked" | "extensionPages" | "aiConversations" of the current target.
    Q_INVOKABLE void setOption(const QString &name, bool on);
    /// "site" | "document".
    Q_INVOKABLE void setContent(const QString &content);
    /// The dry run of the current target.
    Q_INVOKABLE void preview();
    /// Publish the current target (`force`: replace files changed there).
    Q_INVOKABLE void publish(bool force = false);
    Q_INVOKABLE void unpublish(bool force = false);
    Q_INVOKABLE void cancel();
    /// Back to "idle" (the result read).
    Q_INVOKABLE void dismiss();
    /// Read the targets again (brulion-cli may have changed them).
    Q_INVOKABLE void reload();
    /// The notebook's tags, for the tag picker: [{ title, subtitle, icon, path (the tag) }].
    Q_INVOKABLE QVariantList tagRows(const QString &query) const;
    /// The current URL on the clipboard.
    Q_INVOKABLE bool copyUrl() const;

    /// HTTP (tests: an in-memory server); default: a NetworkHttpTransport. Not owned.
    void setTransport(sync::HttpTransport *transport);
    /// Files read per event loop turn (default 64; tests).
    void setReadChunk(int files) { m_readChunk = std::max(1, files); }
    void setClock(std::function<QDateTime()> clock) { m_clock = std::move(clock); }
    /// The app's data folder of the secrets (default: from the settings' password file).
    void setAppDataDir(const QString &dir) { m_appDataDir = dir; }
    QString appDataDir() const;

Q_SIGNALS:
    void workspaceChanged();
    void targetsChanged();
    void currentChanged();
    void languageChanged();
    void stateChanged();
    void progressChanged();
    /// A publish or unpublish ended (translated one-line message, its tone).
    void finished(const QString &message, const QString &tone);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Operation { Preview, Publish, Unpublish };
    publish::PublishStore store() const;
    std::optional<publish::PublishTarget> target(const QString &id) const;
    void saveScope(const publish::PublishTarget &target);
    void refreshTargets();
    QVariantMap targetRow(const publish::PublishTarget &target) const;
    QString statusText(const publish::PublishStatus &status) const;
    publish::PublishCredentials credentials(const publish::PublishTarget &target) const;
    void start(Operation operation, bool force);
    void readStep();
    void buildAndRun();
    void runPublisher(const publish::SiteBuild &site);
    void onReport(const publish::PublishReport &report, const publish::SiteBuild &site);
    QVariantMap planMap(const publish::PublishReport &report, const publish::SiteBuild &site) const;
    void setState(const QString &state, const QString &error = {});
    void setProgress(qreal value, const QString &text);
    sync::HttpTransport *transport();
    QString human(const QDateTime &time) const;
    QDateTime now() const;

    QPointer<shell::Workspace> m_workspace;
    QPointer<shell::AppSettings> m_settings;
    sync::HttpTransport *m_transport = nullptr;
    std::unique_ptr<sync::NetworkHttpTransport> m_ownTransport;
    std::unique_ptr<publish::Publisher> m_publisher;
    std::function<QDateTime()> m_clock;
    QString m_appDataDir;
    QVariantList m_targets;
    QString m_current;
    QString m_state = QStringLiteral("idle");
    qreal m_progress = 0;
    QString m_progressText;
    QVariantMap m_plan;
    QString m_resultText;
    QString m_url;
    QString m_errorText;
    int m_generation = 0;
    int m_readChunk = 64;
    // The operation being prepared.
    Operation m_operation = Operation::Preview;
    bool m_force = false;
    publish::PublishTarget m_target;
    publish::PublishSelection m_selection;
    QList<VaultPath> m_readQueue;
    qsizetype m_readIndex = 0;
    QList<io::FileEntry> m_readFiles;
    QStringList m_unreadable;
    QTimer m_readTimer;
    QString m_pendingKind;
    QString m_pendingValue;
};

} // namespace brulion::ioviews
