#include "notefind.h"

#include "blocklistmodel.h"
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
    m_text.clear();
    m_matches.clear();
    m_current = -1;
    m_appliedQuery.clear();
    Q_EMIT matchesChanged();
}

void NoteFind::setQuery(const QString &query)
{
    // One line: a line break pasted into the field would never match.
    QString line = query;
    line.replace(u'\r', u' ').replace(u'\n', u' ');
    if (m_query == line)
        return;
    m_query = line;
    Q_EMIT queryChanged();
    schedule(true);
}

void NoteFind::setMatchCase(bool on)
{
    if (m_options.matchCase == on)
        return;
    m_options.matchCase = on;
    Q_EMIT optionsChanged();
    schedule(true);
}

void NoteFind::setMatchDiacritics(bool on)
{
    if (m_options.matchDiacritics == on)
        return;
    m_options.matchDiacritics = on;
    Q_EMIT optionsChanged();
    schedule(true);
}

void NoteFind::setWholeWord(bool on)
{
    if (m_options.wholeWord == on)
        return;
    m_options.wholeWord = on;
    Q_EMIT optionsChanged();
    schedule(true);
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
    const QString text = sourceOfSearch();
    if (sourceMode()) {
        m_origin = position >= 0 ? offsetOfSourcePosition(text, position) : 0;
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
    const bool sameSearch = m_appliedQuery == m_query && m_appliedOptions == m_options;
    if (sameSearch && text == m_text && !m_jump)
        return;
    const qsizetype keep = m_current >= 0 && m_current < m_matches.size() ? m_matches.at(m_current).start : -1;
    m_text = std::move(text);
    m_matches = text::findMatches(m_text, m_query, m_options);
    m_appliedQuery = m_query;
    m_appliedOptions = m_options;
    const bool jump = std::exchange(m_jump, false);
    // After an edit the match shown stays current (the nearest one at or after its place); the
    // caret is left alone.
    const qsizetype from = jump || keep < 0 ? m_origin : keep;
    const auto at = std::lower_bound(m_matches.cbegin(), m_matches.cend(), from,
                                     [](const text::TextRange &r, qsizetype v) { return r.start < v; });
    const int nearest = m_matches.isEmpty() ? -1 : at == m_matches.cend() ? 0 : int(std::distance(m_matches.cbegin(), at));
    if (jump) {
        m_current = -1;
        Q_EMIT matchesChanged();
        if (nearest >= 0)
            show(nearest);
        return;
    }
    m_current = keep >= 0 ? nearest : -1;
    Q_EMIT matchesChanged();
}

QVariantMap NoteFind::targetOf(const text::TextRange &range) const
{
    QVariantMap out;
    out[u"sourceFrom"_s] = int(sourcePosition(m_text, range.start));
    out[u"sourceTo"_s] = int(sourcePosition(m_text, range.end()));
    if (sourceMode()) {
        out[u"source"_s] = true;
        return out;
    }
    const PageController::TextLocation place = m_controller->locateText(range.start, range.length);
    out[u"source"_s] = false;
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

void NoteFind::next()
{
    refresh();
    if (m_matches.isEmpty())
        return;
    if (m_current >= 0) {
        show(int((m_current + 1) % m_matches.size()));
        return;
    }
    const auto at = std::lower_bound(m_matches.cbegin(), m_matches.cend(), m_origin,
                                     [](const text::TextRange &r, qsizetype v) { return r.start < v; });
    show(at == m_matches.cend() ? 0 : int(std::distance(m_matches.cbegin(), at)));
}

void NoteFind::previous()
{
    refresh();
    if (m_matches.isEmpty())
        return;
    if (m_current >= 0) {
        show(int((m_current + m_matches.size() - 1) % m_matches.size()));
        return;
    }
    const auto at = std::lower_bound(m_matches.cbegin(), m_matches.cend(), m_origin,
                                     [](const text::TextRange &r, qsizetype v) { return r.start < v; });
    const qsizetype before = std::distance(m_matches.cbegin(), at) - 1;
    show(before >= 0 ? int(before) : int(m_matches.size() - 1));
}

bool NoteFind::replaceCurrent(const QString &replacement)
{
    if (!m_controller)
        return false;
    refresh();
    // The text may have changed under a pending recount: count on what is there now.
    if (m_text != sourceOfSearch()) {
        m_timer.stop();
        recount();
    }
    if (m_matches.isEmpty())
        return false;
    if (m_current < 0) {
        next();
        return false;
    }
    const text::TextRange range = m_matches.at(m_current);
    QString line = replacement;
    line.replace(u'\r', u' ').replace(u'\n', u' ');
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
    QString line = replacement;
    line.replace(u'\r', u' ').replace(u'\n', u' ');
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
