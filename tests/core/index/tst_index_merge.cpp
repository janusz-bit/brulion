// The merge of prepared notes (L1b, docs/PERFORMANCE.md): the keys a note is filed under and the
// planned resolution of its links are derived on the loader's workers (VaultIndex::prepare(),
// index_detail::NoteKeys, ResolvePlan), so the index's thread only looks up and inserts. Here:
// a planned link resolves as the resolution did before plans (a verbatim copy of it is the
// reference), a plan completed on another thread as one completed while it is looked up, and a
// generated notebook — Polish names, NFC and NFD spellings, case clashes, aliases, tags naming
// pages, journals, block references, attachments, relative links, task ids — merged from notes
// prepared on worker threads in the loader's order ends in exactly the state (every record and
// map, IndexAccess::describeState()) of the same notes indexed one by one with update(), with
// the same versions and change log (changesSince()) after every note.
#include "index/index.h"
#include "index/resolve_p.h"
#include "index/textutil_p.h"
#include "index/vaultindex_p.h"
#include "indexcompare.h"
#include "markdown/parse.h"
#include "util/paths.h"

#include <QHash>
#include <QRandomGenerator>
#include <QSet>
#include <QTest>

#include <algorithm>
#include <thread>
#include <vector>

using namespace brulion;
using namespace brulion::index_detail;
using namespace brulion::testing;
using namespace Qt::StringLiterals;

namespace {

/// The name tables and link resolution as they were before plans (resolve.cpp at 438d6eb7),
/// over plain hashes of sets: the oracle a planned resolution must agree with.
class ReferenceNames {
public:
    void addNote(const VaultPath &path, const QStringList &aliases)
    {
        m_byPath[pathKey(path)].insert(path);
        m_noteNames[noteNameKey(path)].insert(path);
        for (const QString &a : aliases) {
            const QString k = aliasKey(a);
            if (!k.isEmpty())
                m_aliases[k].insert(path);
        }
    }
    void addAttachment(const VaultPath &path)
    {
        m_byPath[pathKey(path)].insert(path);
        m_fileNames[fileNameKey(path)].insert(path);
    }

    VaultPath resolve(const QString &target, const VaultPath &from, TargetForm form) const
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
            if (const VaultPath hit = lookupPath(paths::join(fromDir, t), attachmentFirst); !hit.isEmpty())
                return hit;
        }
        if (const VaultPath exact = lookupPath(norm, attachmentFirst); !exact.isEmpty())
            return exact;
        if (!fromDir.isEmpty() && !explicitRelative && !t.startsWith(u'/')) {
            if (const VaultPath hit = lookupPath(paths::join(fromDir, t), attachmentFirst); !hit.isEmpty())
                return hit;
        }
        const VaultPath byName = lookupName(lower(norm));
        return byName.isEmpty() ? lookupAlias(t) : byName;
    }

