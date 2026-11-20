// Templates (docs/SYNTAX.md §12): `{{date}}`, `{{date:FORMAT}}`, `{{time}}`, `{{time:FORMAT}}`,
// `{{title}}`, `{{weekday}}`, `{{month}}`, `{{week}}`, `{{end}}`, `{{end:FORMAT}}`, `{{cursor}}`. Part of the markdown
// module — see markdown/markdown.h for the module overview.
#pragma once

#include "dates/names.h"

#include <QDateTime>
#include <QString>
#include <QStringView>

#include <functional>
#include <optional>

namespace brulion::markdown {

/// Values available to applyTemplate().
struct TemplateVars {
    QDateTime now; ///< local date and time (inject a fixed value in tests)
    QString title; ///< page title
    dates::UiLocale locale = dates::UiLocale::Polish;
    /// The last day of the period a weekly or monthly note covers ({{end}}); invalid: `now`'s day.
    QDate end;
    /// A variable of an extension, `{{silownia/plan Przysiad}}` (docs/TEMPLATES.md "Variables of
    /// extensions"): its text — Markdown, any number of lines —, or nullopt to leave it as written.
    /// Unset: every one is left as written (brulion-cli, quick capture).
    std::function<std::optional<QString>(const QString &extension, const QString &name, const QString &arguments)>
            variable;
};

struct TemplateResult {
    QString text;
    qsizetype cursor = -1; ///< offset of the first `{{cursor}}` in `text`, -1 when absent
};

/// Fill a template. `{{date}}` = yyyy-MM-dd, `{{time}}` = HH:mm, `{{date:FORMAT}}` /
/// `{{time:FORMAT}}` take Qt date-time format strings (names in the UI language: Polish
/// "środa, 14 października 2026"), `{{weekday}}` is the localised weekday name, `{{title}}` the
/// page title (not expanded again), `{{month}}` the month's name as a heading has it ("listopad",
/// "November"; Qt's MMMM writes "listopada"), `{{week}}` the ISO week number of `now`'s day, `{{end}}` /
/// `{{end:FORMAT}}` the period's last day (`vars.end`; else `now`'s day) like `{{date}}`. `{{cursor}}` is removed and
/// its first offset returned. Variable names are case-insensitive and may have blanks inside the braces; unknown
/// variables are left as written. `{{<extension>/<name> arguments}}` (an id of letters, digits, `-` and `_`, a
/// name as markdown::isMacroName takes it, arguments up to the `}}` on the same line) is `vars.variable`'s; a
/// macro, `{{ext:…}}`, is no template variable: it stays as written, alive in the note made from the template.
TemplateResult applyTemplate(QStringView tpl, const TemplateVars &vars);

} // namespace brulion::markdown
