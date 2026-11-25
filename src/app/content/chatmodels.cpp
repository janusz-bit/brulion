#include "chatmodels.h"
#include "calc/table.h"
#include "chart/chart.h"
#include "diagram/diagram.h"
#include "markdown/math.h"

#include "chat/chat.h"
#include "chatlineage.h"
#include "contentutil.h"
#include "dates/dates.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"
#include "workspace.h"
#include "workspacechats.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

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

namespace {

/// The parts of a message rendered: its blocks as rich text (`html`, the bubble's text) and, when
/// it has a drawing — a chart, a diagram, a display formula —, the segments shown one under
/// another (`segments`, else empty).
struct Rendered {
    QString html;
    QVariantList segments;
};

/// The rich text of `parts` as one: no space under the last paragraph (the bubble's padding, or
/// the gap between segments, is the room). "" for none.
QString joinParts(QStringList parts)
{
    if (parts.isEmpty())
        return QString();
    static const QRegularExpression room(u"margin-bottom:\\d+px;"_s);
    parts.last().replace(room, u"margin-bottom:0px;"_s);
    return parts.join(QString());
}

/// A fenced block's text between its fences — only when it is closed: an answer cut off mid-way
/// leaves its drawing open, and it is shown as written.
std::optional<QString> closedFence(const QString &body)
{
    QStringList lines = body.split(u'\n');
    if (lines.size() < 2)
        return std::nullopt;
    const QString open = lines.first().trimmed();
    const QString close = lines.last().trimmed();
    if (open.isEmpty() || (open.at(0) != u'`' && open.at(0) != u'~'))
        return std::nullopt;
    const QChar marker = open.at(0);
    qsizetype length = 0;
    while (length < open.size() && open.at(length) == marker)
        ++length;
    if (close.size() < length || std::ranges::any_of(close, [marker](QChar c) { return c != marker; }))
        return std::nullopt;
    lines.removeFirst();
    lines.removeLast();
    return lines.join(u'\n');
}

/// The LaTeX of a paragraph that is one display formula (`$$…$$` and nothing else), nullopt
/// otherwise.
std::optional<QString> displayFormula(const QString &body)
{
    const QString text = body.trimmed();
    if (!text.startsWith(u"$$"_s) || !text.endsWith(u"$$"_s))
        return std::nullopt;
    const QList<markdown::MathSpan> spans = markdown::findMathSpans(text);
    if (spans.size() != 1 || !spans.first().display || spans.first().from != 0 || spans.first().to != text.size())
        return std::nullopt;
    const markdown::MathSpan &span = spans.first();
    return text.sliced(span.contentFrom, span.contentTo - span.contentFrom).trimmed();
}

/// The numbers of a ```chart block of a message (BlockListModel's chartData: ok, labels, labelsName,
/// series), read from the message's own tables and calc blocks; nullopt when it cannot be drawn
/// (a mistake, no such table, a query — a message runs none): it is shown as written then.
std::optional<QVariantMap> messageChart(const QString &markdown, int line, const QString &source,
                                        calc::CalcLocale locale)
{
    const chart::SpecResult spec = chart::parseSpec(source);
    if (spec.error || spec.spec.source == chart::SourceKind::Query)
        return std::nullopt;
    const chart::DataResult data = chart::resolveData(spec.spec, chart::pageSources(markdown, line, locale));
    if (data.error)
        return std::nullopt;
    QVariantList series;
    for (const chart::Series &serie : data.data.series) {
        QVariantList values;
        values.reserve(serie.values.size());
        for (const double v : serie.values)
            values.append(std::isfinite(v) ? QVariant(v) : QVariant()); // a gap: null
        series.append(QVariantMap{{u"name"_s, serie.name}, {u"values"_s, values}});
    }
    return QVariantMap{{u"ok"_s, true},
                       {u"labels"_s, data.data.labels},
                       {u"labelsName"_s, data.data.labelsName},
                       {u"series"_s, series}};
}

/// A segment of a message: every key every kind has (the delegates read them all).
QVariantMap segment(const QString &kind, const QString &html, const QString &code = QString(),
                    const QString &source = QString(), const QVariantMap &chartData = QVariantMap())
{
    return QVariantMap{{u"kind"_s, kind}, {u"html"_s, html}, {u"code"_s, code}, {u"source"_s, source},
                       {u"chartData"_s, chartData}};
}

Rendered renderParts(const QString &markdown, const markdown::RenderContext &context)
{
    const markdown::BlockDocument doc(markdown);
    const markdown::InlineRenderer renderer(context);
    const calc::CalcLocale calcLocale =
            context.locale == dates::UiLocale::English ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    static const QRegularExpression ordered(u"^\\s*(\\d{1,9}[.)])"_s);
    QStringList parts;   // every block's rich text (the whole message's)
    QStringList run;     // the blocks since the last drawing (a text segment)
    QVariantList segments;
    bool drawn = false;  // a segment that is a drawing
    const auto endRun = [&] {
        if (!run.isEmpty())
            segments.append(segment(u"text"_s, joinParts(std::exchange(run, {}))));
    };
    for (int i = 0; i < doc.count(); ++i) {
        const markdown::BlockEntry &b = doc.at(i);
        const int indent = b.depth * kIndent;
        // Room under a block: more after a blank line (a paragraph's end), little between the
        // items of a list.
        const int below = b.trailing.contains(u'\n') ? 10 : b.kind == BlockKind::ListItem ? 2 : 6;
        QString part;
        if (b.kind == BlockKind::Table)
            part = renderTable(b.body, renderer, context.palette.muted, below, indent);
        if (!part.isEmpty()) {
            // (A table.)
        } else if (b.kind == BlockKind::Code || b.kind == BlockKind::Table || b.kind == BlockKind::Html
                   || b.kind == BlockKind::Frontmatter) {
            part = u"<pre style=\"margin-top:0px; margin-bottom:%1px; margin-left:%2px;\">%3</pre>"_s.arg(below).arg(indent).arg(
                    (b.kind == BlockKind::Code ? codeOf(b.body) : b.body).toHtmlEscaped());
        } else if (b.kind == BlockKind::Rule) {
            part = u"<hr/>"_s;
        } else {
            QString html = renderer.renderBlock(b.body, b.kind);
            if (b.kind == BlockKind::Heading)
                html = u"<b>"_s + html + u"</b>"_s;
            if (b.kind == BlockKind::ListItem && !b.taskStatus) {
                const auto number = ordered.match(b.prefix);
                html = (number.hasMatch() ? number.captured(1) + u"&nbsp;"_s : u"•&nbsp;"_s) + html;
            }
            if (!html.isEmpty())
                part = u"<p style=\"margin-top:0px; margin-bottom:%1px; margin-left:%2px;\">%3</p>"_s.arg(below).arg(indent).arg(html);
        }
        if (part.isEmpty())
            continue;
        parts.append(part);
        // A drawing (docs/UI.md "AI chat"): a closed ```chart block whose numbers are in the message,
        // a closed ```mermaid block of a type Brulion draws, a ```math block or a paragraph that is
        // one $$…$$ formula. Its segment keeps the block's rich text as written: the view shows it
        // when the diagram cannot be drawn after all.
        QVariantMap visual;
        if (b.kind == BlockKind::Code) {
            const QString lang = b.lang.toLower();
            if (const std::optional<QString> inner = closedFence(b.body); inner && !inner->trimmed().isEmpty()) {
                if (lang == u"chart") {
                    if (const std::optional<QVariantMap> data = messageChart(markdown, b.line, *inner, calcLocale))
                        visual = segment(u"chart"_s, joinParts({part}), *inner, b.body, *data);
                } else if (lang == u"mermaid") {
                    const diagram::Kind kind = diagram::detectKind(*inner);
                    if (kind != diagram::Kind::Unsupported && kind != diagram::Kind::Unknown)
                        visual = segment(u"diagram"_s, joinParts({part}), *inner, b.body);
                } else if (lang == u"math" || lang == u"latex" || lang == u"tex") {
                    if (const QString formula = markdown::renderMath(*inner, true); !formula.isEmpty())
                        visual = segment(u"math"_s, formula, *inner, b.body);
                }
            }
        } else if (b.kind == BlockKind::Paragraph) {
            if (const std::optional<QString> latex = displayFormula(b.body)) {
                if (const QString formula = markdown::renderMath(*latex, true); !formula.isEmpty())
                    visual = segment(u"math"_s, formula, *latex, b.body);
            }
        }
        if (visual.isEmpty()) {
            run.append(part);
            continue;
        }
        endRun();
        segments.append(visual);
        drawn = true;
    }
    endRun();
    Rendered out;
    out.html = parts.isEmpty() ? u"…"_s : joinParts(parts);
    if (drawn)
        out.segments = segments;
    return out;
}

} // namespace

