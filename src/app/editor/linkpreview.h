// LinkPreview — what the hover preview of a link shows (docs/UI.md "Link previews"): the page,
// section or block a `page:` or `block:` link of rendered text opens, rendered read-only as an
// embed of it would be (`![[Page]]`, `![[Page#Heading]]`, `![[Page#^id]]`, `!((id))`:
// BlockListModel's transclusions — InlineRenderer's escaped rich text, never a note's own HTML,
// nested embeds depth-limited, at most MaxTransclusionBlocks blocks), read through the EditorHost
// (the shell's Workspace, the playground's FakeEditorHost) from the note the link is in.
//
//   LinkPreview { host: Workspace; href: "page:Dom#Ogr%C3%B3d"; sourcePath: "Projekty/Plan.md"
//                 palette: rendererPalette; locale: DatesHelper.locale; now: DatesHelper.fixedNow }
//   // preview: { kind ("page" | "heading" | "block"), title, href, path, found, message
//   //            ("missingPage" | "missingSection" | "missingBlock" | "empty" | ""), more, blocks:
//   //            [{ kind ("block" | "embed"), html, depth, view, headingLevel, task, path, line, … }] }
//
// `available` is false for a link it does not preview: another scheme (tags, web addresses,
// tasks, dates, path parts), a page link to an attachment (a picture, a PDF) or a drawing. A link
// to a page that does not exist yet is available, with `found` false and message "missingPage".
// The preview is built when the object is completed and again when a property changes, never
// in between (QQmlParserStatus).
#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QString>
#include <QVariantMap>

#include <memory>

namespace brulion {
class EditorHost;
}

namespace brulion::editor {

class BlockListModel;

class LinkPreview : public QObject, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    /// The link (an InlineRenderer URL: page:…, block:…).
    Q_PROPERTY(QString href READ href WRITE setHref NOTIFY hrefChanged FINAL)
    /// The note the link is in (links resolve from it; `[[#Heading]]` is a section of it).
    Q_PROPERTY(QString sourcePath READ sourcePath WRITE setSourcePath NOTIFY sourcePathChanged FINAL)
    /// The renderer's colours, as PageController.palette (BlockEditor.rendererPalette).
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    /// "en" | "pl": chips' words and daily notes' titles.
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// A fixed clock "YYYY-MM-DD[ HH:mm]" (screenshots, tests); "" = the real one.
    Q_PROPERTY(QString now READ now WRITE setNow NOTIFY nowChanged FINAL)
    /// The link is previewed (see above).
    Q_PROPERTY(bool available READ available NOTIFY previewChanged FINAL)
    Q_PROPERTY(QVariantMap preview READ preview NOTIFY previewChanged FINAL)

public:
    explicit LinkPreview(QObject *parent = nullptr);
    ~LinkPreview() override;

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QString href() const { return m_href; }
    void setHref(const QString &href);
    QString sourcePath() const { return m_sourcePath; }
    void setSourcePath(const QString &path);
    QVariantMap palette() const { return m_palette; }
    void setPalette(const QVariantMap &palette);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString now() const { return m_now; }
    void setNow(const QString &now);
    bool available() const { return !m_preview.isEmpty(); }
    QVariantMap preview() const { return m_preview; }

    void classBegin() override;
    void componentComplete() override;

    /// The link's scheme is one a preview is shown for (page:, block:) — what is under the pointer
    /// is worth waiting for; a page link may still turn out unavailable (an attachment).
    Q_INVOKABLE bool previewable(const QString &url) const { return isPreviewable(url); }
    static bool isPreviewable(const QString &url);
    /// Follow a link shown in the preview (or the previewed link itself) as a click on it in a
    /// note does: page:, block:, tag:, part: through the host's navigate(), ext: openUrl();
    /// `fromPath` is the note the link is written in. False for links it does not follow (task:,
    /// reminder:, the chips a click edits).
    Q_INVOKABLE bool openLink(const QString &url, const QString &fromPath, bool newPane = false);
    /// Build the preview again (the note may have changed).
    Q_INVOKABLE void refresh();

Q_SIGNALS:
    void hostChanged();
    void hrefChanged();
    void sourcePathChanged();
    void paletteChanged();
    void localeChanged();
    void nowChanged();
    void previewChanged();

private:
    void update();

    QPointer<EditorHost> m_host;
    QString m_href;
    QString m_sourcePath;
    QVariantMap m_palette;
    QString m_locale;
    QString m_now;
    QVariantMap m_preview;
    std::unique_ptr<BlockListModel> m_model; ///< the renderer's context (no document of its own)
    bool m_deferred = false;                 ///< between classBegin() and componentComplete()
};

} // namespace brulion::editor
