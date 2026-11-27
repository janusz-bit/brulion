// Thematic breaks (`---`) drawn by Atrament: BlockPartRule's look.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "pagecontroller.h"

namespace brulion::editor::atr {

namespace at = brulion::atrament;

bool PageView::isRule(int row) const
{
    return m_model && row >= 0 && row < m_model->rowCount()
           && m_model->index(row, 0).data(BlockListModel::ViewRole).toString() == u"rule";
}

at::AnyElement PageView::renderRuleRow(float textX, bool edited)
{
    // Edited: its Markdown in the field.
    if (edited)
        return at::div().ml(textX).flex1().minW(0).child(*m_input);
    // A 1-px line across the text column, at the middle of a line's height.
    return at::div()
            .ml(textX)
            .flex1()
            .minW(0)
            .h(m_metrics.lineHeight)
            .flexCol()
            .justifyCenter()
            .role(at::Role::Separator)
            .child(at::div().h(1).bg(m_palette.rule));
}

} // namespace brulion::editor::atr
