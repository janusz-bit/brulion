// PageView's query blocks (pageview.h "Pictures, files, embedded blocks and query results";
// docs/ATRAMENT.md M2 step 5): a ```query or ```dataview block drawn as QueryBlockView draws it — a
// raised frame with its header ("Query", how many results), the query's filters as chips, then the
// results of a QueryResultModel kept for the block while it is drawn: group headers, tasks with a
// working box (QueryResultModel::toggleTask), items rendered like the editor renders text (links,
// tags, date chips) with their page as a quiet link, Dataview TABLE rows as columns under their
// headers, "Show more", and what is wrong or missing. A click on an item opens it, a click on the
// header or the filters edits the query (the QML delegate takes the row while its source is
// edited: QueryBlockView's InlineEditor). Database views (`view:`, ```base) stay the QML
// delegate's (drawsMediaRow()).
#include "mediarows_p.h"

#include "blocklistmodel.h"
#include "pagecontroller.h"

#include "dates/format.h"
#include "dates/local.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::editor::atr {

namespace {

QString filterLabel(const QString &key)
{
    static const char *const keys[][2] = {{"type", "type"},         {"status", "status"},
                                          {"tag", "tag"},           {"page", "page"},
                                          {"path", "path"},         {"due", "due"},
                                          {"scheduled", "scheduled"}, {"start", "start"},
                                          {"done", "done"},         {"remind", "reminder"},
                                          {"priority", "priority"}, {"sort", "sort"},
                                          {"group", "group"},       {"limit", "limit"}};
    for (const auto &k : keys)
        if (key == QLatin1StringView(k[0]))
            return translated("QueryBlockView", k[1]);
    return key;
}

// The page of an item for its quiet link: a daily note by its date.
QString pageLabel(const QString &title, const QString &journal, const BlockListModel *model)
{
    if (journal.isEmpty() || !model)
        return title;
    const auto spec = dates::parseDateSpec(journal);
    return spec ? dates::formatHuman(*spec, model->english() ? dates::UiLocale::English : dates::UiLocale::Polish,
                                     model->today())
                : title;
}

std::shared_ptr<const markdown::RenderedText>
renderedOnce(std::vector<std::pair<QString, std::shared_ptr<const markdown::RenderedText>>> &cache, std::size_t i,
             const QString &html)
{
    if (cache.size() <= i)
        cache.resize(i + 1);
    auto &[source, text] = cache[i];
    if (!text || source != html) {
        source = html;
        text = std::make_shared<markdown::RenderedText>(markdown::renderedTextFromHtml(html));
    }
    return text;
}

} // namespace

QueryResultModel *PageView::queryResults(int row) const
{
    if (!m_media || !m_model || row < 0 || row >= m_model->rowCount())
        return nullptr;
    const auto it = m_media->queries.find(m_model->uidAt(row));
    return it != m_media->queries.end() ? it->second.model.get() : nullptr;
}

QVariantMap PageView::activateQueryLink(int row, int item, const QString &link)
{
    QueryResultModel *results = queryResults(row);
    if (!results)
        return {};
    // ("open:": the item's page link, QueryBlockView's openResult.)
    return link == u"open:"_s ? results->open(item, false) : results->activateLink(item, link, false);
}

