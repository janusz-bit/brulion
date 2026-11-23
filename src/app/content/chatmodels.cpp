#include "chatmodels.h"
#include "calc/table.h"

#include "chat/chat.h"
#include "contentutil.h"
#include "dates/dates.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspace.h"
#include "workspacechats.h"

#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::content {

namespace {

constexpr int kIndent = 16; // pixels per level of a list

/// A fenced code block's lines without its fences and their common indentation.
QString codeOf(const QString &body)
{
    QStringList lines = body.split(u'\n');
    static const QRegularExpression fence(u"^\\s*(```|~~~)"_s);
    if (!lines.isEmpty() && fence.match(lines.first()).hasMatch())
        lines.removeFirst();
    if (!lines.isEmpty() && fence.match(lines.last()).hasMatch())
        lines.removeLast();
    return lines.join(u'\n');
}

} // namespace

// ---- ChatMessageModel ------------------------------------------------------------------------------

ChatMessageModel::ChatMessageModel(QObject *parent)
    : WorkspaceListModel(parent)
{
    setRefreshDelay(60);
}

void ChatMessageModel::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    m_revealed.clear();
    Q_EMIT pathChanged();
    scheduleRefresh();
}

void ChatMessageModel::setPalette(const QVariantMap &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    Q_EMIT paletteChanged();
    scheduleRefresh();
}

void ChatMessageModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    scheduleRefresh();
}

void ChatMessageModel::setBudget(int budget)
{
    if (m_budget == budget)
        return;
    m_budget = budget;
    Q_EMIT budgetChanged();
    scheduleRefresh();
}

namespace {

/// A table block of a message as a rich-text table: the header row bold, each cell's text
/// rendered (escaped) as a paragraph's, the columns aligned as the delimiter row says, thin
/// borders in the muted colour. Empty when `body` is no table (it is shown as written then).
/// QTextDocument lays the columns out in the bubble's width, wrapping a long cell.
QString renderTable(const QString &body, const markdown::InlineRenderer &renderer, const QString &border, int below,
                    int indent)
{
    // (In a list item the rows after the first are indented.)
    QStringList lines = body.split(u'\n');
    for (QString &line : lines)
        line = line.trimmed();
    const std::optional<calc::TableModel> table = calc::parseTable(lines.join(u'\n'));
    if (!table || table->header.isEmpty())
        return QString();
    const auto cell = [&](bool header, const QString &text, int column) {
        const calc::TableAlign align = column < table->aligns.size() ? table->aligns.at(column) : calc::TableAlign::None;
        const QString alignment = align == calc::TableAlign::Right    ? u"right"_s
                                  : align == calc::TableAlign::Center ? u"center"_s
                                                                      : u"left"_s;
        const QString html = renderer.renderBlock(text, BlockKind::Paragraph);
        return header ? u"<th align=\"%1\"><b>%2</b></th>"_s.arg(alignment, html)
                      : u"<td align=\"%1\">%2</td>"_s.arg(alignment, html);
    };
    QString out = u"<table border=\"1\" cellspacing=\"0\" cellpadding=\"4\" style=\"border-collapse:collapse; "
                  "border-color:%1; margin-top:0px; margin-bottom:%2px; margin-left:%3px;\"><tr>"_s.arg(border)
                          .arg(below)
                          .arg(indent);
    for (int c = 0; c < table->header.size(); ++c)
        out += cell(true, table->header.at(c), c);
    out += u"</tr>"_s;
    for (const QStringList &row : table->rows) {
        out += u"<tr>"_s;
        for (int c = 0; c < table->header.size(); ++c)
            out += cell(false, c < row.size() ? row.at(c) : QString(), c);
        out += u"</tr>"_s;
    }
    return out + u"</table>"_s;
}

} // namespace

