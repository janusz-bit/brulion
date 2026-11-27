// Charts and Mermaid diagrams drawn and edited by Atrament (docs/ATRAMENT.md "The block editor in
// Atrament (M2)", step 5 "Blocks with views"): ChartBlockView's and MermaidBlockView's sunken panel,
// the diagram painted from its core scene (diagrampaint.h) as tall as it is at the column's width,
// the text alternative its accessible name, a click editing the fenced source in the field (the
// code highlighter's colours) with the diagram drawn again under it as it is typed; a source that
// cannot be drawn shows itself with the translated reason under it. Also the parts the formulas of
// mathrows.cpp share: the row's frame, its click, the edited field's keys.
#include "pageview.h"

#include "blocklistmodel.h"
#include "editorhost.h"
#include "chart/chart.h"
#include "chartview.h"
#include "diagrampaint.h"
#include "editstyles.h"
#include "mermaidview.h"
#include "pagecontroller.h"
#include "renderedtextitem.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

namespace brulion::editor::atr {

namespace at = brulion::atrament;

namespace {

QString hexOf(at::Color c)
{
    auto channel = [](float v) { return int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
    return QStringLiteral("#%1%2%3")
            .arg(channel(c.r), 2, 16, QLatin1Char('0'))
            .arg(channel(c.g), 2, 16, QLatin1Char('0'))
            .arg(channel(c.b), 2, 16, QLatin1Char('0'));
}

std::u16string_view view(const QString &s)
{
    return u16View(s);
}

// The laid-out scenes kept for a few widths (a chart is laid out at its column's width).
constexpr std::size_t kWidthsKept = 4;
// Laid-out sources kept (the rows of a few screens).
constexpr std::size_t kEntriesKept = 48;

} // namespace

// What a chart or a diagram shows, laid out once per source and look.
struct PageView::ViewCaches {
    struct Mermaid {
        QString key;
        std::shared_ptr<const diagram::Result> result;
    };
    struct Chart {
        QString key; // source + look
        QVariantMap data;
        chart::Spec spec;
        chart::Data numbers;
        QString message;
        QString description;
        std::vector<std::pair<float, std::shared_ptr<const diagram::Scene>>> scenes; // by width, recent last
    };
    std::vector<Mermaid> mermaid;              // most recently used last
    std::vector<std::shared_ptr<Chart>> charts; // most recently used last
    std::shared_ptr<const DiagramColors> colors;

