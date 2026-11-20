// What a publishing target publishes (docs/UI.md "Publishing", docs/SYNTAX.md §4 "Publishing"):
// its items — pages, folders, tags, the whole notebook — with the pages they link to when asked,
// the `publish:` property deciding page by page, and what is never published. Pure functions over
// the notebook's index. Part of the publish module — see publish/publish.h for the overview.
#pragma once

#include "types.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace brulion {
class VaultIndex;
}

namespace brulion::publish {

/// One thing a target publishes.
struct PublishItem {
    enum class Kind { Page, Folder, Tag, Notebook };
    Kind kind = Kind::Page;
    /// A page's vault path ("Projekty/Plan.md"), a folder's path ("Blog"), a tag without '#'
    /// ("przepisy"; its sub-tags too); "" for the notebook.
    QString value;

    friend bool operator==(const PublishItem &, const PublishItem &) = default;
};

/// "page" | "folder" | "tag" | "notebook".
QString itemKindName(PublishItem::Kind kind);
std::optional<PublishItem::Kind> itemKindFromName(QStringView name);

/// What a target publishes and how its pages are chosen.
struct PublishScope {
    QList<PublishItem> items;
    /// The pages the items' pages link to (one step) are published too.
    bool linkedPages = false;
    /// Only pages marked `publish: true` (Obsidian Publish's way) — an item that names a page
    /// is chosen by itself; without it every page of the items is published but those marked
    /// `publish: false`.
    bool onlyMarked = false;
    /// Extension pages (tagged #extension: the notebook's Luau code) — left out unless chosen.
    bool extensionPages = false;
    /// Conversations with AI (the folder AI/) — left out unless chosen.
    bool aiConversations = false;

    /// Add `item` unless it is there already; true when it was added.
    bool add(const PublishItem &item);
    friend bool operator==(const PublishScope &, const PublishScope &) = default;
};

/// Why a file of the items is not published.
enum class LeftOutReason {
    Hidden,       ///< in a hidden folder (.trash/, .obsidian/, …) or a hidden file
    Private,      ///< marked `publish: false`
    NotMarked,    ///< PublishScope::onlyMarked and not marked `publish: true`
    Extension,    ///< an extension page (PublishScope::extensionPages off)
    Conversation, ///< a conversation with AI (PublishScope::aiConversations off)
    Template,     ///< in the templates folder
    Encrypted,    ///< encrypted (the page as a whole, or an encrypted file)
    ConflictCopy, ///< a sync conflict copy
};

/// "hidden" | "private" | "not-marked" | "extension" | "conversation" | "template" | "encrypted" |
/// "conflict-copy".
QString reasonName(LeftOutReason reason);

struct LeftOut {
    VaultPath path;
    LeftOutReason reason = LeftOutReason::Private;
    friend bool operator==(const LeftOut &, const LeftOut &) = default;
};

struct PublishSelection {
    QList<VaultPath> pages;       ///< sorted
    QList<VaultPath> attachments; ///< the files the pages link to and embed, sorted
    QList<LeftOut> leftOut;       ///< sorted by path, a path once
    /// Items that name nothing: a page or folder that is gone, a tag no page has.
    QList<PublishItem> missingItems;
};

/// A page's `publish:` property (also `publikuj:`; true / false, yes / no, tak / nie, 1 / 0 in any
/// letter case, frontmatter or a `publish::` line): its value, nullopt when it has none (or one
/// that is neither).
std::optional<bool> publishFlag(const ParsedNote &note);

/// Is the page an extension page (tagged `extension` / `rozszerzenie` at page level)?
bool isExtensionPage(const ParsedNote &note);

/// Does a segment of `path` start with '.' (a hidden folder or file), or is it one that
/// paths::isIgnored() keeps out of the notebook?
bool isHiddenPath(const VaultPath &path);

/// A file encrypted as a whole, by its name: `.mdenc` (Meld Encrypt), `.encrypted`, `.enc`,
/// `.age`, `.gpg`, `.pgp`, `.asc`, `.aes`.
bool isEncryptedFile(const VaultPath &path);

/// A note's text without its encrypted fragments (never published): Meld Encrypt's markers
/// (`%%<U+1F510>α …<U+1F510>%%`, `<U+1F510>β …<U+1F510>` — the closed lock with a key — any
/// variant), fenced blocks whose language is `encrypted`,
/// `encrypt`, `crypt`, `secret`, `age` or `pgp`, and PGP / age armoured messages. Text around
/// them stays byte for byte.
struct StrippedText {
    QString text;
    int removed = 0; ///< fragments removed
    /// Nothing but frontmatter and blank lines is left of a note that had fragments: the page
    /// is encrypted as a whole (left out). Also a page whose frontmatter says `encrypted: true`.
    bool wholePage = false;
};
StrippedText stripEncrypted(const QString &content);

/// The pages and attachments of `scope` among the notes and files `index` knows, with what is
/// left out and why. The rules, in order: hidden paths and conflict copies never; encrypted pages
/// and files never; `publish: false` never; then the templates folder, extension pages and the
/// AI/ folder unless an item names the page itself (or the scope chooses them); onlyMarked. The
/// linked pages follow the same rules. Attachments: every non-page file a published page links
/// to or embeds (hidden and encrypted ones left out), and an Excalidraw drawing's picture beside it.
PublishSelection selectFiles(const VaultIndex &index, const PublishScope &scope);

} // namespace brulion::publish
