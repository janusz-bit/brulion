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
};

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

    // Writing (permission "write"; the engine checks it before calling)
    virtual QString write(const QString &extension, const QString &page, const QString &content) = 0;
    virtual QString appendToJournal(const QString &extension, const QString &date, const QString &markdown) = 0;
    /// Replaces the first line's text (and the checkbox's status when `task` is set) of the block
    /// `ref` names; refused ("stale: …") when the block changed since it was read. `newRef`: the
    /// block's reference after the change.
    virtual QString replaceBlock(const QString &extension, const QString &ref, const QString &text,
                                 const std::optional<QString> &task, QString *newRef) = 0;

    // The device
    virtual QString today() = 0; ///< "2026-11-11", local wall-clock date
    virtual QString language() = 0; ///< the notebook's language for written text: "pl" | "en"
    virtual QString uiLanguage() = 0; ///< the app's interface language: "pl" | "en"
    virtual void timer(const QString &extension, int seconds, const QString &label) = 0;
    virtual void log(const QString &extension, const QString &message) = 0;
};

} // namespace brulion::extensions
