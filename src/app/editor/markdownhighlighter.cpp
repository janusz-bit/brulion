#include "markdownhighlighter.h"

#include "richtext_p.h"

#include "spell/spellwords.h"
#include "syntax/luau.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QQuickTextDocument>
#include <QScopedValueRollback>
#include <QTextBlock>
#include <QTextBlockUserData>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// Property holding the format's name (tests read it back from QTextLayout::formats()).
constexpr int NameProperty = QTextFormat::UserProperty + 1;

/// A text this long (characters) gets its spelling in slices (a large edit, a pass over every
/// line); a smaller one at once.
constexpr int LargeText = 20000;
/// The time a slice of spelling takes at most before the event loop runs again.
constexpr int SpellSliceMs = 6;

/// What the highlighting found on a line, kept on its QTextBlock (it follows the line when lines
/// before it are added or removed): its reminder tokens (MarkdownHighlighter::reminderChips) and
/// the part of it that is prose for spell checking.
struct LineData : QTextBlockUserData {
    QList<MarkdownHighlighter::LineChip> chips;
    MarkdownHighlighter::SpellRange spell;
};

QColor toColor(const QVariant &v)
{
    if (!v.isValid())
        return {};
    if (v.metaType() == QMetaType::fromType<QColor>())
        return v.value<QColor>();
    return QColor::fromString(v.toString());
}

} // namespace

MarkdownHighlighter::MarkdownHighlighter(QObject *parent) : QSyntaxHighlighter(parent)
{
    updateStyleOptions();
    rebuildFormats();
    // Verdicts arrive in batches: the lines waiting for them are highlighted once per pass.
    m_spellTimer.setSingleShot(true);
    m_spellTimer.setInterval(0);
    connect(&m_spellTimer, &QTimer::timeout, this, [this] { rehighlightReadySpelling(true); });
}

MarkdownHighlighter::~MarkdownHighlighter()
{
    // ~QSyntaxHighlighter() detaches from the document, which edits it (contentsChange): by then
    // this object is only a QSyntaxHighlighter, so the line-height and the chips' handlers must be
    // gone first.
    disconnect(m_contentsConnection);
    disconnect(m_bulkConnection);
    disconnect(m_bulkDoneConnection);
    for (const QMetaObject::Connection &c : std::as_const(m_layoutConnections))
        disconnect(c);
}

void MarkdownHighlighter::setTextDocument(QQuickTextDocument *document)
{
    if (m_quickDocument == document)
        return;
    m_quickDocument = document;
    attach(document ? document->textDocument() : nullptr);
    Q_EMIT textDocumentChanged();
}

void MarkdownHighlighter::attach(QTextDocument *document)
{
    if (m_contentsConnection)
        disconnect(m_contentsConnection);
    disconnect(m_bulkConnection);
    disconnect(m_bulkDoneConnection);
    m_spellReady.clear();
    m_spellPending.clear();
    m_spellMarked.clear();
    m_spellSweep = m_spellSweepEnd = -1;
    for (const QMetaObject::Connection &c : std::as_const(m_layoutConnections))
        disconnect(c);
    m_layoutConnections.clear();
    // QTextDocument reports edits (contentsChange) only once it has a layout; a TextEdit's
    // document always has one, a bare one (tests, C++ users) gets it here.
    if (document)
        (void)document->documentLayout();
    // A large edit (the text of a page set, a long paste) is highlighted at once, its spelling in
    // slices afterwards (around QSyntaxHighlighter's own handler, connected in setDocument()).
    if (document)
        m_bulkConnection = connect(document, &QTextDocument::contentsChange, this,
                                   [this](int, int, int added) { m_spellDeferred = added > LargeText; });
    setDocument(document);
    if (document) {
        m_bulkDoneConnection =
                connect(document, &QTextDocument::contentsChange, this, [this](int from, int, int added) {
                    if (!std::exchange(m_spellDeferred, false))
                        return;
                    if (spellActive())
                        sweepSpelling(from, from + added);
                });
        m_contentsConnection = connect(document, &QTextDocument::contentsChange, this,
                                       [this](int from, int, int added) { applyLineHeight(from, added); });
        // The chips' shapes follow the layout (a new width, font or text), as RichTextPills' do.
        QAbstractTextDocumentLayout *layout = document->documentLayout();
        m_layoutConnections = {
                connect(layout, &QAbstractTextDocumentLayout::documentSizeChanged, this,
                        &MarkdownHighlighter::refreshChips),
                connect(layout, &QAbstractTextDocumentLayout::update, this, &MarkdownHighlighter::refreshChips),
        };
        applyLineHeight();
        // QSyntaxHighlighter highlights a non-empty document on the next event loop pass and
        // ignores edits until then: run that pass now (no flash of plain text, typing right
        // away is highlighted).
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
        rehighlight();
    }
    refreshChips();
}

