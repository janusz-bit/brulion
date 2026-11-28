#include "codefiles/fileindex.h"

#include "codefiles/fuzzy.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::codefiles {

QString cleanRelativePath(QStringView path)
{
    QString p = path.toString();
    if (p.contains(u'\\'))
        p.replace(u'\\', u'/');
    while (p.startsWith(u"./"))
        p.remove(0, 2);
    while (p.endsWith(u'/'))
        p.chop(1);
    if (p.isEmpty() || p.startsWith(u'/') || p.contains(u'\0') || p.contains(u'\n') || p.contains(u'\r')
        || (p.size() >= 2 && p.at(1) == u':' && p.at(0).isLetter()))
        return {};
    for (const QStringView part : QStringView(p).split(u'/')) {
        if (part.isEmpty() || part == u"." || part == u"..")
            return {};
    }
    return p;
}

FileIndex FileIndex::fromPaths(const QStringList &files, int maxEntries)
{
    QStringList clean;
    clean.reserve(files.size());
    for (const QString &f : files) {
        QString c = cleanRelativePath(f);
        if (!c.isEmpty())
            clean.append(std::move(c));
    }
    return build(std::move(clean), false, maxEntries);
}

FileIndex FileIndex::fromNulSeparated(QByteArrayView output, int maxEntries)
{
    QStringList files;
    qsizetype from = 0;
    while (from < output.size()) {
        qsizetype nul = output.indexOf('\0', from);
        if (nul < 0)
            nul = output.size();
        if (nul > from) {
            QString c = cleanRelativePath(QString::fromUtf8(output.sliced(from, nul - from)));
            if (!c.isEmpty())
                files.append(std::move(c));
        }
        from = nul + 1;
    }
    return build(std::move(files), false, maxEntries);
}

bool FileIndex::skippedFolder(QStringView name)
{
    if (name.isEmpty() || name.startsWith(u'.'))
        return true;
    static const QStringList exact{u"node_modules"_s, u"build"_s,       u"dist"_s,          u"out"_s,
                                   u"target"_s,       u"__pycache__"_s, u"venv"_s,          u"result"_s,
                                   u"bower_components"_s, u"Pods"_s,    u"DerivedData"_s,   u"obj"_s};
    for (const QString &e : exact) {
        if (name == e)
            return true;
    }
    return name.startsWith(u"build-") || name.startsWith(u"cmake-build-");
}

FileIndex FileIndex::walk(const QString &root, int maxEntries, const std::atomic<bool> *cancel)
{
    QStringList files;
    bool truncated = false;
    const QString base = QDir(root).absolutePath();
    // Breadth first: a big tree that does not fit keeps its upper levels.
    QList<std::pair<QString, int>> queue{{QString(), 0}};
    qsizetype head = 0;
    int folders = 0;
    while (head < queue.size()) {
        if (cancel && cancel->load(std::memory_order_relaxed))
            break;
        const auto [relative, depth] = queue.at(head++);
        QDir dir(relative.isEmpty() ? base : base + u'/' + relative);
        const QFileInfoList children =
                dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                  QDir::Name | QDir::DirsLast);
        for (const QFileInfo &child : children) {
            const QString name = child.fileName();
            const QString path = relative.isEmpty() ? name : relative + u'/' + name;
            // (A symbolic link to a folder is neither followed nor listed.)
            if (child.isDir() && child.isSymLink())
                continue;
            if (child.isDir()) {
                if (skippedFolder(name) || depth + 1 >= MaxWalkDepth)
                    continue;
                if (files.size() + folders >= maxEntries) {
                    truncated = true;
                    continue;
                }
                ++folders;
                queue.append({path, depth + 1});
                continue;
            }
            if (files.size() + folders >= maxEntries) {
                truncated = true;
                break;
            }
            if (!cleanRelativePath(path).isEmpty())
                files.append(path);
        }
    }
    return build(std::move(files), truncated, maxEntries);
}