QString ChatMessageModel::renderMessage(const QString &markdown, const markdown::RenderContext &context)
{
    const markdown::BlockDocument doc(markdown);
    const markdown::InlineRenderer renderer(context);
    static const QRegularExpression ordered(u"^\\s*(\\d{1,9}[.)])"_s);
    QStringList parts;
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &b = doc.at(i);
        const int indent = b.depth * kIndent;
        // Room under a block: more after a blank line (a paragraph's end), little between the
        // items of a list.
        const int below = b.trailing.contains(u'\n') ? 10 : b.kind == BlockKind::ListItem ? 2 : 6;
        if (b.kind == BlockKind::Table) {
            const QString table = renderTable(b.body, renderer, context.palette.muted, below, indent);
            if (!table.isEmpty()) {
                parts.append(table);
                continue;
            }
        }
        switch (b.kind) {
        case BlockKind::Code:
        case BlockKind::Table:
        case BlockKind::Html:
        case BlockKind::Frontmatter:
            parts.append(u"<pre style=\"margin-top:0px; margin-bottom:%1px; margin-left:%2px;\">%3</pre>"_s.arg(below).arg(indent).arg(
                    (b.kind == BlockKind::Code ? codeOf(b.body) : b.body).toHtmlEscaped()));
            continue;
        case BlockKind::Rule:
            parts.append(u"<hr/>"_s);
            continue;
        default:
            break;
        }
        QString html = renderer.renderBlock(b.body, b.kind);
        if (b.kind == BlockKind::Heading)
            html = u"<b>"_s + html + u"</b>"_s;
        if (b.kind == BlockKind::ListItem && !b.taskStatus) {
            const auto number = ordered.match(b.prefix);
            html = (number.hasMatch() ? number.captured(1) + u"&nbsp;"_s : u"•&nbsp;"_s) + html;
        }
        if (html.isEmpty())
            continue;
        parts.append(u"<p style=\"margin-top:0px; margin-bottom:%1px; margin-left:%2px;\">%3</p>"_s.arg(below).arg(indent).arg(html));
    }
    if (parts.isEmpty())
        return u"…"_s;
    // (No space under the last paragraph: the bubble's padding is the room.)
    QString &last = parts.last();
    static const QRegularExpression room(u"margin-bottom:\\d+px;"_s);
    last.replace(room, u"margin-bottom:0px;"_s);
    return parts.join(QString());
}

QString ChatMessageModel::render(const QString &markdown) const
{
    shell::Workspace *ws = workspace();
    if (markdown.isEmpty() || !ws || !ws->isOpen())
        return QString();
    return renderMessage(markdown, renderContext(vaultIndex(), m_path, effectiveToday(QString(), ws),
                                                 uiLocale(m_locale), paletteFromMap(m_palette)));
}