void MarkdownHighlighter::setReminderChips(bool on)
{
    if (m_reminderChips == on)
        return;
    m_reminderChips = on;
    updateStyleOptions();
    rebuildFormats();
    rehighlight();
    Q_EMIT reminderChipsChanged();
    refreshChips();
}

void MarkdownHighlighter::setChipInset(qreal px)
{
    if (qFuzzyCompare(m_chipInset + 1, px + 1))
        return;
    m_chipInset = px;
    Q_EMIT chipInsetChanged();
    refreshChips();
}

void MarkdownHighlighter::setChipLocale(const QString &locale)
{
    if (m_chipLocale == locale)
        return;
    m_chipLocale = locale;
    updateStyleOptions();
    Q_EMIT chipLocaleChanged();
    if (m_reminderChips)
        rehighlight();
}

void MarkdownHighlighter::setChipToday(const QString &today)
{
    if (m_chipToday == today)
        return;
    m_chipToday = today;
    updateStyleOptions();
    Q_EMIT chipTodayChanged();
    if (m_reminderChips)
        rehighlight();
}

std::optional<std::pair<int, int>> MarkdownHighlighter::chipWhere(int position,
                                                                  const std::function<bool(int, int)> &match) const
{
    QTextDocument *doc = document();
    if (!doc || !m_reminderChips || position < 0)
        return std::nullopt;
    const QTextBlock block = doc->findBlock(position);
    const auto *data = block.isValid() ? static_cast<const LineData *>(block.userData()) : nullptr;
    if (!data)
        return std::nullopt;
    for (const LineChip &chip : data->chips) {
        const int from = block.position() + chip.from;
        const int to = block.position() + chip.to;
        if (match(from, to))
            return std::pair{from, to};
    }
    return std::nullopt;
}

int MarkdownHighlighter::chipEndingAt(int position) const
{
    const auto chip = chipWhere(position, [position](int, int to) { return to == position; });
    return chip ? chip->first : -1;
}

int MarkdownHighlighter::chipStartingAt(int position) const
{
    const auto chip = chipWhere(position, [position](int from, int) { return from == position; });
    return chip ? chip->second : -1;
}

int MarkdownHighlighter::chipFromAround(int position) const
{
    const auto chip = chipWhere(position, [position](int from, int to) { return from < position && position < to; });
    return chip ? chip->first : -1;
}

int MarkdownHighlighter::chipToAround(int position) const
{
    const auto chip = chipWhere(position, [position](int from, int to) { return from < position && position < to; });
    return chip ? chip->second : -1;
}

void MarkdownHighlighter::collapseLineChips()
{
    if (m_lineChips.isEmpty())
        return;
    QTextDocument *doc = document();
    const QFont font = doc ? doc->defaultFont() : QFont();
    m_chipFont = font;
    const QFontMetricsF metrics(font);
    QFont tiny = font;
    tiny.setPixelSize(1);
    const QFontMetricsF tinyMetrics(tiny);
    const QString line = currentBlock().text();
    for (const LineChip &chip : std::as_const(m_lineChips)) {
        if (chip.to <= chip.from || chip.to > line.size())
            continue;
        // The token's characters: invisible, in a 1-px font. The first one (a surrogate pair:
        // both halves) takes the label's width; the others next to none (1 %: Qt reads a
        // percentage of 0 as "unchanged"), so no line breaks inside the token.
        const int head = line.at(chip.from).isHighSurrogate() && chip.from + 1 < chip.to ? 2 : 1;
        QTextCharFormat hidden;
        hidden.setProperty(NameProperty, u"chip"_s);
        hidden.setForeground(QColor(Qt::transparent));
        hidden.setProperty(QTextFormat::FontPixelSize, 1);
        QTextCharFormat first = hidden;
        first.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        first.setFontLetterSpacing(std::max<qreal>(0, metrics.horizontalAdvance(chip.label)
                                                              - tinyMetrics.horizontalAdvance(line.mid(chip.from, head))));
        QTextCharFormat rest = hidden;
        rest.setFontLetterSpacingType(QFont::PercentageSpacing);
        rest.setFontLetterSpacing(1);
        mergeRange(chip.from, head, first);
        mergeRange(chip.from + head, chip.to - chip.from - head, rest);
    }
}

