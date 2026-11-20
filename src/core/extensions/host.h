#pragma once
// What an extension may ask of Brulion (the `b` API's functions that need the notebook or the
// device): the app implements it over its Workspace, tests over a fake, the engine (engine.h)
// calls it. Every function is called on the engine's thread, inside an extension's call; writes
// return an error message ("" = done) that the extension sees as a script error.
//
// A block (blocks(), the commands' ctx.block) is a QVariantMap:
//   ref      "Dziennik/2026-11-11.md#L3@1a2b3c4d" — notebook::ItemRef text; stale after a change
//   page     the page's name (its title), date "2026-11-11" for a daily note else absent
//   text     the first line without indentation, marker, checkbox and properties
//   task     " ", "x", "-", "/"… (the status character, a Logseq keyword mapped) or absent
//   tags     lower-cased, without "#"; props: {key: value} (keys lower-cased); children: blocks

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

namespace brulion::extensions {

struct BlockQuery
{
    QString page; ///< non-empty: that page's top-level blocks (each with its subtree)
    QString tag; ///< non-empty: every block carrying the tag (each with its subtree)
    bool journalOnly = false; ///< only blocks of daily notes
    QString from, to; ///< ISO dates, inclusive, for daily notes ("" = open)
    /// Non-empty: every block with one of these properties (lower-case keys) among its own — with
    /// `tag` the blocks that have both; with `page` only that page's (then at any depth).
    QStringList props;
};

/// b.pages: the pages whose own tags have `tag` ("" = any) and, when `props` is not empty, whose
/// page properties (frontmatter or leading `key:: value` lines) have one of these keys.
struct PageQuery
{
    QString tag;
    QStringList props;
    /// Their paths in the notebook ("Projekty/Dom.md"), sorted, instead of their names: two pages of
    /// one name in different folders are two paths (every function that takes a page takes one).
    bool paths = false;
};

/// b.require: the code of an extension page another extension uses (docs/EXTENSIONS.md "Shared
/// code"), or why there is none (`error`).
struct ModuleSource
{
    QString id; ///< the page's extension id ("wspolne-funkcje")
    QString title; ///< as errors name it ("Wspólne funkcje")
    QString path; ///< the page in the notebook
    QString source; ///< its code with the page's line numbers (ExtensionCode::source)
    QString hash; ///< PageExtension::hash: what was approved
    QString error; ///< "" or why it cannot be used ("waits for approval on this device")
};

/// b.timer: an extension's own timer (one per extension: a new one replaces it).
struct TimerSpec
{
    int seconds = 0; ///< 1 … 86 400
    QString label; ///< what it times ("Herbata"), at most 100 characters; "" = the app's word
    QString message; ///< what its end says ("Herbata gotowa."), at most 300 characters; "" = the app's
};

/// Where b.insertBlock puts new lines (blockedit.h Placement).
enum class InsertAt { Before, After, Child };

class Host
{
public:
    virtual ~Host() = default;