void ChatMessageModel::refresh()
{
    QList<Row> next;
    QString page, title;
    bool exists = false;
    int omitted = 0;
    shell::Workspace *ws = workspace();
    if (ws && ws->isOpen() && !m_path.isEmpty() && ws->exists(m_path)) {
        bool ok = false;
        const QString content = ws->read(m_path, &ok);
        const chat::Conversation conversation = ok ? chat::parse(content) : chat::Conversation();
        exists = !conversation.messages.isEmpty();
        page = conversation.page;
        title = ChatListModel::splitTitle(paths::pageTitle(m_path)).second;
        const QDate today = effectiveToday(QString(), ws);
        const markdown::RenderContext context =
                renderContext(vaultIndex(), m_path, today, uiLocale(m_locale), paletteFromMap(m_palette));
        omitted = chat::firstSent(conversation, m_budget);
        QDate lastDay;
        for (qsizetype i = 0; i < conversation.messages.size(); ++i) {
            const chat::Message &m = conversation.messages.at(i);
            Row r;
            r.role = m.role == chat::Role::User ? u"user"_s : u"assistant"_s;
            r.text = m.text;
            // (The pictures are shown apart: the bubble's text is the rest.)
            const QString words = m.pictures.isEmpty() ? m.text : chat::withoutPictures(m.text);
            r.html = words.trimmed().isEmpty() && !m.pictures.isEmpty() ? QString() : renderMessage(words, context);
            r.line = m.line;
            if (m.time.isValid()) {
                r.time = m.time.toString(u"HH:mm"_s);
                if (m.time.date() != lastDay) {
                    r.day = dates::formatHuman(DateSpec{m.time.date(), QTime()}, uiLocale(m_locale), today);
                    if (!r.day.isEmpty())
                        r.day[0] = r.day.at(0).toUpper(); // ("Dziś", "Wczoraj": a heading of its own)
                }
                lastDay = m.time.date();
            }
            for (const QString &picture : m.pictures) {
                const QUrl url = ws->chats()->pictureUrl(picture, m_path);
                if (!url.isValid())
                    continue;
                r.pictures.append(url);
                const QString resolved = ws->chats()->picturePath(picture, m_path);
                r.picturePaths.append(resolved.isEmpty() ? picture : resolved);
            }
            r.sent = i >= omitted;
            r.firstSent = omitted > 0 && i == omitted;
            r.number = int(i + 1);
            r.flag = m.flag == chat::Flag::Reported ? u"reported"_s : m.flag == chat::Flag::Hidden ? u"hidden"_s : QString();
            next.append(r);
        }
    }
    // A message opened again whose mark went: folded again when it is marked once more.
    for (auto it = m_revealed.begin(); it != m_revealed.end();) {
        if (*it > next.size() || next.at(*it - 1).flag.isEmpty())
            it = m_revealed.erase(it);
        else
            ++it;
    }
    // Rows by their number: a message keeps its place, a new one comes last (its delegate and the
    // list's position stay as they are) — also when the lines above move (a line of the
    // frontmatter: a partner chosen, an answer reported).
    applyRows(
            m_rows, next, [](const Row &r) { return r.number; },
            [](const Row &a, const Row &b) {
                return a.role == b.role && a.text == b.text && a.html == b.html && a.line == b.line && a.time == b.time
                       && a.day == b.day && a.pictures == b.pictures && a.sent == b.sent && a.firstSent == b.firstSent
                       && a.flag == b.flag;
            });
    m_page = page;
    m_title = title;
    m_exists = exists;
    m_omitted = omitted;
    Q_EMIT loaded();
}

QVariant ChatMessageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case RoleRole: return r.role;
    case HtmlRole: return r.html;
    case TextRole: return r.text;
    case LineRole: return r.line;
    case TimeRole: return r.time;
    case DayRole: return r.day;
    case PicturesRole: return QVariant::fromValue(r.pictures);
    case PicturePathsRole: return r.picturePaths;
    case SentRole: return r.sent;
    case FirstSentRole: return r.firstSent;
    case FlagRole: return r.flag;
    case RevealedRole: return !r.flag.isEmpty() && m_revealed.contains(r.number);
    default: return {};
    }
}

QHash<int, QByteArray> ChatMessageModel::roleNames() const
{
    return {{RoleRole, "sender"},         {HtmlRole, "messageHtml"},
            {TextRole, "markdown"},       {LineRole, "line"},
            {TimeRole, "time"},           {DayRole, "day"},
            {PicturesRole, "pictures"},   {SentRole, "sent"},
            {FirstSentRole, "firstSent"}, {PicturePathsRole, "picturePaths"},
            {FlagRole, "flag"},           {RevealedRole, "revealed"}};
}

QString ChatMessageModel::textAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).text : QString();
}

QString ChatMessageModel::questionBefore(int row) const
{
    if (row <= 0 || row >= m_rows.size())
        return {};
    int i = row - 1;
    while (i >= 0 && m_rows.at(i).role != u"user")
        --i; // (the answers of its turn before it)
    QStringList questions;
    for (; i >= 0 && m_rows.at(i).role == u"user"; --i) {
        const QString text = chat::withoutPictures(m_rows.at(i).text).trimmed();
        if (!text.isEmpty())
            questions.prepend(text);
    }
    return questions.join(u"\n\n"_s);
}

