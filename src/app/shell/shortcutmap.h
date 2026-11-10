// ShortcutMap — the keyboard shortcuts of the commands (QML singleton `ShortcutMap`; docs/UI.md
// "Custom shortcuts"; Settings → Keyboard shortcuts, qml/shell/ShellShortcutSettings.qml).
//
// The actions are CommandModel's commands (its shortcutActions(): id, title, default shortcut)
// plus "palette" (the command palette itself). Every action may get a custom shortcut instead of
// its default — one key chord with Ctrl, Alt or Meta, or a function key — or none. Custom ones
// are a device setting (AppSettings group "shortcuts": id = portable text, "" = none).
//
// Shortcuts are portable text (QKeySequence::PortableText: "Ctrl+Shift+F", "Alt+Left", "F8").
// What a shortcut cannot be (check()): not one key chord ("Ctrl+K, Ctrl+S", an unknown key), a key
// that types text (letters, digits, Space without Ctrl / Alt / Meta), a key the editor, a drawing
// or the PDF viewer handle themselves (Ctrl+C, Ctrl+Z, Tab, Alt+↑…: reservedUse()), Ctrl+Alt with a
// letter that AltGr turns into a Polish one on Windows (Ctrl+Alt+A = ą…); another action's
// shortcut is a conflict: assign(…, takeOver) moves it here, the other action is left without one.
//
// Settings come from anywhere (hand edits, older versions): a stored value that is no string, is
// malformed or not allowed is ignored (the default applies); when two actions would share a
// shortcut, the custom one of the earlier action in the list keeps it — custom ones before
// defaults —, and an action whose default is taken has none.
//
// QML: `ShortcutMap.sequences[id]` binds a Shortcut's sequence (AppShell: one Shortcut per action;
// the views: find, replace, Markdown source, add property); the palette shows them (CommandModel).
#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::shell {

class AppSettings;

class ShortcutMap : public QObject {
    Q_OBJECT
    /// The shortcut of every action that has one: { id: "Ctrl+J", … }.
    Q_PROPERTY(QVariantMap sequences READ sequences NOTIFY changed FINAL)
    /// The same as a list [{ id, action (= id), sequence }].
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY changed FINAL)
    /// The bindings AppShell's Shortcut objects handle: without the views' own (find, replace,
    /// Markdown source, a page property: "page.find", "page.replace", "page.source",
    /// "page.addProperty") and block.copyRef on Ctrl+Shift+C, which the editor handles itself.
    Q_PROPERTY(QVariantList shellBindings READ shellBindings NOTIFY changed FINAL)
    /// How many actions have a custom shortcut (or none instead of their default).
    Q_PROPERTY(int customCount READ customCount NOTIFY changed FINAL)

public:
    struct Action {
        QString id;
        QString title;
        QString defaultSequence; ///< portable text, "" for none
    };

    explicit ShortcutMap(AppSettings *settings, QObject *parent = nullptr);

    /// The actions in their order (titles in the UI language), then the stored custom shortcuts
    /// are read again.
    void setActions(const QList<Action> &actions);
    const QList<Action> &actions() const { return m_actions; }

    QVariantMap sequences() const;
    QVariantList bindings() const;
    QVariantList shellBindings() const;
    int customCount() const { return int(m_custom.size()); }

    /// The shortcut `id` has now ("" for none or an unknown id).
    Q_INVOKABLE QString sequence(const QString &id) const;
    Q_INVOKABLE QString defaultSequence(const QString &id) const;
    /// `id` has a custom shortcut (or none instead of its default).
    Q_INVOKABLE bool isCustom(const QString &id) const;
    /// The action whose shortcut `sequence` is ("" for none).
    Q_INVOKABLE QString actionFor(const QString &sequence) const;
    Q_INVOKABLE QString title(const QString &id) const;

    /// Whether `sequence` can be `id`'s shortcut: { status, message, sequence (normalized),
    /// conflictId, conflictTitle }. status: "ok", "same" (it is `id`'s already), "invalid" (not
    /// one key chord), "typing" (a key that types text), "reserved" (the editor, a drawing or the
    /// PDF viewer use it; AltGr letters), "conflict" (another action's: conflictId), "unknown"
    /// (no such action). `message` says it in the UI language ("" for ok).
    Q_INVOKABLE QVariantMap check(const QString &id, const QString &sequence) const;
    /// Make `sequence` `id`'s shortcut. A conflict is refused unless `takeOver`: the other action
    /// then has none. False when refused (check() says why); stored at once.
    Q_INVOKABLE bool assign(const QString &id, const QString &sequence, bool takeOver = false);
    /// `id` gets no shortcut.
    Q_INVOKABLE void clear(const QString &id);
    /// `id` back to its default; an action that had taken that shortcut is left without one: its
    /// id, else "".
    Q_INVOKABLE QString reset(const QString &id);
    /// Every action back to its default.
    Q_INVOKABLE void resetAll();
    /// The settings' list: [{ id, action (= id), title, sequence, defaultSequence, custom }] of the
    /// actions whose title, id or shortcut contains `query` (case and diacritics ignored), in order.
    Q_INVOKABLE QVariantList entries(const QString &query = QString()) const;
    /// The shortcut of a key press (QML KeyEvent key and modifiers): "" for a modifier alone or a
    /// key Qt does not know.
    Q_INVOKABLE QString sequenceForKey(int key, int modifiers) const;

    /// One key chord as portable text ("ctrl+shift+f" -> "Ctrl+Shift+F"); "" for anything else.
    static QString normalize(const QString &text);
    /// Where `normalized` is used without being a command ("text", "blocks", "drawing", "pdf",
    /// "altgr"), "" when it is free.
    static QString reservedUse(const QString &normalized);
    /// `normalized` types text (no Ctrl, Alt or Meta, and no function key).
    static bool typesText(const QString &normalized);

Q_SIGNALS:
    void changed();

private:
    const Action *find(const QString &id) const;
    void load();
    void rebuild();
    void store(const QString &id);
    /// `id` without a shortcut (custom unless that is its default), stored.
    void setNone(const QString &id);

    AppSettings *m_settings;
    QList<Action> m_actions;
    /// Custom shortcuts: id -> portable text ("" = none).
    QHash<QString, QString> m_custom;
    /// The resolved shortcuts: id -> portable text (only those that have one).
    QHash<QString, QString> m_effective;
    QHash<QString, QString> m_owner; ///< portable text -> id
};

} // namespace brulion::shell