private:
    using Map = QHash<QString, QSet<VaultPath>>;

    static bool betterPath(const VaultPath &a, const VaultPath &b)
    {
        return a.size() < b.size() || (a.size() == b.size() && a < b);
    }
    static bool endsWithSegments(const QString &path, const QString &suffix)
    {
        return path == suffix
               || (path.size() > suffix.size() && path.endsWith(suffix)
                   && path[path.size() - suffix.size() - 1] == u'/');
    }
    static QString notePathKey(const VaultPath &path) { return paths::stripMarkdownExtension(pathKey(path)); }
    static VaultPath best(const QSet<VaultPath> &set)
    {
        const VaultPath *b = nullptr;
        for (const VaultPath &p : set) {
            if (!b || betterPath(p, *b))
                b = &p;
        }
        return b ? *b : VaultPath();
    }
    template<class KeyOf>
    static VaultPath pickBySuffix(const Map &map, const QString &key, const QString &name, KeyOf keyOf)
    {
        const auto it = map.constFind(key);
        if (it == map.constEnd())
            return {};
        if (!name.contains(u'/'))
            return best(*it);
        const VaultPath *b = nullptr;
        for (const VaultPath &p : *it) {
            if ((!b || betterPath(p, *b)) && endsWithSegments(keyOf(p), name))
                b = &p;
        }
        return b ? *b : VaultPath();
    }

    VaultPath resolveBare(const QString &target, const VaultPath &from) const
    {
        const QString lowered = lower(nfc(target));
        const bool attachmentFirst = isAttachmentExtension(paths::extension(lowered));
        if (const VaultPath hit = lookupPath(target, attachmentFirst); !hit.isEmpty())
            return hit;
        if (const QString fromDir = paths::dirname(from); !fromDir.isEmpty()) {
            if (const VaultPath hit = lookupPath(paths::join(fromDir, target), attachmentFirst); !hit.isEmpty())
                return hit;
        }
        const VaultPath byName = lookupName(lowered);
        return byName.isEmpty() ? lookupAlias(target) : byName;
    }
    VaultPath lookupPath(const QString &path, bool attachmentFirst) const
    {
        const QString spelled = nfc(path);
        const QString key = lower(spelled);
        if (key.endsWith(u".md") || key.endsWith(u".markdown"))
            return lookupExact(key, spelled);
        if (attachmentFirst) {
            const VaultPath file = lookupExact(key, spelled);
            return file.isEmpty() ? lookupNote(key, spelled) : file;
        }
        const VaultPath note = lookupNote(key, spelled);
        return note.isEmpty() ? lookupExact(key, spelled) : note;
    }
    VaultPath lookupExact(const QString &key, const QString &spelled) const
    {
        const auto it = m_byPath.constFind(key);
        if (it == m_byPath.constEnd())
            return {};
        const VaultPath *b = nullptr;
        for (const VaultPath &p : *it) {
            if (!b)
                b = &p;
            else if (const bool pExact = nfc(p) == spelled; pExact != (nfc(*b) == spelled) ? pExact : betterPath(p, *b))
                b = &p;
        }
        return b ? *b : VaultPath();
    }
    VaultPath lookupNote(const QString &key, const QString &spelled) const
    {
        const VaultPath md = lookupExact(key + u".md"_s, spelled + u".md"_s);
        return md.isEmpty() ? lookupExact(key + u".markdown"_s, spelled + u".markdown"_s) : md;
    }
    VaultPath lookupName(const QString &lowerPath) const
    {
        const QString ext = paths::extension(lowerPath);
        const bool md = ext == u".md" || ext == u".markdown";
        const auto notes = [&] {
            const QString stem = md ? paths::stripMarkdownExtension(lowerPath) : lowerPath;
            return pickBySuffix(m_noteNames, paths::basename(stem), stem, notePathKey);
        };
        const auto files = [&]() -> VaultPath {
            if (md || m_fileNames.isEmpty())
                return {};
            return pickBySuffix(m_fileNames, paths::basename(lowerPath), lowerPath, pathKey);
        };
        if (isAttachmentExtension(ext)) {
            const VaultPath f = files();
            return f.isEmpty() ? notes() : f;
        }
        const VaultPath n = notes();
        return n.isEmpty() ? files() : n;
    }
    VaultPath lookupAlias(const QString &target) const
    {
        if (m_aliases.isEmpty())
            return {};
        const auto it = m_aliases.constFind(aliasKey(target));
        return it == m_aliases.constEnd() ? VaultPath() : best(*it);
    }

    Map m_byPath, m_noteNames, m_fileNames, m_aliases;
};

// NFD spellings (macOS file names, pasted text) beside the NFC ones.
const QString kTurtleNfd = u"Z\u0307o\u0301\u0142w"_s;    // Żółw
const QString kLodzNfd = u"\u0141o\u0301dz\u0301"_s;      // Łódź
const QString kPhotoNfd = u"zdje\u0328cie"_s;             // zdjęcie
const QString kTurtlesNfd = u"Z\u0307o\u0301\u0142wie"_s; // Żółwie (a folder)

