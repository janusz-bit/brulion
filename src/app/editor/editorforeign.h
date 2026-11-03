// QML registration of the block editor classes in brulion_editor (src/app/editor/CMakeLists.txt):
// the classes know nothing about QML; these wrappers make them types of the app's QML module
// "Brulion" (PageController, BlockListModel, MarkdownHighlighter, MermaidView, RichTextPills, RichTextLinks,
// LinkPreview, SuggestionModel, QueryResultModel, StreamSelection, FakeEditorHost, the EditorInput and SpellCheck singletons;
// EditorHost as an uncreatable interface type), so qmllint/qmlcachegen see their full API from the library's extracted
// metatypes.
#pragma once

#include "blocklistmodel.h"
#include "editorhost.h"
#include "editorinput.h"
#include "fakeeditorhost.h"
#include "linkpreview.h"
#include "markdownhighlighter.h"
#include "mermaidview.h"
#include "notefind.h"
#include "pagecontroller.h"
#include "queryresultmodel.h"
#include "richtextlinks.h"
#include "richtextpills.h"
#include "spellcheck.h"
#include "streamfind.h"
#include "streamselection.h"
#include "suggestionmodel.h"

#include <QJSEngine>
#include <QtQml/qqmlregistration.h>

namespace brulion::editor {

struct PageControllerForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::PageController)
    QML_NAMED_ELEMENT(PageController)
};

struct NoteFindForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::NoteFind)
    QML_NAMED_ELEMENT(NoteFind)
};

struct StreamSelectionForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::StreamSelection)
    QML_NAMED_ELEMENT(StreamSelection)
};

struct StreamFindForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::StreamFind)
    QML_NAMED_ELEMENT(StreamFind)
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

struct MermaidViewForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::MermaidView)
    QML_NAMED_ELEMENT(MermaidView)
};

struct RichTextPillsForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::RichTextPills)
    QML_NAMED_ELEMENT(RichTextPills)
};

struct RichTextLinksForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::RichTextLinks)
    QML_NAMED_ELEMENT(RichTextLinks)
};

struct LinkPreviewForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::LinkPreview)
    QML_NAMED_ELEMENT(LinkPreview)
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

struct SpellCheckForeign {
    Q_GADGET
    QML_FOREIGN(brulion::editor::SpellCheck)
    QML_NAMED_ELEMENT(SpellCheck)
    QML_SINGLETON
public:
    /// The app's one (SpellCheck::instance(), owned by the application: main.cpp binds it to the settings).
    static SpellCheck *create(QQmlEngine *, QJSEngine *)
    {
        SpellCheck *spell = SpellCheck::instance();
        QJSEngine::setObjectOwnership(spell, QJSEngine::CppOwnership);
        return spell;
    }
};

struct EditorHostForeign {
    Q_GADGET
    QML_FOREIGN(brulion::EditorHost)
    QML_NAMED_ELEMENT(EditorHost)
    QML_UNCREATABLE("EditorHost is an interface implemented in C++")
};

} // namespace brulion::editor
