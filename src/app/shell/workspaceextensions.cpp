#include "workspaceextensions.h"

#include "extensionviewmodel.h"

#include "extensions/builtin.h"
#include "extensions/notebookhost.h"
#include "index/vaultindex.h"
#include "log/categories.h"
#include "util/paths.h"

#include <cmath>

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace brulion::shell {

using logging::lcApp;

// ---- ExtensionFormModel ------------------------------------------------------------------------

int ExtensionFormModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(m_fields.size()); }

QVariant ExtensionFormModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_fields.size())
        return {};
    const Field &f = m_fields[index.row()];
    switch (role) {
    case KeyRole: return f.key;
    case LabelRole: return f.label;
    case KindRole: return f.kind;
    case ValueRole: return f.value;
    case StepRole: return f.step;
    case UnitRole: return f.unit;
    case TextRole: return f.text;
    case DisplayRole: return formatExtensionNumber(f.value, m_comma);
    }
    return {};
}

QHash<int, QByteArray> ExtensionFormModel::roleNames() const
{
    return {{KeyRole, "fieldKey"}, {LabelRole, "fieldLabel"}, {KindRole, "fieldKind"}, {ValueRole, "fieldValue"},
            {StepRole, "fieldStep"}, {UnitRole, "fieldUnit"}, {TextRole, "fieldText"}, {DisplayRole, "fieldDisplay"}};
}

void ExtensionFormModel::reset(const QVariantList &fields)
{
    beginResetModel();
    m_fields.clear();
    for (const QVariant &v : fields) {
        const QVariantMap map = v.toMap();
        Field f;
        f.key = map.value(u"key"_s).toString();
        f.label = map.value(u"label"_s).toString();
        f.kind = map.value(u"kind"_s, u"number"_s).toString();
        f.unit = map.value(u"unit"_s).toString();
        f.value = map.value(u"value"_s).toDouble();
        f.step = map.value(u"step"_s, 1).toDouble();
        if (!std::isfinite(f.step) || f.step <= 0)
            f.step = 1;
        f.text = f.kind == u"number" ? QString() : map.value(u"value"_s).toString();
        if (!f.key.isEmpty())
            m_fields << f;
    }
    endResetModel();
}

QVariantMap ExtensionFormModel::values() const
{
    QVariantMap out;
    for (const Field &f : m_fields)
        out.insert(f.key, f.kind == u"number" ? QVariant(f.value) : QVariant(f.text));
    return out;
}

void ExtensionFormModel::setNumber(int row, double value)
{
    if (row < 0 || row >= m_fields.size() || !std::isfinite(value))
        return;
    m_fields[row].value = std::max(0.0, value);
    Q_EMIT dataChanged(index(row), index(row), {ValueRole, DisplayRole});
}

void ExtensionFormModel::setText(int row, const QString &text)
{
    if (row < 0 || row >= m_fields.size())
        return;
    m_fields[row].text = text;
    Q_EMIT dataChanged(index(row), index(row), {TextRole});
}

void ExtensionFormModel::setNumberText(int row, const QString &text)
{
    QString t = text.trimmed();
    t.replace(u',', u'.');
    t.remove(u' ');
    bool ok = false;
    const double value = t.toDouble(&ok);
    if (ok)
        setNumber(row, value);
}

void ExtensionFormModel::step(int row, int direction)
{
    if (row < 0 || row >= m_fields.size())
        return;
    const Field &f = m_fields[row];
    // Back onto the step's grid first ("82,5" +2,5 → 85), then one step.
    const double base = std::round(f.value / f.step) * f.step;
    setNumber(row, base + (direction < 0 ? -f.step : f.step));
}

// ---- WorkspaceExtensions -----------------------------------------------------------------------

WorkspaceExtensions::WorkspaceExtensions(Clock clock, QObject *parent)
    : QObject(parent), m_clock(std::move(clock)), m_fields(new ExtensionFormModel(this))
{
    m_fields->setComma(true);
    m_restTimer.setInterval(250ms);
    connect(&m_restTimer, &QTimer::timeout, this, &WorkspaceExtensions::tickRest);
}

WorkspaceExtensions::~WorkspaceExtensions() = default;

void WorkspaceExtensions::attach(Vault *vault, VaultIndex *index)
{
    m_vault = vault;
    m_index = index;
    // Views show the notebook: a note changed anywhere (the editor, a sync) draws them again.
    connect(m_index, &VaultIndex::changed, this, &WorkspaceExtensions::changed);
    reload();
}

