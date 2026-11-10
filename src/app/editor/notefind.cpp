#include "notefind.h"

#include "blocklistmodel.h"
#include "markdown/frontmatter.h"
#include "pagecontroller.h"

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// Recount this long after the last change: about what a keystroke costs nobody on a usual note, and
/// on a big one (a long journal, an imported book) typing is not held up.
constexpr int RecountDelayMs = 120;
constexpr int RecountDelayBigMs = 400;
constexpr qsizetype BigNote = 200 * 1024;

/// Position in a TextEdit (every line break one "\n") of `offset` of the text with its own line
/// endings.
qsizetype sourcePosition(QStringView text, qsizetype offset)
{
    qsizetype position = offset;
    for (qsizetype i = 0; i + 1 < offset; ++i) {
        if (text.at(i) == u'\r' && text.at(i + 1) == u'\n')
            --position;
    }
    return position;
}

/// The offset of a TextEdit position: the inverse of sourcePosition().
qsizetype offsetOfSourcePosition(QStringView text, qsizetype position)
{
    qsizetype seen = 0;
    for (qsizetype i = 0; i < text.size(); ++i, ++seen) {
        if (seen >= position)
            return i;
        if (text.at(i) == u'\r' && i + 1 < text.size() && text.at(i + 1) == u'\n')
            ++i;
    }
    return text.size();
}

/// `text` as one line: a line break pasted into the field would never match.
QString oneLine(QString text)
{
    text.replace(u'\r', u' ').replace(u'\n', u' ');
    return text;
}

/// The index of the first of `matches` that starts at or after `origin` (their count when none does).
qsizetype firstFrom(const QList<text::TextRange> &matches, qsizetype origin)
{
    return std::ranges::lower_bound(matches, origin, {}, &text::TextRange::start) - matches.cbegin();
}

} // namespace

NoteFind::NoteFind(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(RecountDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &NoteFind::recount);
}

bool NoteFind::sourceMode() const
{
    return m_controller && m_controller->sourceMode();
}

void NoteFind::setController(PageController *controller)
{
    if (m_controller == controller)
        return;
    m_controller = controller;
    m_current = -1;
    connectController();
    Q_EMIT controllerChanged();
    schedule(false);
}

void NoteFind::connectController()
{
    for (const QMetaObject::Connection &c : std::as_const(m_connections))
        disconnect(c);
    m_connections.clear();
    if (!m_controller || !m_active)
        return;
    const auto changed = [this] { schedule(false); };
    BlockListModel *blocks = m_controller->blocks();
    m_connections = { connect(blocks, &QAbstractItemModel::dataChanged, this, changed),
                      connect(blocks, &QAbstractItemModel::rowsInserted, this, changed),
                      connect(blocks, &QAbstractItemModel::rowsRemoved, this, changed),
                      connect(blocks, &QAbstractItemModel::rowsMoved, this, changed),
                      connect(blocks, &QAbstractItemModel::modelReset, this, changed),
                      connect(m_controller.data(), &PageController::sourceTextChanged, this, changed),
                      connect(m_controller.data(), &PageController::sourceModeChanged, this, changed),
                      connect(m_controller.data(), &PageController::loadingChanged, this, changed) };
}

void NoteFind::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    connectController();
    Q_EMIT activeChanged();
    if (m_active) {
        schedule(false);
        return;
    }
    m_timer.stop();
    m_enter = Enter::None;
    m_text.clear();
    m_matches.clear();
    m_current = -1;
    m_appliedQuery.clear();
    Q_EMIT matchesChanged();
}

void NoteFind::setQuery(const QString &query)
{
    QString line = oneLine(query);
    if (m_query == line)
        return;
    m_query = std::move(line);
    Q_EMIT queryChanged();
    schedule(true);
}

void NoteFind::setOption(bool text::FindOptions::*option, bool on)
{
    if (m_options.*option == on)
        return;
    m_options.*option = on;
    Q_EMIT optionsChanged();
    schedule(true);
}

