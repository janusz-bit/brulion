// Obsidian Bases (docs/SYNTAX.md §10 "Obsidian Bases"): `.base` files and ```base blocks — YAML
// with filters (and / or / not of expressions), formulas, property display names, custom
// summaries and views — read as Obsidian reads them and run through the views of views/view.h:
// table, cards (a gallery), list, kanban (a board); a map is shown as a table with a note, a
// plugin's view type likewise. Expressions are Bases' JavaScript-like language
// (dataview/expression.h, Syntax::Bases). Changing a view's columns or sort in the app writes the
// view's `order:` / `sort:` back — those lines only, the rest of the YAML byte for byte (verified by
// reading the result). Part of the views module — see views/view.h.
//
//   filters:
//     and:
//       - file.hasTag("książka")
//       - 'status != "przeczytana"'
//   formulas:
//     cena_brutto: "cena * 1.23"
//   views:
//     - type: table
//       name: Do przeczytania
//       order: [file.name, autor, formula.cena_brutto]
//       sort:
//         - property: autor
//           direction: ASC
//       summaries:
//         formula.cena_brutto: Sum
//
// Limits: a base comes from anywhere (sync, a shared vault): its filters nest at most 64 levels,
// its YAML is at most MaxBaseBytes, and its expressions keep Dataview's limits (expression depth
// and nesting, value sizes, a step budget shared by the whole run: a base too costly stops with
// a message instead of freezing the app).
#pragma once

#include "dataview/expression.h"
#include "views/view.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>
#include <variant>

namespace brulion::views {

/// The largest base read (a .base file or a ```base block): 1 MiB of YAML.
inline constexpr qsizetype MaxBaseBytes = 1024 * 1024;
/// Filter groups nested in one another at most.
inline constexpr int MaxBaseFilterDepth = 64;

struct BaseFilter {
    enum class Kind { Statement, And, Or, Not };
    Kind kind = Kind::Statement;
    QString statement;               ///< Statement: as written
    dataview::Expression expression; ///< Statement
    QList<BaseFilter> children;      ///< And / Or / Not
};

struct BaseSort {
    QString property; ///< as the base names it ("file.name", "note.autor", "formula.x", "autor")
    bool descending = false;

    friend bool operator==(const BaseSort &, const BaseSort &) = default;
};

struct BaseView {
    QString type; ///< as written: "table", "cards", "list", "kanban", "map", a plugin's
    ViewKind kind = ViewKind::Table;
    QString name;
    std::optional<BaseFilter> filters;
    QStringList order;   ///< the columns (property ids)
    QList<BaseSort> sort;
    std::optional<BaseSort> groupBy;
    std::optional<QStringList> groupOrder; ///< shown groups in order; "" stands for null (no value)
    int limit = 0;                         ///< 0: no limit of its own
    QList<std::pair<QString, QString>> summaries; ///< property id -> summary name
    QString image;  ///< cards / kanban: the image property
    QString notice; ///< what Brulion shows differently (a map as a table, …), translated
};

struct Base {
    std::optional<BaseFilter> filters;
    /// Formulas by canonical name, dependencies first (formulas referring to themselves are left
    /// out: a problem says so).
    QList<std::pair<QString, dataview::Expression>> formulas;
    QHash<QString, QString> displayNames; ///< property id (baseProperty()) -> display name
    QList<std::pair<QString, dataview::Expression>> summaries; ///< custom summaries by name
    QList<BaseView> views;
    QStringList problems; ///< what is left out (translated): formulas, filters, summaries that do not parse
};

/// Read a base (`yaml`: a .base file's text or a ```base block's body). A translated message for
/// text that is no YAML mapping or is too large; every other mistake (an expression that does not
/// parse, an unknown view type) is a problem or a notice and the rest still works. A base without
/// views gets one table.
std::variant<Base, QString> parseBase(const QString &yaml);

/// Run view `view` (0-based; out of range: the first) of `base`. `options.currentPath`: the page
/// showing it (`this`) — the .base file itself, or the note embedding it. Rows: every file of the
/// notebook (attachments too, with their file properties) the filters keep.
std::variant<ViewResult, QString> runBase(const Base &base, int view, const VaultIndex &index,
                                          const ViewOptions &options);

/// The view named `name` (`![[Books.base#Reading]]`, case-insensitive), else 0.
int baseViewNamed(const Base &base, const QString &name);

/// A property id in its full form: "status" -> "note.status"; "file.…", "formula.…", "note.…" kept.
QString baseProperty(const QString &id);

/// `yaml` with view `view`'s `order:` set to `order` (a block list at the view's indentation;
/// added when missing), every other line byte for byte; nullopt when the YAML cannot be changed
/// safely (flow-style views, a view that is not a mapping, a result that does not read back).
std::optional<QString> setBaseViewOrder(const QString &yaml, int view, const QStringList &order);
/// The same for `sort:` (a list of { property, direction: ASC | DESC }; an empty list removes it).
std::optional<QString> setBaseViewSort(const QString &yaml, int view, const QList<BaseSort> &sort);

} // namespace brulion::views
