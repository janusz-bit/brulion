#include "shortcutmap.h"

#include "appsettings.h"

#include "text/text.h"

#include <QKeyCombination>
#include <QKeySequence>

#include <optional>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

constexpr Qt::KeyboardModifiers CommandModifiers = Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier;

std::optional<QKeyCombination> combinationOf(const QString &normalized)
{
    const QKeySequence seq = QKeySequence::fromString(normalized, QKeySequence::PortableText);
    if (seq.count() != 1)
        return std::nullopt;
    return seq[0];
}

bool isModifierKey(int key)
{
    return key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_Alt || key == Qt::Key_Meta
           || key == Qt::Key_AltGr || key == Qt::Key_Super_L || key == Qt::Key_Super_R || key == Qt::Key_Hyper_L
           || key == Qt::Key_Hyper_R || key == Qt::Key_CapsLock || key == Qt::Key_NumLock;
}

/// The keys used where commands are not (normalized -> area), built once.
const QHash<QString, QString> &reservedKeys()
{
    static const QHash<QString, QString> keys = [] {
        QHash<QString, QString> out;
        const auto add = [&out](const QString &area, std::initializer_list<const char *> sequences) {
            for (const char *s : sequences) {
                const QString n = ShortcutMap::normalize(QString::fromLatin1(s));
                Q_ASSERT(!n.isEmpty());
                out.insert(n, area);
            }
        };
        // Editing text: the clipboard, undo, moving and selecting by words, Ctrl+Enter (dialogs
        // accept with it too), Alt+Enter (follow a link).
        add(u"text"_s, {"Ctrl+A",         "Ctrl+C",          "Ctrl+V",          "Ctrl+X",           "Ctrl+Z",
                        "Ctrl+Y",         "Ctrl+Shift+Z",    "Ctrl+Ins",        "Shift+Ins",        "Shift+Del",
                        "Ctrl+Backspace", "Ctrl+Del",        "Ctrl+Left",       "Ctrl+Right",       "Ctrl+Up",
                        "Ctrl+Down",      "Ctrl+Home",       "Ctrl+End",        "Ctrl+Shift+Left",  "Ctrl+Shift+Right",
                        "Ctrl+Shift+Up",  "Ctrl+Shift+Down", "Ctrl+Shift+Home", "Ctrl+Shift+End",   "Ctrl+Return",
                        "Ctrl+Enter",     "Alt+Return",      "Alt+Enter",       "Alt+Shift+Return", "Alt+Shift+Enter",
                        "Ctrl+Tab",       "Ctrl+Shift+Tab"});
        // Blocks: fold, the task status menu, moving blocks.
        add(u"blocks"_s, {"Ctrl+.", "Ctrl+Shift+Return", "Ctrl+Shift+Enter", "Alt+Up", "Alt+Down"});
        // The drawing editor (docs/UI.md "Drawing editor").
        add(u"drawing"_s, {"Ctrl+D", "Ctrl+]", "Ctrl+[", "Ctrl+Shift+]", "Ctrl+Shift+[", "Ctrl+Shift+}", "Ctrl+Shift+{",
                           "Alt+H", "Alt+V", "Ctrl+0", "Ctrl++", "Ctrl+-", "Ctrl+=", "Ctrl+Shift+="});
        // The PDF viewer.
        add(u"pdf"_s, {"Ctrl+G", "F3", "Shift+F3"});
        // Menus: the menu key's and the context menu's.
        add(u"menus"_s, {"F10", "Shift+F10"});
        return out;
    }();
    return keys;
}

/// Ctrl+Alt with these letters is AltGr on Windows: ą ć ę ł ń ó ś ź ż on the Polish keyboard.
bool isAltGrLetter(const QKeyCombination &c)
{
    if ((c.keyboardModifiers() & (Qt::ControlModifier | Qt::AltModifier)) != (Qt::ControlModifier | Qt::AltModifier))
        return false;
    switch (c.key()) {
    case Qt::Key_A:
    case Qt::Key_C:
    case Qt::Key_E:
    case Qt::Key_L:
    case Qt::Key_N:
    case Qt::Key_O:
    case Qt::Key_S:
    case Qt::Key_X:
    case Qt::Key_Z: return true;
    default: return false;
    }
}

QString areaName(const QString &area)
{
    if (area == u"text")
        return ShortcutMap::tr("editing text");
    if (area == u"blocks")
        return ShortcutMap::tr("blocks in the editor");
    if (area == u"drawing")
        return ShortcutMap::tr("drawings");
    if (area == u"pdf")
        return ShortcutMap::tr("the PDF viewer");
    return ShortcutMap::tr("menus");
}

} // namespace