at::AnyElement PageView::queryRow(std::size_t row, const QModelIndex &index, float textX)
{
    const PageMetrics &m = m_metrics;
    const MediaLook &look = m_mediaLook;
    const quint64 uid = index.data(BlockListModel::UidRole).toString().toULongLong();
    const QString source = m_model->editTextAt(int(row));
    const bool dataview = index.data(BlockListModel::LangRole).toString() == u"dataview";
    const QVariantMap query = index.data(BlockListModel::QueryRole).toMap();

    // The block's results: made when it is first drawn and kept while it is (QueryBlockView's
    // QueryResultModel, run at once — the block has its results when it is drawn).
    Media::Query &q = m_media->queries[uid];
    q.frame = m_frame;
    if (!q.model) {
        q.model = std::make_unique<QueryResultModel>();
        QueryResultModel *model = q.model.get();
        model->classBegin();
        model->setController(m_controller);
        model->setLanguage(dataview ? u"dataview"_s : u"query"_s);
        model->setMaxItems(look.queryItems);
        model->setSource(source);
        model->componentComplete();
        at::App *app = m_media->app;
        const at::EntityId self = m_media->self;
        auto redraw = [app, self] {
            if (app)
                app->updateEntity<PageView>(self, [](PageView &, at::Context<PageView> &c) { c.notify(); });
        };
        q.connections.push_back(QObject::connect(model, &QueryResultModel::resultChanged, model, redraw));
        q.connections.push_back(QObject::connect(model, &QAbstractItemModel::modelReset, model, redraw));
        q.connections.push_back(QObject::connect(model, &QAbstractItemModel::dataChanged, model, redraw));
        q.connections.push_back(QObject::connect(model, &QAbstractItemModel::rowsInserted, model, redraw));
        q.connections.push_back(QObject::connect(model, &QAbstractItemModel::rowsRemoved, model, redraw));
        q.language = model->language();
    }
    QueryResultModel *results = q.model.get();
    if (results->controller() != m_controller.data())
        results->setController(m_controller);
    results->setSource(source); // (edited elsewhere: debounced by the model)
    results->setLanguage(dataview ? u"dataview"_s : u"query"_s);

    PageView *self = this;
    const at::EntityId selfId = m_selfId;
    const bool editable = m_controller && !m_controller->readOnly();
    const bool live = results->available();
    const bool emptySource = source.trimmed().isEmpty();
    const bool ready = results->ready();
    const bool ok = results->ok();
    const bool table = results->type() == u"table";
    const float itemSize = look.bodySize;
    const float itemLine = std::round(itemSize * 1.45f);
    const float pad = m.touch ? std::max(3.0f, std::round((44 - itemLine) / 2)) : 1;
    auto editQuery = [selfId, row](const at::ClickEvent &click, at::Window &, at::App &a) {
        a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &) { v.mediaClicked(row, click); });
    };
    auto ui = [&look](at::Div &&d) -> at::Div {
        if (!look.uiFamily.empty())
            return std::move(d).fontFamily(look.uiFamily);
        return std::move(d);
    };

    // ---- The header: what the block is, how many results.
    QString count;
    if (ready && ok && !emptySource && results->itemCount() > 0) {
        const int n = results->itemCount();
        const QString type = results->type();
        const char *what = type == u"tasks"       ? "%n task(s)"
                           : type == u"pages"     ? "%n page(s)"
                           : type == u"reminders" ? "%n reminder(s)"
                           : type == u"list"      ? "%n result(s)"
                           : type == u"table"     ? "%n row(s)"
                                                  : "%n block(s)";
        count = translated("QueryBlockView", what, n);
        if (results->total() > n)
            count = translated("QueryBlockView", "%1 of %2").arg(count).arg(results->total());
    }
    const QString label = dataview ? translated("QueryBlockView", "Dataview") : translated("QueryBlockView", "Query");
    at::Div frame = at::div()
                            .ml(textX)
                            .flex1()
                            .minW(0)
                            .flexCol()
                            .gap(look.spacing)
                            .bg(look.queryBackground)
                            .border(1)
                            .borderColor(look.border)
                            .rounded(look.radius)
                            .pt(12)
                            .pb(12)
                            .pl(16)
                            .pr(16)
                            .role(at::Role::Group)
                            .ariaLabel(u16View(dataview ? translated("QueryBlockView", "Dataview query")
                                                        : translated("QueryBlockView", "Query")));
    at::Div header = ui(at::div()
                                .id(u"header")
                                .flexRow()
                                .itemsCenter()
                                .gap(8)
                                .h(m.touch ? 32 : 22)
                                .textSize(look.smallSize)
                                .child(glyphElement(look.smallSize + 2, m_palette.muted, Glyph::Filter))
                                .child(at::div().fontSemibold().textColor(m_palette.muted).child(at::text(u16View(label))))
                                .child(at::div().flex1().minW(0).overflowHidden().whitespaceNowrap().textColor(look.faint).child(
                                        at::text(u16View(count)))));
    if (editable)
        std::move(header).cursorText().onClick(editQuery);
    std::move(frame).child(std::move(header));

    // ---- The filters as chips ("type: tasks", „term”).
    const QVariantList filters = query.value(u"filters"_s).toList();
    const QStringList terms = query.value(u"terms"_s).toStringList();
    if (!filters.isEmpty() || !terms.isEmpty()) {
        at::Div chips = at::div().id(u"filters").flexRow().flexWrap().gap(6);
        auto chip = [&](const QString &text, bool tag) {
            return ui(at::div()
                              .h(m.touch ? 24 : 20)
                              .flexRow()
                              .itemsCenter()
                              .pl(7)
                              .pr(8)
                              .roundedFull()
                              .bg(tag ? m_palette.tagBackground : look.chipBackground)
                              .textColor(tag ? m_palette.tag : look.chipText)
                              .textSize(look.tinySize)
                              .fontMedium()
                              .whitespaceNowrap()
                              .child(at::text(u16View(text))));
        };
        for (const QVariant &v : filters) {
            const QVariantMap f = v.toMap();
            const QString key = f.value(u"key"_s).toString();
            const QString text = (f.value(u"negated"_s).toBool() ? u"≠ "_s : QString()) + filterLabel(key) + u": "_s
                                 + f.value(u"value"_s).toString();
            std::move(chips).child(chip(text, key == u"tag"));
        }
        for (const QString &t : terms)
            std::move(chips).child(chip(u"„"_s + t + u"”"_s, false));
        if (editable)
            std::move(chips).cursorText().onClick(editQuery);
        std::move(frame).child(std::move(chips));
    }

    // ---- The results.
    const bool showResults = live && ok && !emptySource;
    if (live && ready && !emptySource && (results->itemCount() > 0 || !ok))
        std::move(frame).child(at::div().h(1).mt(4).bg(look.frame).ariaHidden());
    const QStringList columns = results->columns();
    if (showResults && table && results->itemCount() > 0) {
        at::Div heads = at::div().flexRow().textSize(look.tinySize).fontSemibold().textColor(m_palette.muted).ariaHidden();
        for (const QString &c : columns)
            std::move(heads).child(at::div().flex1().flexBasis(at::pct(0)).minW(0).pr(8).overflowHidden().whitespaceNowrap().child(
                    at::text(u16View(c.toUpper()))));
        std::move(frame).child(ui(std::move(heads)));
    }
    if (showResults) {
        at::Div list = at::div().id(u"results").flexCol().gap(m.touch ? 0 : 2).role(at::Role::List);
        const int n = results->rowCount();
        q.texts.resize(std::size_t(n));
        q.cells.resize(std::size_t(n));
        for (int i = 0; i < n; ++i) {
            const QModelIndex r = results->index(i);
            if (r.data(QueryResultModel::KindRole).toString() == u"group") {
                std::move(list).child(ui(at::div()
                                                 .pt(i > 0 ? 12 : 4)
                                                 .pb(4)
                                                 .textSize(look.tinySize)
                                                 .fontSemibold()
                                                 .textColor(m_palette.muted)
                                                 .role(at::Role::Heading)
                                                 .child(at::text(u16View(r.data(QueryResultModel::LabelRole).toString().toUpper()
                                                                         + u"  ·  "_s
                                                                         + QString::number(r.data(QueryResultModel::CountRole).toInt()))))));
                continue;
            }
            const QString status = r.data(QueryResultModel::StatusRole).toString();
            const bool isTask = r.data(QueryResultModel::IsTaskRole).toBool();
            const bool closed = status == u"done" || status == u"cancelled";
            const QString path = r.data(QueryResultModel::PathRole).toString();
            const int line = r.data(QueryResultModel::LineRole).toInt();
            const QString raw = r.data(QueryResultModel::RawRole).toString();
            const QString plain = r.data(QueryResultModel::TextRole).toString();
            auto linkClick = [self, row, i, path, line, raw](const QString &link) {
                return [self, row, i, path, line, raw, link](const at::ClickEvent &, at::Window &, at::App &) {
                    if (self->onQueryLink) {
                        self->onQueryLink(int(row), i, link, path, line, raw);
                    } else {
                        const QVariantMap answer = self->activateQueryLink(int(row), i, link);
                        if (self->onQueryAnswer)
                            self->onQueryAnswer(int(row), answer);
                    }
                };
            };
            // A link's menu (a chip's in the item's own file, a link's): QueryBlockView.resultMenu.
            auto linkMenu = [self, row, i, path, line, raw](const QString &link) {
                at::TextLink::MenuListener menu;
                if (self->onQueryLinkMenu)
                    menu = [self, row, i, path, line, raw, link](at::Point p, at::Window &, at::App &) {
                        if (self->onQueryLinkMenu)
                            self->onQueryLinkMenu(int(row), i, link, path, line, raw, p);
                    };
                return menu;
            };
            // A text of the item (its HTML read into rendered text), with each link a hit region, at
            // most 4 lines with "…" after the fourth (QueryBlockView's maximumLineCount).
            auto itemText = [&](const markdown::RenderedText &rt, const QString &suffix) {
                QString shown = rt.text;
                std::vector<at::TextSpan> spans = renderedSpans(rt, m_palette);
                if (!suffix.isEmpty()) {
                    // The page as a quiet link ("· Projekty", a daily note by its date).
                    const int from = int(shown.size());
                    shown += u"  · "_s + suffix;
                    at::TextStyleRefinement faint;
                    faint.color = look.faint;
                    spans.push_back({at::Range{from, int(shown.size())}, faint});
                }
                at::Text t = at::text(u16View(shown)).spans(spans).maxLines(4);
                PartText &part = partText(row, i);
                for (qsizetype a = 0; a < rt.runs.size();) {
                    const QString &href = rt.runs[a].href;
                    qsizetype b = a + 1;
                    while (b < rt.runs.size() && rt.runs[b].href == href && rt.runs[b].from == rt.runs[b - 1].to)
                        ++b;
                    if (!href.isEmpty()) {
                        const at::Range range{int(rt.runs[a].from), int(rt.runs[b - 1].to)};
                        std::move(t).link(range, linkClick(href), linkMenu(href));
                        part.links.emplace_back(range, href);
                    }
                    a = b;
                }
                if (!suffix.isEmpty()) {
                    // (The item's page: no menu of its own, the block's.)
                    const at::Range range{int(rt.text.size()) + 4, int(shown.size())};
                    std::move(t).link(range, linkClick(u"open:"_s));
                    part.links.emplace_back(range, u"open:"_s);
                }
                std::move(t).trackLayout(part.handle);
                return t;
            };
            at::Div item = at::div()
                                   .id(at::ElementId::number(quint64(i) + 1))
                                   .relative()
                                   .flexRow()
                                   .itemsStart()
                                   .py(pad)
                                   .minH(m.touch ? 44 : 0)
                                   .rounded(look.radiusSm)
                                   .cursorPointer()
                                   .role(at::Role::ListItem)
                                   .textSize(itemSize)
                                   .lineHeight(itemLine)
                                   .fontFamily(m_palette.bodyFamily)
                                   .textColor(closed ? look.faint : m_palette.text);
            const at::Color hover = look.hover;
            if (!m.touch)
                std::move(item).hover([hover](at::Style &s) { s.background = at::Background::solid(hover); });
            if (status == u"cancelled")
                std::move(item).lineThrough();
            if (isTask) {
                // The task's box: it changes the task in its own file (this page: an undoable edit).
                const float box = m_metrics.taskBox;
                at::Div check = at::div()
                                        .id(u"task")
                                        .size(box)
                                        .rounded(3)
                                        .border(1.5f)
                                        .borderColor(m_palette.muted)
                                        .role(at::Role::CheckBox)
                                        .checked(closed)
                                        .ariaLabel(u16View(translated("QueryBlockView", "Task: %1").arg(plain)))
                                        .cursorPointer()
                                        .onClick([self, row, i](const at::ClickEvent &, at::Window &, at::App &) {
                                            if (QueryResultModel *res = self->queryResults(int(row)))
                                                res->toggleTask(i);
                                        });
                if (closed)
                    std::move(check).bg(m_palette.muted);
                std::move(item).child(at::div()
                                              .w(look.taskBoxWidth + 2)
                                              .h(itemLine)
                                              .flexShrink0()
                                              .flexRow()
                                              .itemsCenter()
                                              .child(std::move(check)));
            }
            if (table) {
                // A TABLE row's cells side by side, each at most 4 lines.
                const QStringList cells = r.data(QueryResultModel::CellsRole).toStringList();
                auto &cache = q.cells[std::size_t(i)];
                at::Div cellsRow = at::div().flex1().minW(0).flexRow().role(at::Role::None);
                for (qsizetype c = 0; c < cells.size(); ++c) {
                    const auto rt = renderedOnce(cache, std::size_t(c), cells.at(c));
                    std::move(cellsRow).child(at::div()
                                                      .flex1()
                                                      .flexBasis(at::pct(0))
                                                      .minW(0)
                                                      .pr(8)
                                                      .maxH(4 * itemLine)
                                                      .overflowHidden()
                                                      .child(itemText(*rt, QString())));
                }
                std::move(item).child(std::move(cellsRow));
            } else {
                const QString html = r.data(QueryResultModel::HtmlRole).toString();
                const auto rt = renderedOnce(q.texts, std::size_t(i), html);
                const bool showPage = r.data(QueryResultModel::ShowPageRole).toBool();
                const QString title = r.data(QueryResultModel::TitleRole).toString();
                const QString page = showPage && !title.isEmpty()
                                             ? pageLabel(title, r.data(QueryResultModel::JournalRole).toString(), m_model)
                                             : QString();
                std::move(item).child(at::div().flex1().minW(0).maxH(4 * itemLine).overflowHidden().child(itemText(*rt, page)));
            }
            // A click on the item opens it at its block (on this page: the editor scrolls to it).
            std::move(item).onClick([self, row, i](const at::ClickEvent &, at::Window &, at::App &) {
                if (QueryResultModel *res = self->queryResults(int(row))) {
                    const QVariantMap answer = res->open(i);
                    if (self->onQueryAnswer)
                        self->onQueryAnswer(int(row), answer);
                }
            });
            std::move(list).child(std::move(item));
        }
        std::move(frame).child(std::move(list));
        if (results->hiddenCount() > 0) {
            // "Show 50 more".
            std::move(frame).child(ui(at::div()
                                              .id(u"more")
                                              .selfStart()
                                              .h(m.touch ? 32 : 26)
                                              .px(10)
                                              .flexRow()
                                              .itemsCenter()
                                              .rounded(look.radiusSm)
                                              .textSize(look.smallSize)
                                              .fontMedium()
                                              .textColor(m_palette.muted)
                                              .cursorPointer()
                                              .role(at::Role::Button)
                                              .child(at::text(u16View(translated(
                                                      "QueryBlockView", "Show %n more", std::min(50, results->hiddenCount())))))
                                              .onClick([self, row](const at::ClickEvent &, at::Window &, at::App &) {
                                                  if (QueryResultModel *res = self->queryResults(int(row)))
                                                      res->showMore();
                                              })));
        }
    }

    // ---- Nothing found, errors, no live results.
    QString message;
    if (!live)
        message = translated("QueryBlockView", "Query results are shown when a notebook is open.");
    else if (!ready)
        message.clear();
    else if (!ok)
        message = !results->error().isEmpty() ? results->error() : translated("QueryBlockView", "This query cannot be run.");
    else if (emptySource)
        message = dataview ? translated("QueryBlockView", "Write a Dataview query, e.g. TABLE status FROM #projekt")
                           : translated("QueryBlockView", "Write filters, e.g. type:tasks status:open tag:zakupy");
    else if (results->itemCount() == 0)
        message = results->complete() ? translated("QueryBlockView", "Nothing matches this query.")
                                      : translated("QueryBlockView", "Nothing yet — the notebook is still being indexed.");
    else if (!results->complete())
        message = translated("QueryBlockView", "The notebook is still being indexed: more results may appear.");
    if (!message.isEmpty()) {
        const bool error = live && !ok;
        const at::Color ink = error ? look.danger : look.faint;
        std::move(frame).child(ui(at::div()
                                          .flexRow()
                                          .itemsStart()
                                          .gap(8)
                                          .textSize(look.smallSize)
                                          .textColor(ink)
                                          .child(at::div().pt(1).child(glyphElement(look.smallSize + 2, ink, Glyph::Info)))
                                          .child(at::div().flex1().minW(0).child(at::text(u16View(message))))));
    }

    // The row: the frame at the block's level, the selection's band around it.
    at::Div line = at::div().id(at::ElementId::number(uid)).pl(m.contentX).py(m.rowPad).flexRow();
    if (m.contentWidth > 0)
        std::move(line).maxW(m.contentX + m.contentWidth);
    const QString edge = index.data(BlockListModel::SelectionEdgeRole).toString();
    if (!edge.isEmpty()) {
        const bool start = edge == u"single" || edge == u"first";
        const bool end = edge == u"single" || edge == u"last";
        const float r = m_selectionLook.radius;
        std::move(line).bg(m_selectionLook.band).roundedT(start ? r : 0).roundedB(end ? r : 0);
        if (edge == u"single")
            std::move(line).border(1).borderColor(m_selectionLook.ring.fadedBy(0.65f));
        else
            std::move(line).borderL(2).borderColor(m_selectionLook.ring.fadedBy(0.4f));
    }
    if (onBlockMenu)
        std::move(line).onMouseDown(at::MouseButton::Right,
                                    [selfId, row](const at::MouseDownEvent &e, at::Window &w, at::App &a) {
                                        a.updateEntity<PageView>(selfId, [&](PageView &v, at::Context<PageView> &) {
                                            if (v.onBlockMenu)
                                                v.onBlockMenu(int(row), e.position);
                                        });
                                        w.stopPropagation();
                                    });
    if (onLongPressRow)
        std::move(line).onLongPress([selfId, row](at::Point, at::Window &, at::App &a) {
            a.updateEntity<PageView>(selfId, [row](PageView &v, at::Context<PageView> &) {
                if (v.onLongPressRow)
                    v.onLongPressRow(int(row));
            });
        });
    return std::move(line).child(std::move(frame));
}

} // namespace brulion::editor::atr
