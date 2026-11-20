// CanvasCards — what the canvas view's cards show (docs/UI.md "Canvas view"): a text card's
// Markdown and a note card's page (or the section or block of its subpath) rendered read-only as an
// embed of it would be — InlineRenderer's escaped rich text through the editor's BlockListModel
// (BlockListModel::textPreview(), linkPreview()), never a note's own HTML, nested embeds
// depth-limited —, a PDF card's first page thumbnail (EditorHost::pdfThumbnail(): the PDF
// thumbnails' cache), a drawing's or a picture's URL. Results are kept per text and per note
// until the index or the file changes (`revision` grows: QML bindings that read it ask again).
//
//   CanvasCards { id: cards; host: Workspace; path: board.path; palette: rendererPalette
//                 locale: DatesHelper.locale; now: DatesHelper.fixedNow }
//   cards.revision >= 0 ? cards.text(model.text) : ({})       // {found, message, more, blocks}
//   cards.note(model.filePath, model.subpath)                   // {title, href, found, message, …}
#pragma once

#include "editorhost.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace brulion::editor {
class BlockListModel;
}

namespace brulion::canvasview {

class CanvasCards : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    /// The canvas file (links of text cards resolve from its folder).
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    /// The renderer's colours, as BLinkPreview's rendererPalette.
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    /// "en" | "pl": chips' words.
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// A fixed clock "YYYY-MM-DD[ HH:mm]" (screenshots, tests); "" = the real one.
    Q_PROPERTY(QString now READ now WRITE setNow NOTIFY nowChanged FINAL)
    /// Grows whenever what the cards show may have changed (the index, a file, a thumbnail).
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)

public:
    /// Texts and notes kept rendered (each cache).
    static constexpr int MaxCached = 400;

    explicit CanvasCards(QObject *parent = nullptr);
    ~CanvasCards() override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    QVariantMap palette() const { return m_palette; }
    void setPalette(const QVariantMap &palette);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString now() const { return m_now; }
    void setNow(const QString &now);
    int revision() const { return m_revision; }

    /// A text card's Markdown: {found, message ("empty"), more, blocks} (BLinkPreview's rows).
    Q_INVOKABLE QVariantMap text(const QString &markdown);
    /// A note card's page, or its section / block (`subpath` "#Heading", "#^id"): {kind, title,
    /// href, path, found, message ("missingPage" | "missingSection" | "missingBlock" | "empty"),
    /// more, blocks}.
    Q_INVOKABLE QVariantMap note(const QString &path, const QString &subpath);
    /// A PDF's first page: {state, url, width, height, pages, error, locked} (EditorHost).
    Q_INVOKABLE QVariantMap pdf(const QString &path) const;
    /// A picture's URL (EditorHost::assetUrl) and a drawing's preview in the theme.
    Q_INVOKABLE QUrl pictureUrl(const QString &path) const;
    Q_INVOKABLE QUrl drawingUrl(const QString &path, bool dark) const;
    /// A canvas's picture (CanvasImageProvider: image://canvas/…), "" for none.
    Q_INVOKABLE QUrl canvasUrl(const QString &path, bool dark) const;
    /// The page title a file is shown by ("Kuchnia" for Projekty/Kuchnia.md, a file's name).
    Q_INVOKABLE QString fileTitle(const QString &path) const;
    /// Follow a link of a card's rendered text (page:, block:, tag:, ext:, part:) as a click on
    /// it in a note does; false for links it does not follow.
    Q_INVOKABLE bool openLink(const QString &url, bool newPane = false);
    /// A file card's file opened as the canvas names it (`subpath` "#Heading" / "#^id": a note at
    /// that heading or block; any other file in its view); false when it is not in the notebook.
    Q_INVOKABLE bool openFile(const QString &file, const QString &subpath);
    /// A card opened (CanvasEditor::nodeData()'s map): a file card's file (openFile()), a link
    /// card's address in the browser; false for a file that is not in the notebook.
    Q_INVOKABLE bool openNode(const QVariantMap &node);
    /// Everything rendered again (the theme changed).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void hostChanged();
    void pathChanged();
    void paletteChanged();
    void localeChanged();
    void nowChanged();
    void revisionChanged();

private:
    void invalidate();
    void prepareModel();

    QPointer<EditorHost> m_host;
    QString m_path;
    QVariantMap m_palette;
    QString m_locale = QStringLiteral("en");
    QString m_now;
    int m_revision = 0;
    std::unique_ptr<editor::BlockListModel> m_model;
    QHash<QString, QVariantMap> m_texts;
    QHash<QString, QVariantMap> m_notes;
};