    template<class V, class Match>
    static auto find(V &entries, Match match) -> decltype(&entries.front())
    {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (match(entries[i])) {
                std::rotate(entries.begin() + std::ptrdiff_t(i), entries.begin() + std::ptrdiff_t(i) + 1, entries.end());
                return &entries.back();
            }
        }
        return nullptr;
    }
    template<class V>
    static void trim(V &entries)
    {
        if (entries.size() > kEntriesKept)
            entries.erase(entries.begin(), entries.begin() + std::ptrdiff_t(entries.size() - kEntriesKept));
    }
};

namespace {

struct DiagramState {
    QString status; // "ok", "error", "unsupported"
    QString message;
    QString name;
    QString description;
    std::shared_ptr<PageView::ViewCaches::Chart> chart;
    std::shared_ptr<const diagram::Result> mermaid;
};

std::shared_ptr<const diagram::Result> mermaidResult(PageView::ViewCaches &caches, at::App &app, const QString &source,
                                                     const PageView::ViewLook &look)
{
    const QString key = QString::number(double(look.mermaidFontSize)) + u'\x1f'
                        + QString(reinterpret_cast<const QChar *>(look.family.data()), qsizetype(look.family.size()))
                        + u'\x1f' + source;
    if (auto *hit = PageView::ViewCaches::find(caches.mermaid, [&](const auto &e) { return e.key == key; }))
        return hit->result;
    auto result = std::make_shared<const diagram::Result>(
            diagram::render(source, atramentMetrics(app, look.family, look.mermaidFontSize)));
    caches.mermaid.push_back({key, result});
    PageView::ViewCaches::trim(caches.mermaid);
    return result;
}

std::shared_ptr<PageView::ViewCaches::Chart> chartState(PageView::ViewCaches &caches, const QString &source,
                                                        const QVariantMap &data, const PageView::ViewLook &look)
{
    const QString key = (look.english ? u"en\x1f" : u"pl\x1f") + QString::number(double(look.chartFontSize)) + u'\x1f'
                        + QString(reinterpret_cast<const QChar *>(look.family.data()), qsizetype(look.family.size()))
                        + u'\x1f' + source;
    if (auto *hit = PageView::ViewCaches::find(caches.charts, [&](const auto &e) { return e->key == key && e->data == data; }))
        return *hit;
    // ChartView::reread: the spec, the numbers BlockListModel resolved, the reason when none.
    auto c = std::make_shared<PageView::ViewCaches::Chart>();
    c->key = key;
    c->data = data;
    const chart::SpecResult spec = chart::parseSpec(source);
    c->spec = spec.spec;
    if (spec.error) {
        c->message = spec.error->message();
    } else if (!data.value(QStringLiteral("ok")).toBool()) {
        c->message = data.value(QStringLiteral("error")).toString();
        if (c->message.isEmpty())
            c->message = ChartView::tr("The chart is being prepared.");
    } else {
        c->numbers = ChartView::dataFromMap(data);
        if (c->numbers.series.isEmpty())
            c->message = ChartView::tr("There are no numbers to draw.");
    }
    const calc::CalcLocale locale = look.english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    if (c->message.isEmpty())
        c->description = chart::describeChart(c->spec, c->numbers, locale);
    caches.charts.push_back(c);
    PageView::ViewCaches::trim(caches.charts);
    return c;
}

// The scene of a chart laid out at `width` (ChartView::relayout), from the chart's own cache.
std::shared_ptr<const diagram::Scene> chartScene(const std::shared_ptr<PageView::ViewCaches::Chart> &c, at::App &app,
                                                 const PageView::ViewLook &look, float width)
{
    if (!c->message.isEmpty())
        return nullptr;
    const float w = width > 0 ? std::round(width * 4) / 4 : 640;
    for (std::size_t i = 0; i < c->scenes.size(); ++i) {
        if (c->scenes[i].first == w) {
            std::rotate(c->scenes.begin() + std::ptrdiff_t(i), c->scenes.begin() + std::ptrdiff_t(i) + 1, c->scenes.end());
            return c->scenes.back().second;
        }
    }
    chart::LayoutOptions options;
    options.width = w;
    options.metrics = atramentMetrics(app, look.family, look.chartFontSize);
    options.locale = look.english ? calc::CalcLocale::English : calc::CalcLocale::Polish;
    auto scene = std::make_shared<const diagram::Scene>(chart::layoutChart(c->spec, c->numbers, options));
    c->scenes.emplace_back(w, scene);
    if (c->scenes.size() > kWidthsKept)
        c->scenes.erase(c->scenes.begin());
    return scene;
}

DiagramState diagramState(PageView::ViewCaches &caches, at::App &app, const QModelIndex &index, const QString &source,
                          const PageView::ViewLook &look)
{
    DiagramState st;
    if (index.data(BlockListModel::ViewRole).toString() == u"chart") {
        std::shared_ptr<PageView::ViewCaches::Chart> c =
                chartState(caches, source, index.data(BlockListModel::ChartDataRole).toMap(), look);
        st.chart = c;
        st.status = c->message.isEmpty() ? QStringLiteral("ok") : QStringLiteral("error");
        st.message = c->message;
        // ChartBlockView: the type as the name, every value as the description.
        st.name = chart::typeName(c->spec.type);
        st.description = c->description;
        return st;
    }
    st.mermaid = mermaidResult(caches, app, source, look);
    const diagram::Result &r = *st.mermaid;
    if (r.ok()) {
        st.status = QStringLiteral("ok");
    } else {
        st.status = r.error->code == diagram::Error::Code::Unsupported ? QStringLiteral("unsupported")
                                                                         : QStringLiteral("error");
        st.message = MermaidView::errorMessage(*r.error, r.typeName);
    }
    // MermaidView::description: the title, or what kind of diagram it is.
    if (!r.scene.title.isEmpty()) {
        st.name = r.scene.title;
    } else {
        switch (r.kind) {
        case diagram::Kind::Flowchart: st.name = MermaidView::tr("Flowchart"); break;
        case diagram::Kind::Sequence: st.name = MermaidView::tr("Sequence diagram"); break;
        case diagram::Kind::Pie: st.name = MermaidView::tr("Pie chart"); break;
        case diagram::Kind::Unsupported:
        case diagram::Kind::Unknown: st.name = MermaidView::tr("Diagram"); break;
        }
    }
    return st;
}

} // namespace

// --- the switches and the look ------------------------------------------------------------------

void PageView::setViewLook(const ViewLook &look, at::Context<PageView> &cx)
{
    m_viewLook = look;
    m_app = &cx.app();
    if (!m_viewCaches)
        m_viewCaches = std::make_shared<ViewCaches>();
    // The diagram's palette from the theme (MermaidView::palette: the panel is what it is drawn on).
    diagram::ThemeColors t;
    t.text = hexOf(look.text);
    t.mutedText = hexOf(look.muted);
    t.background = hexOf(look.panel);
    t.border = hexOf(look.border);
    t.accent = hexOf(look.accent);
    t.dark = look.dark;
    m_viewCaches->colors = std::make_shared<const DiagramColors>(DiagramColors::from(diagram::themedPalette(t)));
    m_extents.markAllEstimated();
    cx.notify();
}

void PageView::setDrawsDiagrams(bool on, at::Context<PageView> &cx)
{
    if (!m_viewCaches)
        setViewLook(m_viewLook, cx);
    m_app = &cx.app();
    if (m_drawsDiagrams == on)
        return;
    m_drawsDiagrams = on;
    m_extents.markAllEstimated();
    cx.notify();
}

bool PageView::isDiagram(int row) const
{
    if (!m_drawsDiagrams || !m_app || !m_viewCaches || !m_model || row < 0 || row >= m_model->rowCount())
        return false;
    const QString view = m_model->index(row, 0).data(BlockListModel::ViewRole).toString();
    return view == u"chart" || view == u"mermaid";
}

bool PageView::editsAsCode(int row) const
{
    return isCode(row) || isDiagram(row);
}

bool PageView::editsSource(int row) const
{
    return editsAsCode(row) || mathEditable(row);
}

PageView::ViewRowInfo PageView::viewRowInfo(int row) const
{
    ViewRowInfo info;
    if (!m_model || !m_app || !m_viewCaches || row < 0 || row >= m_model->rowCount())
        return info;
    const QModelIndex index = m_model->index(row, 0);
    const QString view = index.data(BlockListModel::ViewRole).toString();
    if (view == u"math") {
        info.kind = view;
        const auto rendered = index.data(BlockListModel::RenderedTextRole).value<RenderedTextRef>();
        info.status = rendered.d && mathDrawable(*rendered.d) ? QStringLiteral("ok") : QStringLiteral("error");
        info.name = rendered.text();
        return info;
    }
    if (view != u"chart" && view != u"mermaid")
        return info;
    const DiagramState st = diagramState(*m_viewCaches, *m_app, index, m_model->editTextAt(row), m_viewLook);
    info.kind = view;
    info.status = st.status;
    info.message = st.message;
    info.name = st.name;
    info.description = st.description;
    std::shared_ptr<const diagram::Scene> scene;
    if (st.mermaid && st.mermaid->ok())
        scene = std::shared_ptr<const diagram::Scene>(st.mermaid, &st.mermaid->scene);
    else if (st.chart && st.chart->message.isEmpty() && !st.chart->scenes.empty())
        scene = st.chart->scenes.back().second;
    if (scene)
        info.size = {float(scene->size.width()), float(scene->size.height())};
    return info;
}

// --- rows -----------------------------------------------------------------------------------------

at::AnyElement PageView::renderViewRow(std::size_t row, const QModelIndex &index, float textX, at::Div line)
{
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    const bool edited = m_input && uid == m_editUid;
    at::AnyElement panel = isMath(int(row)) ? renderMathPanel(row, index, textX, edited)
                                            : renderDiagramPanel(row, index, textX, edited);
    at::Div d = std::move(line).child(std::move(panel));
    const at::EntityId self = m_selfId;
    if (edited) {
        // The edited source: the editor's keys before the field (Enter a line break, Tab a tab,
        // ↑/↓ out of its first / last line, Esc).
        return std::move(d).captureKeyDown([self](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
            bool handled = false;
            a.updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &c) {
                handled = v.editorKey(e.keystroke, w, c);
            });
            if (handled)
                w.stopPropagation();
        });
    }
    if (onLongPressRow)
        std::move(d).onLongPress([self, row](at::Point, at::Window &, at::App &a) {
            a.updateEntity<PageView>(self, [row](PageView &v, at::Context<PageView> &) {
                if (v.onLongPressRow)
                    v.onLongPressRow(int(row));
            });
        });
    if (m_controller && !m_controller->readOnly())
        // A click or a tap edits the source (no node of the row's own: the panel's are).
        std::move(d).role(at::Role::None).onClick([self, row](const at::ClickEvent &click, at::Window &, at::App &a) {
            a.updateEntity<PageView>(self, [&](PageView &v, at::Context<PageView> &c) { v.viewRowClicked(row, click, c); });
        });
    return d;
}

