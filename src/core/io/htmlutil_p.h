// Small HTML helpers shared by the HTML conversion and the Evernote importer. Internal to the io
// module.
#pragma once

#include <QString>
#include <QStringList>

namespace brulion::io::detail {

/// Remove the elements named `tags` (lower case) with their content: from `<tag …>` to the
/// matching `</tag>`, or to the end of the text when the element is not closed (like browsers do
/// for <script>). Case-insensitive, linear time. `removed` is set when anything was removed.
QString removeElements(const QString &html, const QStringList &tags, bool *removed = nullptr);

} // namespace brulion::io::detail