const QStringList kNames = {u"Żółw"_s, kTurtleNfd,   u"Łódź"_s,      kLodzNfd,       u"Gęś"_s,
                            u"Dom"_s,  u"dom"_s,     u"DOM"_s,       u"Plan"_s,      u"Zażółć gęślą jaźń"_s,
                            u"Ćma"_s,  u"Projekt"_s, u"Spotkanie"_s, u"Dr. Smith"_s, u"Spotkanie 14.10"_s,
                            u"C#"_s,   u"raporty"_s, u"Żurek"_s};
const QStringList kFolders = {QString(),   u"Projekty"_s, u"projekty"_s,     u"Archiwum/Stare"_s, u"Żółwie"_s,
                              kTurtlesNfd, u"Praca"_s,    u"Projekty/Dom"_s, u"Łąka/Głęboka"_s,   u"x"_s};
const QStringList kTags = {u"projekt"_s, u"Praca/raporty"_s, u"[[Dom]]"_s, u"żółw"_s, u"Ćma"_s, u"[[Łódź]]"_s};

QString pick(QRandomGenerator &rng, const QStringList &list)
{
    return list.at(rng.bounded(int(list.size())));
}

QString inFolder(const QString &folder, const QString &file)
{
    return folder.isEmpty() ? file : folder + u'/' + file;
}

/// A line of a generated note (each draw from `rng` in its own statement: the same notebook on
/// every compiler).
QString randomLine(QRandomGenerator &rng)
{
    const int kind = rng.bounded(24);
    const QString name = pick(rng, kNames);
    const QString other = pick(rng, kNames);
    const QString folder = pick(rng, kFolders);
    const int id = rng.bounded(6);
    switch (kind) {
    case 0: return u"- [[%1]] i [[%2|inna nazwa]]"_s.arg(name, other);
    case 1: return u"- [[%1]]"_s.arg(inFolder(folder, name));
    case 2: return u"- [[./%1]] oraz [[../%2]]"_s.arg(name, other);
    case 3: return u"- [[/%1]] i [[%2.md]]"_s.arg(name, other.toUpper());
    case 4: // Markdown links are percent-decoded: `C%23.md` names the file "C#.md" ('#' and '|' kept).
        return u"- [opis](%1.md) i [w górę](../%2.md) [x](%3)"_s.arg(
                QString(name).replace(u' ', u"%20"_s).replace(u'#', u"%23"_s), other,
                QString(inFolder(folder, other)).replace(u' ', u"%20"_s).replace(u'#', u"%23"_s));
    case 5: return u"- ![[zdjęcie.png]] ![[Zdjęcie.PNG]] ![[%1.png]] ![[plan.pdf]]"_s.arg(kPhotoNfd);
    case 6: return u"- #%1 i #%2"_s.arg(pick(rng, kTags), pick(rng, kTags));
    case 7: return u"- tekst bloku o żółwiu ^id%1"_s.arg(id);
    case 8: return u"- ((id%1)) i !((id%2))"_s.arg(id).arg((id + 1) % 6);
    case 9: return u"- [[%1#^id%2]] i [[#^id%2]]"_s.arg(name).arg(id);
    case 10: return u"- [ ] zadanie 🆔 t%1 ⛔ t%2 📅 2026-10-1%1"_s.arg(id).arg((id + 2) % 6);
    case 11: return u"- przypomnienie ⏰ 2026-10-%1 08:00 [[%2]]"_s.arg(10 + id).arg(name);
    case 12: return u"- [[2026-10-1%1]] i [[journals/2026-10-1%2]]"_s.arg(id).arg((id + 3) % 10);
    case 13: return u"- [[%1|%2]] [[Nieistniejąca %3]]"_s.arg(name, other).arg(id);
    case 14: return u"- [[Spotkanie 14.10]] [[Dr. Smith]] [[C#]] [[x/../%1]] [C](C%23.md) [p](A%7CB.md)"_s.arg(name);
    case 15: return u"- [[%1#Nagłówek]] [[ %2 ]]"_s.arg(name, other);
    case 16: return u"  - podpunkt [[%1]] #%2"_s.arg(other, pick(rng, kTags));
    case 17: return u"- Akapit o %1 i %2."_s.arg(name, other);
    case 18: return u"- [[%1]] [[%2]]"_s.arg(inFolder(folder, other), inFolder(pick(rng, kFolders), name).toLower());
    case 19: return u"- [[c.markdown]] [[%1.markdown]] ![[assets/zdjęcie.png]]"_s.arg(name);
    case 20: return u"- [[Remont]] [[Janek]] [[Alias %1]]"_s.arg(id);
    case 21: return u"- [[%1/%2]]"_s.arg(pick(rng, kFolders).toUpper(), name);
    case 22: return u"- `[[nie link]]` [[%1]]"_s.arg(name);
    default: return u"- zwykły tekst %1"_s.arg(id);
    }
}

