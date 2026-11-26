#include "codescriptsmodel.h"

using namespace Qt::StringLiterals;

namespace brulion::shell {

CodeScriptsModel::CodeScriptsModel(QObject *parent) : QAbstractListModel(parent) { }

void CodeScriptsModel::setProjects(CodeProjects *projects)
{
    if (m_projects == projects)
        return;
    m_projects = projects;
    Q_EMIT projectsChanged();
    reload();
}

void CodeScriptsModel::setProject(const QString &project)
{
    if (m_project == project)
        return;
    m_project = project;
    Q_EMIT projectChanged();
    reload();
}

bool CodeScriptsModel::dirty() const
{
    return m_rows != m_kept;
}

int CodeScriptsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant CodeScriptsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    switch (role) {
    case NameRole: return r.script.name;
    case CommandRole: return r.script.command;
    case IconRole: return r.script.icon;
    case IconNameRole: return CodeProjects::iconOf(r.script.icon);
    case ShortcutRole: return r.script.shortcut;
    case OnWorktreeRole: return r.script.onWorktree;
    case WaitRole: return r.script.wait;
    case SourceRole: return r.fromFile ? u"t3json"_s : u"device"_s;
    case OverridesRole: return r.overrides;
    case ScriptIdRole: return r.id;
    default: return {};
    }
}

QHash<int, QByteArray> CodeScriptsModel::roleNames() const
{
    return {{NameRole, "name"},
            {CommandRole, "command"},
            {IconRole, "scriptIcon"},
            {IconNameRole, "iconName"},
            {ShortcutRole, "shortcut"},
            {OnWorktreeRole, "runOnWorktreeCreate"},
            {WaitRole, "wait"},
            {SourceRole, "source"},
            {OverridesRole, "overrides"},
            {ScriptIdRole, "scriptId"}};
}

void CodeScriptsModel::reload()
{
    beginResetModel();
    m_rows.clear();
    if (m_projects) {
        for (const QVariant &v : m_projects->scripts(m_project)) {
            const QVariantMap m = v.toMap();
            Row r;
            r.script = {m.value(u"name"_s).toString(),
                        m.value(u"command"_s).toString(),
                        m.value(u"icon"_s).toString(),
                        m.value(u"shortcut"_s).toString(),
                        m.value(u"runOnWorktreeCreate"_s).toBool(),
                        m.value(u"wait"_s).toBool()};
            r.fromFile = m.value(u"source"_s).toString() == u"t3json";
            r.overrides = m.value(u"overrides"_s).toBool();
            r.id = m.value(u"id"_s).toString();
            m_rows << r;
        }
    }
    m_kept = m_rows;
    endResetModel();
    Q_EMIT countChanged();
    ++m_revision;
    Q_EMIT dirtyChanged();
}

int CodeScriptsModel::deviceCount() const
{
    int n = 0;
    for (const Row &r : m_rows)
        if (!r.fromFile)
            ++n;
    return n;
}

bool CodeScriptsModel::inFile(const QString &name) const
{
    return m_projects
           && std::ranges::any_of(m_projects->config(m_project).scripts, [&name](const CodeProjects::Script &s) {
                  return s.name.compare(name, Qt::CaseInsensitive) == 0;
              });
}

CodeProjects::Script CodeScriptsModel::fileScript(const QString &name) const
{
    if (m_projects)
        for (const CodeProjects::Script &s : m_projects->config(m_project).scripts)
            if (s.name.compare(name, Qt::CaseInsensitive) == 0)
                return s;
    return {};
}

void CodeScriptsModel::changed(int row)
{
    Q_EMIT dataChanged(index(row), index(row));
    ++m_revision;
    Q_EMIT dirtyChanged();
}

int CodeScriptsModel::add()
{
    if (m_rows.size() >= CodeProjects::MaxScripts)
        return -1;
    const int at = deviceCount();
    beginInsertRows({}, at, at);
    Row r;
    r.script.icon = u"play"_s;
    m_rows.insert(at, r);
    endInsertRows();
    Q_EMIT countChanged();
    ++m_revision;
    Q_EMIT dirtyChanged();
    return at;
}

