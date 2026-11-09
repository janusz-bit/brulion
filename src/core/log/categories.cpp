#include "log/categories.h"

#include <utility>

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

QString categoryGroup(QStringView category)
{
    // Checked in order: "brulion" last, it takes every other brulion.* category.
    static const std::pair<QStringView, QString> groups[] = {{u"brulion.sync", u"sync"_s},
                                                             {u"brulion.net", u"net"_s},
                                                             {u"qt.network", u"net"_s},
                                                             {u"brulion.vault", u"vault"_s},
                                                             {u"brulion.workspace", u"vault"_s},
                                                             {u"brulion.index", u"index"_s},
                                                             {u"brulion.reminders", u"reminders"_s},
                                                             {u"brulion.notify", u"reminders"_s},
                                                             {u"brulion.io", u"io"_s},
                                                             {u"brulion", u"app"_s}};
    for (const auto &[prefix, group] : groups) {
        // The category itself or one below it ("brulion.sync.webdav", not "brulion.synchronous").
        if (category.startsWith(prefix) && (category.size() == prefix.size() || category.at(prefix.size()) == u'.'))
            return group;
    }
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