QString ChatMessageModel::renderMessage(const QString &markdown, const markdown::RenderContext &context)
{
    return renderParts(markdown, context).html;
}

QVariantList ChatMessageModel::renderSegments(const QString &markdown, const markdown::RenderContext &context)
{
    return renderParts(markdown, context).segments;
}

QString ChatMessageModel::render(const QString &markdown) const
{
    shell::Workspace *ws = workspace();
    if (markdown.isEmpty() || !ws || !ws->isOpen())
        return QString();
    return renderMessage(markdown, renderContext(vaultIndex(), m_path, effectiveToday(QString(), ws),
                                                 uiLocale(m_locale), paletteFromMap(m_palette)));
}

QVariantList ChatMessageModel::segments(const QString &markdown) const
{
    shell::Workspace *ws = workspace();
    if (markdown.isEmpty() || !ws || !ws->isOpen())
        return {};
    return renderSegments(markdown, renderContext(vaultIndex(), m_path, effectiveToday(QString(), ws),
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
        // (A side chat's pane: not the copy of its conversation.)
        const qsizetype first = m_ownOnly && !conversation.side.isEmpty()
                                        ? std::min<qsizetype>(conversation.sideFrom, conversation.messages.size())
                                        : 0;
        QDate lastDay;
        for (qsizetype i = first; i < conversation.messages.size(); ++i) {
            const chat::Message &m = conversation.messages.at(i);
            Row r;
            r.role = m.role == chat::Role::User ? u"user"_s : u"assistant"_s;
            r.text = m.text;
            // (The pictures are shown apart: the bubble's text is the rest.)
            const QString words = m.pictures.isEmpty() ? m.text : chat::withoutPictures(m.text);
            if (!words.trimmed().isEmpty() || m.pictures.isEmpty()) {
                Rendered rendered = renderParts(words, context);
                r.html = std::move(rendered.html);
                r.segments = std::move(rendered.segments);
            }
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
            if (!m.from.isEmpty()) {
                const QString from = shell::ChatLineage::resolveParent(ws, m.from, m_path);
                r.wakeFrom = from.isEmpty() ? m.from : from;
                r.wakeName = ws->chats()->nameOf(r.wakeFrom);
            }
            next.append(r);
        }
    }
    // A message opened again whose mark went: folded again when it is marked once more.
    for (auto it = m_revealed.begin(); it != m_revealed.end();) {
        const int number = *it;
        const auto row = std::ranges::find_if(next, [number](const Row &r) { return r.number == number; });
        if (row == next.end() || row->flag.isEmpty())
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
                return a.role == b.role && a.text == b.text && a.html == b.html && a.segments == b.segments
                       && a.line == b.line && a.time == b.time && a.day == b.day && a.pictures == b.pictures
                       && a.sent == b.sent && a.firstSent == b.firstSent && a.flag == b.flag && a.wakeFrom == b.wakeFrom
                       && a.wakeName == b.wakeName;
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
    case SegmentsRole: return r.segments;
    case SentRole: return r.sent;
    case FirstSentRole: return r.firstSent;
    case FlagRole: return r.flag;
    case RevealedRole: return !r.flag.isEmpty() && m_revealed.contains(r.number);
    case WakeFromRole: return r.wakeFrom;
    case WakeNameRole: return r.wakeName;
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
            {FlagRole, "flag"},           {RevealedRole, "revealed"},
            {SegmentsRole, "segments"},   {WakeFromRole, "wakeFrom"},
            {WakeNameRole, "wakeName"}};
}

void ChatMessageModel::setOwnOnly(bool own)
{
    if (m_ownOnly == own)
        return;
    m_ownOnly = own;
    Q_EMIT ownOnlyChanged();
    scheduleRefresh();
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

int ChatMessageModel::lineOf(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).line : -1;
}

int ChatMessageModel::rowOfLine(int line) const
{
    int found = -1;
    for (int i = 0; i < m_rows.size() && m_rows.at(i).line <= line; ++i)
        found = i;
    return line < 0 ? -1 : found;
}

// ---- ChatListModel ---------------------------------------------------------------------------------

ChatListModel::ChatListModel(QObject *parent)
    : WorkspaceListModel(parent)
{
    // The turns that run, wait, ask or failed and the unread answers: the rows say so, the ones
    // that need the user come first (no reading of the pages). Coalesced: a turn changes a lot.
    m_stateTimer.setSingleShot(true);
    m_stateTimer.setInterval(0);
    connect(&m_stateTimer, &QTimer::timeout, this, &ChatListModel::rebuild);
    connect(this, &WorkspaceListModel::workspaceChanged, this, [this] {
        for (const QMetaObject::Connection &c : std::as_const(m_turnsChanged))
            disconnect(c);
        m_turnsChanged.clear();
        if (shell::Workspace *ws = workspace()) {
            shell::WorkspaceChats *chats = ws->chats();
            m_turnsChanged = {
                    connect(chats, &shell::WorkspaceChats::stateChanged, this, &ChatListModel::turnsChanged),
                    connect(chats, &shell::WorkspaceChats::questionsChanged, this, &ChatListModel::turnsChanged),
                    connect(chats, &shell::WorkspaceChats::attentionChanged, this, &ChatListModel::turnsChanged),
            };
        }
    });
}

void ChatListModel::turnsChanged()
{
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {RunningRole, QueuedRole});
    if (!m_stateTimer.isActive())
        m_stateTimer.start();
}

void ChatListModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    Q_EMIT localeChanged();
    scheduleRefresh();
}

void ChatListModel::setProject(const QString &project)
{
    if (m_project == project)
        return;
    m_project = project;
    Q_EMIT projectChanged();
    rebuild();
}

void ChatListModel::setQuery(const QString &query)
{
    if (m_query == query)
        return;
    m_query = query;
    Q_EMIT queryChanged();
    rebuild();
}

void ChatListModel::setShowArchived(bool show)
{
    if (m_showArchived == show)
        return;
    m_showArchived = show;
    Q_EMIT showArchivedChanged();
    rebuild();
}

std::pair<QString, QString> ChatListModel::splitTitle(const QString &title)
{
    return chat::splitTitle(title);
}

QString ChatListModel::matchExcerpt(const QString &line, const QStringList &terms)
{
    constexpr int Before = 24; // characters kept before the first match
    const QString words = chat::preview(line, 400);
    const QList<int> at = matchPositions(words, terms, false);
    if (at.isEmpty() || at.first() <= Before + 8)
        return words;
    // From a word a little before it.
    qsizetype from = at.first() - Before;
    const qsizetype space = words.indexOf(u' ', from);
    if (space >= 0 && space < at.first())
        from = space + 1;
    if (words.at(from).isLowSurrogate())
        ++from;
    return u"…"_s + words.mid(from);
}

