#pragma once
// AppScripting — the running app as extensions and AI agents change it (docs/EXTENSIONS.md "The
// app (`b.app`)", docs/CLI.md "Live changes"; QML: AppController.scripting). One object per app,
// made by AppController:
//
// * b.app's operations (core extensions/appapi.h appOperations(), Host::app): the app's state, its
//   palette commands, the device's and the notebook's settings an extension may read and change
//   (registries: settingSpecs(), notebookSettingSpecs()), navigation, a command run, a message. What moves the UI (navigate, open, back,
//   run, toast) runs after the extension's call returns, from the event loop: never inside a Luau
//   call (a palette command may run an extension's command).
// * The theme's overrides: the theme page picked in Settings → Appearance (ThemePages) at the
//   bottom, the extensions' layers (b.app.theme, Engine::themes(), in their order) over it and those of live clients (the remote control's `theme`, an agent's), merged and handed to
//   ui::ThemeOverrides, which Theme.qml's tokens read — a live layer over the extensions'.
// * The items extensions put into the UI (b.app.contribute): activate() does what one does when
//   it is clicked.
// * Live changes: every setting a live client or an extension changes and every theme a live
//   client sets is in a journal (changes()) with its value before, so revert() puts the app back
//   as it was — all of it, or one client's.
//
//   QVariant v = scripting->call(u"moje"_s, u"set"_s, {{u"key"_s, u"density"_s}, {u"value"_s, u"compact"_s}}, &error);
//   scripting->setLiveTheme(u"live:claude"_s, layer);   // the remote control's theme
//   scripting->revert();                                // every live change undone
//
// Single-threaded (the GUI thread).

#include "extensions/appapi.h"
#include "extensions/engine.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace brulion::shell {

class AppController;

/// A device setting extensions and agents may read and change (AppScripting::settingSpecs()).
struct SettingSpec {
    QString key;             ///< b.app.get's name ("density")
    QString group;           ///< "appearance" | "layout" | "editor" | "extensions" | "reminders" | "app"
    QString kind;            ///< "toggle" | "choice" | "number" | "text" | "color" | "list"
    QString property;        ///< the Q_PROPERTY of AppSettings (or of AppController: `onApp`)
    QStringList options;     ///< choice: its values; list: the values it may hold
    double min = 0, max = 0; ///< number
    bool integer = false;    ///< number: whole numbers only
    bool onApp = false;      ///< the property is AppController's, not AppSettings'
    /// Applied after the call returns (switching a built-in extension reloads the engine).
    bool deferred = false;
    int listSize = 0; ///< list: exactly this many values (0: any number)
    QString label;    ///< what it is, in English
    /// A setting of the notebook (.brulion/config.json, synced), not of this device: `property` is
    /// its key there ("journalsFolder", "flashcards.tags" inside the "flashcards" object).
    bool notebook = false;
};

class AppScripting : public QObject {
    Q_OBJECT
    /// Counts the live changes (changes()): the top bar's live indicator follows it.
    Q_PROPERTY(int liveRevision READ liveRevision NOTIFY liveChanged FINAL)
    /// Who changed the app live and has not been reverted: [{who, changes}] (the indicator's menu).
    Q_PROPERTY(QVariantList liveClients READ liveClients NOTIFY liveChanged FINAL)

public:
    explicit AppScripting(AppController *app, QObject *parent = nullptr);
    ~AppScripting() override;

    /// What the UI knows and the app does not (main(): the window, the effective theme):
    /// {dark, touch, narrow, width, height}. Unset (tests): light, no window.
    void setUiState(std::function<QVariantMap()> ui) { m_ui = std::move(ui); }
    /// The live client that loaded an extension ("live:…", "" none: WorkspaceExtensions::liveOwner):
    /// what that live code changes is that client's live change.
    void setLiveOwner(std::function<QString(const QString &extension)> owner) { m_liveOwner = std::move(owner); }

    /// Host::app for `who` (an extension's id, or "live:<client>"): appapi.h appOperations().
    QVariant call(const QString &who, const QString &operation, const QVariantMap &args, QString *error);