QString randomHeader(QRandomGenerator &rng)
{
    const int kind = rng.bounded(6);
    const int n = rng.bounded(4);
    const QString name = pick(rng, kNames);
    switch (kind) {
    case 0: return u"---\naliases: [Alias %1, \"%2\"]\ntags: [projekt]\n---\n"_s.arg(n).arg(name);
    case 1: return u"alias:: Janek, %1\ntags:: praca\n\n"_s.arg(name);
    case 2: return u"---\naliases: [Remont]\n---\n"_s;
    default: return {};
    }
}

struct GenFile {
    VaultPath path;
    QString content;
    bool attachment = false;
};

/// A notebook of tricky files: pages under every name and folder (NFC and NFD spellings, case
/// clashes, `.markdown`), pages tags name, journals, attachments in several spellings.
QList<GenFile> generatedVault(quint32 seed, int pages)
{
    QRandomGenerator rng(seed);
    QHash<VaultPath, QString> notes;
    const auto note = [&]() {
        QString text = randomHeader(rng);
        const int lines = rng.bounded(8);
        for (int i = 0; i < lines; ++i)
            text += randomLine(rng) + u'\n';
        return text;
    };
    for (int i = 0; i < pages; ++i) {
        const QString folder = pick(rng, kFolders);
        const QString name = pick(rng, kNames);
        const int variant = rng.bounded(5);
        const QString suffix = variant == 4 ? u".markdown"_s : u".md"_s;
        const QString file = variant == 3 ? name + u' ' + QString::number(i % 7) : name;
        notes.insert(inFolder(folder, file + suffix), note());
    }
    for (int day = 0; day < 10; ++day)
        notes.insert(u"journals/2026-10-1%1.md"_s.arg(day), note());
    notes.insert(u"journals/2026_10_15.md"_s, note()); // a Logseq file name for a listed day
    // Pages that tags name, a page beside a folder, an empty note, a note with only a BOM-less CRLF.
    notes.insert(u"Projekt.md"_s, note());
    notes.insert(u"Praca/raporty.md"_s, note());
    notes.insert(u"Projekty.md"_s, note());
    notes.insert(u"Pusta.md"_s, QString());
    notes.insert(u"CRLF.md"_s, u"- [[Dom]]\r\n- ((id1)) ^id9\r\n"_s);
    QList<GenFile> out;
    for (auto it = notes.cbegin(); it != notes.cend(); ++it)
        out.append({it.key(), it.value(), false});
    for (const QString &a : {u"assets/zdjęcie.png"_s, u"assets/Zdjęcie.PNG"_s, u"assets/%1.png"_s.arg(kPhotoNfd),
                             u"plan.pdf"_s, u"Projekty/plan.pdf"_s, u"Żółw.png"_s, u"c.markdown.txt"_s})
        out.append({a, QString(), true});
    std::ranges::sort(out, {}, &GenFile::path);
    return out;
}

