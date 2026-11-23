#include "themepages.h"

#include "appcontroller.h"
#include "appscripting.h"
#include "appsettings.h"
#include "workspace.h"
#include "workspaceextensions.h"

#include "index/vaultindex.h"
#include "log/categories.h"
#include "notebook/pages.h"
#include "themepalette.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDate>
#include <QFontDatabase>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

using logging::lcApp;

namespace {

bool fontInstalled(const QString &family)
{
    return QFontDatabase::hasFamily(family.trimmed());
}

} // namespace

ThemePages::ThemePages(AppController *app, QObject *parent) : QObject(parent), m_app(app)
{
    Workspace *ws = m_app->workspace();
    connect(ws, &Workspace::openChanged, this, &ThemePages::reset);
    connect(ws, &Workspace::readyChanged, this, [this] {
        if (m_app->workspace()->ready() && !m_scanned)
            rescan();
    });
    connect(ws, &Workspace::filesChanged, this, &ThemePages::filesChanged);
    connect(ws, &Workspace::configChanged, this, [this] {
        apply();
        bump();
    });
    // A page gone or renamed is no theme page under its old path (a folder renamed: all again).
    connect(ws, &Workspace::pathRemoved, this, [this](const QString &path) {
        if (m_pages.remove(path) > 0) {
            apply();
            bump();
        }
    });
    connect(ws, &Workspace::folderRenamed, this, [this] {
        if (m_scanned)
            rescan();
    });
    // The device's own pick follows a page renamed or moved here (the notebook's follows it as a
    // setting naming it: notebook::relocate).
    connect(ws, &Workspace::pathRenamed, this, [this](const QString &oldPath, const QString &newPath) {
        AppSettings *settings = m_app->settings();
        if (m_scanned && m_pages.remove(oldPath) > 0) {
            update(newPath);
            apply();
            bump();
        }
        if (ownKey().isEmpty() || !settings->value(ownKey(), false).toBool())
            return;
        const QString mine = settings->value(pageKey(), QString()).toString();
        if (mine == oldPath || (paths::isMarkdown(mine) && mine.startsWith(oldPath + u'/')))
            settings->setValue(pageKey(), QString(newPath + mine.mid(oldPath.size())));
    });
    // The swatches show a page over the appearance settings in force.
    connect(m_app->settings(), &AppSettings::accentChanged, this, &ThemePages::bump);
    connect(m_app->settings(), &AppSettings::surfacesChanged, this, &ThemePages::bump);
    connect(m_app->settings(), &AppSettings::valueChanged, this, [this](const QString &key) {
        if (key == ownKey() || key == pageKey()) {
            apply();
            bump();
        }
    });
    if (ws->ready())
        rescan();
}

ThemePages::~ThemePages() = default;

QString ThemePages::notebookId() const
{
    const Workspace *ws = m_app->workspace();
    return ws->isOpen() ? Vault::idForPath(ws->rootPath()) : QString();
}

QString ThemePages::ownKey() const
{
    const QString id = notebookId();
    return id.isEmpty() ? QString() : u"appearance/themePageOwn/"_s + id;
}

QString ThemePages::pageKey() const
{
    const QString id = notebookId();
    return id.isEmpty() ? QString() : u"appearance/themePage/"_s + id;
}

void ThemePages::reset()
{
    m_pages.clear();
    m_scanned = false;
    if (m_app->workspace()->ready())
        rescan();
    else
        apply();
    bump();
}

void ThemePages::rescan()
{
    m_pages.clear();
    VaultIndex *index = m_app->workspace()->index();
    m_scanned = index != nullptr;
    if (index) {
        QStringList found;
        index->forEachNote([&](const VaultPath &path, const ParsedNote &note) {
            if (extensions::isThemePage(note))
                found << path;
        });
        for (const QString &path : std::as_const(found))
            update(path);
    }
    apply();
    bump();
}

bool ThemePages::update(const QString &path)
{
    VaultIndex *index = m_app->workspace()->index();
    const ParsedNote *note = index && paths::isMarkdown(path) ? index->note(path) : nullptr;
    if (!note || !extensions::isThemePage(*note))
        return m_pages.remove(path) > 0;
    Entry entry;
    entry.title = note->title;
    entry.description = extensions::themeDescription(*note);
    entry.page = extensions::parseThemePage(index->content(path), fontInstalled);
    const auto old = m_pages.constFind(path);
    if (old != m_pages.constEnd() && old->title == entry.title && old->description == entry.description
        && old->page.layer == entry.page.layer && old->page.issues == entry.page.issues
        && old->page.blocks == entry.page.blocks)
        return false;
    m_pages.insert(path, entry);
    return true;
}

void ThemePages::filesChanged(const QStringList &changed)
{
    if (!m_scanned)
        return;
    bool any = false;
    for (const QString &path : changed)
        any = update(path) || any;
    if (!any)
        return;
    // An edit of the page in force applies at once, in both modes.
    apply();
    bump();
}

void ThemePages::bump()
{
    ++m_revision;
    Q_EMIT changed();
}

