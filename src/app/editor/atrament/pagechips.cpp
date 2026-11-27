// PageView's reminder chips while a block is edited (docs/ATRAMENT.md "The block editor in Atrament
// (M2)", step 2; InlineEditor.qml's chips over MarkdownHighlighter::reminderChips): every ⏰ / ⏰!
// token the highlighter's rules find in the edited text (markdownstyles.h's LineChip, labelled by
// markdown::reminderChipLabel()) is an inline object of the field's spans — its room the width of
// its words ("⏰ jutro 18:00") in the field's font, the pill of the rendered chip's colours drawn
// 2 px beyond it on each side, the words on the line's baseline — while the text keeps the token
// byte for byte. The caret treats it as one unit (TextInput's caret stops; a caret put inside goes
// to its end); a click opens the reminder's dialog, a right-click or a finger held on it its menu,
// neither moving the caret.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "text/fonts.h"
#include "text/textlayout.h"
#include "text/textsystem.h"

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

// How far the pill reaches beyond the chip's room on each side (InlineEditor.chipPadding).
constexpr float kChipPadding = 2;
// The size the chip's words are measured at (its room is in ems of the field's font).
constexpr float kMeasureSize = 16;

std::shared_ptr<const at::TextLayout> labelLayout(at::App &app, std::u16string_view label, const std::u16string &family,
                                                  float size)
{
    at::TextStyle style;
    style.fontFamily = family;
    style.fontSize = size;
    at::FontRun run;
    run.face = at::resolveFace(app, style);
    run.fontSize = size;
    run.length = int(label.size());
    return app.textSystem().layout(label, std::span<const at::FontRun>(&run, 1), at::ParagraphStyle{});
}

} // namespace

void PageView::setReminderChips(bool on, at::Context<PageView> &cx)
{
    if (m_reminderChips == on)
        return;
    m_reminderChips = on;
    applyEditStyler(cx);
    cx.notify();
}

void PageView::appendEditChips(const std::vector<LineChip> &chips, std::vector<at::TextSpan> &spans)
{
    m_editChips = chips;
    if (chips.empty() || !m_app)
        return;
    at::App *app = m_app;
    const at::EntityId self = m_selfId;
    const std::u16string family = m_palette.bodyFamily;
    at::TextStyle measured;
    measured.fontFamily = family;
    measured.fontSize = kMeasureSize;
    const at::FontMetrics metrics = app->textSystem().fonts().metrics(at::resolveFace(*app, measured));
    for (const LineChip &chip : chips) {
        const std::u16string label(u16View(chip.label));
        const auto measure = labelLayout(*app, label, family, kMeasureSize);
        at::InlineObject object;
        object.size = at::InlineObjectSize{measure->size().width / kMeasureSize, metrics.ascent, metrics.descent, true};
        const at::Color background = m_styles ? m_styles->chipBackground(chip.alarm) : at::rgba(0xf2b33d38);
        const at::Color ink = m_styles ? m_styles->chipText(chip.alarm) : at::rgb(0x7a4f00);
        object.paint = [app, label, family, background, ink](const at::InlineObjectPaint &p, at::Window &w) {
            // The pill around the room (RichTextPills' radius: half its height), then the words.
            const at::Rect pill = at::Rect::fromXYWH(p.bounds.left() - kChipPadding, p.bounds.top(),
                                                     p.bounds.width() + 2 * kChipPadding, p.bounds.height());
            w.paintQuad(pill, at::Background::solid(background), {}, at::transparent(),
                        at::Corners::all(pill.height() / 2));
            const auto words = labelLayout(*app, label, family, p.fontSize > 0 ? p.fontSize : kMeasureSize);
            if (words->lines().empty())
                return;
            const at::TextPaintStyle style{ink, std::nullopt, false, std::nullopt};
            w.paintText(*words, at::Point{p.bounds.left(), p.baseline - words->lines().front().baseline},
                        std::span<const at::TextPaintStyle>(&style, 1));
        };
        const int from = chip.from;
        const int to = chip.to;
        object.onClick = [app, self, from, to](const at::ClickEvent &click, at::Rect, at::Window &) {
            const bool menu = click.up.button == at::MouseButton::Right;
            const at::Point where = click.up.position;
            app->updateEntity<PageView>(
                    self, [&](PageView &v, at::Context<PageView> &c) { v.chipActivated(from, to, menu, where, c); });
        };
        object.onLongPress = [app, self, from, to](at::Point where, at::Rect, at::Window &) {
            app->updateEntity<PageView>(
                    self, [&](PageView &v, at::Context<PageView> &c) { v.chipActivated(from, to, true, where, c); });
        };
        spans.push_back(at::TextSpan{at::Range{from, to}, {}, std::move(object)});
    }
}

void PageView::chipActivated(int from, int to, bool menu, at::Point where, at::Context<PageView> &cx)
{
    const int row = editedRow();
    if (row < 0 || !onReminderChip)
        return;
    // A word the keyboard is still composing becomes text first (the dialog or the menu rewrites
    // the block's text).
    commitComposition(cx);
    onReminderChip(row, from, to, menu, where);
}

bool PageView::snapCaretOutOfChips(at::Context<PageView> &cx)
{
    if (!m_input || !m_reminderChips || m_inputCode)
        return false;
    const at::TextInput &input = m_input->read(cx.app());
    if (input.markedRange())
        return false;
    if (input.text() != m_editChipsText) {
        // The text has not been styled yet (it came from the controller, or was typed): its chips
        // by the same rules now.
        EditStyler::Extras extras;
        extras.chips = true;
        if (m_controller) {
            extras.chipLocale = m_controller->locale() == u"en" ? dates::UiLocale::English : dates::UiLocale::Polish;
            extras.chipToday = m_model ? m_model->today() : QDate();
        }
        static const EditStyler plain(QVariantMap{}, std::u16string());
        (void)plain.spans(input.text(), MarkdownMode::Inline, false, &extras);
        m_editChips = std::move(extras.chipsFound);
        m_editChipsText = input.text();
    }
    const at::InputHandler::Selection sel = input.selection();
    for (const LineChip &chip : m_editChips) {
        if (sel.focus <= chip.from || sel.focus >= chip.to)
            continue;
        // A caret to the chip's end (where it is drawn); a selection's moving end over the whole
        // chip, away from its anchor (InlineEditor.chipSnap()).
        const bool caret = sel.anchor == sel.focus;
        const int focus = caret || sel.anchor < sel.focus ? chip.to : chip.from;
        const int anchor = caret ? focus : sel.anchor;
        m_input->update(cx.app(), [&](at::TextInput &t, at::Context<at::TextInput> &c) { t.setSelection(anchor, focus, c); });
        return true;
    }
    return false;
}

} // namespace brulion::editor::atr
