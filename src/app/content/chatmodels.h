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
//   // some it does not: a line above it says so), flag ("" | "reported" | "hidden": the user
//   // reported or hid it — the page's frontmatter, chat::setFlag; shown folded), revealed (a
//   // folded one opened again in this view: reveal()), segments (a message with a drawing — a ```chart
//   // of its own table, a ```mermaid diagram, a display formula —: its parts one under another,
//   // [{kind: "text" | "chart" | "diagram" | "math", html, code, source, chartData}]; [] for text
//   // only: messageHtml shows it), wakeFrom / wakeName (a message that woke the conversation for a
//   // sub-conversation — chat::wakeSource —: that conversation's path and name, "" for any other)
//   // page: the page the conversation is about ("" none), title: the page's name without the date,
//   // omitted: the messages the model does not get; ownOnly: a side chat's pane shows only its own
//   // messages (from its `side-from:` on), not the copy of its conversation before them
//   // render(markdown): an answer being written as a message shows it; segments(markdown): its
//   // parts when it has a drawing ([] text only)
//   // questionBefore(row): the user's question an answer answers (Report response)
//
//   ChatListModel { workspace: Workspace; locale: DatesHelper.locale; query: field.text;
//                   showArchived: false }
//   // roles: path, title (the name without its date), date ("2026-11-15", "" without one),
//   // dateLabel ("today 09:12": the page's last change), preview (its last message as one line;
//   // "Reported response" / "Hidden response" for one the user reported or hid),
//   // fromAi (the last message is the AI's), messages (how many), running / queued (its turn runs,
//   // waits for the model on the device), pinned / archived (its frontmatter's marks), section
//   // ("pinned" | "recent" | "archived"), asking / question (its turn asks: the question's first
//   // line), failure (why its turn failed, "" none), unread (an answer no chat view showed yet),
//   // titleMatches / matchText / matchMatches / matchLine (a query: the positions of its words in
//   // the title, the matching message's line around them, their positions there, that message's
//   // first line in the page, -1 none).
//   // The pinned first, then the others, then — unfolded (showArchived) or matching a query — the
//   // archived; in each the ones that need the user (a question, a failure, an unread answer) on
//   // top, by when they came back, then the newest change first. A query keeps the conversations
//   // whose name or one of whose messages has all its words (case and diacritics aside) — the
//   // messages' text kept from the last refresh: no page is read for it. Side chats (`/btw`) are
//   // never listed: their conversation's view shows them (one whose conversation is gone is).
#pragma once

#include "workspacemodel.h"

#include "markdown/inlinerenderer.h"

#include <QSet>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
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
    /// Only a side chat's own messages are rows (those from its `side-from:` on): its pane.
    Q_PROPERTY(bool ownOnly READ ownOnly WRITE setOwnOnly NOTIFY ownOnlyChanged FINAL)

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
        PicturePathsRole,
        FlagRole,
        RevealedRole,
        SegmentsRole,
        WakeFromRole,
        WakeNameRole
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
    bool ownOnly() const { return m_ownOnly; }
    void setOwnOnly(bool own);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    /// The Markdown of the message at `row` ("" for a bad row).
    Q_INVOKABLE QString textAt(int row) const;
    /// The row of the message at the page's line `line` (its first line, or one under it), -1
    /// none: a search's match opens the conversation there.
    Q_INVOKABLE int rowOfLine(int line) const;
    /// The first line in the page of the message at `row` (-1 for a bad row).
    Q_INVOKABLE int lineOf(int row) const;
    /// `markdown` as a message of this conversation shows it (renderMessage): the answer being
    /// written, before it is one of the rows.
    Q_INVOKABLE QString render(const QString &markdown) const;
    /// The user's question the message at `row` answers: the user's messages of its turn (a run
    /// of them joined by a blank line) without their pictures; "" when none comes before it.
    Q_INVOKABLE QString questionBefore(int row) const;
    /// Opens a reported or hidden message at `row` in this view (`shown` false folds it again):
    /// the row's `revealed`, kept while the view shows this conversation.
    Q_INVOKABLE void reveal(int row, bool shown);
    /// The segments of `markdown` (renderSegments): the answer being written — rendered again only
    /// when one of its blocks is finished (ChatTurn.partialSettled).
    Q_INVOKABLE QVariantList segments(const QString &markdown) const;

    /// A message's Markdown as the bubble shows it: its blocks as rich text, one paragraph each —
    /// indented by their depth, a list item after its bullet or number (a task's box from the
    /// renderer), a heading in bold, code and tables as preformatted text. Static for tests.
    static QString renderMessage(const QString &markdown, const markdown::RenderContext &context);
    /// A message with a drawing in parts shown one under another (docs/UI.md "AI chat"): the runs
    /// of its other blocks as renderMessage's rich text ("text": `html`), and each drawing — a
    /// closed ```chart block whose table or calc block is in the message ("chart": `chartData`, as
    /// BlockListModel's), a closed ```mermaid block of a type Brulion draws ("diagram"), a
    /// ```math / ```latex / ```tex block or a paragraph that is one `$$…$$` formula ("math": `html`
    /// the formula) — with `code` (what it draws), `source` (the block as written: Copy source) and
    /// `html` (the block as renderMessage shows it: shown when the diagram cannot be drawn). A
    /// drawing that cannot be drawn (a chart's mistake, a query, an unterminated fence, a Mermaid
    /// type not drawn) stays in its text. [] when there is no drawing. Static for tests.
    static QVariantList renderSegments(const QString &markdown, const markdown::RenderContext &context);

