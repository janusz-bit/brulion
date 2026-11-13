// QrShare — the text of the QR code dialog (docs/UI.md "QR code", qml/qr/QrCodeDialog.qml): a
// note's Markdown — or the selected blocks' — as qr::noteText() makes it (as written or as plain
// text, the title first or not), cut into as many codes as it needs (qr::splitText(), at most
// qr::MaxCodes), and the code shown. QrCodeView draws `partText`.
//
//   QrShare { id: share; host: Workspace; plainText: format === "plain"; withTitle: titleBox.checked }
//   share.markdown = page.sourceText; share.title = page.title
//   share.state ("ok" | "empty" | "tooLong"), share.count, share.index, share.partText
#pragma once

#include "editorhost.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace brulion::qrview {

class QrShare : public QObject {
    Q_OBJECT
    /// The notebook: in plain text, the text of a block a `((id))` reference names
    /// (EditorHost::blockText); without one the references stay as written.
    Q_PROPERTY(brulion::EditorHost *host READ host WRITE setHost NOTIFY hostChanged FINAL)
    /// The note's Markdown, or the selected blocks' (PageController.selectionMarkdown()).
    Q_PROPERTY(QString markdown READ markdown WRITE setMarkdown NOTIFY markdownChanged FINAL)
    /// The note's title as the page shows it ("" for none).
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged FINAL)
    /// The codes carry the plain text ("Copy as plain text") instead of the Markdown.
    Q_PROPERTY(bool plainText READ plainText WRITE setPlainText NOTIFY plainTextChanged FINAL)
    /// The title goes first (where it makes a difference: titleOptional).
    Q_PROPERTY(bool withTitle READ withTitle WRITE setWithTitle NOTIFY withTitleChanged FINAL)

    /// The title makes a difference: there is one, and the note does not start with a level-1
    /// heading of its own (qr::hasOwnTitle()).
    Q_PROPERTY(bool titleOptional READ titleOptional NOTIFY textChanged FINAL)
    /// "ok" — `count` codes; "empty" — nothing to share; "tooLong" — more than maxCodes codes.
    Q_PROPERTY(QString state READ state NOTIFY textChanged FINAL)
    /// The codes the text takes: 1 … maxCodes when "ok", else 0.
    Q_PROPERTY(int count READ count NOTIFY textChanged FINAL)
    /// The codes a text too long would take ("tooLong"; else count).
    Q_PROPERTY(int codesNeeded READ codesNeeded NOTIFY textChanged FINAL)
    /// The whole text of the codes and its UTF-8 bytes.
    Q_PROPERTY(QString text READ text NOTIFY textChanged FINAL)
    Q_PROPERTY(int bytes READ bytes NOTIFY textChanged FINAL)
    /// The most codes one text is cut into (qr::MaxCodes).
    Q_PROPERTY(int maxCodes READ maxCodes CONSTANT FINAL)

    /// The code shown, 0 … count - 1 (kept in that range; 0 again for a new text).
    Q_PROPERTY(int index READ index WRITE setIndex NOTIFY partChanged FINAL)
    /// Its text ("" unless "ok").
    Q_PROPERTY(QString partText READ partText NOTIFY partChanged FINAL)

public:
    explicit QrShare(QObject *parent = nullptr);

    EditorHost *host() const { return m_host; }
    void setHost(EditorHost *host);
    QString markdown() const { return m_markdown; }
    void setMarkdown(const QString &markdown);
    QString title() const { return m_title; }
    void setTitle(const QString &title);
    bool plainText() const { return m_plainText; }
    void setPlainText(bool plainText);
    bool withTitle() const { return m_withTitle; }
    void setWithTitle(bool withTitle);

    bool titleOptional() const { return m_titleOptional; }
    QString state() const;
    int count() const { return int(m_parts.size()); }
    int codesNeeded() const { return m_codesNeeded; }
    QString text() const { return m_text; }
    int bytes() const { return m_bytes; }
    static int maxCodes();

    int index() const { return m_index; }
    void setIndex(int index);
    QString partText() const;

    /// The next / the previous code (no further than the last / the first).
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

Q_SIGNALS:
    void hostChanged();
    void markdownChanged();
    void titleChanged();
    void plainTextChanged();
    void withTitleChanged();
    void textChanged();
    void partChanged();

private:
    /// The text and its codes again, after any input changed; the first code is shown again when
    /// the text is not the same.
    void update();

    QPointer<EditorHost> m_host;
    QString m_markdown;
    QString m_title;
    bool m_plainText = false;
    bool m_withTitle = true;

    bool m_titleOptional = false;
    QString m_text;
    int m_bytes = 0;
    int m_codesNeeded = 0;
    QStringList m_parts; ///< empty unless "ok"
    int m_index = 0;
};

} // namespace brulion::qrview
