// StreamSelection — a block selection over several consecutive pages of a stream: the days of the
// journal stream (docs/UI.md "Selecting and copying"). Each day has its own PageController (its
// own selection, undo history); this object spans them: from an anchor block in one page to a
// focus block in another, every page between selected whole. It copies only (Markdown with the
// HTML flavour, or plain text): any other change a day sees — a click, an edit, its own selection
// changed — ends the span, the other days' parts are cleared.
//
//   StreamSelection { id: stream; host: Workspace; model: days; locale: DatesHelper.locale }
//   ContentJournalDay: Component.onCompleted: stream.attach(page); onDestruction: stream.detach(page)
//
// `model` lists the pages top to bottom (the roles "path" and, for plain-text labels, "date" =
// "YYYY-MM-DD": JournalListModel). The state is paths and document rows, not controllers: a day
// destroyed while scrolled away and created again gets its part back once loaded (attach()); a day
// that is not attached is copied from its file (EditorHost::read). Per page in the span: the
// anchor page from the anchor block to its last block (the focus below) or from its first block to
// the anchor (the focus above), the focus page symmetric, the pages between whole — never the
// frontmatter / page properties. A page that leaves the model with an end of the span ends it.
//
// A copy has each page's blocks (markdown::blocksMarkdown / blocksPlainText, as
// PageController::copySelection) preceded by the page's link on a line of its own (`[[2026-10-27]]`;
// plain text: the long date, "poniedziałek, 27 października 2026"), a blank line between pages; one
// text/html flavour rendered on request (EditorHost::renderHtml from the first page), the same
// ClipboardLimit and read-back as PageController::writeClipboard().
#pragma once

#include "editorhost.h"
#include "pagecontroller.h"

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace brulion::editor {

class StreamSelection : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    /// The pages top to bottom (roles "path", "date").
    Q_PROPERTY(QAbstractItemModel *model READ model WRITE setModel NOTIFY modelChanged FINAL)
    /// Language of plain-text labels: "pl" (default) or "en".
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// The selection spans two or more pages.
    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    /// Visible blocks selected over every page of the span (pages not attached: their blocks).
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
    /// Pages in the span.
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed FINAL)
    Q_PROPERTY(QString anchorPath READ anchorPath NOTIFY changed FINAL)
    Q_PROPERTY(int anchorRow READ anchorRow NOTIFY changed FINAL)
    Q_PROPERTY(QString focusPath READ focusPath NOTIFY changed FINAL)
    Q_PROPERTY(int focusRow READ focusRow NOTIFY changed FINAL)
    /// The attached page that shows the span's tools (the focus page, else the anchor page, else
    /// another one in the span); null while not active.
    Q_PROPERTY(brulion::editor::PageController *toolsController READ toolsController NOTIFY changed FINAL)
    /// The span is being applied to its pages: their selection signals are its own (a day must not
    /// take them for the user selecting there).
    Q_PROPERTY(bool applying READ applying NOTIFY changed FINAL)