    // Reading
    virtual QStringList pages(const QString &tag) = 0; ///< page names, sorted; tag "" = every page
    virtual std::optional<QString> read(const QString &page) = 0;
    /// {name, tags, props} of an existing page, else an empty map.
    virtual QVariantMap page(const QString &page) = 0;
    virtual QVariantList blocks(const BlockQuery &query) = 0;
    /// Full-text search over every note (VaultIndex::search: diacritics optional, prefixes, fuzzy),
    /// the best first, at most `limit`: {page, date?, excerpt, line? (1-based; none for a match in
    /// the title), ref?, text?, task?} — ref, text and task of the block holding that line.
    virtual QVariantList search(const QString &query, int limit) = 0;
    /// A query of the query language (docs/SYNTAX.md §10: "type:tasks status:todo due:before:today
    /// tag:dom"): {total, items}, at most `limit` items — blocks as blocks() gives them, without
    /// children, with `due`, `scheduled`, `start` of a task and `reminder` ("2026-11-12 09:00") of
    /// a reminder; {page, date?} for pages. `error`: why `source` is no query.
    virtual QVariantMap query(const QString &source, int limit, QString *error) = 0;
    /// query() from item `offset` on (0 = the first): `total` counts them all. The default asks
    /// query() for offset + limit items and drops the first.
    virtual QVariantMap queryPage(const QString &source, int limit, int offset, QString *error)
    {
        QVariantMap result = query(source, limit + offset, error);
        if (offset > 0 && result.contains(QStringLiteral("items")))
            result.insert(QStringLiteral("items"), result.value(QStringLiteral("items")).toList().mid(offset, limit));
        return result;
    }
    /// pages() with a property filter (PageQuery); the default knows tags only.
    virtual QStringList findPages(const PageQuery &query)
    {
        return query.props.isEmpty() ? pages(query.tag) : QStringList();
    }
    /// The notebook's files that are no notes (pictures, PDFs …): paths in the notebook, sorted.
    virtual QStringList attachments() { return {}; }
    /// A number that changes whenever the notes change (the index's version and the host's own
    /// writes): a script may keep what it computed until it differs.
    virtual qint64 revision() { return 0; }
    /// b.checkExtension: `content` loaded as the extension page `page` would be, in a sandbox of its
    /// own (sandbox.h: {ok, error, line, commands, views, hooks, …}). A host without a notebook
    /// cannot: the default says so.
    virtual QVariantMap checkExtension(const QString &page, const QString &content)
    {
        Q_UNUSED(page);
        Q_UNUSED(content);
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("extensions cannot be checked here")}};
    }
    /// b.require: the extension page `name` (a page name as in a link) whose code `requester` runs
    /// in an environment of its own, with the requester's `b` — its permissions, never the page's.
    /// Only an extension page, and only one this device lets run (approved, not switched off).
    virtual ModuleSource module(const QString &requester, const QString &name)
    {
        Q_UNUSED(requester);
        ModuleSource none;
        none.error = QStringLiteral("no extension page named “%1”").arg(name);
        return none;
    }

    // An extension's own settings (b.settings, b.setting, b.setSetting): its values on this
    // device, by key — booleans, numbers, texts. The default keeps nothing.
    virtual QVariantMap settings(const QString &extension)
    {
        Q_UNUSED(extension);
        return {};
    }
    /// Stores `value` (a null QVariant removes the key); an error message, "" = done.
    virtual QString setSetting(const QString &extension, const QString &key, const QVariant &value)
    {
        Q_UNUSED(extension);
        Q_UNUSED(key);
        Q_UNUSED(value);
        return QStringLiteral("settings are not kept here");
    }

    // Writing (permission "write"; the engine checks it before calling)
    virtual QString write(const QString &extension, const QString &page, const QString &content) = 0;
    virtual QString appendToJournal(const QString &extension, const QString &date, const QString &markdown) = 0;
    /// Replaces the first line's text (and the checkbox's status when `task` is set) of the block
    /// `ref` names; refused ("stale: …") when the block changed since it was read. `newRef`: the
    /// block's reference after the change.
    virtual QString replaceBlock(const QString &extension, const QString &ref, const QString &text,
                                 const std::optional<QString> &task, QString *newRef) = 0;
    /// Appends `markdown` as the last block of the page (made where new pages go when missing).
    virtual QString appendToPage(const QString &extension, const QString &page, const QString &markdown)
    {
        Q_UNUSED(extension);
        Q_UNUSED(page);
        Q_UNUSED(markdown);
        return QStringLiteral("not available here");
    }
    /// Inserts `markdown` before, after or under the block `ref` names (stale as replaceBlock);
    /// `newRef`: the first new line's reference.
    virtual QString insertBlock(const QString &extension, const QString &ref, const QString &markdown, InsertAt where,
                                QString *newRef)
    {
        Q_UNUSED(extension);
        Q_UNUSED(ref);
        Q_UNUSED(markdown);
        Q_UNUSED(where);
        Q_UNUSED(newRef);
        return QStringLiteral("not available here");
    }
    /// Removes the block `ref` names with its children.
    virtual QString removeBlock(const QString &extension, const QString &ref)
    {
        Q_UNUSED(extension);
        Q_UNUSED(ref);
        return QStringLiteral("not available here");
    }
    /// Sets the checkbox of the block `ref` names to `status` (one character), its text untouched.
    virtual QString setTask(const QString &extension, const QString &ref, const QString &status, QString *newRef)
    {
        Q_UNUSED(extension);
        Q_UNUSED(ref);
        Q_UNUSED(status);
        Q_UNUSED(newRef);
        return QStringLiteral("not available here");
    }
    /// Sets (an empty value: removes) the property `key` of the block `ref` names.
    virtual QString setProperty(const QString &extension, const QString &ref, const QString &key,
                                const QString &value, QString *newRef)
    {
        Q_UNUSED(extension);
        Q_UNUSED(ref);
        Q_UNUSED(key);
        Q_UNUSED(value);
        Q_UNUSED(newRef);
        return QStringLiteral("not available here");
    }

    // The device
    virtual QString today() = 0; ///< "2026-11-11", local wall-clock date
    /// "2026-11-11 14:05", local wall-clock time; the default: today at midnight.
    virtual QString now() { return today() + QStringLiteral(" 00:00"); }
    /// The notebook's first day of the week, 1 = Monday … 7 = Sunday.
    virtual int weekStart() { return 1; }
    virtual QString language() = 0; ///< the notebook's language for written text: "pl" | "en"
    virtual QString uiLanguage() = 0; ///< the app's interface language: "pl" | "en"
    virtual void timer(const QString &extension, int seconds, const QString &label) = 0;
    /// b.timer with what its end says; the default is timer() (a host that shows no message).
    virtual void startTimer(const QString &extension, const TimerSpec &spec)
    {
        timer(extension, spec.seconds, spec.label);
    }
    /// b.stopTimer: the extension's timer ends without a word.
    virtual void stopTimer(const QString &extension) { Q_UNUSED(extension); }
    virtual void log(const QString &extension, const QString &message) = 0;
    /// b.status: what the running call does now ("" = nothing to say), for the user to see while
    /// it waits (the chat shows it under the AI's dots). A host that shows nothing ignores it.
    virtual void status(const QString &extension, const QString &text)
    {
        Q_UNUSED(extension);
        Q_UNUSED(text);
    }
};

} // namespace brulion::extensions
