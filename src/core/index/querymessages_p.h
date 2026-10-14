// Translated texts of the query language (errors and group labels). English source strings in
// QCoreApplication::translate() calls with the context "brulion::Query" (picked up by lupdate).
// Internal to the index module.
#pragma once

#include <QString>

namespace brulion::index_detail::messages {

QString unknownKey(const QString &key, const QString &allowed);
QString emptyValue(const QString &key, const QString &example);
QString badValue(const QString &key, const QString &value, const QString &allowed);
QString badDate(const QString &key, const QString &value);
QString operatorWithKeyword(const QString &key, const QString &op, const QString &value);
QString badLimit(const QString &value, int max);
QString unclosedQuote();
QString notNegatable(const QString &key);
QString typeConflict(const QString &key, const QString &value, const QString &type);
QString duplicate(const QString &key);

/// Labels of groups without a value.
QString noDate();
QString noTag();
QString noStatus();
QString noPriority();

} // namespace brulion::index_detail::messages
