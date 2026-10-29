#include "index/resolve_p.h"

#include "index/textutil_p.h"
#include "util/paths.h"

#include <algorithm>
#include <functional>

namespace brulion::index_detail {

using namespace Qt::StringLiterals;

namespace {

bool isMarkdownExtension(QStringView ext)
{
    return ext == u".md" || ext == u".markdown";
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

VaultPath pickBestIf(const PathSet *set, const std::function<bool(const VaultPath &)> &accept)
{
    if (!set)
        return {};
    const VaultPath *best = nullptr;
    set->forEach([&](const VaultPath &p) {
        if ((!best || betterPath(p, *best)) && accept(p))
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
    constexpr qsizetype MaxExtensionLength = 10;
    if (ext.size() < 2 || ext.size() > MaxExtensionLength + 1 || ext[0] != u'.' || isMarkdownExtension(ext))
        return false;
    bool letter = false;
    for (const QChar c : ext.sliced(1)) {
        const bool alpha = (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
        if (!alpha && !(c >= u'0' && c <= u'9'))
            return false;
        letter = letter || alpha;
    }
    return letter;
}

bool isExternalTarget(QStringView target)
{
    // ^[A-Za-z][A-Za-z0-9+.-]*:   but not a Windows drive (C:\ or C:/).
    if (target.isEmpty() || !(target[0].isLetter() && target[0].unicode() < 0x80))
        return false;
    qsizetype i = 1;
    while (i < target.size()) {
        const QChar c = target[i];
        const bool schemeChar = c.unicode() < 0x80 && (c.isLetterOrNumber() || c == u'+' || c == u'.' || c == u'-');
        if (!schemeChar)
            break;
        ++i;
    }
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

void NameTable::addNote(const VaultPath &path, const QStringList &aliases)
{
    add(&NameTable::m_byPath, pathKey(path), path);
    add(&NameTable::m_noteNames, noteNameKey(path), path);
    for (const QString &a : aliases) {
        const QString k = aliasKey(a);
        if (!k.isEmpty())
            add(&NameTable::m_aliases, k, path);
    }
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

void NameTable::overlay(const NameTable *base)
{
    clear();
    m_base = base;
}

const PathSet *NameTable::find(Map NameTable::*field, const QString &key) const
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
    const QString t = cleanTarget(target, form);
    if (t.isEmpty())
        return from;
    if (!t.contains(u'/') && !t.startsWith(u'.'))
        return resolveBare(t, from);
    const QString norm = paths::normalize(t);
    if (norm.isEmpty())
        return {};
    const bool attachmentFirst = isAttachmentExtension(paths::extension(norm));
    const bool explicitRelative = t.startsWith(u"./") || t.startsWith(u"../");
    const QString fromDir = from.isEmpty() ? QString() : paths::dirname(paths::normalize(from));
    if (explicitRelative && !from.isEmpty()) {
        const VaultPath hit = lookupPath(paths::join(fromDir, t), attachmentFirst);
        if (!hit.isEmpty())
            return hit;
    }
    if (const VaultPath exact = lookupPath(norm, attachmentFirst); !exact.isEmpty())
        return exact;
    if (!fromDir.isEmpty() && !explicitRelative && !t.startsWith(u'/')) {
        const VaultPath hit = lookupPath(paths::join(fromDir, t), attachmentFirst);
        if (!hit.isEmpty())
            return hit;
    }
    if (const VaultPath byName = lookupName(lower(norm)); !byName.isEmpty())
        return byName;
    return lookupAlias(t);
}

VaultPath NameTable::resolveNote(const QString &target, TargetForm form) const
{
    const QString t = cleanTarget(target, form);
    const QString spelled = nfc(paths::normalize(t));
    if (spelled.isEmpty())
        return {};
    const QString key = lower(spelled);
    const bool md = key.endsWith(u".md") || key.endsWith(u".markdown");
    // 1) the note at exactly that vault path.
    VaultPath hit = md ? lookupExact(key, spelled) : lookupExact(key + u".md"_s, spelled + u".md"_s);
    if (hit.isEmpty() && !md)
        hit = lookupExact(key + u".markdown"_s, spelled + u".markdown"_s);
    if (!hit.isEmpty())
        return hit;
    // 2) base name, for a target with folders a path suffix (shortest path, then alphabetical).
    const QString stem = md ? paths::stripMarkdownExtension(key) : key;
    if (const PathSet *set = find(&NameTable::m_noteNames, paths::basename(stem))) {
        hit = !stem.contains(u'/') ? pickBest(*set) : pickBestIf(set, [&](const VaultPath &p) {
            return endsWithSegments(paths::stripMarkdownExtension(pathKey(p)), stem);
        });
        if (!hit.isEmpty())
            return hit;
    }
    // 3) aliases.
    return lookupAlias(t);
}

VaultPath NameTable::resolveBare(const QString &target, const VaultPath &from) const
{
    const QString lowered = lower(nfc(target));
    const QString ext = paths::extension(lowered);
    const bool attachmentFirst = isAttachmentExtension(ext);
    // 1) the file at the vault root, 2) the file in the linking file's folder.
    if (const VaultPath hit = lookupPath(target, attachmentFirst); !hit.isEmpty())
        return hit;
    if (const QString fromDir = paths::dirname(from); !fromDir.isEmpty()) {
        if (const VaultPath hit = lookupPath(paths::join(fromDir, target), attachmentFirst); !hit.isEmpty())
            return hit;
    }
    // 3) base name (shortest path, then alphabetical).
    const bool md = isMarkdownExtension(ext);
    const PathSet *notes = find(&NameTable::m_noteNames, md ? paths::stripMarkdownExtension(lowered) : lowered);
    const PathSet *files = md ? nullptr : find(&NameTable::m_fileNames, lowered);
    const VaultPath bestNote = notes ? pickBest(*notes) : VaultPath();
    const VaultPath bestFile = files ? pickBest(*files) : VaultPath();
    const VaultPath best =
            attachmentFirst ? (bestFile.isEmpty() ? bestNote : bestFile) : (bestNote.isEmpty() ? bestFile : bestNote);
    if (!best.isEmpty())
        return best;
    // 4) aliases.
    return lookupAlias(target);
}

VaultPath NameTable::lookupPath(const QString &path, bool attachmentFirst) const
{
    const QString spelled = nfc(path);
    const QString key = lower(spelled);
    if (key.endsWith(u".md") || key.endsWith(u".markdown"))
        return lookupExact(key, spelled);
    auto asNote = [&] {
        const VaultPath md = lookupExact(key + u".md"_s, spelled + u".md"_s);
        return md.isEmpty() ? lookupExact(key + u".markdown"_s, spelled + u".markdown"_s) : md;
    };
    if (attachmentFirst) {
        const VaultPath file = lookupExact(key, spelled);
        return file.isEmpty() ? asNote() : file;
    }
    const VaultPath note = asNote();
    return note.isEmpty() ? lookupExact(key, spelled) : note;
}

VaultPath NameTable::lookupExact(const QString &key, const QString &spelled) const
{
    const PathSet *set = find(&NameTable::m_byPath, key);
    if (!set || set->isEmpty())
        return {};
    if (set->size() == 1)
        return set->sorted().constFirst();
    const VaultPath exact = set->min([&](const VaultPath &a, const VaultPath &b) {
        const bool aExact = nfc(a) == spelled;
        return aExact != (nfc(b) == spelled) ? aExact : betterPath(a, b);
    });
    return exact;
}

VaultPath NameTable::lookupName(const QString &lowerPath) const
{
    const QString ext = paths::extension(lowerPath);
    const bool md = isMarkdownExtension(ext);
    const bool hasDir = lowerPath.contains(u'/');
    auto notes = [&]() -> VaultPath {
        const QString stem = md ? paths::stripMarkdownExtension(lowerPath) : lowerPath;
        const PathSet *set = find(&NameTable::m_noteNames, hasDir ? paths::basename(stem) : stem);
        if (!set)
            return {};
        if (!hasDir)
            return pickBest(*set);
        return pickBestIf(set, [&](const VaultPath &p) {
            return endsWithSegments(paths::stripMarkdownExtension(pathKey(p)), stem);
        });
    };
    auto files = [&]() -> VaultPath {
        if (md || !hasAny(&NameTable::m_fileNames))
            return {};
        const PathSet *set = find(&NameTable::m_fileNames, hasDir ? paths::basename(lowerPath) : lowerPath);
        if (!set)
            return {};
        if (!hasDir)
            return pickBest(*set);
        return pickBestIf(set, [&](const VaultPath &p) { return endsWithSegments(pathKey(p), lowerPath); });
    };
    if (isAttachmentExtension(ext)) {
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

} // namespace brulion::index_detail
