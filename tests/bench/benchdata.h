// Shared generators and helpers of the QBENCHMARK suites (tests/bench, scripts/bench.sh).
//
// Everything is deterministic (fixed seeds, fixed "now"), so two builds measure the same input.
// Content is realistic Polish note text: diacritics, wiki links to other pages, #tags, tasks with
// 📅/✅ dates, ⏰ reminders, 🔁 rules, block ids and references, tables, calc and code blocks.
//
// RULE: at most one call that draws from an Rng per statement (sentence(), pageTarget(), r.pick()…
// all count). C++ leaves the order of the operands of `a + b` and of a call's arguments
// unspecified, and GCC evaluates them in a different order on x86_64 and aarch64, so
// `sentence(r) + isoDate(d.addDays(r.below(9)))` generated different vaults on the laptop and on
// the Pi. Draw into named locals first, then build the string. reportInput() prints a digest of
// each generated input ("[bench] input …"), so runs on two machines can be checked for identity.
//
// Profiles: BRULION_BENCH_PROFILE=small (the Raspberry Pi / phone-class run) drops the largest
// sizes; anything else (default "full") runs them all. Suites call bench::sizes() to pick rows.
#pragma once

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include <initializer_list>
#include <memory>
#include <type_traits>
#include <utility>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

namespace bench {

using namespace Qt::StringLiterals;

/// The small profile (BRULION_BENCH_PROFILE=small): Pi / phone-class machines.
inline bool smallProfile()
{
    return qEnvironmentVariable("BRULION_BENCH_PROFILE") == u"small"_s;
}

/// `full` normally, `small` in the small profile.
inline QList<int> sizes(std::initializer_list<int> full, std::initializer_list<int> small)
{
    return smallProfile() ? QList<int>(small) : QList<int>(full);
}

/// Fixed "now" of all suites: Saturday 2026-10-17 10:00 local time.
inline QDateTime fixedNow()
{
    return QDateTime(QDate(2026, 10, 17), QTime(10, 0));
}
inline QDate fixedToday()
{
    return fixedNow().date();
}

/// splitmix64 — small, fast, deterministic.
class Rng {
public:
    explicit Rng(quint64 seed) : m_state(seed) { }
    quint64 next()
    {
        quint64 z = (m_state += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }
    int below(int n) { return n <= 0 ? 0 : int(next() % quint64(n)); }
    int range(int lo, int hi) { return lo + below(hi - lo + 1); }
    bool chance(int percent) { return below(100) < percent; }
    const QString &pick(const QStringList &list) { return list[below(int(list.size()))]; }

private:
    quint64 m_state;
};

// The word tables keep their aligned columns.
// clang-format off
inline const QStringList &words()
{
    static const QStringList w = {
        u"notatka"_s,   u"zadanie"_s,    u"żółw"_s,       u"łódź"_s,      u"projekt"_s,    u"spotkanie"_s,
        u"raport"_s,    u"zakupy"_s,     u"kawa"_s,       u"ogród"_s,     u"dom"_s,        u"praca"_s,
        u"źródło"_s,    u"gęś"_s,        u"jabłko"_s,     u"książka"_s,   u"rower"_s,      u"wakacje"_s,
        u"budżet"_s,    u"faktura"_s,    u"umowa"_s,      u"klient"_s,    u"szkolenie"_s,  u"prezentacja"_s,
        u"zespół"_s,    u"termin"_s,     u"pomysł"_s,     u"plan"_s,      u"lista"_s,      u"pytanie"_s,
        u"odpowiedź"_s, u"wiadomość"_s,  u"telefon"_s,    u"mama"_s,      u"tata"_s,       u"dzieci"_s,
        u"szkoła"_s,    u"lekarz"_s,     u"apteka"_s,     u"samochód"_s,  u"przegląd"_s,   u"ubezpieczenie"_s,
        u"podatek"_s,   u"rachunek"_s,   u"prąd"_s,       u"gaz"_s,       u"woda"_s,       u"czynsz"_s,
        u"remont"_s,    u"kuchnia"_s,    u"łazienka"_s,   u"farba"_s,     u"ściana"_s,     u"podłoga"_s,
        u"wycieczka"_s, u"góry"_s,       u"morze"_s,      u"pociąg"_s,    u"bilet"_s,      u"hotel"_s,
        u"przepis"_s,   u"obiad"_s,      u"kolacja"_s,    u"śniadanie"_s, u"chleb"_s,      u"masło"_s,
        u"ser"_s,       u"mleko"_s,      u"jajka"_s,      u"pomidory"_s,  u"ziemniaki"_s,  u"cebula"_s,
        u"czosnek"_s,   u"marchew"_s,    u"truskawki"_s,  u"czereśnie"_s, u"wiśnie"_s,     u"śliwki"_s,
        u"gruszki"_s,   u"orzechy"_s,    u"miód"_s,       u"herbata"_s,   u"sok"_s,        u"woda"_s,
        u"bieganie"_s,  u"siłownia"_s,   u"basen"_s,      u"spacer"_s,    u"pies"_s,       u"kot"_s,
        u"weterynarz"_s, u"urodziny"_s,  u"prezent"_s,    u"kwiaty"_s,    u"tort"_s,       u"goście"_s,
        u"przyjęcie"_s, u"muzyka"_s,     u"film"_s,       u"serial"_s,    u"teatr"_s,      u"koncert"_s,
        u"wystawa"_s,   u"muzeum"_s,     u"biblioteka"_s, u"artykuł"_s,   u"wpis"_s,       u"blog"_s,
        u"kod"_s,       u"błąd"_s,       u"poprawka"_s,   u"wydanie"_s,   u"wersja"_s,     u"testy"_s,
        u"serwer"_s,    u"kopia"_s,      u"synchronizacja"_s, u"hasło"_s, u"konto"_s,      u"bank"_s,
        u"przelew"_s,   u"oszczędności"_s, u"inwestycja"_s, u"emerytura"_s, u"zdrowie"_s, u"sen"_s,
        u"dieta"_s,     u"witaminy"_s,   u"wizyta"_s,     u"badanie"_s,   u"wynik"_s,      u"recepta"_s,
        u"się"_s,       u"jest"_s,       u"oraz"_s,       u"ale"_s,       u"że"_s,         u"też"_s,
        u"bardzo"_s,    u"jutro"_s,      u"dzisiaj"_s,    u"później"_s,   u"wcześniej"_s,  u"zawsze"_s,
        u"trzeba"_s,    u"można"_s,      u"warto"_s,      u"chyba"_s,     u"może"_s,       u"właśnie"_s,
        u"w"_s,         u"z"_s,          u"na"_s,         u"do"_s,        u"o"_s,          u"i"_s,
        u"dla"_s,       u"po"_s,         u"przed"_s,      u"między"_s,    u"pod"_s,        u"nad"_s,
    };
    return w;
}

inline const QStringList &tagNames()
{
    static const QStringList t = {u"praca"_s,  u"dom"_s,        u"zakupy"_s,         u"projekt/brulion"_s,
                                  u"zdrowie"_s, u"ogród"_s,     u"finanse"_s,        u"książki"_s,
                                  u"podróże"_s, u"rodzina"_s,   u"pomysły"_s,        u"do-przeczytania"_s,
                                  u"ważne"_s,   u"spotkania"_s, u"projekt/kuchnia"_s, u"nauka"_s};
    return t;
}
// clang-format on

inline QString capitalized(QString s)
{
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

/// "Kupić farbę do ogrodu i zadzwonić do mamy." — `minWords`..`maxWords` words.
inline QString sentence(Rng &r, int minWords = 5, int maxWords = 14)
{
    const int n = r.range(minWords, maxWords);
    QString s;
    for (int i = 0; i < n; ++i) {
        if (i)
            s += u' ';
        s += r.pick(words());
        if (i + 1 < n && r.chance(6))
            s += u',';
    }
    QString end = u"."_s;
    if (!r.chance(85))
        end = r.chance(50) ? u"!"_s : u"?"_s;
    return capitalized(s) + end;
}

inline QString isoDate(QDate d)
{
    return d.toString(u"yyyy-MM-dd"_s);
}

/// A link target: "Strona N" with a skewed distribution (a few popular pages get many links).
inline QString pageTarget(Rng &r, int pages)
{
    if (pages <= 0)
        return u"Strona 0"_s;
    const bool popular = r.chance(25);
    const int n = popular ? r.below(qMin(10, pages)) : r.below(pages);
    return u"Strona %1"_s.arg(n);
}

/// Text of one list item (without the "- " marker): plain text, tasks with dates, reminders,
/// links, tags, emphasis, URLs, block ids and references.
inline QString itemText(Rng &r, int pages, int noteIndex, int blockIndex)
{
    const QDate base = fixedToday().addDays(r.range(-60, 60));
    QString t;
    const int kind = r.below(100);
    if (kind < 14) {
        t = u"[ ] "_s + sentence(r, 3, 8) + u" 📅 "_s + isoDate(base);
    } else if (kind < 18) {
        const QString text = sentence(r, 3, 8);
        const int daysAgo = r.below(10);
        t = u"[x] "_s + text + u" ✅ "_s + isoDate(base.addDays(-daysAgo));
    } else if (kind < 20) {
        t = u"[ ] "_s + sentence(r, 3, 6) + u" 📅 "_s + isoDate(base) + u" 🔁 every week"_s;
    } else if (kind < 24) {
        const QString text = sentence(r, 3, 8);
        const int hour = r.range(7, 21);
        const bool onTheHour = r.chance(50);
        t = text + u" ⏰ "_s + isoDate(base) + u" %1:%2"_s.arg(hour).arg(onTheHour ? u"00"_s : u"30"_s);
    } else if (kind < 26) {
        t = u"TODO "_s + sentence(r, 3, 8);
    } else {
        t = sentence(r, 4, 16);
    }
    if (r.chance(22))
        t += u" [["_s + pageTarget(r, pages) + u"]]"_s;
    if (r.chance(18))
        t += u" #"_s + r.pick(tagNames());
    if (r.chance(8))
        t = u"**"_s + t + u"**"_s;
    if (r.chance(3))
        t += u" https://example.com/%1/%2"_s.arg(noteIndex).arg(blockIndex);
    if (r.chance(3)) {
        const int refPage = r.below(qMax(1, pages));
        const int refBlock = r.below(5);
        t += u" ((b%1x%2))"_s.arg(refPage).arg(refBlock);
    }
    if (blockIndex < 5 && r.chance(20))
        t += u" ^b%1x%2"_s.arg(noteIndex).arg(blockIndex);
    return t;
}

/// A Logseq-style outline of `blocks` list items, nested up to `maxDepth`.
inline QString outlineNote(Rng &r, int blocks, int pages, int noteIndex = 0, int maxDepth = 4)
{
    QString out;
    out.reserve(blocks * 80);
    int depth = 0;
    for (int b = 0; b < blocks; ++b) {
        const int move = r.below(10);
        if (move < 3 && depth + 1 < maxDepth && b > 0)
            ++depth;
        else if (move < 5 && depth > 0)
            depth -= r.range(1, depth);
        out += QString(depth * 2, u' ') + u"- "_s + itemText(r, pages, noteIndex, b) + u'\n';
        if (r.chance(4))
            out += QString(depth * 2 + 2, u' ') + u"collapsed:: true\n"_s;
    }
    return out;
}

inline QString tableBlock(Rng &r, int rows)
{
    QString t = u"| Pozycja | Ilość | Cena | Razem |\n|---|--:|--:|--:|\n"_s;
    for (int i = 0; i < rows; ++i) {
        const QString item = capitalized(r.pick(words()));
        const int quantity = r.range(1, 9);
        const int zloty = r.range(1, 120);
        const int grosze = r.range(10, 99);
        t += u"| %1 | %2 | %3,%4 zł | =[Ilość]*[Cena] |\n"_s.arg(item).arg(quantity).arg(zloty).arg(grosze);
    }
    t += u"| Suma | | | =sum(above) |\n"_s;
    return t;
}

inline QString calcBlock(Rng &r, int lines)
{
    QString c = u"```calc\n"_s;
    for (int i = 0; i < lines; ++i) {
        switch (i % 5) {
        case 0: c += u"czynsz = %1 zł\n"_s.arg(r.range(1500, 3200)); break;
        case 1: {
            const int zloty = r.range(80, 300);
            const int grosze = r.range(10, 99);
            c += u"prąd = %1,%2 zł + 23%\n"_s.arg(zloty).arg(grosze);
            break;
        }
        case 2: c += u"dojazd = %1 km * 0,85 zł/km\n"_s.arg(r.range(10, 400)); break;
        case 3: c += u"Jedzenie: %1 zł * 4\n"_s.arg(r.range(200, 500)); break;
        default: c += u"suma\n"_s;
        }
    }
    return c + u"```\n"_s;
}

/// A document-style page of about `targetBytes` UTF-8 bytes: optional frontmatter, headings,
/// paragraphs, lists with tasks/links/tags, sometimes a table, a calc block, code, a callout.
inline QString documentNote(Rng &r, int noteIndex, int pages, qsizetype targetBytes)
{
    QString out;
    qsizetype bytes = 0;
    const auto add = [&](const QString &piece) {
        out += piece;
        bytes += piece.toUtf8().size();
    };
    if (r.chance(20)) {
        const QString tag1 = r.pick(tagNames());
        const QString tag2 = r.pick(tagNames());
        QString fm = u"---\ntags: [%1, %2]\n"_s.arg(tag1, tag2);
        if (r.chance(50))
            fm += u"aliases: [Alias %1]\n"_s.arg(noteIndex);
        add(fm + u"status: aktywny\n---\n"_s);
    }
    const QString title1 = capitalized(r.pick(words()));
    const QString title2 = r.pick(words());
    add(u"# "_s + title1 + u' ' + title2 + u" %1\n\n"_s.arg(noteIndex));
    int section = 0;
    while (bytes < targetBytes) {
        const int kind = r.below(100);
        if (kind < 30) {
            QString p;
            const int n = r.range(2, 6);
            for (int s = 0; s < n; ++s) {
                if (s)
                    p += u' ';
                p += sentence(r);
                if (r.chance(15))
                    p += u" Zobacz [["_s + pageTarget(r, pages) + u"]]."_s;
                if (r.chance(10))
                    p += u" #"_s + r.pick(tagNames());
            }
            add(p + u"\n\n"_s);
        } else if (kind < 65) {
            const int blocks = r.range(3, 12);
            add(outlineNote(r, blocks, pages, noteIndex, 3) + u'\n');
        } else if (kind < 75) {
            add(u"## "_s + capitalized(r.pick(words())) + u" %1\n\n"_s.arg(++section));
        } else if (kind < 80) {
            const int rows = r.range(3, 8);
            add(tableBlock(r, rows) + u'\n');
        } else if (kind < 84) {
            const int lines = r.range(4, 10);
            add(calcBlock(r, lines) + u'\n');
        } else if (kind < 88) {
            add(u"```cpp\nint main() {\n    return %1;\n}\n```\n\n"_s.arg(r.below(100)));
        } else if (kind < 92) {
            const QString title = sentence(r, 2, 5);
            const QString body = sentence(r);
            add(u"> [!note] "_s + title + u"\n> "_s + body + u"\n\n"_s);
        } else {
            const QString text = sentence(r, 3, 8);
            const QDate due = fixedToday().addDays(r.range(-20, 40));
            const QDate remind = fixedToday().addDays(r.range(-5, 20));
            add(u"- [ ] "_s + text + u" 📅 "_s + isoDate(due) + u" ⏰ "_s + isoDate(remind) + u" 09:00\n\n"_s);
        }
    }
    return out;
}

/// '<span>a</span>' × n on one line (pathological raw inline HTML).
inline QString inlineHtml(int n)
{
    QString s;
    s.reserve(n * 15 + 1);
    for (int i = 0; i < n; ++i)
        s += u"<span>a</span>"_s;
    return s + u'\n';
}

/// A flat list of `items` short items.
inline QString longList(int items)
{
    Rng r(7);
    QString s;
    s.reserve(items * 48);
    for (int i = 0; i < items; ++i)
        s += u"- "_s + sentence(r, 3, 6) + u'\n';
    return s;
}

/// `lines` paragraph lines, each with three [[links]] and two #tags.
inline QString manyLinksTags(int lines)
{
    Rng r(11);
    QString s;
    for (int i = 0; i < lines; ++i) {
        const int page1 = r.below(500);
        const int page2 = r.below(500);
        const QString tag1 = r.pick(tagNames());
        const QString tag2 = r.pick(tagNames());
        s += u"Wpis %1 [[Strona %2]] i [[Strona %3|alias]] oraz [[Projekty/Dom#Kuchnia]] #%4 #%5 koniec.\n"_s.arg(i)
                     .arg(page1)
                     .arg(page2)
                     .arg(tag1, tag2);
    }
    return s;
}

struct VaultFile {
    QString path;
    QString content;
};

/// A synthetic vault of `notes` notes: one third daily notes (outlines, newest 2026-10-17
/// backwards), the rest pages "Strony/Folder F/Strona N.md" (50 folders) with document content.
/// Sizes: 70 % 0.3–1.5 KB, 25 % 1.5–5 KB, 5 % 5–20 KB (mean ≈ 2 KB).
inline QList<VaultFile> makeVault(int notes, quint64 seed = 42)
{
    Rng r(seed);
    QList<VaultFile> out;
    out.reserve(notes);
    const int pages = notes - notes / 3;
    int journal = 0;
    int page = 0;
    for (int i = 0; i < notes; ++i) {
        const int sizeClass = r.below(100);
        const int bytes = sizeClass < 70   ? r.range(300, 1500)
                          : sizeClass < 95 ? r.range(1500, 5000)
                                           : r.range(5000, 20000);
        if (i % 3 == 0) {
            const QDate d = fixedToday().addDays(-journal++);
            out.append({u"journals/%1.md"_s.arg(isoDate(d)), outlineNote(r, qMax(3, bytes / 80), pages, i)});
        } else {
            const int n = page++;
            out.append({u"Strony/Folder %1/Strona %2.md"_s.arg(n % 50).arg(n), documentNote(r, n, pages, bytes)});
        }
    }
    return out;
}

/// Write `files` below `root` (folders created). Returns false on the first failure.
inline bool writeVault(const QString &root, const QList<VaultFile> &files)
{
    for (const VaultFile &f : files) {
        const QString abs = root + u'/' + f.path;
        if (!QDir().mkpath(QFileInfo(abs).absolutePath()))
            return false;
        QFile file(abs);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const QByteArray bytes = f.content.toUtf8();
        if (file.write(bytes) != bytes.size())
            return false;
    }
    return true;
}

inline qint64 totalBytes(const QList<VaultFile> &files)
{
    qint64 n = 0;
    for (const VaultFile &f : files)
        n += f.content.toUtf8().size();
    return n;
}

/// SHA-256 of a generated input (first 16 hex digits); for a vault over every path and content.
inline QByteArray digest(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
}
inline QByteArray digest(const QList<VaultFile> &files)
{
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (const VaultFile &f : files) {
        h.addData(f.path.toUtf8());
        h.addData(QByteArrayView("\n", 1));
        h.addData(f.content.toUtf8());
        h.addData(QByteArrayView("\0", 1));
    }
    return h.result().toHex().left(16);
}

/// Print "[bench] input <name>: <bytes> bytes, sha256 <digest>" once per name and process.
/// Inputs must be byte-identical on every machine (see the RULE at the top): compare these lines
/// of two runs (scripts/bench.sh shows them; raw/*.txt keeps them).
template<class Input>
void reportInput(const QString &name, const Input &input)
{
    static QSet<QString> reported;
    if (reported.contains(name))
        return;
    reported.insert(name);
    qint64 bytes = 0;
    if constexpr (std::is_same_v<Input, QString>)
        bytes = input.toUtf8().size();
    else
        bytes = totalBytes(input);
    qInfo("[bench] input %s: %lld bytes, sha256 %s", qPrintable(name), qlonglong(bytes), digest(input).constData());
}

/// A temporary folder for vaults on disk: below $BRULION_BENCH_TMP when set (scripts/bench.sh
/// points it into the build dir, so file benchmarks hit a real disk, not a tmpfs /tmp), else
/// below QDir::tempPath().
inline std::unique_ptr<QTemporaryDir> makeTempDir()
{
    QString base = qEnvironmentVariable("BRULION_BENCH_TMP");
    if (base.isEmpty())
        base = QDir::tempPath();
    QDir().mkpath(base);
    auto dir = std::make_unique<QTemporaryDir>(base + u"/brulion-bench-XXXXXX"_s);
    if (!dir->isValid())
        qFatal("cannot create a temporary folder below %s", qPrintable(base));
    return dir;
}

/// Resident set size of this process in KiB (Linux /proc; 0 elsewhere).
inline qint64 rssKb()
{
    QFile f(u"/proc/self/status"_s);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray &line : f.readAll().split('\n')) {
        if (line.startsWith("VmRSS:"))
            return line.mid(6).trimmed().split(' ').value(0).toLongLong();
    }
    return 0;
}

/// Bytes currently allocated by malloc (glibc: in-use arena chunks + mmapped blocks; -1 elsewhere).
/// Unlike RSS it drops when memory is freed, so deltas around one build are meaningful.
inline qint64 heapBytes()
{
#if defined(__GLIBC__)
    const struct mallinfo2 mi = mallinfo2();
    return qint64(mi.uordblks) + qint64(mi.hblkhd);
#else
    return -1;
#endif
}

/// Report a manually timed result (for work that needs setup per run or an event loop).
inline void reportMs(const QElapsedTimer &timer)
{
    QTest::setBenchmarkResult(qreal(timer.nsecsElapsed()) / 1e6, QTest::WalltimeMilliseconds);
}

/// Time `run()` once after `setup()` (not timed) and report it.
template<class Setup, class Run>
void timeOnce(Setup &&setup, Run &&run)
{
    setup();
    QElapsedTimer t;
    t.start();
    run();
    reportMs(t);
}

} // namespace bench
