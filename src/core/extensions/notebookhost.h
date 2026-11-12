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
// by up to 50 lines, else "stale" and nothing is written.
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
    };

    NotebookHost(Vault &vault, VaultIndex &index, Options options);

    std::function<void(const QString &extension, int seconds, const QString &label)> onTimer;
    std::function<void(const QString &extension, const QString &message)> onLog;
    /// Called after every write the host made (the path written).
    std::function<void(const QString &extension, const QString &path)> onWritten;

    void setLanguages(const QString &language, const QString &uiLanguage);
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
    QString write(const QString &extension, const QString &page, const QString &content) override;
    QString appendToJournal(const QString &extension, const QString &date, const QString &markdown) override;
    QString replaceBlock(const QString &extension, const QString &ref, const QString &text,
                         const std::optional<QString> &task, QString *newRef) override;
    QString today() override;
    QString language() override { return m_options.language; }
    QString uiLanguage() override { return m_options.uiLanguage; }
    void timer(const QString &extension, int seconds, const QString &label) override;
    void log(const QString &extension, const QString &message) override;

private:
    QDateTime now() const;
    QString writeFile(const QString &extension, const VaultPath &path, const QString &content);

    Vault &m_vault;
    VaultIndex &m_index;
    Options m_options;
};

/// A page's tags from its properties (frontmatter `tags:`, a `tags::` line): lower-case, without '#'.
QStringList pageTags(const ParsedNote &note);

} // namespace brulion::extensions