bool isAttachment(const GenFile &f)
{
    return f.attachment;
}

/// The loader's merge order (notebook::IndexLoader): attachments, the journals newest first, the
/// rest by path; `rank` = the note's position in path order (its recency).
QList<qsizetype> loaderOrder(const QList<GenFile> &files, QHash<qsizetype, quint64> *rank = nullptr)
{
    QList<qsizetype> attachments, journals, rest;
    quint64 position = 0;
    for (qsizetype i = 0; i < files.size(); ++i) {
        if (isAttachment(files[i])) {
            attachments.append(i);
            continue;
        }
        if (rank)
            rank->insert(i, position);
        ++position;
        (files[i].path.startsWith(u"journals/") ? journals : rest).append(i);
    }
    std::ranges::reverse(journals);
    return attachments + journals + rest;
}

/// Every note of `files` prepared on `threads` threads (the loader's workers).
std::vector<PreparedNote> prepareOnThreads(const QList<GenFile> &files, const VaultConfig &config, int threads)
{
    std::vector<PreparedNote> prepared(size_t(files.size()));
    std::vector<std::thread> workers;
    for (int w = 0; w < threads; ++w) {
        workers.emplace_back([&, w] {
            for (qsizetype i = w; i < files.size(); i += threads) {
                if (!isAttachment(files[i]))
                    prepared[size_t(i)] = VaultIndex::prepare(files[i].path, files[i].content, config);
            }
        });
    }
    for (std::thread &t : workers)
        t.join();
    return prepared;
}

/// The same files indexed one by one in path order with update() (the serial path).
std::unique_ptr<VaultIndex> serialIndex(const QList<GenFile> &files, const VaultConfig &config)
{
    auto index = std::make_unique<VaultIndex>(config);
    VaultIndex::Batch batch(*index);
    for (const GenFile &f : files)
        index->update(f.path, isAttachment(f) ? QString() : f.content);
    return index;
}

/// The state lines that differ (for a readable failure).
QString firstDifference(const QStringList &a, const QStringList &b)
{
    const QSet<QString> sa(a.cbegin(), a.cend());
    const QSet<QString> sb(b.cbegin(), b.cend());
    for (const QString &l : a) {
        if (!sb.contains(l))
            return u"only in the first: "_s + l;
    }
    for (const QString &l : b) {
        if (!sa.contains(l))
            return u"only in the second: "_s + l;
    }
    return a.size() == b.size() ? QString() : u"duplicate lines"_s;
}

void compareStates(const VaultIndex &a, const VaultIndex &b, const QString &what)
{
    const QStringList sa = IndexAccess::describeState(a);
    const QStringList sb = IndexAccess::describeState(b);
    QVERIFY2(sa == sb, qPrintable(what + u": "_s + firstDifference(sa, sb)));
}

/// Every note keeps the plans of exactly its links that resolve to nothing (Entry::unresolvedPlans),
/// and a kept plan still resolves its link as the link's target does.
void checkKeptPlans(const VaultIndex &index, const QString &what)
{
    for (const Entry *e : IndexAccess::sortedEntries(index)) {
        QList<int> unresolved, kept;
        for (qsizetype i = 0; i < e->links.size(); ++i) {
            if (e->links[i].target.isEmpty() && !e->links[i].key.isEmpty())
                unresolved.append(int(i));
        }
        if (e->unresolvedPlans) {
            QVERIFY2(!e->unresolvedPlans->isEmpty(), qPrintable(what + u": an empty list kept for "_s + e->path));
            for (const auto &[i, plan] : std::as_const(*e->unresolvedPlans)) {
                kept.append(i);
                QVERIFY2(!e->links.at(i).key.isEmpty(), qPrintable(e->path));
            }
        }
        std::ranges::sort(kept);
        QVERIFY2(kept == unresolved,
                 qPrintable(u"%1: %2 keeps plans %3 for the unresolved links %4"_s.arg(what, e->path)
                                    .arg(QString::number(kept.size()))
                                    .arg(unresolved.size())));
    }
}

