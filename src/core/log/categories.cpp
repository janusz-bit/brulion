#include "log/categories.h"

using namespace Qt::StringLiterals;

namespace brulion::logging {

Q_LOGGING_CATEGORY(lcApp, "brulion.app")
Q_LOGGING_CATEGORY(lcSync, "brulion.sync")
Q_LOGGING_CATEGORY(lcNet, "brulion.net")
Q_LOGGING_CATEGORY(lcVault, "brulion.vault")
Q_LOGGING_CATEGORY(lcIndex, "brulion.index")
Q_LOGGING_CATEGORY(lcReminders, "brulion.reminders")
Q_LOGGING_CATEGORY(lcIo, "brulion.io")
Q_LOGGING_CATEGORY(lcPlatform, "brulion.platform")

namespace {

bool isOrBelow(QStringView category, QStringView prefix)
{
    return category == prefix
           || (category.startsWith(prefix) && category.size() > prefix.size() && category.at(prefix.size()) == u'.');
}

} // namespace

QString categoryGroup(QStringView category)
{
    if (isOrBelow(category, u"brulion.sync"))
        return u"sync"_s;
    if (isOrBelow(category, u"brulion.net") || isOrBelow(category, u"qt.network"))
        return u"net"_s;
    if (isOrBelow(category, u"brulion.vault") || isOrBelow(category, u"brulion.workspace"))
        return u"vault"_s;
    if (isOrBelow(category, u"brulion.index"))
        return u"index"_s;
    if (isOrBelow(category, u"brulion.reminders") || isOrBelow(category, u"brulion.notify"))
        return u"reminders"_s;
    if (isOrBelow(category, u"brulion.io"))
        return u"io"_s;
    if (isOrBelow(category, u"brulion"))
        return u"app"_s;
    return u"system"_s;
}

QStringList categoryGroups()
{
    return {u"sync"_s, u"net"_s, u"vault"_s, u"index"_s, u"reminders"_s, u"io"_s, u"app"_s, u"system"_s};
}

QStringList verbosityCategories()
{
    return {u"brulion.app"_s,        u"brulion.sync"_s,      u"brulion.net"_s,      u"brulion.vault"_s,
            u"brulion.index"_s,      u"brulion.reminders"_s, u"brulion.io"_s,       u"brulion.platform"_s,
            u"brulion.platform.*"_s, u"brulion.workspace"_s, u"brulion.notify.*"_s, u"brulion.android"_s};
}

} // namespace brulion::logging