void MarkdownHighlighter::storeLineData()
{
    const QList<LineChip> chips = std::exchange(m_lineChips, {});
    const auto *data = static_cast<const LineData *>(currentBlockUserData());
    const bool chipsChanged = (data ? data->chips : QList<LineChip>()) != chips;
    if (!chipsChanged && (data ? data->spell : SpellRange()) == m_lineSpell)
        return;
    if (chips.isEmpty() && m_lineSpell == SpellRange()) {
        setCurrentBlockUserData(nullptr);
    } else {
        auto *stored = new LineData;
        stored->chips = chips;
        stored->spell = m_lineSpell;
        setCurrentBlockUserData(stored);
    }
    // (The layout reports the new formats too; a token whose text did not move needs this.)
    if (chipsChanged)
        QMetaObject::invokeMethod(this, &MarkdownHighlighter::refreshChips, Qt::QueuedConnection);
}

void MarkdownHighlighter::refreshChips()
{
    if (m_computingChips)
        return;
    QList<RichTextPill> out;
    QTextDocument *doc = document();
    if (doc && m_reminderChips) {
        const QScopedValueRollback computing(m_computingChips, true);
        QAbstractTextDocumentLayout *layout = doc->documentLayout();
        const QFontMetricsF metrics(doc->defaultFont());
        const QColor reminderBg = color("reminderBackground", QColor(242, 179, 61, 56));
        const QColor alarmBg = color("alarmBackground", QColor(u"#f2b33d"_s));
        const QColor reminderFg = color("reminderChip", color("reminder", QColor(u"#7a4f00"_s)));
        const QColor alarmFg = color("alarmChip", color("alarm", QColor(u"#7a4f00"_s)));
        // A new font (size, family): the chips are laid out again for their labels' new width.
        if (doc->defaultFont() != m_chipFont) {
            m_chipFont = doc->defaultFont();
            QMetaObject::invokeMethod(this, &QSyntaxHighlighter::rehighlight, Qt::QueuedConnection);
        }
        for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
            const auto *data = static_cast<const LineData *>(block.userData());
            if (!data)
                continue;
            for (const LineChip &chip : data->chips) {
                const int from = block.position() + chip.from;
                const int to = block.position() + chip.to;
                const QList<detail::LineSegment> segments = detail::lineSegments(layout, block, from, to);
                bool labelled = false;
                for (const detail::LineSegment &segment : segments) {
                    const QPointF origin = block.layout()->position();
                    // Centred on the glyphs, not on the fixed-height line box (as RichTextPills).
                    const qreal baseline = segment.line.y() + segment.line.ascent();
                    const qreal top = baseline - metrics.ascent() + m_chipInset;
                    const qreal bottom = baseline + metrics.descent() - m_chipInset;
                    RichTextPill pill;
                    pill.x = origin.x() + segment.x1;
                    pill.y = origin.y() + top;
                    pill.width = segment.x2 - segment.x1;
                    pill.height = std::max<qreal>(1, bottom - top);
                    pill.color = chip.alarm ? alarmBg : reminderBg;
                    pill.kind = u"pill"_s;
                    pill.from = from;
                    pill.to = to;
                    // The words on the chip's first part (it has one: the token never wraps).
                    if (!std::exchange(labelled, true)) {
                        pill.label = chip.label;
                        pill.textColor = chip.alarm ? alarmFg : reminderFg;
                        pill.baseline = origin.y() + baseline;
                    }
                    out.append(pill);
                }
            }
        }
    }
    if (out == m_chipShapes)
        return;
    m_chipShapes = std::move(out);
    Q_EMIT chipShapesChanged();
}