/// The canvas's colours for QML (CanvasColors singleton): a canvasColor as the app shows it, the
/// presets of the colour menu.
class CanvasColors : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    /// "1"–"6" (Obsidian's presets, lighter in the dark theme) or a hex colour as "#rrggbb"; ""
    /// for none or anything else (canvas::colorHex).
    Q_INVOKABLE QString hex(const QString &canvasColor, bool dark) const;
    /// `color` as a canvasColor ("1"–"6", "#rrggbb"; "" for anything else).
    Q_INVOKABLE QString normalize(const QString &color) const;
};

/// What one card shows, for its QML delegate (typed rows; QML reads no keys of a returned map):
/// a text card's Markdown (`kind` "text"), a note card's page (`kind` "note": `file`, `subpath`),
/// a picture's, a drawing's, a canvas's or a PDF's first page's picture (`kind` "picture",
/// "drawing", "canvas", "pdf": `pictureUrl`), through `cards`, again whenever an input or
/// `cards.revision` changes.
///
///   CanvasCardContent { cards: board.cards; kind: "text"; text: model.text }
///   // rows: [{kind, view, task, headingLevel, depth, title, html, raw, path}] (BLinkPreview's)
class CanvasCardContent : public QObject {
    Q_OBJECT
    Q_PROPERTY(brulion::canvasview::CanvasCards *cards READ cards WRITE setCards NOTIFY cardsChanged FINAL)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY inputChanged FINAL)
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY inputChanged FINAL)
    Q_PROPERTY(QString file READ file WRITE setFile NOTIFY inputChanged FINAL)
    Q_PROPERTY(QString subpath READ subpath WRITE setSubpath NOTIFY inputChanged FINAL)
    /// Nothing is rendered while false (a card too small on screen to read).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY inputChanged FINAL)
    /// The app is dark (drawings and canvases follow it).
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY inputChanged FINAL)
    /// A picture of the file ("" none, or not yet: a PDF's thumbnail being drawn).
    Q_PROPERTY(QUrl pictureUrl READ pictureUrl NOTIFY contentChanged FINAL)
    /// A PDF: "loading", "ready", "failed", "unsupported"; its page count (0 unknown).
    Q_PROPERTY(QString pdfState READ pdfState NOTIFY contentChanged FINAL)
    Q_PROPERTY(int pdfPages READ pdfPages NOTIFY contentChanged FINAL)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY contentChanged FINAL)
    /// A note: its title ("Kuchnia › Szafki"), whether it was found, why not ("missingPage", …).
    Q_PROPERTY(QString title READ title NOTIFY contentChanged FINAL)
    Q_PROPERTY(bool found READ found NOTIFY contentChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY contentChanged FINAL)
    /// Blocks left out (at most BlockListModel::MaxTransclusionBlocks are shown).
    Q_PROPERTY(int more READ more NOTIFY contentChanged FINAL)

public:
    explicit CanvasCardContent(QObject *parent = nullptr);

    CanvasCards *cards() const { return m_cards; }
    void setCards(CanvasCards *cards);
    QString kind() const { return m_kind; }
    void setKind(const QString &kind);
    QString text() const { return m_text; }
    void setText(const QString &text);
    QString file() const { return m_file; }
    void setFile(const QString &file);
    QString subpath() const { return m_subpath; }
    void setSubpath(const QString &subpath);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool dark() const { return m_dark; }
    void setDark(bool dark);
    QUrl pictureUrl() const { return m_pictureUrl; }
    QString pdfState() const { return m_pdfState; }
    int pdfPages() const { return m_pdfPages; }
    QVariantList rows() const { return m_rows; }
    QString title() const { return m_title; }
    bool found() const { return m_found; }
    QString message() const { return m_message; }
    int more() const { return m_more; }

Q_SIGNALS:
    void cardsChanged();
    void inputChanged();
    void contentChanged();

private:
    void update();

    QPointer<CanvasCards> m_cards;
    QString m_kind;
    QString m_text;
    QString m_file;
    QString m_subpath;
    bool m_active = true;
    bool m_dark = false;
    QUrl m_pictureUrl;
    QString m_pdfState;
    int m_pdfPages = 0;
    QVariantList m_rows;
    QString m_title;
    bool m_found = false;
    QString m_message;
    int m_more = 0;
};

} // namespace brulion::canvasview