QString ThemePages::notebookTheme() const
{
    const Workspace *ws = m_app->workspace();
    return ws->isOpen() ? ws->vaultConfig().value(u"themePage"_s).toString() : QString();
}

bool ThemePages::deviceOnly() const
{
    const QString key = ownKey();
    return !key.isEmpty() && m_app->settings()->value(key, false).toBool();
}

void ThemePages::setDeviceOnly(bool on)
{
    if (ownKey().isEmpty() || on == deviceOnly())
        return;
    if (on)
        m_app->settings()->setValue(pageKey(), current()); // keeps what shows
    m_app->settings()->setValue(ownKey(), on);
    apply();
    bump();
}

QString ThemePages::current() const
{
    if (!m_app->workspace()->isOpen())
        return {};
    return deviceOnly() ? m_app->settings()->value(pageKey(), QString()).toString() : notebookTheme();
}

bool ThemePages::missing() const
{
    const QString path = current();
    return !path.isEmpty() && m_scanned && !m_pages.contains(path);
}

bool ThemePages::isThemePageOrNone(const QString &path) const
{
    return path.isEmpty() || m_pages.contains(path);
}

bool ThemePages::pick(const QString &rawPath)
{
    const QString path = paths::normalize(rawPath);
    Workspace *ws = m_app->workspace();
    if (!ws->isOpen() || !isThemePageOrNone(path))
        return false;
    if (deviceOnly()) {
        m_app->settings()->setValue(pageKey(), path);
    } else if (!ws->setConfig({{u"themePage"_s, path}})) {
        return false;
    }
    qCInfo(lcApp, "Theme page: %s%s", path.isEmpty() ? "none" : qUtf8Printable(path),
           deviceOnly() ? " (this device)" : "");
    apply();
    bump();
    return true;
}

void ThemePages::apply()
{
    const QString path = current();
    const auto it = m_pages.constFind(path);
    if (it == m_pages.constEnd())
        m_app->scripting()->setPageTheme(QString(), {});
    else
        m_app->scripting()->setPageTheme(path, it->page.layer);
}

QVariantList ThemePages::pages() const
{
    const AppSettings *s = m_app->settings();
    const QVariantMap light =
            ui::paletteMap(false, s->lightSurface(), s->darkSurface(), s->accentColor(), s->customAccent());
    const QVariantMap dark = ui::paletteMap(true, s->lightSurface(), s->darkSurface(), s->accentColor(), s->customAccent());
    const QString picked = current();
    QList<QString> order = m_pages.keys();
    std::ranges::sort(order, [&](const QString &a, const QString &b) {
        const int c = QString::localeAwareCompare(m_pages[a].title, m_pages[b].title);
        return c != 0 ? c < 0 : a < b;
    });
    QVariantList out;
    for (const QString &path : std::as_const(order)) {
        const Entry &e = m_pages[path];
        const QVariantMap l = e.page.layer.forMode(false);
        const QVariantMap d = e.page.layer.forMode(true);
        const auto pick = [](const QVariantMap &tokens, const QVariantMap &palette, const QString &token) {
            return tokens.value(token, palette.value(token)).toString();
        };
        out << QVariantMap{{u"path"_s, path},
                           {u"title"_s, e.title},
                           {u"description"_s, e.description},
                           {u"tokens"_s, e.page.tokenCount()},
                           {u"issues"_s, int(e.page.issues.size())},
                           {u"current"_s, path == picked},
                           {u"lightBg"_s, pick(l, light, u"bg"_s)},
                           {u"lightAccent"_s, pick(l, light, u"accent"_s)},
                           {u"lightFg"_s, pick(l, light, u"fg"_s)},
                           {u"darkBg"_s, pick(d, dark, u"bg"_s)},
                           {u"darkAccent"_s, pick(d, dark, u"accent"_s)},
                           {u"darkFg"_s, pick(d, dark, u"fg"_s)}};
    }
    return out;
}

QString ThemePages::issueText(const extensions::ThemeIssue &issue) const
{
    using K = extensions::ThemeIssueKind;
    switch (issue.kind) {
    case K::Syntax:
        return tr("“%1” is not “token: value”").arg(issue.value);
    case K::UnknownToken:
        return issue.detail.isEmpty() ? tr("no token “%1”").arg(issue.token)
                                      : tr("no token “%1” (did you mean “%2”?)").arg(issue.token, issue.detail);
    case K::BadColor:
        return tr("“%1” is not a colour: “%2” (#rrggbb)").arg(issue.token, issue.value);
    case K::BadNumber:
        return tr("“%1” is a number from %2: “%3”").arg(issue.token, issue.detail, issue.value);
    case K::BadFont:
        return tr("“%1” is not a font's name").arg(issue.token);
    case K::MissingFont:
        return tr("the font “%1” is not installed on this device").arg(issue.value);
    case K::ModeInMode:
        return tr("“%1:” inside “%2:”").arg(issue.token, issue.detail);
    case K::ModeValue:
        return tr("“%1:” starts the mode's tokens on the lines below it").arg(issue.token);
    case K::TooMany:
        return tr("more than %1 tokens").arg(issue.detail);
    }
    return {};
}