void MarkdownHighlighter::setPalette(const QVariantMap &palette)
{
    if (m_palette == palette)
        return;
    m_palette = palette;
    rebuildFormats();
    rehighlight();
    Q_EMIT paletteChanged();
}

void MarkdownHighlighter::setMode(const QString &mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    updateStyleOptions();
    rehighlight();
    Q_EMIT modeChanged();
}

void MarkdownHighlighter::setLanguage(const QString &language)
{
    if (m_language == language)
        return;
    m_language = language;
    updateStyleOptions();
    rehighlight();
    Q_EMIT languageChanged();
}

void MarkdownHighlighter::setMonoFamily(const QString &family)
{
    if (m_monoFamily == family)
        return;
    m_monoFamily = family;
    rebuildFormats();
    rehighlight();
    Q_EMIT monoFamilyChanged();
}

void MarkdownHighlighter::setLineHeight(qreal px)
{
    if (qFuzzyCompare(m_lineHeight + 1, px + 1))
        return;
    m_lineHeight = px;
    applyLineHeight();
    Q_EMIT lineHeightChanged();
}

void MarkdownHighlighter::applyLineHeight(int from, int charsAdded)
{
    QTextDocument *doc = document();
    if (!doc || m_lineHeight <= 0 || m_applyingFormat)
        return;
    const QScopedValueRollback applying(m_applyingFormat, true);
    // The block editor keeps its own undo history; format changes must not land in TextEdit's.
    if (doc->isUndoRedoEnabled())
        doc->setUndoRedoEnabled(false);
    QTextBlock block = charsAdded < 0 ? doc->begin() : doc->findBlock(from);
    const QTextBlock end = charsAdded < 0 ? QTextBlock() : doc->findBlock(from + std::max(0, charsAdded)).next();
    for (; block.isValid() && block != end; block = block.next()) {
        const QTextBlockFormat current = block.blockFormat();
        if (current.lineHeightType() == QTextBlockFormat::FixedHeight && qFuzzyCompare(current.lineHeight(), m_lineHeight))
            continue;
        QTextBlockFormat f;
        f.setLineHeight(m_lineHeight, QTextBlockFormat::FixedHeight);
        QTextCursor(block).mergeBlockFormat(f);
    }
}

QColor MarkdownHighlighter::color(const char *key, const QColor &fallback) const
{
    const QColor c = toColor(m_palette.value(QString::fromLatin1(key)));
    return c.isValid() ? c : fallback;
}

QColor MarkdownHighlighter::paletteColor(const PaletteColor &source) const
{
    QColor c = toColor(m_palette.value(QString(source.key)));
    if (!c.isValid() && !source.fallbackKey.isEmpty())
        c = toColor(m_palette.value(QString(source.fallbackKey)));
    return c.isValid() ? c : QColor::fromString(source.fallback);
}

void MarkdownHighlighter::rebuildFormats()
{
    // One format per style, from the table of markdownstyles.h (what each one sets, the palette
    // keys of its colours with their fallbacks); the flattened runs read the same table.
    for (int i = 0; i < MarkdownStyleCount; ++i) {
        const auto style = MarkdownStyle(i);
        const StyleAttributes a = styleAttributes(style);
        QTextCharFormat f;
        f.setProperty(NameProperty, QString(markdownStyleName(style)));
        if (a.weight != FontWeight::Normal)
            f.setFontWeight(a.weight == FontWeight::Bold ? QFont::Bold : QFont::DemiBold);
        if (a.color != ColorRole::None)
            f.setForeground(paletteColor(colorRolePalette(a.color, m_reminderChips)));
        if (a.underline)
            f.setFontUnderline(true);
        if (a.background != BackgroundRole::None)
            f.setBackground(paletteColor(backgroundRolePalette(a.background)));
        if (a.monospace && !m_monoFamily.isEmpty())
            f.setFontFamilies({m_monoFamily});
        if (a.italic)
            f.setFontItalic(true);
        if (a.strike)
            f.setFontStrikeOut(true);
        m_styleFormats[size_t(i)] = f;
    }
    // A plain underline in the colour: Qt Quick 6.11 draws no wavy SpellCheckUnderline.
    m_spellingFormat = QTextCharFormat();
    m_spellingFormat.setProperty(NameProperty, u"spelling"_s);
    m_spellingFormat.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    m_spellingFormat.setUnderlineColor(m_spellColor);
}

