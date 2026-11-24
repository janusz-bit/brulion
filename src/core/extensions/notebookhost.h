#pragma once
// The Host of extensions over a notebook: a Vault and its VaultIndex (the app's Workspace, the
// command line's Notebook, tests). Reads come from the index (blocks with their task status, tags,
// properties and children, never parsed again by the script), writes go to the vault with
// content-preserving edits (blockedit.h, notebook/journals.h, notebook/pages.h).
//
//   NotebookHost host(vault, index, {.today = [] { return QDate::currentDate(); }});
//   host.onTimer = [](const QString &ext, int seconds, const QString &label) { … };
//
// A block's reference is a notebook::ItemRef line reference ("Dziennik/2026-11-11.md#L3@1a2b3c4d");
// replaceBlock() resolves it against the file as it is now: the same line, or the same text moved
// by up to 50 lines, else "stale" and nothing is written. A reference is taken only as a block had
// it — a note the index knows, a line and its hash: a script may pass any text, and no text makes
// the host read or write another file of the notebook's folder (.git/config, an attachment).
//
// A dry run (Options::dryRun: `brulion-cli extension check / run / eval`) writes nothing: every
// write is applied to the page's text as it would be — the same edits — and kept in dryWrites();
// read() and later writes of the same run see those texts, the index (blocks, search, query) the
// notebook as it is.
//
// Single-threaded (the vault's and the index's thread).

#include "extensions/host.h"

#include "types.h"

#include <QDateTime>

#include <functional>

namespace brulion {
class Vault;
class VaultIndex;
}

namespace brulion::extensions {

class NotebookHost : public Host
{
public:
    struct Options
    {
        std::function<QDateTime()> now; ///< local wall-clock time; unset: QDateTime::currentDateTime()
        QString language = QStringLiteral("pl"); ///< written text ("pl" | "en")
        QString uiLanguage = QStringLiteral("en");
        /// Update the index right after each write (a caller whose index does not follow the
        /// vault's changes by itself: tests, the command line).
        bool updateIndex = false;
        int maxBlocks = 20000; ///< blocks() returns at most this many (each with its subtree)
        bool dryRun = false; ///< write nothing: dryWrites() has what would have been written
        /// b.require: whether this device lets the extension page `path` with the code `hash` run
        /// — "" when it does, else why not ("waits for approval on this device"). Unset: every
        /// extension page may (a sandbox: `brulion-cli extension …`, b.checkExtension).
        std::function<QString(const QString &path, const QString &hash)> moduleAllowed;
    };

    /// A page a dry run would have written: its text before (`existed` false: a new page) and after.
    struct DryWrite
    {
        VaultPath path;
        QString before;
        QString after;
        bool existed = true;
    };

    NotebookHost(Vault &vault, VaultIndex &index, Options options);

    std::function<void(const QString &extension, int seconds, const QString &label)> onTimer;
    /// b.timer with its message, b.stopTimer (a spec with 0 seconds); unset: onTimer.
    std::function<void(const QString &extension, const TimerSpec &spec)> onExtensionTimer;
    std::function<void(const QString &extension, const QString &message)> onLog;
    std::function<void(const QString &extension, const QString &text)> onStatus;
    /// Called after every write the host made (the path written).
    std::function<void(const QString &extension, const QString &path)> onWritten;
    /// Called after b.setSetting stored (or removed) a value of `extension`'s (never in a dry run):
    /// what follows from its settings is out of date (the app's macros of it).
    std::function<void(const QString &extension)> onSettingChanged;
    /// b.app's operations (Host::app): the app answers them. Unset: a dry run answers the ones
    /// that read with a state of no app ({view = "", platform = "cli"}, no commands, of the settings
    /// the notebook's own from its config — b.app.get("journalsFolder") — and no device's) and notes
    /// the ones that change it (dryApp()); a host that is no dry run says the app is not there.
    std::function<QVariant(const QString &extension, const QString &operation, const QVariantMap &args, QString *error)>
            onApp;
    /// The theme or the UI items of an extension changed (Host::appChanged).
    std::function<void(const QString &extension)> onAppChanged;

    void setLanguages(const QString &language, const QString &uiLanguage);
    /// The pages a dry run would have written, in the order first written.
    QList<DryWrite> dryWrites() const { return m_dryWrites; }
    /// What a dry run's calls asked of the app that would change it, in order: [{extension,
    /// operation, …args}] (b.app.navigate, b.app.set …).
    QVariantList dryApp() const { return m_dryApp; }
    /// The note a page name ([[link]] target) names, or "".
    VaultPath pathOf(const QString &page) const;
    /// The block at `line` of `path` as a block map (see host.h), or an empty map.
    QVariantMap blockAt(const VaultPath &path, int line) const;