ShortcutMap::ShortcutMap(AppSettings *settings, QObject *parent) : QObject(parent), m_settings(settings) { }

ShortcutMap::~ShortcutMap() = default;

QString ShortcutMap::normalize(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return {};
    const auto combination = combinationOf(trimmed);
    if (!combination)
        return {};
    const int key = combination->key();
    if (key == 0 || key == Qt::Key_unknown || isModifierKey(key))
        return {};
    const QKeyCombination clean(combination->keyboardModifiers() & ~Qt::KeypadModifier, combination->key());
    const QString out = QKeySequence(clean).toString(QKeySequence::PortableText);
    // Something Qt reads but cannot write back the same way is no shortcut people can type.
    const auto again = combinationOf(out);
    return again && *again == clean ? out : QString();
}

QString ShortcutMap::reservedUse(const QString &normalized)
{
    const auto combination = combinationOf(normalized);
    if (!combination)
        return {};
    if (isAltGrLetter(*combination))
        return u"altgr"_s;
    return reservedKeys().value(normalized);
}

bool ShortcutMap::typesText(const QString &normalized)
{
    const auto combination = combinationOf(normalized);
    if (!combination)
        return true;
    if (combination->keyboardModifiers() & CommandModifiers)
        return false;
    const int key = combination->key();
    return key < Qt::Key_F1 || key > Qt::Key_F35;
}

void ShortcutMap::setActions(const QList<Action> &actions)
{
    m_actions = actions;
    load();
}

const ShortcutMap::Action *ShortcutMap::find(const QString &id) const
{
    for (const Action &a : m_actions) {
        if (a.id == id)
            return &a;
    }
    return nullptr;
}

void ShortcutMap::load()
{
    m_custom.clear();
    const QVariantMap stored = m_settings ? m_settings->shortcutOverrides() : QVariantMap();
    for (const Action &a : std::as_const(m_actions)) {
        const auto it = stored.constFind(a.id);
        // (A value with a comma comes back from the INI file as a list: never a shortcut.)
        if (it == stored.cend() || it->typeId() != QMetaType::QString)
            continue;
        const QString text = it->toString().trimmed();
        if (text.isEmpty()) {
            if (!a.defaultSequence.isEmpty())
                m_custom.insert(a.id, QString());
            continue;
        }
        const QString n = normalize(text);
        if (n.isEmpty() || n == a.defaultSequence || typesText(n) || !reservedUse(n).isEmpty())
            continue;
        m_custom.insert(a.id, n);
    }
    rebuild();
}

void ShortcutMap::rebuild()
{
    m_effective.clear();
    m_owner.clear();
    // Custom shortcuts first, in the actions' order: one already taken goes (its default may
    // still apply below).
    QStringList dropped;
    for (const Action &a : std::as_const(m_actions)) {
        const auto it = m_custom.constFind(a.id);
        if (it == m_custom.cend() || it->isEmpty())
            continue;
        if (m_owner.contains(*it)) {
            dropped.append(a.id);
            continue;
        }
        m_owner.insert(*it, a.id);
        m_effective.insert(a.id, *it);
    }
    for (const QString &id : std::as_const(dropped))
        m_custom.remove(id);
    for (const Action &a : std::as_const(m_actions)) {
        if (m_custom.contains(a.id) || a.defaultSequence.isEmpty() || m_owner.contains(a.defaultSequence))
            continue;
        m_owner.insert(a.defaultSequence, a.id);
        m_effective.insert(a.id, a.defaultSequence);
    }
    Q_EMIT changed();
}

void ShortcutMap::store(const QString &id)
{
    if (!m_settings)
        return;
    const auto it = m_custom.constFind(id);
    m_settings->setShortcutOverride(id, it == m_custom.cend() ? std::nullopt : std::optional<QString>(*it));
}