void WorkspaceExtensions::detach()
{
    if (m_index)
        disconnect(m_index, nullptr, this, nullptr);
    cancelQuestion();
    m_engine.reset();
    m_host.reset();
    m_vault = nullptr;
    m_index = nullptr;
    Q_EMIT commandsChanged();
}

void WorkspaceExtensions::setLanguage(const QString &language)
{
    const QString lang = language == u"en" ? u"en"_s : u"pl"_s;
    if (lang == m_language)
        return;
    m_language = lang;
    m_fields->setComma(lang == u"pl");
    reload(); // the extensions pick their words when they load
}

void WorkspaceExtensions::setEnabled(const QStringList &ids)
{
    if (ids == m_enabled)
        return;
    m_enabled = ids;
    Q_EMIT enabledChanged();
    reload();
}

void WorkspaceExtensions::setNotifier(std::function<void(const QString &, const QString &)> notify)
{
    m_notify = std::move(notify);
}

void WorkspaceExtensions::reload()
{
    cancelQuestion();
    m_engine.reset();
    m_host.reset();
    if (m_vault && m_index && !m_enabled.isEmpty()) {
        extensions::NotebookHost::Options options;
        options.now = m_clock;
        options.language = m_language;
        options.uiLanguage = m_language;
        m_host = std::make_unique<extensions::NotebookHost>(*m_vault, *m_index, options);
        m_host->onTimer = [this](const QString &, int seconds, const QString &label) { startRest(seconds, label); };
        m_host->onLog = [](const QString &extension, const QString &text) {
            qCInfo(lcApp, "extension %s: %s", qUtf8Printable(extension), qUtf8Printable(text));
        };
        m_engine = std::make_unique<extensions::Engine>();
        QString error;
        if (!m_engine->start(m_host.get(), extensions::Limits{}, &error)) {
            qCWarning(lcApp, "extensions: %s", qUtf8Printable(error));
            m_engine.reset();
        } else {
            for (const extensions::BuiltinExtension &builtin : extensions::builtinExtensions()) {
                if (!m_enabled.contains(builtin.extension.id))
                    continue;
                const extensions::Result r = m_engine->load(builtin.extension);
                if (!r.ok())
                    qCWarning(lcApp, "extension %s: %s", qUtf8Printable(builtin.extension.id), qUtf8Printable(r.error));
            }
        }
    }
    Q_EMIT commandsChanged();
    Q_EMIT changed();
}

QList<extensions::CommandInfo> WorkspaceExtensions::commands() const
{
    return m_engine ? m_engine->commands() : QList<extensions::CommandInfo>();
}

QString WorkspaceExtensions::title(const QString &extension) const
{
    for (const extensions::BuiltinExtension &builtin : extensions::builtinExtensions())
        if (builtin.extension.id == extension)
            return m_language == u"en" ? builtin.titleEn : builtin.titlePl;
    return extension;
}

bool WorkspaceExtensions::isWaiting() const { return !m_questionKind.isEmpty(); }

void WorkspaceExtensions::runCommand(const QString &extension, const QString &name, const QString &path, int line)
{
    if (!m_engine)
        return;
    QVariantMap ctx{{u"page"_s, path.isEmpty() ? QString() : paths::pageTitle(path)}, {u"selection"_s, QString()}};
    if (!path.isEmpty() && line >= 0) {
        const QVariantMap block = m_host->blockAt(path, line);
        if (!block.isEmpty())
            ctx.insert(u"block"_s, block);
    }
    cancelQuestion();
    handle(m_engine->runCommand(extension, name, ctx), false);
}

void WorkspaceExtensions::runBlockCommand(const QString &command, const QString &path, int line)
{
    const qsizetype slash = command.indexOf(u'/');
    if (slash > 0)
        runCommand(command.left(slash), command.mid(slash + 1), path, line);
}

QVariantMap WorkspaceExtensions::render(const QString &extension, const QString &view, const QVariantMap &params, QString *error)
{
    if (!m_engine || isWaiting())
        return {};
    const extensions::Result r = m_engine->render(extension, view, params);
    if (r.status == extensions::Status::Suspended) {
        m_engine->cancel(); // a view never asks
        return {};
    }
    if (!r.ok()) {
        qCWarning(lcApp, "extension view %s/%s: %s", qUtf8Printable(extension), qUtf8Printable(view), qUtf8Printable(r.error));
        if (error)
            *error = r.error.section(u'\n', 0, 0);
        return {};
    }
    return r.value.toMap();
}