FileIndex FileIndex::build(QStringList files, bool truncated, int maxEntries)
{
    FileIndex index;
    maxEntries = std::max(0, maxEntries);
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    // The folders of the files, as long as files and folders fit together.
    QSet<QString> folders;
    QStringList kept;
    kept.reserve(files.size());
    // (Sorted: the files of a folder come together — their folders are known already.)
    QStringView lastFolder;
    QStringList newFolders;
    for (const QString &f : std::as_const(files)) {
        newFolders.clear();
        const qsizetype lastSlash = f.lastIndexOf(u'/');
        const QStringView parent = lastSlash < 0 ? QStringView() : QStringView(f).first(lastSlash);
        if (lastSlash >= 0 && parent != lastFolder) {
            for (qsizetype slash = f.indexOf(u'/'); slash >= 0; slash = f.indexOf(u'/', slash + 1)) {
                const QString folder = f.left(slash);
                if (!folders.contains(folder))
                    newFolders.append(folder);
            }
        }
        if (kept.size() + folders.size() + newFolders.size() + 1 > maxEntries) {
            truncated = true;
            continue;
        }
        for (const QString &folder : std::as_const(newFolders))
            folders.insert(folder);
        kept.append(f);
        if (lastSlash >= 0)
            lastFolder = parent;
    }
    // A path that is a file and a folder (a submodule listed beside its files): the folder.
    for (const QString &folder : std::as_const(folders)) {
        const auto it = std::lower_bound(kept.begin(), kept.end(), folder);
        if (it != kept.end() && *it == folder)
            kept.erase(it);
    }
    struct Item {
        QString path;
        bool folder;
    };
    QList<Item> all;
    all.reserve(kept.size() + folders.size());
    for (QString &f : kept)
        all.append({std::move(f), false});
    for (const QString &d : std::as_const(folders))
        all.append({d, true});
    std::sort(all.begin(), all.end(), [](const Item &a, const Item &b) { return a.path < b.path; });

    qsizetype chars = 0;
    for (const Item &i : std::as_const(all))
        chars += i.path.size();
    index.m_text.reserve(chars);
    index.m_entries.reserve(all.size());
    for (const Item &i : std::as_const(all)) {
        Entry e;
        e.offset = int(index.m_text.size());
        e.length = int(i.path.size());
        e.nameStart = int(i.path.lastIndexOf(u'/') + 1);
        e.folder = i.folder;
        index.m_text.append(i.path);
        index.m_entries.append(e);
        if (!i.folder)
            ++index.m_files;
    }
    index.m_truncated = truncated;
    // Shallow first: fewer "/", then path order (stable over the sorted entries).
    index.m_shallow.resize(index.m_entries.size());
    for (int i = 0; i < int(index.m_entries.size()); ++i)
        index.m_shallow[i] = i;
    QList<int> depth(index.m_entries.size());
    for (int i = 0; i < int(index.m_entries.size()); ++i)
        depth[i] = int(index.path(i).count(u'/'));
    std::stable_sort(index.m_shallow.begin(), index.m_shallow.end(),
                     [&depth](int a, int b) { return depth[a] < depth[b]; });
    return index;
}

QStringView FileIndex::path(int entry) const
{
    if (entry < 0 || entry >= int(m_entries.size()))
        return {};
    const Entry &e = m_entries.at(entry);
    return QStringView(m_text).sliced(e.offset, e.length);
}

QStringView FileIndex::name(int entry) const
{
    if (entry < 0 || entry >= int(m_entries.size()))
        return {};
    return path(entry).sliced(m_entries.at(entry).nameStart);
}

int FileIndex::nameStart(int entry) const
{
    return entry < 0 || entry >= int(m_entries.size()) ? 0 : m_entries.at(entry).nameStart;
}

bool FileIndex::isFolder(int entry) const
{
    return entry >= 0 && entry < int(m_entries.size()) && m_entries.at(entry).folder;
}

int FileIndex::find(QStringView p) const
{
    int lo = 0;
    int hi = int(m_entries.size());
    while (lo < hi) {
        const int mid = lo + (hi - lo) / 2;
        if (path(mid) < p)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < int(m_entries.size()) && path(lo) == p ? lo : -1;
}

QStringList FileIndex::folders(int max) const
{
    QStringList out;
    for (const int i : m_shallow) {
        if (out.size() >= max)
            break;
        if (m_entries.at(i).folder)
            out.append(path(i).toString());
    }
    return out;
}

QStringList FileIndex::files() const
{
    QStringList out;
    out.reserve(m_files);
    for (int i = 0; i < int(m_entries.size()); ++i) {
        if (!m_entries.at(i).folder)
            out.append(path(i).toString());
    }
    return out;
}

QList<FileIndex::Hit> FileIndex::search(QStringView query, int limit, Kinds kinds) const
{
    QList<Hit> out;
    if (limit <= 0)
        return out;
    const auto wanted = [this, kinds](int i) {
        return kinds == Kinds::All || (kinds == Kinds::Files) != m_entries.at(i).folder;
    };
    const FuzzyQuery q = prepareQuery(query);
    if (q.isEmpty()) {
        for (const int i : m_shallow) {
            if (!wanted(i))
                continue;
            out.append({i, 1, {}});
            if (out.size() >= limit)
                break;
        }
        return out;
    }
    // The best `limit` by score (ties: shorter, then earlier), kept as a heap whose top is the worst.
    struct Scored {
        int score;
        int entry;
    };
    const auto better = [this](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score > b.score;
        const int la = m_entries.at(a.entry).length;
        const int lb = m_entries.at(b.entry).length;
        if (la != lb)
            return la < lb;
        return a.entry < b.entry;
    };
    QList<Scored> heap;
    heap.reserve(limit + 1);
    for (int i = 0; i < int(m_entries.size()); ++i) {
        if (!wanted(i))
            continue;
        const Entry &e = m_entries.at(i);
        const int score = scorePath(q, QStringView(m_text).sliced(e.offset, e.length), e.nameStart);
        if (score <= 0)
            continue;
        const Scored s{score, i};
        if (heap.size() < limit) {
            heap.append(s);
            std::push_heap(heap.begin(), heap.end(), better);
        } else if (better(s, heap.front())) {
            std::pop_heap(heap.begin(), heap.end(), better);
            heap.back() = s;
            std::push_heap(heap.begin(), heap.end(), better);
        }
    }
    std::sort(heap.begin(), heap.end(), better);
    out.reserve(heap.size());
    for (const Scored &s : std::as_const(heap)) {
        const Entry &e = m_entries.at(s.entry);
        FuzzyMatch m = matchPath(q, QStringView(m_text).sliced(e.offset, e.length), e.nameStart);
        out.append({s.entry, s.score, std::move(m.positions)});
    }
    return out;
}

} // namespace brulion::codefiles