    // ---- settings -------------------------------------------------------------------------------
    /// The device's settings (AppSettings, AppController).
    static const QList<SettingSpec> &settingSpecs();
    /// The notebook's own settings (.brulion/config.json through Workspace.setConfig): its folders,
    /// the journal's file name and template, the weekly and monthly notes, the note modes, the
    /// reminders' time, the week's first day, the flashcards' card settings, the theme page.
    static const QList<SettingSpec> &notebookSettingSpecs();
    /// A device's or the notebook's setting (their keys never clash).
    static const SettingSpec *settingSpec(const QString &key);
    /// [{key, scope ("device" | "notebook"), group, kind, label, value, options?, min?, max?}]: the
    /// device's in the registry's order, then the notebook's while one is open.
    QVariantList settings() const;
    /// The value of `key`; `*error` for an unknown one (with the nearest key).
    QVariant setting(const QString &key, QString *error) const;
    /// Sets `key` to `value` (checked against its kind, normalized: "2" is no number, a choice is
    /// one of its options); `who` is noted in the journal. False and `*error` when it does not fit.
    bool setSetting(const QString &who, const QString &key, const QVariant &value, QString *error);

    // ---- the theme ------------------------------------------------------------------------------
    /// The extensions' layers (WorkspaceExtensions::themes(), after every change).
    void setExtensionThemes(const QList<extensions::ThemeInfo> &themes);
    /// A live client's layer (an empty one removes it), over the extensions'; noted in the journal.
    void setLiveTheme(const QString &who, const extensions::ThemeLayer &layer);
    /// The theme page in force (ThemePages: `path`, "" none) — the bottom layer, under the
    /// extensions' and the live ones: a look the user picked, which code may still change.
    void setPageTheme(const QString &path, const extensions::ThemeLayer &layer);
    /// The theme page's checker of b.app.set("themePage", path): true for "" and a theme page.
    void setThemePageCheck(std::function<bool(const QString &path)> check) { m_themePageCheck = std::move(check); }
    /// {page?: {path, tokens}, extensions: [{extension, tokens}], live: [{who, tokens}], light: {…},
    /// dark: {…}} — the layers and the tokens in force in each mode.
    QVariantMap theme() const;

    // ---- the items of the UI (b.app.contribute) --------------------------------------------------
    /// Does what the item `id` of `extension` in `slot` does — its command (on the block at
    /// `line` of `path` in a block's menu, the slash menu and the keyboard's toolbar: ctx.block;
    /// else `path` is ctx.page), view, page or palette command; for `insert` nothing here (the
    /// editor inserts it). `context`: the slot's own fields — the folder tree's row {path,
    /// folder?}; the keyboard toolbar's {selection} (ctx.selection); a share action's {text} (the shared text as
    /// edited, `path` ""), which becomes ctx.shared with the share waiting (AppController::sharedForExtensions()), or,
    /// with `path`, the page shared from Brulion as ctx.shared. False when the item is gone.
    Q_INVOKABLE bool activate(const QString &slot, const QString &extension, const QString &id,
                              const QString &path = QString(), int line = -1,
                              const QVariantMap &context = QVariantMap());

    // ---- live changes ----------------------------------------------------------------------------
    int liveRevision() const { return m_liveRevision; }
    QVariantList liveClients() const;
    /// [{at, who, kind ("setting" | "theme"), key, before, after}], oldest first.
    QVariantList changes() const;
    /// Undoes the changes of `who` ("" = everyone), newest first: settings back to their value
    /// before, live themes removed. The extensions `who` loaded live are unloaded by the caller.
    /// The number of changes undone.
    Q_INVOKABLE int revert(const QString &who = QString());
    /// Notes a live change made elsewhere (an extension loaded live: kind "extension") so the
    /// indicator shows it and revert() counts it; `undo` runs on revert.
    void noteLive(const QString &who, const QString &kind, const QString &key, std::function<void()> undo);
    /// Forgets the live changes of `kind` and `key` without undoing them (a live extension its
    /// client took out again: nothing is left to undo). The number forgotten.
    int forgetLive(const QString &kind, const QString &key);

Q_SIGNALS:
    void liveChanged();

private:
    struct Change {
        QDateTime at;
        QString who;
        QString kind; ///< "setting" | "theme" | "extension"
        QString key;
        QVariant before;
        QVariant after;
        std::function<void()> undo; ///< "extension": what revert() runs
    };
    QVariant read(const SettingSpec &spec) const;
    bool write(const SettingSpec &spec, const QVariant &value);
    QVariantMap state() const;
    void applyTheme();
    void later(std::function<void()> f);
    static bool isLive(const QString &who) { return who.startsWith(QStringLiteral("live:")); }

    AppController *m_app;
    std::function<QVariantMap()> m_ui;
    std::function<QString(const QString &)> m_liveOwner;
    std::function<bool(const QString &)> m_themePageCheck;
    QString m_pageThemePath;
    extensions::ThemeLayer m_pageTheme;
    QList<extensions::ThemeInfo> m_extensionThemes;
    QList<std::pair<QString, extensions::ThemeLayer>> m_liveThemes; // in the order set
    QList<Change> m_changes;
    int m_liveRevision = 0;
};

} // namespace brulion::shell
