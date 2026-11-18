#include "index/resolve_p.h"

#include "index/textutil_p.h"
#include "util/paths.h"

#include <QVarLengthArray>

#include <algorithm>

namespace brulion::index_detail {

using namespace Qt::StringLiterals;

namespace {

bool isMarkdownExtension(QStringView ext)
{
    return ext == u".md" || ext == u".markdown";
}

/// Does the lower-cased path `key` end with `.md` / `.markdown`?
bool hasMarkdownSuffix(QStringView key)
{
    return key.endsWith(u".md") || key.endsWith(u".markdown");
}

/// Does `path` equal `suffix` or end with "/" + `suffix`?
bool endsWithSegments(const QString &path, const QString &suffix)
{
    return path == suffix
           || (path.size() > suffix.size() && path.endsWith(suffix) && path[path.size() - suffix.size() - 1] == u'/');
}

/// Shortest path, then smallest by code units (pickBest()).
bool betterPath(const VaultPath &a, const VaultPath &b)
{
    return a.size() < b.size() || (a.size() == b.size() && a < b);
}

/// pathKey() without `.md`: what a note's name key and the folders of a target are compared with.
QString notePathKey(const VaultPath &path)
{
    return paths::stripMarkdownExtension(pathKey(path));
}

/// pickBest() of `set` (the files filed under the base name of `name`, nullptr when none); for a
/// `name` with folders only among the files whose `keyOf(path)` ends with its segments.
template<class KeyOf>
VaultPath pickBySuffix(const PathSet *set, const QString &name, KeyOf keyOf)
{
    if (!set)
        return {};
    if (!name.contains(u'/'))
        return pickBest(*set);
    const VaultPath *best = nullptr;
    set->forEach([&](const VaultPath &p) {
        if ((!best || betterPath(p, *best)) && endsWithSegments(keyOf(p), name))
            best = &p;
    });
    return best ? *best : VaultPath();
}

} // namespace

QString pathKey(const QString &path)
{
    return lower(nfc(path));
}

QString noteNameKey(const VaultPath &path)
{
    return lower(paths::basename(paths::stripMarkdownExtension(nfc(path))));
}

QString fileNameKey(const VaultPath &path)
{
    return lower(paths::basename(nfc(path)));
}

QString aliasKey(const QString &alias)
{
    return lower(nfc(alias).trimmed());
}

QString cleanTarget(const QString &target, TargetForm form)
{
    QString t = target.trimmed();
    if (form == TargetForm::Raw) {
        if (t.startsWith(u"[[") && t.endsWith(u"]]") && t.size() >= 4)
            t = t.sliced(2, t.size() - 4);
        const qsizetype pipe = t.indexOf(u'|');
        if (pipe >= 0) {
            t.truncate(pipe);
            if (t.endsWith(u'\\'))
                t.chop(1);
        }
        const qsizetype hash = t.indexOf(u'#');
        if (hash >= 0)
            t.truncate(hash);
        t = t.trimmed();
    }
    t.replace(u'\\', u'/');
    return t;
}

QString linkKey(const QString &target, TargetForm form)
{
    return lower(paths::basename(paths::stripMarkdownExtension(paths::normalize(cleanTarget(target, form)))));
}

QStringList aliasLinkKeys(const QString &alias)
{
    const QString raw = linkKey(alias, TargetForm::Raw);
    const QString parsed = linkKey(alias, TargetForm::Parsed);
    return raw == parsed ? QStringList{raw} : QStringList{raw, parsed};
}

bool isAttachmentExtension(QStringView ext)
{
    return paths::isAttachmentExtension(ext);
}

bool isExternalTarget(QStringView target)
{
    // ^[A-Za-z][A-Za-z0-9+.-]*:   but not a Windows drive (C:\ or C:/).
    const auto isSchemeChar = [](QChar c) {
        return c.unicode() < 0x80 && (c.isLetterOrNumber() || c == u'+' || c == u'.' || c == u'-');
    };
    if (target.isEmpty() || !(target[0].isLetter() && target[0].unicode() < 0x80))
        return false;
    qsizetype i = 1;
    while (i < target.size() && isSchemeChar(target[i]))
        ++i;
    if (i >= target.size() || target[i] != u':')
        return false;
    const bool drive = i == 1 && i + 1 < target.size() && (target[i + 1] == u'/' || target[i + 1] == u'\\');
    return !drive;
}

bool isPageTarget(const QString &target, TargetForm form)
{
    return !isAttachmentExtension(paths::extension(cleanTarget(target, form)));
}

VaultPath pickBest(const PathSet &paths)
{
    return paths.min(betterPath);
}

// ---------------------------------------------------------------------------------------------
// NameTable
// ---------------------------------------------------------------------------------------------

NameTable::NoteNames NameTable::NoteNames::of(const VaultPath &path, const QStringList &aliases)
{
    NoteNames out{index_detail::pathKey(path), index_detail::noteNameKey(path), {}};
    for (const QString &a : aliases) {
        if (QString k = index_detail::aliasKey(a); !k.isEmpty())
            out.aliasKeys.append(std::move(k));
    }
    return out;
}

void NameTable::addNote(const VaultPath &path, const QStringList &aliases)
{
    addNote(path, NoteNames::of(path, aliases));
}

void NameTable::addNote(const VaultPath &path, const NoteNames &names)
{
    add(&NameTable::m_byPath, names.pathKey, path);
    add(&NameTable::m_noteNames, names.nameKey, path);
    for (const QString &k : names.aliasKeys)
        add(&NameTable::m_aliases, k, path);
}

void NameTable::removeNote(const VaultPath &path, const QStringList &aliases)
{
    remove(&NameTable::m_byPath, pathKey(path), path);
    remove(&NameTable::m_noteNames, noteNameKey(path), path);
    for (const QString &a : aliases)
        remove(&NameTable::m_aliases, aliasKey(a), path);
}

void NameTable::addAttachment(const VaultPath &path)
{
    add(&NameTable::m_byPath, pathKey(path), path);
    add(&NameTable::m_fileNames, fileNameKey(path), path);
}

void NameTable::removeAttachment(const VaultPath &path)
{
    remove(&NameTable::m_byPath, pathKey(path), path);
    remove(&NameTable::m_fileNames, fileNameKey(path), path);
}

void NameTable::clear()
{
    m_byPath.clear();
    m_noteNames.clear();
    m_fileNames.clear();
    m_aliases.clear();
    m_base = nullptr;
}

QStringList NameTable::describe() const
{
    QStringList out;
    const std::pair<const char *, const Map *> maps[] = {
            {"path", &m_byPath}, {"note", &m_noteNames}, {"file", &m_fileNames}, {"alias", &m_aliases}};
    for (const auto &[name, map] : maps) {
        for (auto it = map->cbegin(); it != map->cend(); ++it)
            out.append(QLatin1StringView(name) + u' ' + it.key() + u" -> "_s + it->sorted().join(u'|'));
    }
    if (m_base)
        out.append(u"overlay"_s);
    out.sort();
    return out;
}

void NameTable::overlay(const NameTable *base)
{
    clear();
    m_base = base;
}

const PathSet *NameTable::find(Map NameTable::*field, QStringView key) const
{
    const Map &map = this->*field;
    const auto it = map.constFind(key);
    if (it != map.constEnd())
        return it->isEmpty() ? nullptr : &*it;
    return m_base ? m_base->find(field, key) : nullptr;
}

bool NameTable::hasAny(Map NameTable::*field) const
{
    return !(this->*field).isEmpty() || (m_base && m_base->hasAny(field));
}

void NameTable::add(Map NameTable::*field, const QString &key, const VaultPath &path)
{
    Map &map = this->*field;
    auto it = map.find(key);
    if (it == map.end()) {
        // An overlay starts from the base's files under the key.
        const PathSet *base = m_base ? m_base->find(field, key) : nullptr;
        it = map.insert(key, base ? *base : PathSet());
    }
    it->insert(path);
}

void NameTable::remove(Map NameTable::*field, const QString &key, const VaultPath &path)
{
    Map &map = this->*field;
    auto it = map.find(key);
    if (!m_base) {
        if (it == map.end())
            return;
        it->remove(path);
        if (it->isEmpty())
            map.erase(it);
        return;
    }
    // An overlay keeps an emptied entry: it hides the base's files under the key.
    if (it == map.end()) {
        const PathSet *base = m_base->find(field, key);
        if (!base || !base->contains(path))
            return;
        it = map.insert(key, *base);
    }
    it->remove(path);
}

VaultPath NameTable::resolve(const QString &target, const VaultPath &from, TargetForm form) const
{
    return resolve(ResolvePlan(target, from, form));
}

VaultPath NameTable::resolve(const ResolvePlan &plan) const
{
    using Kind = ResolvePlan::Kind;
    switch (plan.m_kind) {
    case Kind::Nothing: return {};
    case Kind::From: return plan.m_from;
    case Kind::Bare:
    case Kind::Path: break;
    }
    // Bare: 1) the file at the vault root, 2) the file in the linking file's folder. Path: the
    // explicitly relative path, the exact path, the path under the linking file's folder.
    for (int i = 0; i < plan.probeCount(); ++i) {
        if (const VaultPath hit = lookupPath(plan.probe(i), plan.m_attachmentFirst); !hit.isEmpty())
            return hit;
    }
    // 3) base name (shortest path, then alphabetical), 4) aliases.
    const VaultPath byName = lookupName(plan.name());
    return byName.isEmpty() ? lookupAlias(plan) : byName;
}

VaultPath NameTable::resolveNote(const QString &target, TargetForm form) const
{
    const QString t = cleanTarget(target, form);
    const QString spelled = nfc(paths::normalize(t));
    if (spelled.isEmpty())
        return {};
    const QString key = lower(spelled);
    const bool md = hasMarkdownSuffix(key);
    // 1) the note at exactly that vault path.
    if (const VaultPath hit = md ? lookupExact(key, spelled) : lookupNote(key, spelled); !hit.isEmpty())
        return hit;
    // 2) base name, for a target with folders a path suffix (shortest path, then alphabetical).
    const QString stem = md ? paths::stripMarkdownExtension(key) : key;
    const VaultPath byName = pickBySuffix(find(&NameTable::m_noteNames, paths::basename(stem)), stem, notePathKey);
    // 3) aliases.
    return byName.isEmpty() ? lookupAlias(t) : byName;
}

VaultPath NameTable::notePath(const QString &path) const
{
    const QString spelled = nfc(path);
    return spelled.isEmpty() ? VaultPath() : lookupNote(lower(spelled), spelled);
}

VaultPath NameTable::lookupPath(const ResolvePlan::Probe &probe, bool attachmentFirst) const
{
    if (probe.markdown)
        return lookupExact(probe.key, probe.spelled);
    if (attachmentFirst) {
        const VaultPath file = lookupExact(probe.key, probe.spelled);
        return file.isEmpty() ? lookupNote(probe.key, probe.spelled) : file;
    }
    const VaultPath note = lookupNote(probe.key, probe.spelled);
    return note.isEmpty() ? lookupExact(probe.key, probe.spelled) : note;
}

VaultPath NameTable::lookupExact(QStringView key, QStringView spelled, QLatin1StringView suffix) const
{
    // `key` + `suffix` without a heap allocation for the usual lengths (the map is searched by
    // a view: QHash's heterogeneous lookup).
    QVarLengthArray<QChar, 256> joined;
    QStringView full = key;
    if (!suffix.isEmpty()) {
        joined.append(key.data(), key.size());
        for (const char c : suffix)
            joined.append(QLatin1Char(c));
        full = QStringView(joined.constData(), joined.size());
    }
    const PathSet *set = find(&NameTable::m_byPath, full);
    if (!set)
        return {};
    // nfc(a) == spelled + suffix
    const auto spelledAs = [&](const VaultPath &a) {
        const QString n = nfc(a);
        return n.size() == spelled.size() + suffix.size() && QStringView(n).startsWith(spelled)
               && QStringView(n).sliced(spelled.size()) == suffix;
    };
    return set->min([&](const VaultPath &a, const VaultPath &b) {
        const bool aExact = spelledAs(a);
        return aExact != spelledAs(b) ? aExact : betterPath(a, b);
    });
}

VaultPath NameTable::lookupNote(QStringView key, QStringView spelled) const
{
    const VaultPath md = lookupExact(key, spelled, QLatin1StringView(".md"));
    return md.isEmpty() ? lookupExact(key, spelled, QLatin1StringView(".markdown")) : md;
}

VaultPath NameTable::lookupName(const ResolvePlan::Name &name) const
{
    const auto notes = [&] {
        return pickBySuffix(find(&NameTable::m_noteNames, name.noteBase), name.stem, notePathKey);
    };
    const auto files = [&]() -> VaultPath {
        if (name.markdown || !hasAny(&NameTable::m_fileNames))
            return {};
        return pickBySuffix(find(&NameTable::m_fileNames, name.fileBase), name.path, pathKey);
    };
    if (name.attachment) {
        const VaultPath f = files();
        return f.isEmpty() ? notes() : f;
    }
    const VaultPath n = notes();
    return n.isEmpty() ? files() : n;
}

VaultPath NameTable::lookupAlias(const QString &target) const
{
    if (!hasAny(&NameTable::m_aliases))
        return {};
    const PathSet *set = find(&NameTable::m_aliases, aliasKey(target));
    return set ? pickBest(*set) : VaultPath();
}

VaultPath NameTable::lookupAlias(const ResolvePlan &plan) const
{
    if (!hasAny(&NameTable::m_aliases))
        return {};
    const PathSet *set = find(&NameTable::m_aliases, plan.aliasKey());
    return set ? pickBest(*set) : VaultPath();
}

// ---------------------------------------------------------------------------------------------
// ResolvePlan
// ---------------------------------------------------------------------------------------------

ResolvePlan::From ResolvePlan::From::of(const VaultPath &path)
{
    return From{path, paths::dirname(path), path.isEmpty() ? QString() : paths::dirname(paths::normalize(path))};
}

ResolvePlan::ResolvePlan(const QString &target, const VaultPath &from, TargetForm form)
{
    init(target, from, form, nullptr, nullptr);
}

ResolvePlan::ResolvePlan(const QString &target, const From &from, TargetForm form)
{
    init(target, from.path, form, &from.dir, &from.normalizedDir);
}

void ResolvePlan::init(const QString &target, const VaultPath &from, TargetForm form, const QString *dir,
                       const QString *normalizedDir)
{
    m_target = cleanTarget(target, form);
    m_from = from;
    if (m_target.isEmpty()) {
        m_kind = Kind::From;
        return;
    }
    if (!m_target.contains(u'/') && !m_target.startsWith(u'.')) {
        m_kind = Kind::Bare;
        m_lowered = lower(nfc(m_target));
        m_attachmentFirst = isAttachmentExtension(paths::extension(m_lowered));
        m_fromDir = dir ? *dir : paths::dirname(from);
        m_probeCount = m_fromDir.isEmpty() ? 1 : 2;
        return;
    }
    m_lowered = paths::normalize(m_target); // (Path: the normalised target, not lower-cased)
    if (m_lowered.isEmpty()) {
        m_kind = Kind::Nothing;
        return;
    }
    m_kind = Kind::Path;
    m_attachmentFirst = isAttachmentExtension(paths::extension(m_lowered));
    m_explicitRelative = m_target.startsWith(u"./") || m_target.startsWith(u"../");
    m_fromDir = normalizedDir ? *normalizedDir : (from.isEmpty() ? QString() : paths::dirname(paths::normalize(from)));
    const bool relativeFirst = m_explicitRelative && !from.isEmpty();
    const bool underFrom = !m_fromDir.isEmpty() && !m_explicitRelative && !m_target.startsWith(u'/');
    m_probeCount = 1 + (relativeFirst || underFrom ? 1 : 0);
}

QString ResolvePlan::probePath(int i) const
{
    if (m_kind == Kind::Bare)
        return i == 0 ? m_target : paths::join(m_fromDir, m_target);
    // Path: [the explicitly relative path], the exact path, [the path under the file's folder].
    const bool relativeFirst = m_explicitRelative && !m_from.isEmpty();
    if (i == 0 && relativeFirst)
        return paths::join(m_fromDir, m_target);
    if (i == (relativeFirst ? 1 : 0))
        return m_lowered;
    return paths::join(m_fromDir, m_target);
}

const ResolvePlan::Probe &ResolvePlan::probe(int i) const
{
    const quint8 bit = i == 0 ? Probe0 : Probe1;
    Probe &p = m_probes[i];
    if (!(m_derived & bit)) {
        p.spelled = nfc(probePath(i));
        // A bare name's own key is its lower-cased NFC (shared, not built again).
        p.key = m_kind == Kind::Bare && i == 0 ? m_lowered : lower(p.spelled);
        p.markdown = hasMarkdownSuffix(p.key);
        m_derived |= bit;
    }
    return p;
}

const ResolvePlan::Name &ResolvePlan::name() const
{
    if (!(m_derived & NameKeys)) {
        Name &n = m_name;
        n.path = m_kind == Kind::Bare ? m_lowered : lower(m_lowered);
        const QString ext = paths::extension(n.path);
        n.markdown = isMarkdownExtension(ext);
        n.attachment = isAttachmentExtension(ext);
        n.stem = n.markdown ? paths::stripMarkdownExtension(n.path) : n.path;
        n.noteBase = paths::basename(n.stem);
        n.fileBase = paths::basename(n.path);
        m_derived |= NameKeys;
    }
    return m_name;
}

const QString &ResolvePlan::aliasKey() const
{
    if (!(m_derived & AliasKey)) {
        m_aliasKey = index_detail::aliasKey(m_target);
        if (m_aliasKey == m_lowered)
            m_aliasKey = m_lowered; // (a bare name's: one string)
        m_derived |= AliasKey;
    }
    return m_aliasKey;
}

void ResolvePlan::complete() const
{
    if (m_kind != Kind::Bare && m_kind != Kind::Path)
        return;
    for (int i = 0; i < probeCount(); ++i)
        probe(i);
    name();
    aliasKey();
}

} // namespace brulion::index_detail