void NoteFind::setWrap(bool wrap)
{
    if (m_wrap == wrap)
        return;
    m_wrap = wrap;
    Q_EMIT wrapChanged();
}

bool NoteFind::stale() const
{
    return m_timer.isActive() || m_jump || m_appliedQuery != m_query || m_appliedOptions != m_options
           || m_text != sourceOfSearch();
}

void NoteFind::enter(bool first)
{
    m_enter = first ? Enter::First : Enter::Last;
    m_jump = false;
    if (!m_controller || !m_controller->loaded() || !m_active)
        return; // once the note is loaded (recount())
    m_timer.stop();
    recount();
}

void NoteFind::schedule(bool jump)
{
    m_jump = m_jump || jump;
    if (!m_active)
        return;
    m_timer.setInterval(m_text.size() > BigNote ? RecountDelayBigMs : RecountDelayMs);
    m_timer.start();
}

QString NoteFind::sourceOfSearch() const
{
    if (!m_controller || !m_controller->loaded())
        return {};
    return m_controller->sourceMode() ? m_controller->sourceText() : m_controller->text();
}

void NoteFind::setOriginHere(int position)
{
    if (!m_controller)
        return;
    if (sourceMode()) {
        m_origin = position >= 0 ? offsetOfSourcePosition(sourceOfSearch(), position) : 0;
        return;
    }
    const int row = m_controller->editingRow() >= 0 ? m_controller->editingRow() : m_controller->selectedRow();
    m_origin = row >= 0 ? m_controller->textOffsetOfRow(row) : 0;
}

void NoteFind::refresh()
{
    if (m_timer.isActive() || m_appliedQuery != m_query || m_appliedOptions != m_options) {
        m_timer.stop();
        recount();
    }
}

void NoteFind::recount()
{
    QString text = sourceOfSearch();
    const Enter enter = m_enter;
    if (enter != Enter::None) {
        // Entering a note (the journal stream): its first or last match once it is loaded.
        if (!m_controller || !m_controller->loaded())
            return;
        m_enter = Enter::None;
        m_jump = false;
    } else if (m_appliedQuery == m_query && m_appliedOptions == m_options && text == m_text && !m_jump) {
        return;
    }
    const qsizetype keep = m_current >= 0 && m_current < m_matches.size() ? m_matches.at(m_current).start : -1;
    m_text = std::move(text);
    m_matches = text::findMatches(m_text, m_query, m_options);
    m_appliedQuery = m_query;
    m_appliedOptions = m_options;
    countRisks();
    m_current = -1;
    const bool jump = std::exchange(m_jump, false);
    // After an edit no match is current: which one is on show is not known any more (the caret is
    // left alone), and replacing must not hit one nobody has seen. The next goes from here.
    if (enter == Enter::None && !jump && keep >= 0)
        m_origin = keep;
    Q_EMIT matchesChanged();
    if (enter != Enter::None) {
        if (m_matches.isEmpty())
            Q_EMIT endReached(enter == Enter::First);
        else
            show(enter == Enter::First ? 0 : count() - 1);
    } else if (jump) {
        showForward(firstFrom(m_matches, m_origin));
    }
}

void NoteFind::countRisks()
{
    m_linkMatches = 0;
    m_frontmatterMatches = 0;
    if (m_matches.isEmpty())
        return;
    const std::optional<markdown::FrontmatterBlock> frontmatter = markdown::findFrontmatter(m_text);
    const qsizetype bodyStart = frontmatter ? frontmatter->bodyStart : 0;
    for (const text::TextRange &range : std::as_const(m_matches)) {
        if (range.start < bodyStart)
            ++m_frontmatterMatches;
        else if (text::isInsideLink(m_text, range))
            ++m_linkMatches;
    }
}

