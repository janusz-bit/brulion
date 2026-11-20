#pragma once
// Macros of extensions as the app shows them (docs/EXTENSIONS.md "Macros"): what a macro's value
// becomes in the text (macroViewFrom: a text, a number, b.ui.text / b.ui.link nodes or a list of
// them — bounded, never HTML), and the cache of computed macros (MacroCache): each value with what
// its call read of the notebook (Engine's Reads), stale when one of those notes changes, its
// extension loads again or switches off — shown as it was until its new value is there. GUI-free:
// the app's WorkspaceExtensions runs the calls, the editor's renderer (markdown::RenderContext::
// macro) asks for the views.
//
//   MacroCache cache;
//   const QString key = MacroCache::keyOf({u"silownia"_s, u"rekord"_s, u"Przysiad"_s, path});
//   if (cache.needsRun(key)) { …engine.runMacro… cache.store(key, request, view, reads, nowMs); }
//   cache.view(key)                      // what the text shows now (nullopt: nothing yet)
//   cache.notesChanged({u"Siłownia.md"_s}, nowMs)   // the keys that went stale

#include "extensions/engine.h"
#include "markdown/macros.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

namespace brulion::extensions {

/// Bounds of a macro's output (docs/EXTENSIONS.md "Sandbox and limits").
inline constexpr int MaxMacroPieces = 32;
inline constexpr int MaxMacroCharacters = 400; ///< all its pieces' texts together; more is cut with "…"
inline constexpr int MacroTimeoutMs = 250;     ///< per call
inline constexpr int MaxMacrosPerPage = 200;   ///< distinct macros a note computes; the rest say "too many"
inline constexpr int MacroBudgetMs = 2000;     ///< macro calls per page and round of changes, all together

/// What a macro's value shows: a text, a number (as b.formatNumber writes it: `comma` in a
/// Polish notebook), a boolean, b.ui.text (muted, tone), b.ui.link (page, label), b.ui.heading
/// (bold), or a list or b.ui.row of those; nil shows nothing. Anything else — a table of
/// another kind, a node of another kind — is an Error view with `error` saying so. Line breaks
/// are blanks; at most MaxMacroPieces pieces and MaxMacroCharacters characters.
markdown::MacroView macroViewFrom(const QVariant &value, bool comma, QString *error = nullptr);

/// A template variable's value as the text it puts into the note (a text as written, a number as
/// macroViewFrom writes it, a boolean, nil: ""); nullopt for anything else. At most 20 000
/// characters.
std::optional<QString> variableTextFrom(const QVariant &value, bool comma);

/// What a macro is asked for: its extension, name, arguments (as written) and the note it is in.
struct MacroRequest {
    QString extension;
    QString name;
    QString arguments;
    QString path; ///< the note's path in the notebook ("" none)
    bool operator==(const MacroRequest &other) const = default;
};

class MacroCache {
public:
    static QString keyOf(const MacroRequest &request);

    /// What the macro shows now: the last value computed (also while it is stale), nullopt
    /// before the first.
    std::optional<markdown::MacroView> view(const QString &key) const;
    /// It has to be computed: never yet, or stale — and not given up on (failures()).
    bool needsRun(const QString &key) const;
    bool isStale(const QString &key) const;
    bool contains(const QString &key) const { return m_entries.contains(key); }
    /// The request a key was made of (stored or asked for).
    std::optional<MacroRequest> request(const QString &key) const;

    /// Asked for (the renderer showed it): kept while it is asked for; returns false when it is new
    /// and the note has MaxMacrosPerPage other macros already (it is not kept then).
    bool ask(const QString &key, const MacroRequest &request, qint64 nowMs);
    /// A value computed: kept with what it read; a failed call (`failed`) counts towards giving up.
    /// True when what it shows changed.
    bool store(const QString &key, const markdown::MacroView &view, const Reads &reads, bool failed, qint64 nowMs);
    /// Consecutive failures that timed out (a macro given up on after two: not run again until its
    /// extension loads again).
    int timeouts(const QString &key) const;
    void countTimeout(const QString &key);

    /// Notes changed: the keys whose call read one of them (or everything, or a name one of them
    /// now has) — they go stale.
    QStringList notesChanged(const QStringList &paths);
    /// An extension loaded again, or was switched on or off: its macros go stale (their timeouts
    /// forgotten). Returns their keys.
    QStringList extensionChanged(const QString &extension);
    /// Every macro goes stale (extensions were loaded again): their keys.
    QStringList markAllStale();
    /// The stale or never computed keys asked for since `sinceMs`, the most recently asked first.
    QStringList pending(qint64 sinceMs) const;
    /// The keys of a note's macros (the per-page bound, the budget).
    QStringList keysOf(const QString &path) const;
    /// Forgets the entries not asked for since `beforeMs`, then the oldest beyond `maxEntries`.
    void prune(qint64 beforeMs, int maxEntries = 4000);
    void clear() { m_entries.clear(); }
    int size() const { return int(m_entries.size()); }

private:
    struct Entry {
        MacroRequest request;
        std::optional<markdown::MacroView> view;
        Reads reads;
        bool stale = true;
        int timeouts = 0;
        qint64 askedMs = 0;
    };
    QHash<QString, Entry> m_entries;
};

} // namespace brulion::extensions
