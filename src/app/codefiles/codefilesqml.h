// QML registration of the codefiles area (library brulion_codefiles, codefiles/area.cmake) in the
// app's QML module "Brulion" (the src/app/ui pattern: the classes know nothing about QML):
//
//   CodeFileIndex      creatable — CodeFileIndex { folder: …; query: …; kinds: "files" }: ranked rows
//   CodeFileDocument   creatable — CodeFileDocument { folder: …; path: … }: the viewer's lines
//   CodeFileTable      creatable — CodeFileTable { document: doc }: a CSV / TSV file as a table
//   CodeSearch         creatable — CodeSearch { folder: … }: the project search's rows
//   CodeFiles          singleton — links, @-mentions, review comments, recent files, the clipboard
#pragma once

#include "codefiledocument.h"
#include "codefiletable.h"
#include "codefileindex.h"
#include "codefiles.h"
#include "codesearch.h"

#include <QtQml/qqmlregistration.h>

namespace brulion::codefiles {

struct CodeFileIndexForeign {
    Q_GADGET
    QML_FOREIGN(brulion::codefiles::CodeFileIndex)
    QML_NAMED_ELEMENT(CodeFileIndex)
};

struct CodeFileDocumentForeign {
    Q_GADGET
    QML_FOREIGN(brulion::codefiles::CodeFileDocument)
    QML_NAMED_ELEMENT(CodeFileDocument)
};

struct CodeFileTableForeign {
    Q_GADGET
    QML_FOREIGN(brulion::codefiles::CodeFileTable)
    QML_NAMED_ELEMENT(CodeFileTable)
};

struct CodeSearchForeign {
    Q_GADGET
    QML_FOREIGN(brulion::codefiles::CodeSearch)
    QML_NAMED_ELEMENT(CodeSearch)
};

struct CodeFilesForeign {
    Q_GADGET
    QML_FOREIGN(brulion::codefiles::CodeFiles)
    QML_NAMED_ELEMENT(CodeFiles)
    QML_SINGLETON
public:
    static CodeFiles *create(QQmlEngine *, QJSEngine *) { return new CodeFiles; }
};

} // namespace brulion::codefiles
