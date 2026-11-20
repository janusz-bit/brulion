// Translated texts of database views (column names, summaries, errors, notices). English source
// strings in QCoreApplication::translate() calls with the context "brulion::Views" (picked up by
// lupdate). Internal to the views module.
#pragma once

#include "index/query.h"

#include <QString>

namespace brulion::views::messages {

/// The header of a built-in column ("Title", "Status", …).
QString columnLabel(QueryColumnKind kind);
QString summaryName(const QString &function);
QString summaryWithoutColumn(const QString &column);
QString formulaError(const QString &column, const QString &message);
QString days(int n);
QString mapAsTable();
QString unknownViewType(const QString &type);
QString boardNeedsGroup();
QString boardWithoutDrops();
QString notYaml();
QString noViews();
QString badView(int number, const QString &message);
QString badFilter(const QString &where, const QString &message);
QString badFormula(const QString &name, const QString &message);
QString circularFormula(const QString &name);
QString unknownSummary(const QString &name);
QString unsupportedKey(const QString &key);
QString blockKeyNotAscii(const QString &key);
QString notEditable();
QString invalidValue(const QString &type);
QString yamlNotEditable();
QString fileName();
QString noValue();

} // namespace brulion::views::messages
