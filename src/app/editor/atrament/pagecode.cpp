// Code blocks' line numbers drawn by Atrament (docs/ATRAMENT.md "The block editor in Atrament (M2)",
// step 5's code entry; CodeBlockView.qml's gutter, BlockViews.codeLineNumbers): with
// AppSettings.codeLineNumbers or the note's own `line-numbers` (CodeLook::lineNumbers), a column
// left of the code — Theme.space3 in from the panel's edge, as wide as the longest number,
// right-aligned (9 and 10 end in one column), in the code face and size in the label's faint
// colour, Theme.space3 before the code — numbers each line of the code on its first visual line (a
// wrapped line once), on that line's baseline. The numbers are painted from the code's own layout
// of this frame (the drawn code's Text, the edited code's TextInput: both laid out in prepaint, the
// numbers painted after), so a new wrapping moves them in the same frame; they are not text of the
// row (no node for screen readers, as CodeBlockView's `Accessible.ignored`).
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "text/textlayout.h"
#include "text/textsystem.h"

#include <string>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

at::AnyElement PageView::codeLineNumbers(std::size_t row, const QString &code, bool edited)
{
    const CodeLook &look = m_codeLook;
    // The column is as wide as the longest number: as many zeros (the code face's digits are all
    // one width), laid out invisible.
    const std::string count = std::to_string(code.count(u'\n') + 1);
    const std::u16string widest(count.size(), u'0');
    PageView *self = this; // (the frame's elements never outlive the view)
    const quint64 uid = m_model ? m_model->uidAt(int(row)) : 0;
    const std::u16string family = m_palette.codeFamily;
    const at::Color ink = look.label;
    const float size = look.fontSize;
    const float lineHeight = look.lineHeight;
    return at::div()
            .flexShrink0()
            .ariaHidden()
            .child(at::div().opacity(0).child(at::text(widest)))
            .child(at::canvas([self, uid, edited, family, ink, size, lineHeight](at::Rect b, at::Window &w, at::App &app) {
                       // The code as laid out in this frame.
                       const at::TextLayout *layout = nullptr;
                       at::Point origin;
                       if (edited) {
                           if (!self->m_input)
                               return;
                           const at::TextInput &input = self->m_input->read(app);
                           layout = input.layout();
                           origin = input.bounds().value_or(at::Rect{}).origin;
                       } else {
                           const auto it = self->m_rowText.find(uid);
                           if (it == self->m_rowText.end())
                               return;
                           layout = it->second.layout();
                           origin = it->second.bounds().origin;
                       }
                       if (!layout || layout->lines().empty())
                           return;
                       at::TextStyle style;
                       style.fontFamily = family;
                       style.fontSize = size;
                       at::FontRun run;
                       run.face = at::resolveFace(app, style);
                       run.fontSize = size;
                       at::ParagraphStyle paragraph;
                       paragraph.lineHeight = lineHeight;
                       const at::TextPaintStyle paint{ink, std::nullopt, false, std::nullopt};
                       const std::u16string &text = layout->text();
                       // (The numbers go down the code, past the column's own box; only those in
                       // the view are painted.)
                       const at::Rect mask = w.contentMask();
                       int number = 1;
                       for (int start = 0;;) {
                           // The line's first visual line.
                           const at::TextLine &line = layout->lines()[std::size_t(layout->lineIndex(start))];
                           const std::u16string label = [&] {
                               const std::string n = std::to_string(number);
                               return std::u16string(n.begin(), n.end());
                           }();
                           run.length = int(label.size());
                           const auto digits = app.textSystem().layout(label, std::span<const at::FontRun>(&run, 1), paragraph);
                           if (!digits->lines().empty()) {
                               const float y = origin.y + line.baseline - digits->lines().front().baseline;
                               if (y < mask.bottom() && y + digits->size().height > mask.top())
                                   w.paintText(*digits, at::Point{b.right() - digits->size().width, y},
                                               std::span<const at::TextPaintStyle>(&paint, 1));
                           }
                           const std::size_t next = text.find(u'\n', std::size_t(start));
                           if (next == std::u16string::npos)
                               break;
                           start = int(next) + 1;
                           ++number;
                       }
                   })
                           .absolute()
                           .top(0)
                           .left(0)
                           .sizeFull());
}

} // namespace brulion::editor::atr
