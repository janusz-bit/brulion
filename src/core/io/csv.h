// CSV (RFC 4180) reading and writing: Notion databases on import, task lists on export. Part of
// the io module — see io/io.h for the overview.
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QStringView>

namespace brulion::io {

/// Parse CSV text into rows of fields. Quoted fields may contain separators, `""` and line
/// breaks; CRLF, LF and CR end records; a leading BOM is skipped; a final line break does not
/// add an empty row. Malformed quoting is read leniently (a stray quote is kept as text).
QList<QStringList> parseCsv(QStringView text, QChar separator = u',');

/// One field: quoted (with `"` doubled) when it contains the separator, a quote, a line break
/// or leading/trailing whitespace.
QString csvField(QStringView value, QChar separator = u',');

/// One record (no line terminator).
QString csvRow(const QStringList &fields, QChar separator = u',');

/// Neutralise spreadsheet formulas in untrusted text ("CSV injection"): a value starting with
/// `=`, `+`, `-`, `@`, tab or CR gets a leading apostrophe. Numbers like "-5" are kept.
QString csvSafeText(const QString &value);

} // namespace brulion::io
