// QML registration of the content classes (library brulion_content, content/CMakeLists.txt) in
// the app's QML module "Brulion" — the classes know nothing about QML (the src/app/ui, editor and
// planning pattern). All are creatable and take the `Workspace` singleton:
//
//   JournalListModel  { workspace: Workspace; today: DatesHelper.today }      JournalsView
//   BacklinksModel    { workspace: Workspace; path: page.path }                PageView, side panel
//   TagBlocksModel    { workspace: Workspace; tag: "praca" }                   TagsView (one tag)
//   TagListModel      { workspace: Workspace }                                 TagsView
//   PageListModel     { workspace: Workspace; sortKey: "modified" }            AllPagesView
//   SearchResultModel { workspace: Workspace; query: field.text }              SearchView
//   HistoryModel      { workspace: Workspace; path: route.path }               HistoryView
//   OutlineModel      { source: page.blocks }                                  side panel
//   PageTools         { workspace: Workspace }                                 page header and menu
#pragma once

#include "historymodel.h"
#include "journallistmodel.h"
#include "outlinemodel.h"
#include "pagelistmodel.h"
#include "pagetools.h"
#include "refmodels.h"
#include "searchmodel.h"
#include "taglistmodel.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::content {

struct JournalListModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::JournalListModel)
    QML_NAMED_ELEMENT(JournalListModel)
};

struct BacklinksModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::BacklinksModel)
    QML_NAMED_ELEMENT(BacklinksModel)
};

struct TagBlocksModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::TagBlocksModel)
    QML_NAMED_ELEMENT(TagBlocksModel)
};

struct TagListModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::TagListModel)
    QML_NAMED_ELEMENT(TagListModel)
};

struct PageListModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::PageListModel)
    QML_NAMED_ELEMENT(PageListModel)
};

struct SearchResultModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::SearchResultModel)
    QML_NAMED_ELEMENT(SearchResultModel)
};

struct HistoryModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::HistoryModel)
    QML_NAMED_ELEMENT(HistoryModel)
};

struct OutlineModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::OutlineModel)
    QML_NAMED_ELEMENT(OutlineModel)
};

struct PageToolsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::content::PageTools)
    QML_NAMED_ELEMENT(PageTools)
};

} // namespace brulion::content
