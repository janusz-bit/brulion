#include "cleanupselection.h"

#include <QSet>
#include <QVariantMap>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// `paths` with `path` ticked (`on`: once) or not.
void tick(QStringList &paths, const QString &path, bool on)
{
    if (!on)
        paths.removeAll(path);
    else if (!paths.contains(path))
        paths.append(path);
}

} // namespace

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
    const bool attachments = m_kind == u"attachments";
    const bool all = !attachments && m_kind != u"notes";
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
    tick(next, path, on);
    return next;
}

QStringList CleanupSelection::withAllShown(bool on) const
{
    QStringList next = m_ticked;
    for (const QVariant &value : m_shown)
        tick(next, value.toMap().value(u"path"_s).toString(), on);
    return next;
}

QStringList CleanupSelection::existingTicks() const
{
    QSet<QString> paths;
    for (const QVariant &value : m_files)
        paths.insert(value.toMap().value(u"path"_s).toString());
    QStringList kept = m_ticked;
    kept.removeIf([&](const QString &path) { return !paths.contains(path); });
    return kept;
}

QStringList CleanupSelection::routeTicks(const QVariantMap &route) const
{
    return route.value(u"selected"_s).toStringList();
}

} // namespace brulion::shell
