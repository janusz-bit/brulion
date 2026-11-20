// QML registration of the attachments' search (library brulion_attachments,
// attachments/CMakeLists.txt) in the app's QML module "Brulion": the type of Workspace.attachments
// (Settings → Search, the search view's progress line, the picture view's highlights).
#pragma once

#include "attachmentindexer.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace brulion::attachsearch {

struct AttachmentIndexerForeign {
    Q_GADGET
    QML_FOREIGN(brulion::attachsearch::AttachmentIndexer)
    QML_NAMED_ELEMENT(AttachmentIndexer)
    QML_UNCREATABLE("Use Workspace.attachments")
};

} // namespace brulion::attachsearch
