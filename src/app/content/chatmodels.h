// The AI chat's models (docs/UI.md "AI chat"; ChatView): the messages of one conversation and the
// list of conversations — pages in AI/ read through the Workspace (core chat/chat.h), refreshed
// when they change like every content model (WorkspaceListModel).
//
//   ChatMessageModel { workspace: Workspace; path: route.path; palette: …; locale: DatesHelper.locale;
//                      budget: Workspace.chats.historyBudget }
//   // roles: sender ("user" | "assistant"), messageHtml (the message as rich text: InlineRenderer's
//   // escaped HTML, never a note's own — its pictures left out), markdown (its text, for Copy),
//   // line (its first line in the page), time ("09:12", "" not said), day ("Today", "15 Nov":
//   // above the first message of a day, "" otherwise), pictures (their addresses to show; their
//   // vault paths: picturePaths, to open), sent
//   // (the model gets it: chat::firstSent with `budget`), firstSent (the first one it gets after
//   // some it does not: a line above it says so)
//   // page: the page the conversation is about ("" none), title: the page's name without the date,
//   // omitted: the messages the model does not get
//   // render(markdown): an answer being written as a message shows it
//
//   ChatListModel { workspace: Workspace; locale: DatesHelper.locale }
//   // roles: path, title (the name without its date), date ("2026-11-15", "" without one),
//   // dateLabel ("today 09:12": the page's last change), preview (its last message as one line),
//   // fromAi (the last message is the AI's), messages (how many), running / queued (its turn runs,
//   // waits for the model on the device). Newest change first.
#pragma once

#include "workspacemodel.h"

#include "markdown/inlinerenderer.h"

#include <QString>
#include <QUrl>
#include <QVariantMap>

namespace brulion::content {

class ChatMessageModel : public WorkspaceListModel {
    Q_OBJECT
    /// The conversation's page ("" = a new conversation: no rows).
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged FINAL)
    /// Renderer colours (BlockEditor.rendererPalette's keys); empty = the light defaults.
    Q_PROPERTY(QVariantMap palette READ palette WRITE setPalette NOTIFY paletteChanged FINAL)
    /// "pl" (default) | "en": dates in links.
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// The page the conversation is about: its first message's ([[Page]]), "" none.
    Q_PROPERTY(QString page READ page NOTIFY loaded FINAL)
    /// The conversation's name: its page's title without the date in front ("Co mam jutro").
    Q_PROPERTY(QString title READ title NOTIFY loaded FINAL)
    /// The page exists and holds a conversation.
    Q_PROPERTY(bool exists READ exists NOTIFY loaded FINAL)
    /// The characters of the conversation the model gets at most (0: all of it).
    Q_PROPERTY(int budget READ budget WRITE setBudget NOTIFY budgetChanged FINAL)
    /// How many of its first messages the model does not get (they do not fit it).
    Q_PROPERTY(int omitted READ omitted NOTIFY loaded FINAL)

public:
    enum Role {
        RoleRole = Qt::UserRole + 1,
        HtmlRole,
        TextRole,
        LineRole,
        TimeRole,
        DayRole,
        PicturesRole,
        SentRole,
        FirstSentRole,
        PicturePathsRole
    };
    explicit ChatMessageModel(QObject *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    QVariantMap palette() const { return m_palette; }
    void setPalette(const QVariantMap &palette);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString page() const { return m_page; }
    QString title() const { return m_title; }
    bool exists() const { return m_exists; }
    int budget() const { return m_budget; }
    void setBudget(int budget);
    int omitted() const { return m_omitted; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    /// The Markdown of the message at `row` ("" for a bad row).
    Q_INVOKABLE QString textAt(int row) const;
    /// `markdown` as a message of this conversation shows it (renderMessage): the answer being
    /// written, before it is one of the rows.
    Q_INVOKABLE QString render(const QString &markdown) const;

    /// A message's Markdown as the bubble shows it: its blocks as rich text, one paragraph each —
    /// indented by their depth, a list item after its bullet or number (a task's box from the
    /// renderer), a heading in bold, code and tables as preformatted text. Static for tests.
    static QString renderMessage(const QString &markdown, const markdown::RenderContext &context);

Q_SIGNALS:
    void pathChanged();
    void paletteChanged();
    void localeChanged();
    void budgetChanged();
    /// The conversation was read again (page, title, exists may have changed).
    void loaded();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    struct Row {
        QString role;
        QString text;
        QString html;
        int line = 0;
        QString time;
        QString day;
        QList<QUrl> pictures;
        QStringList picturePaths;
        bool sent = true;
        bool firstSent = false;
    };
    QList<Row> m_rows;
    QString m_path;
    QVariantMap m_palette;
    QString m_locale = QStringLiteral("pl");
    QString m_page;
    QString m_title;
    bool m_exists = false;
    int m_budget = 0;
    int m_omitted = 0;
};

class ChatListModel : public WorkspaceListModel {
    Q_OBJECT
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        DateRole,
        DateLabelRole,
        PreviewRole,
        FromAiRole,
        MessagesRole,
        RunningRole,
        QueuedRole
    };
    explicit ChatListModel(QObject *parent = nullptr);

    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    /// The row of the conversation `path`, -1 when it is not listed.
    Q_INVOKABLE int rowOf(const QString &path) const;

    /// "2026-11-15 Co mam jutro" → {"2026-11-15", "Co mam jutro"}; a name without a date: {"", name}.
    static std::pair<QString, QString> splitTitle(const QString &title);

Q_SIGNALS:
    void localeChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    struct Row {
        QString path;
        QString title;
        QString date;
        qint64 modified = 0;
        QString preview;
        bool fromAi = false;
        int messages = 0;
    };
    QList<Row> m_rows;
    QString m_locale = QStringLiteral("pl");
    QMetaObject::Connection m_turnsChanged;
    /// The turns' state changed: the rows' running / queued again.
    void turnsChanged();
};

} // namespace brulion::content