void ChatListModel::refresh()
{
    QList<Entry> next;
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
            // A side chat (/btw): shown inside its conversation only — unless that one is gone.
            if (!conversation.side.isEmpty()
                && !shell::ChatLineage::resolveParent(ws, conversation.side, f.path).isEmpty())
                continue;
            Entry e;
            e.path = f.path;
            const auto [date, title] = splitTitle(paths::pageTitle(f.path));
            e.date = date;
            e.title = title;
            e.foldedTitle = text::foldKey(title);
            e.modified = f.mtime;
            const chat::Message &last = conversation.messages.last();
            // (An answer the user reported or hid is not shown here either.)
            e.preview = last.flag == chat::Flag::Reported ? tr("Reported response")
                        : last.flag == chat::Flag::Hidden ? tr("Hidden response")
                                                          : chat::preview(last.text);
            e.fromAi = last.role == chat::Role::Assistant;
            e.messages = int(conversation.messages.size());
            e.pinned = conversation.pinned;
            e.archived = conversation.archived;
            e.project = conversation.project;
            e.texts.reserve(conversation.messages.size());
            for (const chat::Message &m : conversation.messages)
                e.texts.append({m.line, m.text, text::foldKey(m.text)});
            next.append(e);
        }
    }
    m_all = std::move(next);
    rebuild();
}

