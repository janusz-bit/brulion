// The links of the texts the page draws (docs/ATRAMENT.md step 5): a block's, a table cell's, an
// embedded block's, a query result's — each a hit region of its text (Text::link) with its click, its
// menu and its preview's cue, the hooks each kind of text has (onLink / onLinkAt / onLinkMenu, a
// cell's cellLinkClicked and onCellLinkMenu, onEmbeddedLink(Menu), onQueryLink(Menu)), and the hover
// cues (onLinkHover).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

namespace brulion::editor::atr {

namespace at = brulion::atrament;
using namespace Qt::StringLiterals;

PageView::LinkTarget PageView::LinkTarget::make(LinkHome home, std::size_t row, int tableRow, int column, int item,
                                                const QString &path, int line, const QString &raw)
{
    LinkTarget t;
    t.home = home;
    t.row = row;
    t.cellRow = tableRow;
    t.cellColumn = column;
    t.item = item;
    t.path = path;
    t.line = line;
    t.raw = raw;
    return t;
}

// The text a link is in, for its hover key.
int PageView::linkPart(const LinkTarget &to)
{
    switch (to.home) {
    case LinkHome::Block: return kOwnText;
    case LinkHome::Cell: return cellPart(to.cellRow, to.cellColumn);
    case LinkHome::Embedded: return kEmbedPart;
    case LinkHome::Result: break;
    }
    return to.item;
}

void PageView::addLinks(at::Text &t, const LinkTarget &to, const markdown::RenderedText &rt, PartText *record)
{
    PageView *self = this;
    const bool menus = hasLinkMenu(to.home);
    // (The listeners live in the frame arena, which destroys them — the hrefs with them.)
    for (const RenderedLink &l : renderedLinks(rt)) {
        const QString href = l.href;
        at::TextLink::MenuListener menu;
        if (menus)
            menu = [self, to, href](at::Point p, at::Window &, at::App &) { self->linkMenu(to, href, p); };
        std::move(t).link(
                l.range,
                [self, to, href](const at::ClickEvent &click, at::Window &, at::App &app) {
                    self->linkClicked(to, href, click.up.position, app);
                },
                std::move(menu), linkHover(int(to.row), linkPart(to), l, to.path));
        if (record)
            record->links.emplace_back(l.range, href);
    }
}

bool PageView::hasLinkMenu(LinkHome home) const
{
    switch (home) {
    case LinkHome::Block: return bool(onLinkMenu);
    case LinkHome::Cell: return true; // (on a phone in the selection mode: the row's long press)
    case LinkHome::Embedded: return bool(onEmbeddedLinkMenu);
    case LinkHome::Result: break;
    }
    return bool(onQueryLinkMenu);
}

void PageView::linkClicked(const LinkTarget &to, const QString &href, at::Point at, at::App &app)
{
    const int row = int(to.row);
    switch (to.home) {
    case LinkHome::Block:
        if (onLinkAt)
            onLinkAt(row, href, at);
        else if (onLink)
            onLink(row, href);
        return;
    case LinkHome::Cell:
        app.updateEntity<PageView>(m_selfId, [&](PageView &v, at::Context<PageView> &cx) {
            v.cellLinkClicked(to.row, to.cellRow, to.cellColumn, href, at, cx);
        });
        return;
    case LinkHome::Embedded:
        if (onEmbeddedLink)
            onEmbeddedLink(row, href, to.path, to.line, to.raw);
        else if (onLink)
            onLink(row, href);
        return;
    case LinkHome::Result: resultLinkClicked(to, href); return;
    }
}

void PageView::resultLinkClicked(const LinkTarget &to, const QString &href)
{
    if (onQueryLink) {
        onQueryLink(int(to.row), to.item, href, to.path, to.line, to.raw);
        return;
    }
    const QVariantMap answer = activateQueryLink(int(to.row), to.item, href);
    if (onQueryAnswer)
        onQueryAnswer(int(to.row), answer);
}

// A cell's link's menu (a date chip's, a link's); on a phone in the selection mode the row's long
// press.
void PageView::cellLinkMenu(const LinkTarget &to, const QString &href, at::Point at)
{
    if (m_touchGrips && onLongPressRow)
        onLongPressRow(int(to.row));
    else if (onCellLinkMenu)
        onCellLinkMenu(int(to.row), to.cellRow, to.cellColumn, href, at);
}

void PageView::linkMenu(const LinkTarget &to, const QString &href, at::Point at)
{
    const int row = int(to.row);
    switch (to.home) {
    case LinkHome::Block:
        if (onLinkMenu)
            onLinkMenu(row, href, at);
        return;
    case LinkHome::Cell: cellLinkMenu(to, href, at); return;
    case LinkHome::Embedded:
        if (onEmbeddedLinkMenu)
            onEmbeddedLinkMenu(row, href, to.path, to.line, to.raw, at);
        return;
    case LinkHome::Result:
        if (onQueryLinkMenu)
            onQueryLinkMenu(row, to.item, href, to.path, to.line, to.raw, at);
        return;
    }
}

at::TextLink::HoverListener PageView::linkHover(int row, int part, const RenderedLink &link, const QString &fromPath)
{
    // (Always: a hook set later is told without the page drawn again.)
    PageView *self = this;
    return [self, key = HoverKey{row, part, link.range.start}, href = link.href, fromPath](bool on, at::Point p, at::Window &,
                                                                                           at::App &) {
        self->linkHovered(key, on ? href : QString(), fromPath, p);
    };
}

void PageView::linkHovered(HoverKey key, const QString &href, const QString &fromPath, at::Point at)
{
    if (href.isEmpty() && m_hoveredLink != key)
        return; // (another link was told since)
    m_hoveredLink = href.isEmpty() ? HoverKey{} : key;
    if (!onLinkHover)
        return;
    const auto [link, path] = previewedLink(key.row, href, fromPath);
    onLinkHover(key.row, link, path, at);
}

std::pair<QString, QString> PageView::previewedLink(int row, const QString &href, const QString &fromPath) const
{
    if (href.isEmpty() || !m_controller)
        return {href, href.isEmpty() ? QString() : fromPath};
    // A day chip previews its day's page (BlockText's previewLink).
    return {href.startsWith(u"day:") ? m_controller->dateChipUrl(row, href) : href,
            fromPath.isEmpty() ? m_controller->path() : fromPath};
}

} // namespace brulion::editor::atr