QTextCharFormat MarkdownHighlighter::formatOf(const StyleOp &op) const
{
    QTextCharFormat f = m_styleFormats[size_t(op.style)];
    switch (op.modifier) {
    case StyleModifier::None: break;
    case StyleModifier::DemiBold: f.setFontWeight(QFont::DemiBold); break;
    case StyleModifier::OnHeading: {
        // The heading's weight, the style's colour and name.
        QTextCharFormat heading = m_styleFormats[size_t(MarkdownStyle::Heading)];
        heading.merge(f);
        f = heading;
        break;
    }
    }
    if (op.tint != markdown::HighlightColor::Default)
        f.setBackground(QColor(markdown::highlightTint(op.tint)));
    return f;
}

void MarkdownHighlighter::updateStyleOptions()
{
    m_styleOptions.mode = m_mode == u"code"     ? MarkdownMode::Code
                          : m_mode == u"source" ? MarkdownMode::Source
                                                : MarkdownMode::Inline;
    m_styleOptions.luau = syntax::isLuauLanguage(m_language);
    m_styleOptions.reminderChips = m_reminderChips && m_mode == u"inline";
    m_styleOptions.chipLocale = m_chipLocale == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
    m_styleOptions.chipToday = QDate::fromString(m_chipToday, Qt::ISODate);
}

void MarkdownHighlighter::setRange(int from, int length, const QTextCharFormat &format)
{
    if (length > 0)
        setFormat(from, length, format);
}

void MarkdownHighlighter::mergeRange(int from, int length, const QTextCharFormat &format, const QColor &plainColor)
{
    // Merge with what is already there, one run of equal formats at a time.
    int pos = from;
    const int end = from + length;
    while (pos < end) {
        const QTextCharFormat current = QSyntaxHighlighter::format(pos);
        int run = pos + 1;
        while (run < end && QSyntaxHighlighter::format(run) == current)
            ++run;
        QTextCharFormat merged = current;
        merged.merge(format);
        // Keep the most specific name for tests: the newly merged one.
        merged.setProperty(NameProperty, format.property(NameProperty));
        if (plainColor.isValid() && !current.hasProperty(QTextFormat::ForegroundBrush))
            merged.setForeground(plainColor);
        setFormat(pos, run - pos, merged);
        pos = run;
    }
}

void MarkdownHighlighter::setFindOption(bool text::FindOptions::*option, bool on)
{
    text::FindOptions options = m_findOptions;
    options.*option = on;
    findChangedTo(m_findQuery, options);
}

void MarkdownHighlighter::setFindColor(const QColor &color)
{
    if (m_findColor == color)
        return;
    m_findColor = color;
    Q_EMIT findChanged();
    if (!m_findQuery.isEmpty())
        rehighlight();
}

void MarkdownHighlighter::findChangedTo(const QString &query, text::FindOptions options)
{
    if (m_findQuery == query && m_findOptions == options)
        return;
    const bool shown = !m_findQuery.isEmpty() || !query.isEmpty();
    m_findQuery = query;
    m_findOptions = options;
    Q_EMIT findChanged();
    if (shown)
        rehighlight();
}

void MarkdownHighlighter::setSpellCheck(SpellCheck *spell)
{
    if (m_spell == spell)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_spellConnections))
        disconnect(c);
    m_spellConnections.clear();
    const bool was = spellActive();
    m_spell = spell;
    m_spellPending.clear();
    if (spell) {
        m_spellConnections = {
                connect(spell, &SpellCheck::checked, this,
                        [this](bool changed) {
                            // A word marked before may be right now (added to the dictionary).
                            if (changed)
                                m_spellReady.unite(m_spellMarked);
                            // The lines waiting for verdicts, once every word asked for has its
                            // answer (one pass, not one per batch of a large text).
                            if (m_spell && m_spell->idle())
                                m_spellReady.unite(std::exchange(m_spellPending, {}));
                            if (!m_spellReady.isEmpty() && !m_spellTimer.isActive())
                                m_spellTimer.start();
                        }),
                connect(spell, &SpellCheck::invalidated, this, [this] { respell(); }),
                connect(spell, &SpellCheck::activeChanged, this, [this] { respell(); }),
        };
    }
    Q_EMIT spellCheckChanged();
    if (was || spellActive())
        respell();
}

