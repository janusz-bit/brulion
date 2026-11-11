#include "extensionmodels.h"

using namespace Qt::StringLiterals;

namespace brulion::shell {

// ---- ExtensionListModel ------------------------------------------------------------------------

int ExtensionListModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_rows.size()); }

QVariant ExtensionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const ExtensionRow &r = m_rows[index.row()];
    switch (role) {
    case IdRole: return r.id;
    case TitleRole: return r.title;
    case KindRole: return r.kind;
    case StateRole: return r.state;
    case MessageRole: return r.message;
    case PathRole: return r.path;
    case PermissionsRole: return r.permissions;
    case DescriptionRole: return r.description;
    case CommandsRole: return r.commands;
    case ViewsRole: return r.views;
    case HooksRole: return r.hooks;
    case ErrorLineRole: return r.errorLine;
    }
    return {};
}

QHash<int, QByteArray> ExtensionListModel::roleNames() const
{
    return {{IdRole, "extId"},
            {TitleRole, "extTitle"},
            {KindRole, "extKind"},
            {StateRole, "extState"},
            {MessageRole, "extMessage"},
            {PathRole, "extPath"},
            {PermissionsRole, "extPermissions"},
            {DescriptionRole, "extDescription"},
            {CommandsRole, "extCommands"},
            {ViewsRole, "extViews"},
            {HooksRole, "extHooks"},
            {ErrorLineRole, "extErrorLine"}};
}

void ExtensionListModel::setRows(const QList<ExtensionRow> &rows)
{
    if (rows == m_rows)
        return;
    const bool countChanges = rows.size() != m_rows.size();
    if (countChanges) {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        Q_EMIT countChanged();
        return;
    }
    // Same rows, new states: changed in place (a delegate keeps its focus and its buttons).
    m_rows = rows;
    Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
}

// ---- ExtensionConsoleModel ---------------------------------------------------------------------

int ExtensionConsoleModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_lines.size()); }

QVariant ExtensionConsoleModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_lines.size())
        return {};
    const Line &l = m_lines[index.row()];
    switch (role) {
    case KindRole: return l.kind;
    case TextRole: return l.text;
    case SourceRole: return l.source;
    }
    return {};
}

QHash<int, QByteArray> ExtensionConsoleModel::roleNames() const
{
    return {{KindRole, "lineKind"}, {TextRole, "lineText"}, {SourceRole, "lineSource"}};
}

void ExtensionConsoleModel::append(const QString &kind, const QString &text, const QString &source)
{
    if (m_lines.size() >= MaxLines) {
        const int drop = MaxLines / 10;
        beginRemoveRows({}, 0, drop - 1);
        m_lines.remove(0, drop);
        endRemoveRows();
    }
    beginInsertRows({}, int(m_lines.size()), int(m_lines.size()));
    m_lines.append({kind, text.left(20000), source});
    endInsertRows();
    if (kind == u"input" && (m_inputs.isEmpty() || m_inputs.last() != text)) {
        m_inputs.append(text);
        if (m_inputs.size() > 200)
            m_inputs.removeFirst();
    }
    Q_EMIT countChanged();
}

void ExtensionConsoleModel::clear()
{
    if (m_lines.isEmpty())
        return;
    beginResetModel();
    m_lines.clear();
    endResetModel();
    Q_EMIT countChanged();
}

QString ExtensionConsoleModel::inputAt(int back) const
{
    return back >= 0 && back < m_inputs.size() ? m_inputs[m_inputs.size() - 1 - back] : QString();
}

QString ExtensionConsoleModel::text() const
{
    QStringList out;
    for (const Line &l : m_lines) {
        if (l.kind == u"input")
            out << u"> "_s + QString(l.text).replace(u'\n', u"\n  "_s);
        else if (!l.source.isEmpty() && l.kind != u"result")
            out << l.source + u": "_s + l.text;
        else
            out << l.text;
    }
    return out.join(u'\n');
}

} // namespace brulion::shell
