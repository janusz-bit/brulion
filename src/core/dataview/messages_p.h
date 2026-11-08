// Translated texts of Dataview queries (errors, headers, durations). English source strings in
// QCoreApplication::translate() calls with the context "brulion::Dataview" (picked up by
// lupdate). Internal to the dataview module.
#pragma once

#include <QString>

namespace brulion::dataview::messages {

QString atLine(int line, const QString &message);
QString emptyQuery();
QString expectedQueryType(const QString &found);
QString calendarNotSupported();
QString unknownCommand(const QString &found);
QString fromNotFirst();
QString badSource(const QString &found);
QString csvNotSupported();
QString badLimit(const QString &found, int max);
QString unclosedString();
QString unclosedLink();
QString expectedExpression(const QString &found);
QString expectedClosing(const QString &closing, const QString &found);
QString expectedName(const QString &after);
QString unexpected(const QString &found);
QString unknownFunction(const QString &name);
QString unsupportedFunction(const QString &name);
QString wrongArgumentCount(const QString &name);
QString tooDeep();
QString badDate(const QString &text);
QString badDuration(const QString &text);
QString tooCostly();
QString tooManyRows(int max);
QString notAFunction(const QString &name);

/// The first column of a TABLE (the page).
QString fileHeader();
/// The first column of a grouped TABLE without a GROUP BY … AS name.
QString groupHeader();

/// Durations in words ("3 days, 2 hours").
QString years(int n);
QString months(int n);
QString weeks(int n);
QString days(int n);
QString hours(int n);
QString minutes(int n);
QString seconds(int n);
QString milliseconds(int n);

} // namespace brulion::dataview::messages