void ChatMessageModel::reveal(int row, bool shown)
{
    if (row < 0 || row >= m_rows.size())
        return;
    const int number = m_rows.at(row).number;
    if (shown == m_revealed.contains(number))
        return;
    if (shown)
        m_revealed.insert(number);
    else
        m_revealed.remove(number);
    Q_EMIT dataChanged(index(row), index(row), {RevealedRole});
}

// ---- ChatListModel ---------------------------------------------------------------------------------

ChatListModel::ChatListModel(QObject *parent)
    : WorkspaceListModel(parent)
{
    // The turns that run or wait: the rows say so (no reading of the pages).
    connect(this, &WorkspaceListModel::workspaceChanged, this, [this] {
        disconnect(m_turnsChanged);
        if (shell::Workspace *ws = workspace())
            m_turnsChanged =
                    connect(ws->chats(), &shell::WorkspaceChats::stateChanged, this, &ChatListModel::turnsChanged);
    });
}

void ChatListModel::turnsChanged()
{
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {RunningRole, QueuedRole});
}

void ChatListModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    scheduleRefresh();
}

std::pair<QString, QString> ChatListModel::splitTitle(const QString &title)
{
    return chat::splitTitle(title);
}

void ChatListModel::refresh()
{
    QList<Row> next;
    shell::Workspace *ws = workspace();
    if (ws && ws->isOpen() && ws->vault()) {
        const QList<FileStat> files = ws->vault()->markdownFiles();
        for (const FileStat &f : files) {
            if (!chat::inFolder(f.path))
                continue;
            bool ok = false;
            const QString content = ws->read(f.path, &ok);
            if (!ok)
                continue;
            const chat::Conversation conversation = chat::parse(content);
            if (conversation.messages.isEmpty())
                continue;
            Row r;
            r.path = f.path;
            const auto [date, title] = splitTitle(paths::pageTitle(f.path));
            r.date = date;
            r.title = title;
            r.modified = f.mtime;
            const chat::Message &last = conversation.messages.last();
            // (An answer the user reported or hid is not shown here either.)
            r.preview = last.flag == chat::Flag::Reported ? tr("Reported response")
                        : last.flag == chat::Flag::Hidden ? tr("Hidden response")
                                                          : chat::preview(last.text);
            r.fromAi = last.role == chat::Role::Assistant;
            r.messages = int(conversation.messages.size());
            next.append(r);
        }
        std::sort(next.begin(), next.end(), [](const Row &a, const Row &b) {
            return a.modified != b.modified ? a.modified > b.modified : a.path < b.path;
        });
    }
    applyRows(m_rows, next, [](const Row &r) { return r.path; }, [](const Row &a, const Row &b) {
        return a.title == b.title && a.date == b.date && a.modified == b.modified && a.preview == b.preview
               && a.fromAi == b.fromAi && a.messages == b.messages;
    });
}

QVariant ChatListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case PathRole: return r.path;
    case TitleRole: return r.title;
    case DateRole: return r.date;
    case DateLabelRole: return humanTime(r.modified, effectiveToday(QString(), workspace()), uiLocale(m_locale));
    case PreviewRole: return r.preview;
    case FromAiRole: return r.fromAi;
    case MessagesRole: return r.messages;
    case RunningRole:
    case QueuedRole: {
        const shell::Workspace *ws = workspace();
        if (!ws)
            return false;
        return role == RunningRole ? ws->chats()->isRunning(r.path) : ws->chats()->isQueued(r.path);
    }
    default: return {};
    }
}

QHash<int, QByteArray> ChatListModel::roleNames() const
{
    return {{PathRole, "path"},           {TitleRole, "title"},     {DateRole, "date"},
            {DateLabelRole, "dateLabel"}, {PreviewRole, "preview"}, {FromAiRole, "fromAi"},
            {MessagesRole, "messages"},   {RunningRole, "running"}, {QueuedRole, "queued"}};
}

int ChatListModel::rowOf(const QString &path) const
{
    const auto it = std::ranges::find_if(m_rows, [&path](const Row &r) { return r.path.compare(path, Qt::CaseInsensitive) == 0; });
    return rowIndex(m_rows, it);
}

} // namespace brulion::content