QVariantMap NoteFind::targetOf(text::TextRange range) const
{
    const bool source = sourceMode();
    QVariantMap out{{u"sourceFrom"_s, int(sourcePosition(m_text, range.start))},
                    {u"sourceTo"_s, int(sourcePosition(m_text, range.end()))},
                    {u"source"_s, source}};
    if (source)
        return out;
    const PageController::TextLocation place = m_controller->locateText(range.start, range.length);
    out[u"row"_s] = place.row;
    out[u"from"_s] = place.from;
    out[u"to"_s] = place.to;
    out[u"editable"_s] = place.editable;
    return out;
}

void NoteFind::show(int index)
{
    if (!m_controller || index < 0 || index >= m_matches.size())
        return;
    m_current = index;
    Q_EMIT matchesChanged();
    Q_EMIT targetRequested(targetOf(m_matches.at(index)));
}

void NoteFind::showForward(qsizetype index)
{
    if (index >= m_matches.size() && !m_wrap)
        Q_EMIT endReached(true); // nothing from here on in this note
    else
        show(int(index < m_matches.size() ? index : 0)); // (no match at all: nothing to show)
}

void NoteFind::next()
{
    refresh();
    showForward(m_current >= 0 ? m_current + 1 : firstFrom(m_matches, m_origin));
}

void NoteFind::previous()
{
    refresh();
    // The match before the current one, else the last one before the origin.
    const qsizetype before = (m_current >= 0 ? m_current : firstFrom(m_matches, m_origin)) - 1;
    if (before < 0 && !m_wrap)
        Q_EMIT endReached(false);
    else
        show(int(before >= 0 ? before : m_matches.size() - 1));
}

bool NoteFind::replaceCurrent(const QString &replacement)
{
    if (!m_controller)
        return false;
    // Something changed under a pending recount: the current match is not what is on show.
    if (m_jump || m_appliedQuery != m_query || m_appliedOptions != m_options || m_text != sourceOfSearch()) {
        m_timer.stop();
        recount();
        if (m_current < 0)
            next();
        return false;
    }
    m_timer.stop();
    if (m_matches.isEmpty())
        return false;
    if (m_current < 0) {
        next();
        return false;
    }
    const text::TextRange range = m_matches.at(m_current);
    const QString line = oneLine(replacement);
    if (QStringView(m_text).sliced(range.start, range.length) == line) {
        showForward(m_current + 1);
        return false;
    }
    if (sourceMode()) {
        Q_EMIT sourceReplaceRequested({ QVariantMap{ { u"from"_s, int(sourcePosition(m_text, range.start)) },
                                                     { u"to"_s, int(sourcePosition(m_text, range.end())) } } },
                                      line);
    } else if (!m_controller->replaceTextRanges({ range }, line)) {
        return false;
    }
    m_origin = range.start + line.size();
    m_jump = true;
    m_timer.stop();
    recount();
    return true;
}

int NoteFind::replaceAll(const QString &replacement)
{
    if (!m_controller)
        return 0;
    m_timer.stop();
    m_jump = false;
    recount();
    const QList<text::TextRange> ranges = m_matches;
    if (ranges.isEmpty())
        return 0;
    const QString line = oneLine(replacement);
    if (text::replaceMatches(m_text, ranges, line) == m_text)
        return 0;
    if (sourceMode()) {
        QVariantList positions;
        positions.reserve(ranges.size());
        qsizetype removed = 0; // "\r\n" pairs passed: one step through the text, not one per match
        qsizetype scanned = 0;
        for (const text::TextRange &range : ranges) {
            for (qsizetype i = scanned; i + 1 < range.start; ++i) {
                if (m_text.at(i) == u'\r' && m_text.at(i + 1) == u'\n')
                    ++removed;
            }
            scanned = std::max(scanned, range.start - 1);
            positions.append(QVariantMap{ { u"from"_s, int(range.start - removed) },
                                          { u"to"_s, int(range.end() - removed) } });
        }
        Q_EMIT sourceReplaceRequested(positions, line);
    } else if (!m_controller->replaceTextRanges(ranges, line)) {
        return 0;
    }
    m_current = -1;
    m_origin = 0;
    m_timer.stop();
    recount();
    return int(ranges.size());
}

} // namespace brulion::editor
