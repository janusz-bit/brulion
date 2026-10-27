// SuggestionModel — items of the editor's autocomplete popup (docs/UI.md "Autocomplete popup"):
//   pages  `[[` — "create page" first when no page or alias has the typed name (so it is always in
//          view, on top of `limit` pages), then EditorHost::suggestPages
//   blocks `((` — EditorHost::suggestBlocks (ids are created when one is accepted)
//   tags   `#`  — EditorHost::suggestTags: tags in use, then pages (a tag names a page)
//   slash  `/`  — the slash commands (Polish + English names, diacritic-insensitive filter)
//   date   after `📅 ⏳ 🛫 ⏰ ⏰!` or `@` — natural-language dates (dates::parseNaturalDate,
//          Polish + English) offered as canonical "YYYY-MM-DD[ HH:mm]" values; `@` also offers
//          the journal page link and a reminder
//   templates — EditorHost::templates (for the "Szablon…" command)
//
// Usage from QML: `detect(text, cursor)` gives the trigger at the caret ({ mode, filter, from,
// to, token, closing }); set `mode`, `filter` and `token` from it; `get(i)` returns an item for
// PageController.applyCompletion(row, trigger, item). `defaultIndex` is the row Enter/Tab takes
// before the arrows move: the best existing match, not "create page" above it.
//
// Roles: title, subtitle, icon, insertText, kind ("page" | "alias" | "create" | "block" | "tag" |
// "command" | "date" | "journal" | "reminder" | "template"), hint (syntax preview, right-aligned),
// command (slash command id), path, line, blockId.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace brulion {
class EditorHost;
}

namespace brulion::editor {

class SuggestionModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged FINAL)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged FINAL)
    /// The trigger token of date mode ("⏰", "⏰!", "📅", "⏳", "🛫", "@").
    Q_PROPERTY(QString token READ token WRITE setToken NOTIFY tokenChanged FINAL)
    /// "pl" (default) or "en": names of slash commands, date descriptions.
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// Fixed clock "YYYY-MM-DD HH:mm" (tests, screenshots); empty = the real clock.
    Q_PROPERTY(QString now READ now WRITE setNow NOTIFY nowChanged FINAL)
    /// The page being edited ("create page" suggestions, block refs within the page).
    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY currentPathChanged FINAL)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// The row highlighted when the items change (Enter/Tab take it): the first existing match —
    /// row 1 when "create page" leads the list and a page follows —, else 0. Set before
    /// modelReset() ends, so a view resetting its highlight reads the new value.
    Q_PROPERTY(int defaultIndex READ defaultIndex NOTIFY defaultIndexChanged FINAL)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        IconRole,
        InsertTextRole,
        KindRole,
        HintRole,
        CommandRole,
        PathRole,
        LineRole,
        BlockIdRole,
    };
    Q_ENUM(Role)

    explicit SuggestionModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QString mode() const { return m_mode; }
    void setMode(const QString &mode);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    QString token() const { return m_token; }
    void setToken(const QString &token);
    QString locale() const { return m_english ? QStringLiteral("en") : QStringLiteral("pl"); }
    void setLocale(const QString &locale);
    QString now() const { return m_fixedNow; }
    void setNow(const QString &now);
    QString currentPath() const { return m_currentPath; }
    void setCurrentPath(const QString &path);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    int count() const { return int(m_items.size()); }
    int defaultIndex() const { return m_defaultIndex; }

    /// The completion trigger at `cursor` of `text` ({ mode: "" } when none), see
    /// brulion::editor::completionTriggerAt.
    Q_INVOKABLE QVariantMap detect(const QString &text, int cursor) const;
    /// Set mode, filter and token from a detect() result in one go (one refresh).
    Q_INVOKABLE void setTrigger(const QVariantMap &trigger);
    /// Item `row` as a map (all roles by name); empty for an invalid row.
    Q_INVOKABLE QVariantMap get(int row) const;
    /// Recompute the items (e.g. after the index changed).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void hostChanged();
    void modeChanged();
    void filterChanged();
    void tokenChanged();
    void localeChanged();
    void nowChanged();
    void currentPathChanged();
    void limitChanged();
    void countChanged();
    void defaultIndexChanged();

private:
    QDateTime currentDateTime() const;
    QVariantList pageItems() const;
    QVariantList blockItems() const;
    QVariantList tagItems() const;
    QVariantList slashItems() const;
    QVariantList dateItems() const;
    QVariantList templateItems() const;

    QPointer<EditorHost> m_host;
    QString m_mode;
    QString m_filter;
    QString m_token;
    bool m_english = false;
    QString m_fixedNow;
    QString m_currentPath;
    int m_limit = 12;
    QVariantList m_items;
    int m_defaultIndex = 0;
};

} // namespace brulion::editor
