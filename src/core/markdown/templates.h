// Templates (docs/SYNTAX.md §12): `{{date}}`, `{{date:FORMAT}}`, `{{time}}`, `{{time:FORMAT}}`,
// `{{title}}`, `{{weekday}}`, `{{cursor}}`. Part of the markdown module — see
// markdown/markdown.h for the module overview.
#pragma once

#include "dates/names.h"

#include <QDateTime>
#include <QString>
#include <QStringView>

namespace brulion::markdown {

/// Values available to applyTemplate().
struct TemplateVars {
    QDateTime now; ///< local date and time (inject a fixed value in tests)
    QString title; ///< page title
    dates::UiLocale locale = dates::UiLocale::Polish;
};

struct TemplateResult {
    QString text;
    qsizetype cursor = -1; ///< offset of the first `{{cursor}}` in `text`, -1 when absent
};

/// Fill a template. `{{date}}` = yyyy-MM-dd, `{{time}}` = HH:mm, `{{date:FORMAT}}` /
/// `{{time:FORMAT}}` take Qt date-time format strings (names in the UI language: Polish
/// "środa, 14 października 2026"), `{{weekday}}` is the localised weekday name, `{{title}}` the
/// page title (not expanded again). `{{cursor}}` is removed and its first offset returned.
/// Variable names are case-insensitive and may have blanks inside the braces; unknown
/// variables are left as written.
TemplateResult applyTemplate(QStringView tpl, const TemplateVars &vars);

} // namespace brulion::markdown
