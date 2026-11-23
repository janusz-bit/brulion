#pragma once
// ThemePages — the notebook's theme pages (docs/UI.md "Look & feel", docs/SYNTAX.md §13 "Theme
// pages"; core extensions/themepage.h): pages tagged #motyw / #theme whose ```theme blocks declare
// tokens of the theme. Settings → Appearance lists them next to the appearance settings and picks
// one; the picked page's tokens go to AppScripting as the theme's bottom layer (extensions' and live
// themes over it), in both modes, again whenever the page changes (an edit, a sync). QML singleton
// `ThemePages` (shellqml.h; also AppController::themePages()).
//
// Which page is picked: the notebook's choice (`themePage` in .brulion/config.json — synced, every
// device that follows it shows that theme), or this device's own (AppSettings
// "appearance/themePageOwn/<notebook id>" and "appearance/themePage/<notebook id>": a phone may keep
// a darker theme than the computer). A notebook's choice whose page is gone (deleted, not synced yet)
// applies nothing and says so (`missing`). A renamed or moved page stays picked: the notebook's
// choice follows it as a setting naming it (notebook::relocate), the device's here.
//
//   themes->pick(u"Motywy/Sepia.md"_s);       // the notebook's (or this device's, deviceOnly)
//   themes->pageState(path)                   // the page's chip: {theme, applied, tokens, issues}
//   const QString path = themes->exportCurrent();  // the look in force as a new theme page
//
// Single-threaded (the GUI thread).

#include "extensions/themepage.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace brulion::shell {

class AppController;

class ThemePages : public QObject {
    Q_OBJECT
    /// The notebook's theme pages by title: [{path, title, description, tokens, issues, current,
    /// lightBg, lightAccent, lightFg, darkBg, darkAccent, darkFg}] — the swatches as the page
    /// would show over the appearance settings in force.
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed FINAL)
    /// How many there are.
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
    /// The theme page in force on this device ("" none: the appearance settings alone).
    Q_PROPERTY(QString current READ current NOTIFY changed FINAL)
    /// The notebook's choice (config.json "themePage", "" none).
    Q_PROPERTY(QString notebookTheme READ notebookTheme NOTIFY changed FINAL)
    /// This device picks its own theme page instead of following the notebook's. Switched on, it
    /// keeps the page in force; switched off, the notebook's applies again.
    Q_PROPERTY(bool deviceOnly READ deviceOnly WRITE setDeviceOnly NOTIFY changed FINAL)
    /// The page picked is no theme page of this notebook (gone, renamed elsewhere, not synced yet).
    Q_PROPERTY(bool missing READ missing NOTIFY changed FINAL)
    /// Bumped on every change (the page chips follow it).
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)

public:
    explicit ThemePages(AppController *app, QObject *parent = nullptr);
    ~ThemePages() override;

    QVariantList pages() const;
    int count() const { return int(m_pages.size()); }
    QString current() const;
    QString notebookTheme() const;
    bool deviceOnly() const;
    void setDeviceOnly(bool on);
    bool missing() const;
    int revision() const { return m_revision; }

    /// Is `path` a theme page of the open notebook ("" counts: no theme page)?
    bool isThemePageOrNone(const QString &path) const;
    /// Picks the theme page `path` ("" none): the notebook's choice, or this device's with
    /// deviceOnly. False for a path that is no theme page (or the notebook's settings not saved).
    Q_INVOKABLE bool pick(const QString &path);
    /// The page's chip (PageView): {theme (it is a theme page), applied (in force), tokens, blocks,
    /// issues: [{line (0-based), text (translated)}]} — {} for any other page.
    Q_INVOKABLE QVariantMap pageState(const QString &path) const;
    /// The same, one typed value each (compiled QML reads no member of a map).
    Q_INVOKABLE bool isThemePage(const QString &path) const { return m_pages.contains(path); }
    Q_INVOKABLE bool pageApplied(const QString &path) const;
    Q_INVOKABLE int pageTokens(const QString &path) const;
    Q_INVOKABLE int pageBlocks(const QString &path) const;
    Q_INVOKABLE int pageIssueCount(const QString &path) const;
    /// The first ignored line (0-based, -1 none) and why, translated.
    Q_INVOKABLE int pageIssueLine(const QString &path) const;
    Q_INVOKABLE QString pageIssueText(const QString &path) const;
    /// The look in force — the appearance settings' colours in both modes, the interface and code
    /// fonts, every override of a theme page, an extension or a live client — as a new theme page
    /// in the folder "Themes" ("Motywy"): its path, "" when it could not be made.
    Q_INVOKABLE QString exportCurrent();

    /// Reads the notebook's theme pages again (after the index is ready; tests).
    void rescan();

Q_SIGNALS:
    void changed();

private:
    struct Entry {
        QString title;
        QString description;
        extensions::ThemePage page;
    };
    void reset();
    void filesChanged(const QStringList &paths);
    /// Reads `path` again: true when what the list or the theme shows changed.
    bool update(const QString &path);
    void apply();
    void bump();
    QString notebookId() const;
    QString ownKey() const;
    QString pageKey() const;
    QString issueText(const extensions::ThemeIssue &issue) const;

    AppController *m_app;
    QHash<QString, Entry> m_pages;
    bool m_scanned = false;
    int m_revision = 0;
};

} // namespace brulion::shell