QVariantMap ThemePages::pageState(const QString &path) const
{
    const auto it = m_pages.constFind(path);
    if (it == m_pages.constEnd())
        return {};
    QVariantList issues;
    for (const extensions::ThemeIssue &issue : it->page.issues)
        issues << QVariantMap{{u"line"_s, issue.line}, {u"text"_s, issueText(issue)}};
    return {{u"theme"_s, true},
            {u"applied"_s, path == current()},
            {u"tokens"_s, it->page.tokenCount()},
            {u"blocks"_s, it->page.blocks},
            {u"firstLine"_s, it->page.firstLine},
            {u"issues"_s, issues}};
}

bool ThemePages::pageApplied(const QString &path) const
{
    return m_pages.contains(path) && path == current();
}

int ThemePages::pageTokens(const QString &path) const
{
    const auto it = m_pages.constFind(path);
    return it == m_pages.constEnd() ? 0 : it->page.tokenCount();
}

int ThemePages::pageBlocks(const QString &path) const
{
    const auto it = m_pages.constFind(path);
    return it == m_pages.constEnd() ? 0 : it->page.blocks;
}

int ThemePages::pageIssueCount(const QString &path) const
{
    const auto it = m_pages.constFind(path);
    return it == m_pages.constEnd() ? 0 : int(it->page.issues.size());
}

int ThemePages::pageIssueLine(const QString &path) const
{
    const auto it = m_pages.constFind(path);
    return it == m_pages.constEnd() || it->page.issues.isEmpty() ? -1 : it->page.issues.first().line;
}

QString ThemePages::pageIssueText(const QString &path) const
{
    const auto it = m_pages.constFind(path);
    return it == m_pages.constEnd() || it->page.issues.isEmpty() ? QString() : issueText(it->page.issues.first());
}

QString ThemePages::exportCurrent()
{
    Workspace *ws = m_app->workspace();
    if (!ws->isOpen() || !ws->vault())
        return {};
    const AppSettings *s = m_app->settings();
    QVariantMap modes[2];
    for (const bool dark : {false, true}) {
        // The colours Theme.qml shows: the palette of the appearance settings, an overridden accent's
        // family, then every override in force (ThemeColors::update()'s rule).
        ui::ThemeColors colors;
        colors.setDark(dark);
        colors.setLightSurface(s->lightSurface());
        colors.setDarkSurface(s->darkSurface());
        colors.setAccentName(s->accentColor());
        colors.setCustomAccent(s->customAccent());
        QVariantMap &out = modes[dark ? 1 : 0];
        const QVariantMap map = colors.map();
        for (auto it = map.cbegin(); it != map.cend(); ++it) {
            const QString name = it.key() == u"accentOn" ? u"fgOnAccent"_s : it.key();
            if (extensions::themeToken(name))
                out.insert(name, it.value());
        }
        const QVariantMap overrides = ui::ThemeOverrides::instance()->tokens(dark);
        for (auto it = overrides.cbegin(); it != overrides.cend(); ++it)
            if (extensions::themeToken(it.key()))
                out.insert(it.key(), it.value());
        // The fonts chosen in the settings (a theme's own win, above).
        if (!out.contains(u"fontUi"_s) && !s->uiFontFamily().isEmpty())
            out.insert(u"fontUi"_s, s->uiFontFamily());
        if (!out.contains(u"fontMono"_s) && !s->editorMonoFamily().isEmpty())
            out.insert(u"fontMono"_s, s->editorMonoFamily());
    }
    const extensions::ThemeLayer layer = extensions::foldedThemeLayer(modes[0], modes[1]);
    const bool en = ws->extensions()->language() == u"en";
    const QString date = QDate::currentDate().toString(Qt::ISODate);
    const QString content =
            (en ? u"---\ntags: [theme]\ndescription: The look of the app, saved on %1.\n---\n"_s
                : u"---\ntags: [motyw]\nopis: Wygląd aplikacji zapisany %1.\n---\n"_s)
                    .arg(date)
            + (en ? u"A theme page: Settings → Appearance → Themes applies it on every device that follows the "
                    "notebook. Change a colour below and it shows at once; copy this file into another "
                    "notebook to take the theme along.\n\n"_s
                  : u"Strona motywu: Ustawienia → Wygląd → Motywy włącza ją na każdym urządzeniu, które idzie "
                    "za notatnikiem. Zmień kolor poniżej, a od razu będzie widać; skopiuj ten plik do innego "
                    "notatnika, by zabrać motyw ze sobą.\n\n"_s)
            + extensions::themeBlockText(layer);
    const notebook::CreatedPage created = notebook::createPage(*ws->vault(), en ? u"My theme"_s : u"Mój motyw"_s,
                                                               en ? u"Themes"_s : u"Motywy"_s, std::nullopt, content);
    if (created.path.isEmpty() || created.existing)
        return {};
    qCInfo(lcApp, "Theme page exported: %s (%d tokens)", qUtf8Printable(created.path),
           int(layer.both.size() + layer.light.size() + layer.dark.size()));
    return created.path;
}

} // namespace brulion::shell