void MarkdownHighlighter::respell()
{
    QTextDocument *doc = document();
    if (!doc)
        return;
    if (!spellActive()) {
        // Only the marks go.
        m_spellPending.clear();
        m_spellReady.unite(std::exchange(m_spellMarked, {}));
        if (doc->characterCount() <= LargeText)
            rehighlightReadySpelling(false);
        else if (!m_spellReady.isEmpty())
            m_spellTimer.start();
        return;
    }
    if (doc->characterCount() <= LargeText) {
        m_spellReady.clear();
        m_spellPending.clear();
        m_spellSweep = m_spellSweepEnd = -1;
        rehighlight();
        return;
    }
    sweepSpelling(0, doc->characterCount());
}

void MarkdownHighlighter::sweepSpelling(int from, int to)
{
    QTextDocument *doc = document();
    if (!doc)
        return;
    const int first = doc->findBlock(std::max(0, from)).blockNumber();
    QTextBlock lastBlock = doc->findBlock(std::max(0, to));
    const int last = lastBlock.isValid() ? lastBlock.blockNumber() : doc->blockCount() - 1;
    if (m_spellViewFirst < 0)
        sweepLines(first, last);
    else
        sweepLines(std::max(first, m_spellViewFirst), std::min(last, m_spellViewLast));
}

void MarkdownHighlighter::setSpellView(int from, int to)
{
    if (m_spellViewFrom == from && m_spellViewTo == to)
        return;
    m_spellViewFrom = from;
    m_spellViewTo = to;
    Q_EMIT spellViewChanged();
    QTextDocument *doc = document();
    const int oldFirst = m_spellViewFirst;
    const int oldLast = m_spellViewLast;
    if (!doc || from < 0 || to < 0) {
        m_spellViewFirst = m_spellViewLast = -1;
    } else {
        const QTextBlock a = doc->findBlock(std::min(from, to));
        const QTextBlock b = doc->findBlock(std::max(from, to));
        m_spellViewFirst = std::max(0, (a.isValid() ? a.blockNumber() : 0) - SpellViewMargin);
        m_spellViewLast = (b.isValid() ? b.blockNumber() : doc->blockCount() - 1) + SpellViewMargin;
    }
    if (!doc || !spellActive() || (oldFirst == m_spellViewFirst && oldLast == m_spellViewLast))
        return;
    // The lines that came into the view's range are checked (those that left keep their marks
    // until they are highlighted again: nobody sees them).
    if (m_spellViewFirst < 0) {
        sweepLines(0, doc->blockCount() - 1);
    } else if (oldFirst < 0 || m_spellViewLast < oldFirst || m_spellViewFirst > oldLast) {
        sweepLines(m_spellViewFirst, m_spellViewLast);
    } else {
        if (m_spellViewFirst < oldFirst)
            sweepLines(m_spellViewFirst, oldFirst - 1);
        if (m_spellViewLast > oldLast)
            sweepLines(oldLast + 1, m_spellViewLast);
    }
}

void MarkdownHighlighter::sweepLines(int first, int last)
{
    QTextDocument *doc = document();
    if (!doc)
        return;
    first = std::max(0, first);
    last = std::min(last, doc->blockCount() - 1);
    if (last < first)
        return;
    if (m_spellSweep < 0) {
        m_spellSweep = std::max(0, first);
        m_spellSweepEnd = last;
    } else {
        m_spellSweep = std::min(m_spellSweep, std::max(0, first));
        m_spellSweepEnd = std::max(m_spellSweepEnd, last);
    }
    if (!m_spellTimer.isActive())
        m_spellTimer.start();
}