void PageView::viewRowClicked(std::size_t row, const at::ClickEvent &click, at::Context<PageView> &)
{
    if (!m_controller || m_controller->readOnly() || click.isKeyboard || click.fromAccessibility)
        return;
    if (m_dragSelected) { // the release of a press that selected blocks by dragging
        m_dragSelected = false;
        return;
    }
    // On the source shown (a diagram that cannot be drawn): the character under the pointer; on
    // the picture or a formula: the end (the QML views' startEditing(row, -1)).
    int cursor = -1;
    if (const auto it = m_rowText.find(m_model->uidAt(int(row))); it != m_rowText.end()) {
        if (const std::optional<int> at = it->second.offsetAt(click.up.position);
            at && it->second.bounds().contains(click.up.position))
            cursor = std::clamp(*at, 0, int(m_model->editTextAt(int(row)).size()));
    }
    if (onEditRequest && onEditRequest(int(row), cursor, click.up.modifiers))
        return;
    m_controller->startEditing(int(row), cursor);
}

at::AnyElement PageView::renderDiagramPanel(std::size_t row, const QModelIndex &index, float textX, bool edited)
{
    const ViewLook &v = m_viewLook;
    const QString kind = index.data(BlockListModel::ViewRole).toString();
    const bool isChart = kind == u"chart";
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    const QString source = m_model->editTextAt(int(row));
    const DiagramState st = diagramState(*m_viewCaches, *m_app, index, source, v);
    const bool ok = st.status == u"ok";
    // ChartBlockView / MermaidBlockView: the source shows while edited, or when there is nothing
    // to draw; the panel's border is the focus ring's while edited.
    const bool showSource = edited || !ok;
    at::Div panel = at::div()
                            .ml(textX)
                            .flex1()
                            .minW(0)
                            .flexCol()
                            .bg(v.panel)
                            .border(1)
                            .borderColor(edited ? v.editingBorder : v.panelBorder)
                            .rounded(v.radius)
                            .my(m_metrics.documentMode ? 0.0f : std::max(0.0f, v.framedPad - m_metrics.rowPad))
                            .pb(12);
    if (showSource) {
        const float labelWidth = v.labelSize * 0.62f * float(kind.size());
        at::Div code = at::div()
                               .mt(12)
                               .ml(16)
                               .mr(16 + labelWidth + 8)
                               .fontFamily(m_palette.codeFamily)
                               .textSize(v.codeSize)
                               .lineHeight(v.codeLineHeight)
                               .cursorText();
        if (edited) {
            std::move(code).child(*m_input);
        } else {
            at::Text text = at::text(view(source)).id(u"source");
            if (m_styles)
                std::move(text).spans(m_styles->spans(view(source), MarkdownMode::Code));
            // Where a click lands in the source (it is shown as it is edited).
            std::move(text).trackLayout(m_rowText[uid]);
            m_rowTextFrame[uid] = m_frame;
            std::move(code).child(std::move(text));
        }
        std::move(panel)
                .child(at::div()
                               .absolute()
                               .top(8)
                               .right(12)
                               .fontFamily(m_palette.codeFamily)
                               .textSize(v.labelSize)
                               .lineHeight(v.labelSize * 1.3f)
                               .textColor(v.label)
                               .ariaHidden()
                               .child(at::text(view(kind))))
                .child(std::move(code));
    } else {
        m_rowText.erase(uid);
        m_rowTextFrame.erase(uid);
    }
    if (ok) {
        at::App *app = m_app;
        const ViewLook look = v;
        DiagramElement::SceneAt sceneAt;
        if (st.mermaid) {
            std::shared_ptr<const diagram::Result> result = st.mermaid;
            sceneAt = [result](float) { return std::shared_ptr<const diagram::Scene>(result, &result->scene); };
        } else {
            std::shared_ptr<ViewCaches::Chart> c = st.chart;
            sceneAt = [c, app, look](float width) { return chartScene(c, *app, look, width); };
        }
        // A chart at the panel's width less space3 on each side; a diagram less space4, scaled down
        // to 70 % at most, then scrolling sideways.
        at::Div picture = at::div()
                                  .id(u"diagram")
                                  .mt(12)
                                  .mx(isChart ? 12 : 16)
                                  .flexCol()
                                  .role(at::Role::Image)
                                  .ariaLabel(view(st.name))
                                  .cursorPointer();
        if (!st.description.isEmpty())
            std::move(picture).ariaDescription(view(st.description));
        if (!isChart)
            std::move(picture).overflowXScroll();
        std::move(picture).child(
                DiagramElement(std::move(sceneAt), m_viewCaches->colors, v.family, isChart ? 0.0f : 0.7f));
        std::move(panel).child(std::move(picture));
    } else {
        // The reason, under the source: a mistake in the danger colour, a type Brulion does not
        // draw yet in the muted one.
        std::move(panel).child(at::div()
                                       .mt(8)
                                       .mx(16)
                                       .fontFamily(v.family)
                                       .textSize(v.labelSize)
                                       .lineHeight(v.labelSize * 1.4f)
                                       .textColor(st.status == u"unsupported" ? v.muted : v.danger)
                                       .child(at::text(view(st.message)).id(u"message")));
    }
    return panel;
}

} // namespace brulion::editor::atr