void ChatListModel::rebuild()
{
    m_stateTimer.stop();
    const shell::Workspace *ws = workspace();
    shell::WorkspaceChats *chats = ws ? ws->chats() : nullptr;
    const QStringList terms = searchTerms(m_query);
    const bool searching = !terms.isEmpty();
    const auto hasAll = [&terms](const QString &folded) {
        return std::ranges::all_of(terms, [&folded](const QString &t) { return folded.contains(t); });
    };
    QList<Row> next;
    int archived = 0, pinned = 0;
    for (const Entry &e : std::as_const(m_all)) {
        // The notebook's conversations, or Code mode's threads (of one project).
        if (m_project.isEmpty() ? !e.project.isEmpty()
            : m_project == u"*" ? e.project.isEmpty()
                                : e.project != m_project)
            continue;
        Row r;
        if (searching) {
            if (hasAll(e.foldedTitle)) {
                r.titleMatches = matchPositions(e.title, terms, false);
            } else {
                // The newest message that has all of the words, at its line that has the most.
                const auto found = std::find_if(e.texts.crbegin(), e.texts.crend(),
                                                [&](const Entry::Text &t) { return hasAll(t.folded); });
                if (found == e.texts.crend())
                    continue;
                QString best;
                qsizetype most = -1;
                for (const QString &line : found->text.split(u'\n')) {
                    const QString folded = text::foldKey(line);
                    const qsizetype n =
                            std::ranges::count_if(terms, [&](const QString &t) { return folded.contains(t); });
                    if (n > most) {
                        most = n;
                        best = line;
                    }
                }
                r.matchText = matchExcerpt(best, terms);
                r.matchMatches = matchPositions(r.matchText, terms, false);
                r.matchLine = found->line;
            }
        }
        if (e.archived)
            ++archived;
        else if (e.pinned)
            ++pinned;
        if (e.archived && !m_showArchived && !searching)
            continue;
        r.path = e.path;
        r.title = e.title;
        r.date = e.date;
        r.modified = e.modified;
        r.preview = e.preview;
        r.fromAi = e.fromAi;
        r.messages = e.messages;
        r.pinned = e.pinned;
        r.archived = e.archived;
        r.project = e.project;
        r.section = e.archived ? u"archived"_s : e.pinned ? u"pinned"_s : u"recent"_s;
        if (chats) {
            r.asking = chats->asking(e.path);
            r.question = r.asking ? chats->questionText(e.path).section(u'\n', 0, 0).trimmed() : QString();
            r.failure = chats->failureOf(e.path).section(u'\n', 0, 0).trimmed();
            r.unread = chats->isUnread(e.path);
            r.cameBack =
                    r.asking || !r.failure.isEmpty() || r.unread ? std::max<qint64>(chats->cameBack(e.path), 1) : 0;
        }
        next.append(r);
    }
    const auto rank = [](const Row &r) { return r.archived ? 2 : r.pinned ? 0 : 1; };
    std::sort(next.begin(), next.end(), [&rank](const Row &a, const Row &b) {
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        // What needs the user first: by when it came back to them.
        if ((a.cameBack > 0) != (b.cameBack > 0))
            return a.cameBack > 0;
        if (a.cameBack != b.cameBack)
            return a.cameBack > b.cameBack;
        return a.modified != b.modified ? a.modified > b.modified : a.path < b.path;
    });
    const qsizetype before = m_rows.size();
    applyRows(m_rows, next, [](const Row &r) { return r.path; }, [](const Row &a, const Row &b) { return a == b; });
    if (archived != m_archivedCount || pinned != m_pinnedCount || before != m_rows.size()) {
        m_archivedCount = archived;
        m_pinnedCount = pinned;
        Q_EMIT sectionsChanged();
    }
}

QVariant ChatListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    const auto positions = [](const QList<int> &list) {
        QVariantList out;
        out.reserve(list.size());
        for (const int p : list)
            out.append(p);
        return out;
    };
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
    case PinnedRole: return r.pinned;
    case ArchivedRole: return r.archived;
    case SectionRole: return r.section;
    case AskingRole: return r.asking;
    case QuestionRole: return r.question;
    case FailureRole: return r.failure;
    case UnreadRole: return r.unread;
    case TitleMatchesRole: return positions(r.titleMatches);
    case MatchTextRole: return r.matchText;
    case MatchMatchesRole: return positions(r.matchMatches);
    case MatchLineRole: return r.matchLine;
    case ProjectRole: return r.project;
    default: return {};
    }
}

QHash<int, QByteArray> ChatListModel::roleNames() const
{
    return {{PathRole, "path"},
            {TitleRole, "title"},
            {DateRole, "date"},
            {DateLabelRole, "dateLabel"},
            {PreviewRole, "preview"},
            {FromAiRole, "fromAi"},
            {MessagesRole, "messages"},
            {RunningRole, "running"},
            {QueuedRole, "queued"},
            {PinnedRole, "pinned"},
            {ArchivedRole, "archived"},
            {SectionRole, "section"},
            {AskingRole, "asking"},
            {QuestionRole, "question"},
            {FailureRole, "failure"},
            {UnreadRole, "unread"},
            {TitleMatchesRole, "titleMatches"},
            {MatchTextRole, "matchText"},
            {MatchMatchesRole, "matchMatches"},
            {MatchLineRole, "matchLine"},
            {ProjectRole, "project"}};
}

int ChatListModel::rowOf(const QString &path) const
{
    const auto it = std::ranges::find_if(m_rows, [&path](const Row &r) { return r.path.compare(path, Qt::CaseInsensitive) == 0; });
    return rowIndex(m_rows, it);
}

QString ChatListModel::pathAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).path : QString();
}

int ChatListModel::matchLineAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).matchLine : -1;
}

} // namespace brulion::content