Q_SIGNALS:
    void pathChanged();
    void paletteChanged();
    void localeChanged();
    void budgetChanged();
    void ownOnlyChanged();
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
        int number = 0; ///< its number in the page, from 1 (chat::setFlag)
        QString flag;   ///< "" | "reported" | "hidden"
        QVariantList segments;
        QString wakeFrom; ///< the sub-conversation a wake message comes from ("" none)
        QString wakeName;
    };
    QList<Row> m_rows;
    QSet<int> m_revealed; // the numbers of the folded messages opened again
    QString m_path;
    QVariantMap m_palette;
    QString m_locale = QStringLiteral("pl");
    QString m_page;
    QString m_title;
    bool m_exists = false;
    int m_budget = 0;
    int m_omitted = 0;
    bool m_ownOnly = false;
};

class ChatListModel : public WorkspaceListModel {
    Q_OBJECT
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY localeChanged FINAL)
    /// Words the conversations are filtered by ("" all of them).
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged FINAL)
    /// The archived conversations are listed (their section unfolded).
    Q_PROPERTY(bool showArchived READ showArchived WRITE setShowArchived NOTIFY showArchivedChanged FINAL)
    /// How many are archived (with a query: of those that match it), listed or not / pinned.
    Q_PROPERTY(int archivedCount READ archivedCount NOTIFY sectionsChanged FINAL)
    Q_PROPERTY(int pinnedCount READ pinnedCount NOTIFY sectionsChanged FINAL)
    /// Which conversations: "" those of the notebook (no `project:` — the AI chat), "*" every
    /// thread of Code mode, a project's id its threads (docs/CODE.md).
    Q_PROPERTY(QString project READ project WRITE setProject NOTIFY projectChanged FINAL)
    /// How many are listed (rows).
    Q_PROPERTY(int count READ count NOTIFY sectionsChanged FINAL)

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
        QueuedRole,
        PinnedRole,
        ArchivedRole,
        SectionRole,
        AskingRole,
        QuestionRole,
        FailureRole,
        UnreadRole,
        TitleMatchesRole,
        MatchTextRole,
        MatchMatchesRole,
        MatchLineRole,
        ProjectRole
    };
    explicit ChatListModel(QObject *parent = nullptr);
    QString project() const { return m_project; }
    void setProject(const QString &project);
    int count() const { return int(m_rows.size()); }

    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    bool showArchived() const { return m_showArchived; }
    void setShowArchived(bool show);
    int archivedCount() const { return m_archivedCount; }
    int pinnedCount() const { return m_pinnedCount; }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    /// The row of the conversation `path`, -1 when it is not listed.
    Q_INVOKABLE int rowOf(const QString &path) const;
    /// The conversation of row `row` ("" for a bad row) / the first line of its message that
    /// matches the query (-1 none).
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE int matchLineAt(int row) const;

    /// "2026-11-15 Co mam jutro" → {"2026-11-15", "Co mam jutro"}; a name without a date: {"", name}.
    static std::pair<QString, QString> splitTitle(const QString &title);
    /// The part of `line` (one line of a message's Markdown) the list shows for a match of
    /// `terms` (contentutil searchTerms()): its plain words (chat::preview), from a little before
    /// the first match on when it is far in ("…" before it).
    static QString matchExcerpt(const QString &line, const QStringList &terms);

Q_SIGNALS:
    void localeChanged();
    void queryChanged();
    void showArchivedChanged();
    void sectionsChanged();
    void projectChanged();

protected:
    void refresh() override;
    int rows() const override { return int(m_rows.size()); }

private:
    /// A conversation as the last refresh read it.
    struct Entry {
        QString path;
        QString title;
        QString date;
        qint64 modified = 0;
        QString preview;
        bool fromAi = false;
        int messages = 0;
        bool pinned = false;
        bool archived = false;
        QString project;
        /// Its messages for a query: {first line in the page, text, the text folded}.
        struct Text {
            int line = 0;
            QString text;
            QString folded;
        };
        QList<Text> texts;
        QString foldedTitle;
    };
    struct Row {
        QString path;
        QString title;
        QString date;
        qint64 modified = 0;
        QString preview;
        bool fromAi = false;
        int messages = 0;
        bool pinned = false;
        bool archived = false;
        QString section;
        bool asking = false;
        QString question;
        QString failure;
        bool unread = false;
        qint64 cameBack = 0;
        QList<int> titleMatches;
        QString matchText;
        QList<int> matchMatches;
        int matchLine = -1;
        QString project;
        bool operator==(const Row &) const = default;
    };
    QList<Entry> m_all;
    QList<Row> m_rows;
    QString m_locale = QStringLiteral("pl");
    QString m_query;
    bool m_showArchived = false;
    QString m_project;
    int m_archivedCount = 0;
    int m_pinnedCount = 0;
    QList<QMetaObject::Connection> m_turnsChanged;
    QTimer m_stateTimer;
    /// The turns' state changed: the rows' running / queued again.
    void turnsChanged();
    /// The rows of m_all as they are listed now: the query, the sections, what needs the user.
    void rebuild();
};

} // namespace brulion::content
