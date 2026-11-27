// PageView's spell marks (docs/ATRAMENT.md "The block editor in Atrament (M2)", step 2; docs/UI.md
// "Spell checking"): the misspelled words of the edited text's prose — the words of each line's
// SpellRange (markdownstyles.h, through EditStyler's extras), MarkdownHighlighter::highlightSpelling's
// rule: the word ending at the caret waits while it is typed — and, with SpellCheck::wholePage, of
// the drawn rows: SpellCheck::wrongWords() over the block's Markdown, each word put on the display
// text through the rendered text's source map when it is shown as written (spellmarks.h's rule,
// BlockListModel::misspelledInRendered()). Underlined with a wavy line (what Qt Quick could not
// draw: the QML editor's line is straight) in the spelling's colour. A right-click on a marked word
// (a tap on one in the edited text on touch UIs) asks for its menu.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "renderedtextitem.h"
#include "spellcheck.h"

#include <algorithm>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

at::TextSpan wavy(int from, int to, at::Color color)
{
    at::TextStyleRefinement r;
    at::UnderlineStyle u;
    u.color = color;
    u.thickness = 1;
    u.wavy = true;
    r.underline = std::optional<at::UnderlineStyle>(u);
    return {at::Range{from, to}, std::move(r)};
}

QStringView viewOf(std::u16string_view s)
{
    return QStringView(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

// The views whose prose spell checking covers (BlockListModel's spellChecked()).
bool spellChecked(const QString &view)
{
    return view == u"text" || view == u"heading" || view == u"quote" || view == u"callout";
}

// The display range of source range [from, to) when it is shown as written: every piece of the
// source map over it maps character by character (spellmarks.h's verbatim anchors: a Markdown
// link's text, a chip — shown as a whole — are not, even when their text is the word itself);
// {−1, −1} otherwise.
std::pair<qsizetype, qsizetype> shownAsWritten(const markdown::RenderedText &text, qsizetype from, qsizetype to)
{
    bool any = false;
    for (const markdown::RenderedSourceSegment &s : text.source) {
        if (s.sourceTo <= from || s.sourceFrom >= to)
            continue;
        if (!s.linear())
            return {-1, -1};
        any = true;
    }
    if (!any)
        return {-1, -1};
    const auto range = text.displayRange(from, to);
    if (range.first < 0 || range.second - range.first != to - from)
        return {-1, -1};
    return range;
}

// The document index of model row `row` (the rows are the document's blocks in order: the same
// index but for a page whose rows differ, found by the block's uid then).
int documentIndex(const BlockListModel &model, int row)
{
    const quint64 uid = model.uidAt(row);
    const markdown::BlockDocument &doc = model.document();
    if (row >= 0 && row < doc.count() && doc.at(row).uid == uid)
        return row;
    return doc.indexOfUid(uid);
}

} // namespace

void PageView::setSpellCheck(SpellCheck *spell, at::Color color, at::Context<PageView> &cx)
{
    if (spell == m_spell && color == m_spellColor)
        return;
    m_spellColor = color;
    if (spell != m_spell) {
        for (const QMetaObject::Connection &c : m_spellConnections)
            QObject::disconnect(c);
        m_spellConnections.clear();
        m_spell = spell;
        if (spell) {
            // New verdicts (or none any more), the checker on or off, the whole page's marks on or
            // off: the edited text is styled again and the rows drawn again.
            at::App *app = &cx.app();
            const at::EntityId self = cx.entityId();
            auto again = [app, self] {
                app->updateEntity<PageView>(self, [](PageView &v, at::Context<PageView> &c) { v.applyEditStyler(c); c.notify(); });
            };
            m_spellConnections.push_back(QObject::connect(spell, &SpellCheck::revisionChanged, spell, again));
            m_spellConnections.push_back(QObject::connect(spell, &SpellCheck::activeChanged, spell, again));
            m_spellConnections.push_back(QObject::connect(spell, &SpellCheck::wholePageChanged, spell, again));
        }
    }
    applyEditStyler(cx);
    cx.notify();
}

bool PageView::editSpellActive() const
{
    return m_spell && m_spell->active();
}

void PageView::appendEditSpelling(std::u16string_view text, const std::vector<EditStyler::Extras::Prose> &prose,
                                  std::vector<at::TextSpan> &spans)
{
    const int caret = m_editSpell.caret;
    for (const EditStyler::Extras::Prose &p : prose) {
        const SpellRange &r = p.range;
        if (r.from < 0 || r.from > p.lineLength)
            continue; // no prose on the line
        const int to = r.to < 0 ? p.lineLength : std::min(r.to, p.lineLength);
        if (to <= r.from)
            continue;
        const QStringView line = viewOf(text.substr(std::size_t(p.lineStart), std::size_t(p.lineLength)));
        for (spell::WordSpan w : spell::findWords(line.sliced(r.from, to - r.from), r.lineStart)) {
            w.from += r.from;
            w.to += r.from;
            if (m_spell->verdict(spell::checkedText(line, w)) != SpellCheck::Verdict::Wrong)
                continue; // (a pending one: revisionChanged styles the text again)
            spell::WordSpan inText = w;
            inText.from += p.lineStart;
            inText.to += p.lineStart;
            m_editSpell.wrong.push_back(inText);
            // The word being typed (the caret right after it) waits until it is finished.
            if (int(inText.to) == caret && !inText.dot) {
                m_editSpell.suppressed = int(inText.to);
                continue;
            }
            spans.push_back(wavy(int(inText.from), int(inText.to), m_spellColor));
        }
    }
}

void PageView::editCaretMoved(int caret, at::Context<PageView> &cx)
{
    if (caret == m_editSpell.caret)
        return;
    m_editSpell.caret = caret;
    if (!editSpellActive() || !m_input)
        return;
    // The word left unmarked for the old caret, or a wrong one ending at the new caret: styled
    // again (MarkdownHighlighter::setSpellCaret).
    bool again = m_editSpell.suppressed >= 0 && m_editSpell.suppressed != caret;
    for (const spell::WordSpan &w : m_editSpell.wrong)
        again = again || (int(w.to) == caret && !w.dot);
    if (again)
        m_input->update(cx.app(), [](at::TextInput &t, at::Context<at::TextInput> &c) { t.restyle(c); });
}

void PageView::appendRowSpelling(int row, const markdown::RenderedText &text, std::vector<at::TextSpan> &spans) const
{
    if (!m_spell || !m_spell->active() || !m_spell->wholePage() || !m_model)
        return;
    const QModelIndex index = m_model->index(row, 0);
    if (!spellChecked(index.data(BlockListModel::ViewRole).toString()))
        return;
    const int i = documentIndex(*m_model, row);
    if (i < 0)
        return;
    const QString &body = m_model->document().at(i).body;
    for (const spell::WordSpan &w : m_spell->wrongWords(body)) {
        // Only a word shown as it is written (a link's text, a chip: shown as a whole, left alone).
        const auto [from, to] = shownAsWritten(text, w.from, w.to);
        if (from >= 0)
            spans.push_back(wavy(int(from), int(to), m_spellColor));
    }
}

std::vector<at::Range> PageView::spellMarks(int row) const
{
    std::vector<at::Range> out;
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return out;
    const auto rendered = m_model->index(row, 0).data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    if (!rendered.d)
        return out;
    std::vector<at::TextSpan> spans;
    appendRowSpelling(row, *rendered.d, spans);
    for (const at::TextSpan &s : spans)
        out.push_back(s.range);
    return out;
}

std::optional<PageView::Misspelling> PageView::misspelledAt(int row, at::Point p) const
{
    if (!m_model || !m_spell || !m_spell->active() || row < 0 || row >= m_model->rowCount())
        return std::nullopt;
    if (row == editedRow() && m_input && m_app) {
        if (m_inputCode)
            return std::nullopt;
        const at::TextInput &input = m_input->read(*m_app);
        const std::optional<int> at = input.offsetForPoint(p);
        if (!at)
            return std::nullopt;
        const QStringView text = viewOf(input.text());
        // The word there: inside it or right after it (MarkdownHighlighter::misspelledAt).
        for (const spell::WordSpan &w : m_editSpell.wrong) {
            // (Not the one being typed: it is not marked.)
            if (*at < w.from || *at > w.to || int(w.to) == m_editSpell.suppressed)
                continue;
            Misspelling m;
            m.row = row;
            m.edited = true;
            m.from = int(w.from);
            m.to = int(w.to);
            m.word = spell::checkedText(text, w);
            m.text = text.sliced(w.from, w.length()).toString();
            return m;
        }
        return std::nullopt;
    }
    if (!m_spell->wholePage())
        return std::nullopt;
    const at::TextLayoutHandle *shown = rowText(row);
    if (!shown)
        return std::nullopt;
    const std::optional<int> display = shown->offsetAt(p);
    const auto rendered = m_model->index(row, 0).data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
    const int i = documentIndex(*m_model, row);
    if (!display || !rendered.d || i < 0
        || !spellChecked(m_model->index(row, 0).data(BlockListModel::ViewRole).toString()))
        return std::nullopt;
    // A word marked there (appendRowSpelling's: shown as written), its end included
    // (BlockListModel::misspelledInRendered()).
    const QString &body = m_model->document().at(i).body;
    for (const spell::WordSpan &w : m_spell->wrongWords(body)) {
        const auto [from, to] = shownAsWritten(*rendered.d, w.from, w.to);
        if (from < 0 || *display < from || *display > to)
            continue;
        Misspelling m;
        m.row = row;
        m.from = int(w.from);
        m.to = int(w.to);
        m.word = spell::checkedText(body, w);
        m.text = body.mid(w.from, w.length());
        return m;
    }
    return std::nullopt;
}

bool PageView::spellingMenuAt(int row, at::Point p)
{
    if (!onSpellingMenu)
        return false;
    const std::optional<Misspelling> m = misspelledAt(row, p);
    if (!m)
        return false;
    onSpellingMenu(*m, p);
    return true;
}

void PageView::editedRowTapped(int row, const at::MouseUpEvent &e)
{
    // Touch UIs: a tap on a misspelled word of the edited text opens its corrections (the tap has
    // placed the caret there first, as on any text; InlineEditor's TapHandler).
    if (e.cancelled || e.pointer != at::PointerKind::Touch || !m_metrics.touch)
        return;
    spellingMenuAt(row, e.position);
}

} // namespace brulion::editor::atr