void MarkdownHighlighter::setSpellCaret(int position)
{
    if (m_spellCaret == position)
        return;
    m_spellCaret = position;
    Q_EMIT spellCaretChanged();
    if (!spellActive() || !document())
        return;
    // The line that left a word unmarked for the old caret, and the one with a wrong word ending
    // at the new caret.
    const int suppressed = m_spellSuppressedBlock;
    const int wrong = wrongWordBlockAt(position);
    rehighlightLine(suppressed);
    if (wrong != suppressed)
        rehighlightLine(wrong);
}

void MarkdownHighlighter::setSpellColor(const QColor &color)
{
    if (m_spellColor == color)
        return;
    m_spellColor = color;
    rebuildFormats();
    Q_EMIT spellColorChanged();
    if (spellActive())
        respell();
}

bool MarkdownHighlighter::spellActive() const
{
    return m_spell && m_spell->active() && m_mode != u"code";
}

std::optional<spell::WordSpan> MarkdownHighlighter::spellWordAt(const QTextBlock &block, int position) const
{
    if (!block.isValid())
        return std::nullopt;
    const auto *data = static_cast<const LineData *>(block.userData());
    const SpellRange range = data ? data->spell : SpellRange();
    const QString text = block.text();
    if (range.from < 0 || range.from > text.size())
        return std::nullopt;
    const qsizetype to = range.to < 0 ? text.size() : std::min<qsizetype>(range.to, text.size());
    const QStringView part = QStringView(text).sliced(range.from, to - range.from);
    auto word = spell::wordAt(part, position - range.from, range.lineStart);
    if (!word)
        return std::nullopt;
    word->from += range.from;
    word->to += range.from;
    return word;
}

int MarkdownHighlighter::wrongWordBlockAt(int position) const
{
    QTextDocument *doc = document();
    if (!doc || position < 0 || !m_spell)
        return -1;
    const QTextBlock block = doc->findBlock(position);
    const auto word = spellWordAt(block, position - block.position());
    if (!word || word->to != position - block.position() || word->dot)
        return -1;
    const auto verdict = m_spell->verdict(spell::checkedText(block.text(), *word));
    return verdict == SpellCheck::Verdict::Wrong ? block.blockNumber() : -1;
}

void MarkdownHighlighter::rehighlightLine(int number)
{
    const QTextBlock block = number >= 0 && document() ? document()->findBlockByNumber(number) : QTextBlock();
    if (block.isValid())
        rehighlightBlock(block);
}

void MarkdownHighlighter::rehighlightReadySpelling(bool sliced)
{
    QTextDocument *doc = document();
    if (!doc) {
        m_spellReady.clear();
        m_spellSweep = m_spellSweepEnd = -1;
        return;
    }
    QElapsedTimer clock;
    clock.start();
    const auto timeLeft = [&] { return !sliced || clock.elapsed() < SpellSliceMs; };
    // The lines whose verdicts came (or changed) first, in order, then a pass over every line.
    QList<int> ready(m_spellReady.cbegin(), m_spellReady.cend());
    std::ranges::sort(ready);
    for (const int number : std::as_const(ready)) {
        if (!timeLeft())
            break;
        m_spellReady.remove(number);
        rehighlightLine(number);
    }
    QTextBlock block = m_spellSweep >= 0 ? doc->findBlockByNumber(m_spellSweep) : QTextBlock();
    while (m_spellSweep >= 0 && timeLeft()) {
        if (!block.isValid() || m_spellSweep > m_spellSweepEnd) {
            m_spellSweep = m_spellSweepEnd = -1;
            break;
        }
        rehighlightBlock(block);
        block = block.next();
        m_spellSweep = block.isValid() ? block.blockNumber() : -1;
    }
    if (m_spellSweep > m_spellSweepEnd)
        m_spellSweep = m_spellSweepEnd = -1;
    if (!m_spellReady.isEmpty() || m_spellSweep >= 0) {
        // A large text: the event loop gets as long as a slice between slices. Every line whose
        // marks change costs the document's layout a walk over all its lines (QTextDocumentLayout
        // lays out each change on its own outside an edit's signal), so such a pass is spread out
        // rather than hurried.
        m_spellTimer.setInterval(doc->characterCount() > LargeText ? SpellSliceMs : 0);
        m_spellTimer.start();
    }
}