public:
    explicit StreamSelection(QObject *parent = nullptr);
    ~StreamSelection() override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QAbstractItemModel *model() const { return m_model; }
    void setModel(QAbstractItemModel *model);
    QString locale() const { return m_english ? QStringLiteral("en") : QStringLiteral("pl"); }
    void setLocale(const QString &locale);
    bool active() const { return m_active; }
    int count() const { return m_count; }
    int pageCount() const;
    QString anchorPath() const { return m_active ? m_anchorPath : QString(); }
    int anchorRow() const { return m_active ? m_anchorRow : -1; }
    QString focusPath() const { return m_active ? m_focusPath : QString(); }
    int focusRow() const { return m_active ? m_focusRow : -1; }
    PageController *toolsController() const { return m_tools; }
    bool applying() const { return m_applying > 0; }
    /// The pages top to bottom (the model's "path" role).
    QStringList paths() const { return m_paths; }

    /// A page of the stream exists (its controller): it takes its part of the span (now, or once
    /// loaded). Pages not attached are read from their files.
    Q_INVOKABLE void attach(brulion::editor::PageController *page);
    Q_INVOKABLE void detach(brulion::editor::PageController *page);
    /// Shift+click, a tap in the touch selection mode: the selection's end goes to `row` of `page`,
    /// from the span's anchor — or, without a span, from the block selected or being edited in
    /// another attached page (its anchor end). Back in the anchor page: a selection of that page
    /// alone (the span ends). False when nothing else holds a selection (the page selects alone).
    Q_INVOKABLE bool extendTo(brulion::editor::PageController *page, int row);
    /// A drag from `fromRow` of `from` (where it was pressed) to `row` of `page`; `page` == `from`:
    /// that page alone (the span ends).
    Q_INVOKABLE bool extendFrom(brulion::editor::PageController *from, int fromRow,
                                brulion::editor::PageController *page, int row);
    /// Shift+↓ (`step` 1) / Shift+↑ (-1): the focus end one block further (PageController's own
    /// step inside the focus page), past a page's edge into the next / previous page. Without a
    /// span, from `page` at its edge (its own step changed nothing): the span starts. False when
    /// there is nowhere to go.
    Q_INVOKABLE bool extendBy(brulion::editor::PageController *page, int step);
    /// Another attached page than `page` has a block selection (a tap there extends it on touch UIs).
    Q_INVOKABLE bool othersSelected(brulion::editor::PageController *page) const;
    /// End the span: every attached page's selection cleared.
    Q_INVOKABLE void clear();
    /// The span as Markdown / plain text (labels and every page's blocks, see above); "" without one.
    Q_INVOKABLE QString markdown() const;
    Q_INVOKABLE QString plainText() const;
    /// Copy the span: { ok, count (blocks), pages, reason: "empty" | "tooLarge" | "noClipboard" }.
    Q_INVOKABLE QVariantMap copy(bool plainText = false);

Q_SIGNALS:
    void hostChanged();
    void modelChanged();
    void localeChanged();
    void changed();
    /// The focus end moved to `row` of `page` (that day becomes the active one; `reveal`: scroll it
    /// into view — the keys; a click or drag points at it already).
    void focusMoved(brulion::editor::PageController *page, int row, bool reveal);
    /// The focus end is in a page that is not attached (scrolled far away): the stream should
    /// scroll there (the next step works once it exists).
    void revealRequested(const QString &path);

private:
    struct Part {
        QString path;
        QString markdown;
        QString plainText;
        int count = 0;
    };
    void readPaths();
    int indexOf(const QString &path) const { return int(m_paths.indexOf(path)); }
    PageController *attached(const QString &path) const;
    /// The span's first / last page index; (-1, -1) without one.
    std::pair<int, int> bounds() const;
    /// The rows (anchor, focus) `page` selects: (-1, -1) when it selects nothing.
    std::pair<int, int> rowsIn(PageController *page) const;
    /// The same for page `path` whose blocks to select run from `first` to `last`.
    std::pair<int, int> rowsIn(const QString &path, int first, int last) const;
    void apply();
    void applyTo(PageController *page);
    void collapseTo(int row);
    void setFocus(const QString &path, int row, bool reveal);
    /// The span ends; every attached page but `except` loses its selection.
    void end(PageController *except);
    void update();
    void pageChanged(PageController *page);
    Part partOf(const QString &path, bool withText) const;
    QString label(const QString &path, bool plainText) const;
    /// The copy's text; `count`: the blocks in it.
    QString joined(bool plainText, int *count = nullptr) const;

    QPointer<EditorHost> m_host;
    QPointer<QAbstractItemModel> m_model;
    QList<QMetaObject::Connection> m_modelConnections;
    QStringList m_paths;
    QStringList m_dates;
    bool m_english = false;
    QList<QPointer<PageController>> m_pages;
    bool m_active = false;
    QString m_anchorPath;
    int m_anchorRow = -1;
    QString m_focusPath;
    int m_focusRow = -1;
    int m_count = 0;
    int m_applying = 0;
    QPointer<PageController> m_tools;
    /// Block counts of pages in the span that are not attached (read once per ends and direction;
    /// dropped when the file changes: EditorHost::fileChanged).
    mutable QHash<QString, int> m_fileCounts;
};

} // namespace brulion::editor
