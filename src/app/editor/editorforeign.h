// QML registration of the block editor classes in brulion_editor (src/app/editor/CMakeLists.txt):
// the classes know nothing about QML; these wrappers make them types of the app's QML module
// "Brulion" (PageController, BlockListModel, MarkdownHighlighter, RichTextPills, SuggestionModel,
// QueryResultModel, StreamSelection, FakeEditorHost, the EditorInput singleton; EditorHost as an uncreatable
// interface type), so
// qmllint/qmlcachegen see their full API from the library's extracted metatypes.
#pragma once

#include "blocklistmodel.h"
#include "editorhost.h"
#include "editorinput.h"
#include "fakeeditorhost.h"
#include "markdownhighlighter.h"
#include "pagecontroller.h"
#include "queryresultmodel.h"
#include "richtextpills.h"
#include "streamselection.h"
#include "suggestionmodel.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::editor {

struct PageControllerForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::PageController)
    QML_NAMED_ELEMENT(PageController)
};

struct StreamSelectionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::StreamSelection)
    QML_NAMED_ELEMENT(StreamSelection)
};

struct BlockListModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::BlockListModel)
    QML_NAMED_ELEMENT(BlockListModel)
    QML_UNCREATABLE("BlockListModel is owned by a PageController (PageController.blocks)")
};

struct MarkdownHighlighterForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::MarkdownHighlighter)
    QML_NAMED_ELEMENT(MarkdownHighlighter)
};

struct RichTextPillsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::RichTextPills)
    QML_NAMED_ELEMENT(RichTextPills)
};

struct SuggestionModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::SuggestionModel)
    QML_NAMED_ELEMENT(SuggestionModel)
};

struct QueryResultModelForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::QueryResultModel)
    QML_NAMED_ELEMENT(QueryResultModel)
};

struct FakeEditorHostForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::FakeEditorHost)
    QML_NAMED_ELEMENT(FakeEditorHost)
};

struct EditorInputForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::EditorInput)
    QML_NAMED_ELEMENT(EditorInput)
    QML_SINGLETON
};

struct EditorHostForeign {
    Q_GADGET
    QML_FOREIGN(brulion::EditorHost)
    QML_NAMED_ELEMENT(EditorHost)
    QML_UNCREATABLE("EditorHost is an interface implemented in C++")
};

} // namespace brulion::editor