void MarkdownHighlighter::highlightSpelling(const QString &line)
{
    const int number = currentBlock().blockNumber();
    if (m_spellSuppressedBlock == number)
        m_spellSuppressedBlock = -1;
    m_spellMarked.remove(number);
    // A large edit: its lines are spell-checked in slices after it (sweepSpelling()); lines far
    // from the view are not checked.
    if (!spellActive() || m_spellDeferred || !inSpellView(number))
        return;
    // Not inside fenced code, frontmatter, a formula or a comment running over lines (source
    // mode: highlightLine() says which part of the line is prose).
    const SpellRange range = m_lineSpell;
    if (range.from < 0 || range.from > line.size())
        return;
    const int to = range.to < 0 ? int(line.size()) : std::min(range.to, int(line.size()));
    const int caret = m_spellCaret - currentBlock().position();
    bool pending = false;
    QList<spell::WordSpan> words =
            spell::findWords(QStringView(line).sliced(range.from, to - range.from), range.lineStart);
    for (spell::WordSpan &word : words) {
        word.from += range.from;
        word.to += range.from;
    }
    for (const spell::WordSpan &word : std::as_const(words)) {
        switch (m_spell->verdict(spell::checkedText(line, word))) {
        case SpellCheck::Verdict::Correct: break;
        case SpellCheck::Verdict::Pending: pending = true; break;
        case SpellCheck::Verdict::Wrong:
            // The word being typed (the caret right after it) waits until it is finished.
            m_spellMarked.insert(number);
            if (word.to == caret && !word.dot) {
                m_spellSuppressedBlock = number;
                break;
            }
            // Qt Quick draws a highlighter's format only when it has a colour (QQuickTextNodeEngine
            // takes a layout's formats with a foreground or a background): plain text gets the
            // palette's "text" colour with its underline.
            mergeRange(int(word.from), int(word.length()), m_spellingFormat, color("text", QColor()));
            break;
        }
    }
    if (pending)
        m_spellPending.insert(number);
}

QVariantMap MarkdownHighlighter::misspelledAt(int position) const
{
    QTextDocument *doc = document();
    if (!doc || position < 0 || !spellActive())
        return {};
    const QTextBlock block = doc->findBlock(position);
    const auto word = spellWordAt(block, position - block.position());
    if (!word)
        return {};
    const QString text = block.text();
    const QString checked = spell::checkedText(text, *word);
    if (m_spell->verdict(checked) != SpellCheck::Verdict::Wrong)
        return {};
    return {{u"from"_s, int(block.position() + word->from)},
            {u"to"_s, int(block.position() + word->to)},
            {u"word"_s, checked},
            {u"text"_s, text.mid(word->from, word->length())}};
}

void MarkdownHighlighter::highlightBlock(const QString &line)
{
    m_lineChips.clear();
    highlightLine(line);
    highlightSpelling(line);
    if (!m_findQuery.isEmpty()) {
        QTextCharFormat hit;
        hit.setBackground(m_findColor);
        for (const text::TextRange &range : text::findMatches(line, m_findQuery, m_findOptions))
            mergeRange(int(range.start), int(range.length), hit);
    }
    collapseLineChips();
    storeLineData();
}

void MarkdownHighlighter::highlightLine(const QString &line)
{
    // The rules are markdownstyles.h's; here their operations get the formats, in order.
    if (m_styleOptions.mode == MarkdownMode::Source)
        m_styleOptions.firstLine = currentBlock().blockNumber() == 0;
    styleMarkdownLine(line, previousBlockState(), m_styleOptions, &m_lineStyles);
    for (const StyleOp &op : std::as_const(m_lineStyles.ops)) {
        if (op.merge)
            mergeRange(op.from, op.length, formatOf(op));
        else
            setRange(op.from, op.length, formatOf(op));
    }
    setCurrentBlockState(m_lineStyles.state);
    m_lineSpell = m_lineStyles.spell;
    m_lineChips = std::move(m_lineStyles.chips);
}

} // namespace brulion::editor