bool WorkspaceExtensions::act(const QString &extension, const QString &view, const QString &action, const QVariantMap &args)
{
    if (!m_engine)
        return false;
    cancelQuestion();
    const extensions::Result r = m_engine->act(extension, view, action, args);
    handle(r, true);
    return r.ok() || r.status == extensions::Status::Suspended;
}

void WorkspaceExtensions::handle(const extensions::Result &result, bool fromAction)
{
    switch (result.status) {
    case extensions::Status::Suspended:
        setQuestion(result.value.toMap());
        return;
    case extensions::Status::Ok:
        if (result.value.metaType().id() == QMetaType::QString && !result.value.toString().isEmpty())
            Q_EMIT message(result.value.toString(), u"info"_s);
        break;
    case extensions::Status::NotFound:
        break;
    default: {
        qCWarning(lcApp, "extension: %s", qUtf8Printable(result.error));
        // A line changed elsewhere (a sync, another pane) since the view showed it: the view shows
        // it again, nothing was written.
        if (fromAction && result.error.contains(u"stale:"_s))
            Q_EMIT message(tr("The line changed in the meantime. The view shows it again; nothing was written."), u"info"_s);
        else if (result.status == extensions::Status::Timeout)
            Q_EMIT message(tr("The extension took too long and was stopped."), u"error"_s);
        else if (result.status == extensions::Status::OutOfMemory)
            Q_EMIT message(tr("The extension needed too much memory and was stopped."), u"error"_s);
        else
            Q_EMIT message(tr("The extension failed: %1").arg(result.error.section(u'\n', 0, 0)), u"error"_s);
    }
    }
    Q_EMIT changed();
}

void WorkspaceExtensions::setQuestion(const QVariantMap &question)
{
    m_questionKind = question.value(u"kind"_s).toString();
    m_questionTitle = question.value(m_questionKind == u"form" ? u"title"_s : u"prompt"_s).toString();
    m_questionOptions = question.value(u"options"_s).toStringList();
    m_fields->reset(question.value(u"fields"_s).toList());
    ++m_questionSerial;
    Q_EMIT questionChanged();
}

void WorkspaceExtensions::answer(const QVariant &value)
{
    if (!m_engine || m_questionKind.isEmpty())
        return;
    m_questionKind.clear();
    m_questionTitle.clear();
    m_questionOptions.clear();
    Q_EMIT questionChanged();
    handle(m_engine->resume(value), false);
}

void WorkspaceExtensions::answerText(const QString &text) { answer(text); }

void WorkspaceExtensions::answerChoice(const QString &option)
{
    if (m_questionKind == u"choose" && !m_questionOptions.contains(option))
        return;
    answer(option);
}

void WorkspaceExtensions::answerForm() { answer(m_fields->values()); }

void WorkspaceExtensions::cancelQuestion()
{
    if (m_questionKind.isEmpty())
        return;
    const QString kind = m_questionKind;
    // ask gets "" (nothing typed), choose and form nil: the extension ends its call itself
    answer(kind == u"ask" ? QVariant(QString()) : QVariant());
}

void WorkspaceExtensions::startRest(int seconds, const QString &label)
{
    m_restTotal = std::max(1, seconds);
    m_restRemaining = m_restTotal;
    m_restLabel = label.isEmpty() ? tr("Rest") : label;
    m_restEnd = QDateTime::currentDateTimeUtc().addSecs(m_restTotal);
    m_restTimer.start();
    Q_EMIT restChanged();
}

QString WorkspaceExtensions::restText() const
{
    return u"%1:%2"_s.arg(m_restRemaining / 60).arg(m_restRemaining % 60, 2, 10, QChar(u'0'));
}

void WorkspaceExtensions::stopRest()
{
    if (!m_restRemaining && !m_restTimer.isActive())
        return;
    m_restTimer.stop();
    m_restRemaining = 0;
    Q_EMIT restChanged();
}

void WorkspaceExtensions::tickRest()
{
    const qint64 left = (QDateTime::currentDateTimeUtc().msecsTo(m_restEnd) + 999) / 1000;
    if (left > 0) {
        if (left != m_restRemaining) {
            m_restRemaining = int(left);
            Q_EMIT restChanged();
        }
        return;
    }
    m_restTimer.stop();
    m_restRemaining = 0;
    Q_EMIT restChanged();
    if (m_notify)
        m_notify(tr("%1 is over").arg(m_restLabel), tr("Time for the next set."));
    Q_EMIT message(tr("%1 is over: time for the next set.").arg(m_restLabel), u"info"_s);
}

} // namespace brulion::shell