QVariantMap ShortcutMap::sequences() const
{
    QVariantMap out;
    for (auto it = m_effective.cbegin(); it != m_effective.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QVariantList ShortcutMap::bindings() const
{
    QVariantList out;
    for (const Action &a : m_actions) {
        const QString s = m_effective.value(a.id);
        if (!s.isEmpty())
            out.append(QVariantMap{{u"id"_s, a.id}, {u"sequence"_s, s}});
    }
    return out;
}

QString ShortcutMap::sequence(const QString &id) const
{
    return m_effective.value(id);
}

QString ShortcutMap::defaultSequence(const QString &id) const
{
    const Action *a = find(id);
    return a ? a->defaultSequence : QString();
}

bool ShortcutMap::isCustom(const QString &id) const
{
    return m_custom.contains(id);
}

QString ShortcutMap::actionFor(const QString &sequence) const
{
    return m_owner.value(normalize(sequence));
}

QString ShortcutMap::title(const QString &id) const
{
    const Action *a = find(id);
    return a ? a->title : QString();
}

QVariantMap ShortcutMap::check(const QString &id, const QString &sequence) const
{
    const QString n = normalize(sequence);
    QVariantMap out{{u"sequence"_s, n}, {u"conflictId"_s, QString()}, {u"conflictTitle"_s, QString()}};
    const auto result = [&out](const char *status, const QString &message) {
        out.insert(u"status"_s, QString::fromLatin1(status));
        out.insert(u"message"_s, message);
        return out;
    };
    if (!find(id))
        return result("unknown", tr("There is no such command."));
    const QString shown = n.isEmpty() ? sequence.trimmed() : n;
    if (n.isEmpty())
        return result(
                "invalid",
                tr("“%1” is not a key combination Brulion can use: press one key with Ctrl, Alt or a function key.")
                        .arg(shown));
    if (typesText(n))
        return result(
                "typing",
                tr("%1 types or moves the caret in text. Add Ctrl or Alt, or use a function key (F1–F12).").arg(n));
    const QString reserved = reservedUse(n);
    if (reserved == u"altgr")
        return result("reserved", tr("%1 types a Polish letter on Windows (AltGr). Choose another key.").arg(n));
    if (!reserved.isEmpty())
        return result("reserved",
                      tr("%1 is used for %2 and cannot be a command's shortcut.").arg(n, areaName(reserved)));
    if (sequence == this->sequence(id) || n == this->sequence(id))
        return result("same", QString());
    const QString owner = m_owner.value(n);
    if (!owner.isEmpty() && owner != id) {
        out.insert(u"conflictId"_s, owner);
        out.insert(u"conflictTitle"_s, title(owner));
        return result("conflict", tr("%1 is already the shortcut of “%2”.").arg(n, title(owner)));
    }
    return result("ok", QString());
}

bool ShortcutMap::assign(const QString &id, const QString &sequence, bool takeOver)
{
    const QVariantMap verdict = check(id, sequence);
    const QString status = verdict.value(u"status"_s).toString();
    if (status == u"same")
        return true;
    if (status != u"ok" && !(status == u"conflict" && takeOver))
        return false;
    const QString n = verdict.value(u"sequence"_s).toString();
    if (status == u"conflict")
        setNone(verdict.value(u"conflictId"_s).toString());
    if (n == defaultSequence(id))
        m_custom.remove(id);
    else
        m_custom.insert(id, n);
    store(id);
    rebuild();
    return true;
}

void ShortcutMap::clear(const QString &id)
{
    if (!find(id) || sequence(id).isEmpty())
        return;
    setNone(id);
    rebuild();
}

void ShortcutMap::setNone(const QString &id)
{
    // No shortcut is the default of an action without one: nothing custom about it.
    if (defaultSequence(id).isEmpty())
        m_custom.remove(id);
    else
        m_custom.insert(id, QString());
    store(id);
}

QString ShortcutMap::reset(const QString &id)
{
    const Action *a = find(id);
    if (!a || !m_custom.contains(id))
        return {};
    QString loser;
    if (!a->defaultSequence.isEmpty()) {
        loser = m_owner.value(a->defaultSequence);
        if (loser == id)
            loser.clear();
        if (!loser.isEmpty())
            setNone(loser);
    }
    m_custom.remove(id);
    store(id);
    rebuild();
    return loser;
}

void ShortcutMap::resetAll()
{
    m_custom.clear();
    if (m_settings)
        m_settings->clearShortcutOverrides();
    rebuild();
}

QVariantList ShortcutMap::entries(const QString &query) const
{
    const QString q = text::foldKey(query.trimmed());
    QVariantList out;
    for (const Action &a : m_actions) {
        const QString s = m_effective.value(a.id);
        if (!q.isEmpty() && !text::foldKey(a.title).contains(q) && !a.id.contains(q, Qt::CaseInsensitive)
            && !s.contains(q, Qt::CaseInsensitive))
            continue;
        out.append(QVariantMap{{u"id"_s, a.id},
                               {u"title"_s, a.title},
                               {u"sequence"_s, s},
                               {u"defaultSequence"_s, a.defaultSequence},
                               {u"custom"_s, m_custom.contains(a.id)}});
    }
    return out;
}

QString ShortcutMap::sequenceForKey(int key, int modifiers) const
{
    if (key == 0 || key == Qt::Key_unknown || isModifierKey(key))
        return {};
    Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers)
                                 & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    if (key == Qt::Key_Backtab) {
        key = Qt::Key_Tab;
        mods |= Qt::ShiftModifier;
    }
    return normalize(QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText));
}

} // namespace brulion::shell
