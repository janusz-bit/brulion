// Contract between the block editor (src/app/editor, qml/editor) and the rest of the app.
//
// The editor never touches the vault, the index or the platform directly: everything goes
// through an EditorHost. The shell's Workspace implements it for the real app; the editor
// playground and the tests use an in-memory fake. Keep this interface small and stable —
// the editor and the shell are developed in parallel against it.
#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

namespace brulion {

class EditorHost : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~EditorHost() override = default;

    // --- Files -------------------------------------------------------------------------------
    /// Current content of a vault file ("" + ok=false when missing).
    virtual QString read(const QString &path, bool *ok = nullptr) = 0;
    /// Save content (the host makes it atomic, records history, updates the index).
    virtual bool write(const QString &path, const QString &content) = 0;

    // --- Index-backed helpers (all return plain QVariant data for QML) -------------------------
    /// Page completions for `[[`: list of { path, title, alias?, folder }.
    virtual QVariantList suggestPages(const QString &prefix, int limit = 20) const = 0;
    /// Block completions for `((`: list of { path, line, text, blockId? }.
    virtual QVariantList suggestBlocks(const QString &text, int limit = 20) const = 0;
    /// Tag completions for `#`: list of { tag, count }.
    virtual QVariantList suggestTags(const QString &prefix, int limit = 20) const = 0;
    /// Resolve a wiki-link target seen from `fromPath`; "" when unresolved.
    virtual QString resolveLink(const QString &target, const QString &fromPath) const = 0;
    /// Text of the block with this id (for ((id)) rendering); "" when unknown.
    virtual QString blockText(const QString &blockId) const = 0;
    /// Id of the block at `line` of `path`, writing ` ^id` into that file when missing.
    virtual QString ensureBlockId(const QString &path, int line) = 0;
    /// Templates: list of { path, title }.
    virtual QVariantList templates() const = 0;

    // --- Assets ------------------------------------------------------------------------------
    /// URL the UI can load an attachment from (file:// on desktop/Android, blob/data on WASM).
    virtual QUrl assetUrl(const QString &path) const = 0;
    /// Store pasted/dropped bytes in the attachments folder; returns the new vault path.
    virtual QString saveAttachment(const QString &fileName, const QByteArray &data) = 0;

    // --- Context -----------------------------------------------------------------------------
    /// Vault configuration as a map (journalsFolder, journalFileFormat, attachmentsFolder,
    /// templatesFolder, defaultReminderTime "HH:mm", weekStartsOn 1..7).
    virtual QVariantMap vaultConfig() const = 0;

    // --- Navigation --------------------------------------------------------------------------
    /// Open a route ({view: "page", path, line?, blockId?} / {view: "tag", tag} /
    /// {view: "journal", date: "YYYY-MM-DD"}, see docs/UI.md); newPane = side panel/new tab.
    virtual void navigate(const QVariantMap &route, bool newPane = false) = 0;
    /// Open an external http(s)/mailto link with the platform handler.
    virtual void openUrl(const QUrl &url) = 0;

Q_SIGNALS:
    /// A file changed outside this editor (sync, Syncthing, another editor/view, import).
    void fileChanged(const QString &path);
    /// The index changed (link targets, block texts, tags may render differently).
    void indexChanged();
};

} // namespace brulion