QString describeChanges(const IndexChanges &c)
{
    return u"%1 %2 [%3]"_s.arg(c.complete).arg(c.names).arg(c.notes.join(u'|'));
}

} // namespace

class TestIndexMerge : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // A planned resolution — derived as the lookups need it, or completed beforehand on another
    // thread — finds what the resolution before plans found, for every target form over tables
    // with and without attachments and aliases.
    void plannedResolutionAsBefore_data()
    {
        QTest::addColumn<quint32>("seed");
        QTest::addColumn<bool>("attachments");
        QTest::addColumn<bool>("aliases");
        QTest::newRow("notes only") << 1u << false << false;
        QTest::newRow("notes and aliases") << 2u << false << true;
        QTest::newRow("notes and attachments") << 3u << true << false;
        QTest::newRow("everything") << 4u << true << true;
        QTest::newRow("everything, other files") << 5u << true << true;
    }
    void plannedResolutionAsBefore()
    {
        QFETCH(quint32, seed);
        QFETCH(bool, attachments);
        QFETCH(bool, aliases);
        QRandomGenerator rng(seed);
        NameTable table;
        ReferenceNames reference;
        QStringList froms{QString(), u"Notatka.md"_s};
        for (const GenFile &f : generatedVault(seed, 120)) {
            if (isAttachment(f)) {
                if (!attachments)
                    continue;
                table.addAttachment(f.path);
                reference.addAttachment(f.path);
                continue;
            }
            const QStringList names = aliases ? markdown::parseNote(f.path, f.content).aliases : QStringList();
            table.addNote(f.path, names);
            reference.addNote(f.path, names);
            froms.append(f.path);
        }
        QStringList targets{QString(),
                            u"#^id1"_s,
                            u"  "_s,
                            u"/"_s,
                            u"./"_s,
                            u".."_s,
                            u"../.."_s,
                            u"[[Dom]]"_s,
                            u"[[Dom|domek]]"_s,
                            u"Dom#Nagłówek"_s,
                            u"Dom\\|x"_s,
                            u"a\\b"_s,
                            u".hidden"_s,
                            u"Remont"_s,
                            u"Janek"_s,
                            u"Alias 2"_s,
                            u"plan.pdf"_s,
                            u"PLAN.PDF"_s,
                            u"zdjęcie.png"_s,
                            kPhotoNfd + u".png"_s,
                            u"assets/zdjęcie"_s,
                            u"c.markdown"_s};
        for (const QString &n : kNames) {
            targets << n << n.toUpper() << n + u".md"_s << n + u".markdown"_s << n + u".png"_s << u"./"_s + n
                    << u"../"_s + n << u"/"_s + n << u" "_s + n + u" "_s;
            for (const QString &folder : kFolders)
                targets << inFolder(folder, n) << inFolder(folder.toLower(), n) + u".md"_s;
        }
        for (int i = 0; i < 200; ++i)
            targets << pick(rng, targets) + pick(rng, {u"/"_s, u"#x"_s, u" "_s, u".md"_s, u"/../x"_s});
        // A sample of the files linking (all of them would take too long under a sanitizer).
        QStringList sampled{QString(), u"Notatka.md"_s, u"x/y/z.md"_s, kTurtlesNfd + u"/Plan.md"_s};
        for (int i = 0; i < 12; ++i)
            sampled.append(pick(rng, froms));
        struct Case {
            QString target;
            QString from;
            TargetForm form;
        };
        QList<Case> cases;
        std::vector<ResolvePlan> plans;
        for (const QString &target : std::as_const(targets)) {
            for (const QString &from : std::as_const(sampled)) {
                for (const TargetForm form : {TargetForm::Raw, TargetForm::Parsed}) {
                    cases.append({target, from, form});
                    plans.emplace_back(target, from, form);
                }
            }
        }
        std::thread([&plans] {
            for (const ResolvePlan &p : plans)
                p.complete();
        }).join();
        for (qsizetype i = 0; i < cases.size(); ++i) {
            const Case &c = cases[i];
            const VaultPath expected = reference.resolve(c.target, c.from, c.form);
            const VaultPath lazily = table.resolve(c.target, c.from, c.form);
            const VaultPath planned = table.resolve(plans[size_t(i)]);
            const VaultPath again = table.resolve(plans[size_t(i)]); // a complete plan stays as it is
            if (lazily != expected || planned != expected || again != expected)
                QFAIL(qPrintable(u"%1 from %2 (%3): %4 / %5 / %6, before: %7"_s.arg(c.target, c.from)
                                         .arg(c.form == TargetForm::Raw ? u"raw"_s : u"parsed"_s)
                                         .arg(lazily, planned, again, expected)));
        }
    }

    // The keys a prepared note carries are the ones derived when the note is put on the index's
    // thread: the same names, folders, link keys and tag counts.
    void preparedKeysAsOnTheIndexThread()
    {
        for (const GenFile &f : generatedVault(7, 80)) {
            if (isAttachment(f))
                continue;
            const ParsedNote parsed = markdown::parseNote(f.path, f.content);
            const std::unique_ptr<Entry> e = makeEntry(f.path, f.content, parsed);
            NoteKeys onWorker;
            std::thread([&] { onWorker = makeNoteKeys(*e, true); }).join();
            const NoteKeys here = makeNoteKeys(*e, false);
            const QByteArray what = f.path.toUtf8();
            QVERIFY2(onWorker.names.pathKey == pathKey(f.path), what.constData());
            QVERIFY2(onWorker.names.nameKey == noteNameKey(f.path), what.constData());
            QCOMPARE(onWorker.names.aliasKeys, here.names.aliasKeys);
            QCOMPARE(onWorker.folders, here.folders);
            QCOMPARE(onWorker.folders, noteFolderKeys(f.path));
            QCOMPARE(onWorker.nameLinkKeys, here.nameLinkKeys);
            QVERIFY(onWorker.nameLinkKeys.contains(noteNameKey(f.path)));
            QCOMPARE(onWorker.tagBlocks, here.tagBlocks);
            QCOMPARE(onWorker.plans.size(), e->links.size());
        }
    }

    // The loader's way — notes prepared on four threads, merged in its order (attachments,
    // journals newest first, the rest by path) with the recency of path order, a batch per slice
    // — ends in the state of the serial path: every record and map, every derived list and
    // search, the recency.
    void loadEqualsSerial_data()
    {
        QTest::addColumn<quint32>("seed");
        QTest::addColumn<int>("pages");
        QTest::newRow("small") << 11u << 40;
        QTest::newRow("medium") << 12u << 250;
        QTest::newRow("dense") << 13u << 400;
    }
    void loadEqualsSerial()
    {
        QFETCH(quint32, seed);
        QFETCH(int, pages);
        const QList<GenFile> files = generatedVault(seed, pages);
        const VaultConfig config;
        std::vector<PreparedNote> prepared = prepareOnThreads(files, config, 4);
        VaultIndex loaded(config);
        QHash<qsizetype, quint64> rank;
        const QList<qsizetype> order = loaderOrder(files, &rank);
        const quint64 base = loaded.reserveSequence(rank.size());
        for (qsizetype from = 0; from < order.size(); from += 16) {
            VaultIndex::Batch batch(loaded);
            for (qsizetype k = from; k < std::min(order.size(), from + 16); ++k) {
                const qsizetype i = order[k];
                if (isAttachment(files[i]))
                    loaded.update(files[i].path, QString());
                else
                    loaded.updatePrepared(std::move(prepared[size_t(i)]), base + rank.value(i));
            }
        }
        const std::unique_ptr<VaultIndex> serial = serialIndex(files, config);
        checkKeptPlans(loaded, u"loaded"_s);
        checkKeptPlans(*serial, u"serial"_s);
        compareStates(loaded, *serial, u"loaded"_s);
        compareIndexes(loaded, *serial, u"loaded"_s);
        QCOMPARE(describeRecency(loaded), describeRecency(*serial));
    }

    // Merged in the same order, a prepared note and the same note put with update() change the
    // index alike after every note: its state, version() and what changesSince() reports (the
    // targeted notifications of the views, 2026-11-18).
    void preparedMergeChangesLikeUpdate()
    {
        const QList<GenFile> files = generatedVault(21, 150);
        const VaultConfig config;
        std::vector<PreparedNote> prepared = prepareOnThreads(files, config, 3);
        VaultIndex byUpdate(config);
        VaultIndex byPrepared(config);
        QCOMPARE(byPrepared.version(), byUpdate.version());
        int step = 0;
        for (const qsizetype i : loaderOrder(files)) {
            const int before = byUpdate.version();
            QCOMPARE(byPrepared.version(), before);
            if (isAttachment(files[i])) {
                byUpdate.update(files[i].path, QString());
                byPrepared.update(files[i].path, QString());
            } else {
                byUpdate.update(files[i].path, files[i].content);
                byPrepared.updatePrepared(std::move(prepared[size_t(i)]));
            }
            QCOMPARE(byPrepared.version(), byUpdate.version());
            QCOMPARE(describeChanges(byPrepared.changesSince(before)), describeChanges(byUpdate.changesSince(before)));
            if (++step % 25 == 0)
                compareStates(byPrepared, byUpdate, u"step %1"_s.arg(step));
        }
        compareStates(byPrepared, byUpdate, u"merged"_s);
        checkKeptPlans(byPrepared, u"merged"_s);
        // Edits afterwards (the vault writer's prepared saves, Z1), removals and renames — links
        // that come to resolve to nothing, or to a page again: the same again.
        QRandomGenerator rng(99);
        QStringList present;
        for (const GenFile &f : files) {
            if (!isAttachment(f))
                present.append(f.path);
        }
        for (int edit = 0; edit < 90; ++edit) {
            const QString path = pick(rng, present);
            const int kind = rng.bounded(6);
            const int before = byUpdate.version();
            if (kind == 0) {
                byUpdate.remove(path);
                byPrepared.remove(path);
                present.removeOne(path);
            } else if (kind == 1) {
                const QString to = inFolder(pick(rng, kFolders), pick(rng, kNames) + u".md"_s);
                if (present.contains(to))
                    continue;
                const QString text = randomLine(rng) + u'\n';
                byUpdate.rename(path, to, text);
                byPrepared.rename(path, to, text);
                present.removeOne(path);
                present.append(to);
            } else {
                const QString text = randomHeader(rng) + randomLine(rng) + u'\n' + randomLine(rng) + u'\n';
                byUpdate.update(path, text);
                PreparedNote p;
                std::thread([&] { p = VaultIndex::prepare(path, text, config); }).join();
                byPrepared.updatePrepared(std::move(p));
            }
            QCOMPARE(byPrepared.version(), byUpdate.version());
            QCOMPARE(describeChanges(byPrepared.changesSince(before)), describeChanges(byUpdate.changesSince(before)));
            if (edit % 15 == 0) {
                checkKeptPlans(byPrepared, u"edit %1"_s.arg(edit));
                checkKeptPlans(byUpdate, u"edit %1"_s.arg(edit));
            }
        }
        checkKeptPlans(byPrepared, u"edited"_s);
        checkKeptPlans(byUpdate, u"edited"_s);
        compareStates(byPrepared, byUpdate, u"edited"_s);
        compareIndexes(byPrepared, byUpdate, u"edited"_s);
    }
};

QTEST_GUILESS_MAIN(TestIndexMerge)
#include "tst_index_merge.moc"
