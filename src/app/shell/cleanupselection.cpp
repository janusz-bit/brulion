#include "cleanupselection.h"

#include <QSet>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace brulion::shell {

CleanupSelection::CleanupSelection(QObject *parent) : QObject(parent) { }

void CleanupSelection::setFiles(const QVariantList &files)
{
    m_files = files;
    update();
}

void CleanupSelection::setKind(const QString &kind)
{
    if (m_kind == kind)
        return;
    m_kind = kind;
    update();
}

void CleanupSelection::setTicked(const QStringList &ticked)
{
    if (m_ticked == ticked)
        return;
    m_ticked = ticked;
    update();
}

void CleanupSelection::update()
{
    const bool all = m_kind != u"notes"_s && m_kind != u"attachments"_s;
    const bool attachments = m_kind == u"attachments"_s;
    const QSet<QString> ticked(m_ticked.cbegin(), m_ticked.cend());
    m_shown.clear();
    m_shownTicked.clear();
    m_attachmentCount = 0;
    m_tickedWithWork = 0;
    for (const QVariant &value : std::as_const(m_files)) {
        const QVariantMap file = value.toMap();
        const bool attachment = file.value(u"attachment"_s).toBool();
        if (attachment)
            ++m_attachmentCount;
        if (!all && attachment != attachments)
            continue;
        m_shown.append(value);
        const QString path = file.value(u"path"_s).toString();
        if (!ticked.contains(path))
            continue;
        m_shownTicked.append(path);
        if (file.value(u"openTasks"_s).toInt() > 0 || file.value(u"reminders"_s).toInt() > 0)
            ++m_tickedWithWork;
    }
    Q_EMIT changed();
}

QStringList CleanupSelection::withTicked(const QString &path, bool on) const
{
    QStringList next = m_ticked;
    if (on) {
        if (!next.contains(path))
            next.append(path);
    } else {
        next.removeAll(path);
    }
    return next;
}

QStringList CleanupSelection::withAllShown(bool on) const
{
    QStringList next = m_ticked;
    for (const QVariant &value : m_shown) {
        const QString path = value.toMap().value(u"path"_s).toString();
        if (on) {
            if (!next.contains(path))
                next.append(path);
        } else {
            next.removeAll(path);
        }
    }
    return next;
}

QStringList CleanupSelection::existingTicks() const
{
    QSet<QString> paths;
    for (const QVariant &value : m_files)
        paths.insert(value.toMap().value(u"path"_s).toString());
    QStringList kept;
    for (const QString &path : m_ticked) {
        if (paths.contains(path))
            kept.append(path);
    }
    return kept;
}

QStringList CleanupSelection::routeTicks(const QVariantMap &route) const
{
    return route.value(u"selected"_s).toStringList();
}

} // namespace brulion::shell