    QStringList pages(const QString &tag) override;
    std::optional<QString> read(const QString &page) override;
    QVariantMap page(const QString &page) override;
    QVariantList blocks(const BlockQuery &query) override;
    QVariantList search(const QString &query, int limit) override;
    QVariantMap query(const QString &source, int limit, QString *error) override;
    QVariantMap queryPage(const QString &source, int limit, int offset, QString *error) override;
    QStringList findPages(const PageQuery &query) override;
    QStringList attachments() override;
    qint64 revision() override;
    /// Through checkExtensionPage() (sandbox.h): the page `page` is (or would be, a new one where new
    /// pages go) loaded from `content` in an engine of its own — 1 s for its top-level code, 32 MB.
    QVariantMap checkExtension(const QString &page, const QString &content) override;
    QString write(const QString &extension, const QString &page, const QString &content) override;
    QString appendToJournal(const QString &extension, const QString &date, const QString &markdown) override;
    QString replaceBlock(const QString &extension, const QString &ref, const QString &text,
                         const std::optional<QString> &task, QString *newRef) override;
    QString appendToPage(const QString &extension, const QString &page, const QString &markdown) override;
    QString insertBlock(const QString &extension, const QString &ref, const QString &markdown, InsertAt where,
                        QString *newRef) override;
    QString removeBlock(const QString &extension, const QString &ref) override;
    QString setTask(const QString &extension, const QString &ref, const QString &status, QString *newRef) override;
    QString setProperty(const QString &extension, const QString &ref, const QString &key, const QString &value,
                        QString *newRef) override;
    QString setProperties(const QString &extension, const QString &ref, const QList<std::pair<QString, QString>> &props,
                          QString *newRef) override;
    std::optional<QVariantList> lines(const QString &page) override;
    QString replaceLine(const QString &extension, const QString &ref, const QString &text, QString *newRef) override;
    QString insertLines(const QString &extension, const QString &ref, const QString &text, bool before,
                        QString *newRef) override;
    QString removeLine(const QString &extension, const QString &ref) override;
    QString today() override;
    QString now() override;
    int weekStart() override;
    QString language() override { return m_options.language; }
    QString uiLanguage() override { return m_options.uiLanguage; }
    void timer(const QString &extension, int seconds, const QString &label) override;
    void startTimer(const QString &extension, const TimerSpec &spec) override;
    void stopTimer(const QString &extension) override;
    /// The extension page `name` as b.require takes it (Options::moduleAllowed decides whether).
    ModuleSource module(const QString &requester, const QString &name) override;
    /// The extension's settings on this device: the notebook's device state (LocalStore
    /// `extensions/settings/<id>`, settingsStoreKey()); a dry run keeps its changes to itself.
    QVariantMap settings(const QString &extension) override;
    QString setSetting(const QString &extension, const QString &key, const QVariant &value) override;
    void log(const QString &extension, const QString &message) override;
    void status(const QString &extension, const QString &text) override;
    QVariant app(const QString &extension, const QString &operation, const QVariantMap &args, QString *error) override;
    void appChanged(const QString &extension) override;

private:
    QDateTime clock() const;
    /// The note and line a block reference names as the file is now (a dry run's text first):
    /// "" and `content` / `line` set, or the error ("not a block reference: …", "stale: …").
    QString resolveRef(const QString &ref, VaultPath *path, QString *content, int *line);
    QString writeFile(const QString &extension, const VaultPath &path, const QString &content);
    /// The text of `path` as the writes see it (a dry run's own text first); `ok` false: unreadable.
    QString currentText(const VaultPath &path, bool *ok);
    DryWrite *dryWrite(const VaultPath &path);

    Vault &m_vault;
    VaultIndex &m_index;
    Options m_options;
    QList<DryWrite> m_dryWrites;
    QHash<QString, VaultPath> m_dryPages; // a dry run's new pages by the name b.write was given
    QHash<QString, QVariantMap> m_drySettings; // a dry run's settings by extension
    QVariantList m_dryApp;                     // a dry run's changes of the app (dryApp())
    qint64 m_writes = 0; // writes made (revision(): the index may follow them later)
};

/// A page's tags from its properties (frontmatter `tags:`, a `tags::` line): lower-case, without '#'.
QStringList pageTags(const ParsedNote &note);

/// The key of an extension's settings in the notebook's device state (LocalStore): a JSON object.
inline QString settingsStoreKey(const QString &extension)
{
    return QStringLiteral("extensions/settings/") + extension;
}
/// What an extension's settings may take in the device state, as compact JSON.
constexpr int kMaxSettingsBytes = 64 * 1024;

} // namespace brulion::extensions