bool CodeScriptsModel::remove(int row)
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).fromFile)
        return false;
    const Row gone = m_rows.at(row);
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
    // (An override gone: the file's script of its name again, after the device's rows.)
    if (gone.overrides && inFile(gone.script.name)) {
        const int at = deviceCount();
        beginInsertRows({}, at, at);
        Row r;
        r.script = fileScript(gone.script.name);
        r.fromFile = true;
        r.id = gone.id;
        m_rows.insert(at, r);
        endInsertRows();
    }
    Q_EMIT countChanged();
    ++m_revision;
    Q_EMIT dirtyChanged();
    return true;
}

bool CodeScriptsModel::canMove(int row, int delta) const
{
    const int to = row + delta;
    return row >= 0 && row < m_rows.size() && !m_rows.at(row).fromFile && to >= 0 && to < deviceCount();
}

bool CodeScriptsModel::moveRow(int row, int delta)
{
    if (!canMove(row, delta) || delta == 0)
        return false;
    const int to = row + delta;
    beginMoveRows({}, row, row, {}, to > row ? to + 1 : to);
    m_rows.move(row, to);
    endMoveRows();
    ++m_revision;
    Q_EMIT dirtyChanged();
    return true;
}

int CodeScriptsModel::own(int row)
{
    // (A row of the file changed: the device's own now — overriding the file's script of its name —,
    // moved to the end of the device's rows, before the file's.)
    if (!m_rows.at(row).fromFile)
        return row;
    m_rows[row].fromFile = false;
    m_rows[row].overrides = true;
    const int at = deviceCount() - 1;
    if (at != row) {
        beginMoveRows({}, row, row, {}, at > row ? at + 1 : at);
        m_rows.move(row, at);
        endMoveRows();
    }
    return at;
}

void CodeScriptsModel::setText(int row, const QString &field, const QString &value)
{
    if (row < 0 || row >= m_rows.size())
        return;
    CodeProjects::Script s = m_rows.at(row).script;
    if (field == u"name")
        s.name = value.simplified().left(60);
    else if (field == u"command")
        s.command = value;
    else if (field == u"icon")
        s.icon = value;
    else if (field == u"shortcut")
        s.shortcut = value.trimmed();
    else
        return;
    if (s == m_rows.at(row).script)
        return;
    row = own(row);
    m_rows[row].script = s;
    m_rows[row].overrides = inFile(s.name);
    changed(row);
}

void CodeScriptsModel::setFlag(int row, const QString &field, bool value)
{
    if (row < 0 || row >= m_rows.size())
        return;
    CodeProjects::Script s = m_rows.at(row).script;
    if (field == u"runOnWorktreeCreate") {
        s.onWorktree = value;
        if (!value)
            s.wait = false;
    } else if (field == u"wait") {
        s.wait = value && s.onWorktree;
    } else {
        return;
    }
    if (s == m_rows.at(row).script)
        return;
    row = own(row);
    m_rows[row].script = s;
    changed(row);
}

bool CodeScriptsModel::nameTaken(int row) const
{
    if (row < 0 || row >= m_rows.size() || m_rows.at(row).script.name.isEmpty())
        return false;
    for (int i = 0; i < m_rows.size(); ++i)
        if (i != row && m_rows.at(i).script.name.compare(m_rows.at(row).script.name, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void CodeScriptsModel::save()
{
    if (!m_projects)
        return;
    QVariantList out;
    for (const Row &r : std::as_const(m_rows)) {
        if (r.fromFile)
            continue;
        out << QVariantMap{{u"name"_s, r.script.name},
                           {u"command"_s, r.script.command},
                           {u"icon"_s, r.script.icon},
                           {u"shortcut"_s, r.script.shortcut},
                           {u"runOnWorktreeCreate"_s, r.script.onWorktree},
                           {u"wait"_s, r.script.wait}};
    }
    m_projects->setScripts(m_project, out);
    reload();
}

} // namespace brulion::shell
