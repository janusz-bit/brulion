// The publish module's words for people (translated: i18n/, the app and brulion-cli): failures by
// their code, why a file was left out, what happens to a file, the kinds of targets.
// Part of the publish module — see publish/publish.h.
#pragma once

#include "publish/plan.h"
#include "publish/selection.h"
#include "publish/target.h"

#include <QCoreApplication>
#include <QString>

namespace brulion::publish {

class PublishMessages {
    Q_DECLARE_TR_FUNCTIONS(PublishMessages)
};

/// A failure's code (PublishReport::errorCode, FileFailure::code, checkTarget()) in words;
/// `detail` (the server's own message) is added where it helps.
QString errorText(const QString &code, const QString &detail = {});
/// A warning's code (PublishReport::warningCodes) in words.
QString warningText(const QString &code, const QString &detail = {});
/// Why a file is left out ("Marked publish: false").
QString reasonText(LeftOutReason reason);
/// What happens to a file ("New", "Changed", "Unchanged", "Removed", "Changed there since").
QString actionText(FileAction action, bool deletion = false);
/// "GitHub Pages", "WebDAV", "Nextcloud", "Folder".
QString kindText(TargetKind kind);
/// The footer of published pages: "Published with Brulion".
QString publishedFooter();

} // namespace brulion::publish
