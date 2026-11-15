#include "ai/cpus.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <functional>

using namespace Qt::StringLiterals;

namespace brulion::ai {

namespace {

/// The most cores read: far above any phone or PC (and llama.cpp's own limit of threads).
constexpr int MaxCores = 1024;

/// A small sysfs file's first line, trimmed ("" when there is none).
QByteArray readLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(256).trimmed();
}

qint64 readNumber(const QString &path)
{
    bool ok = false;
    const qint64 value = readLine(path).toLongLong(&ok);
    return ok && value > 0 ? value : 0;
}

/// "0-3,5": the indexes listed, in ascending order, without repeats; nothing for a list that is
/// not one.
QList<int> parseList(const QByteArray &text)
{
    QList<int> out;
    for (const QByteArray &part : text.split(',')) {
        const QByteArray item = part.trimmed();
        if (item.isEmpty())
            continue;
        const qsizetype dash = item.indexOf('-');
        bool okFrom = false;
        bool okTo = false;
        const int from = (dash < 0 ? item : item.left(dash)).toInt(&okFrom);
        const int to = dash < 0 ? from : item.mid(dash + 1).toInt(&okTo);
        if (!okFrom || (dash >= 0 && !okTo) || from < 0 || to < from || to >= MaxCores)
            return {};
        for (int i = from; i <= to; ++i)
            out.append(i);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

/// The cores whose `speed` is at the levels above the widest gap between neighbouring levels,
/// when that gap is at least `gap` (the faster level `gap` times the slower one) and leaves two
/// cores at least; else all of them.
QList<CpuCore> aboveWidestGap(const QList<CpuCore> &cores, qint64 (*speed)(const CpuCore &), double gap)
{
    QList<qint64> levels;
    for (const CpuCore &core : cores)
        levels.append(speed(core));
    std::sort(levels.begin(), levels.end(), std::greater<>());
    levels.erase(std::unique(levels.begin(), levels.end()), levels.end());

    double widest = 1.0;
    qint64 cut = 0;
    for (qsizetype i = 0; i + 1 < levels.size(); ++i) {
        const auto above = std::ranges::count_if(cores, [&](const CpuCore &c) { return speed(c) >= levels[i]; });
        const double ratio = double(levels[i]) / double(levels[i + 1]);
        if (above >= 2 && ratio > widest) {
            widest = ratio;
            cut = levels[i];
        }
    }
    if (widest < gap)
        return cores;
    QList<CpuCore> kept;
    for (const CpuCore &core : cores) {
        if (speed(core) >= cut)
            kept.append(core);
    }
    return kept;
}

} // namespace

QList<CpuCore> cpuCores(const QString &root)
{
    const QDir dir(root);
    if (!dir.exists())
        return {};
    QList<int> indexes = parseList(readLine(dir.filePath(u"online"_s)));
    if (indexes.isEmpty()) {
        // No list of the online cores: the cores' own folders.
        static const QRegularExpression name(u"^cpu(\\d+)$"_s);
        for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QRegularExpressionMatch m = name.match(entry);
            bool ok = false;
            const int index = m.hasMatch() ? m.captured(1).toInt(&ok) : -1;
            if (ok && index >= 0 && index < MaxCores)
                indexes.append(index);
        }
        std::sort(indexes.begin(), indexes.end());
    }
    QList<CpuCore> cores;
    for (const int index : std::as_const(indexes)) {
        const QString core = dir.filePath(u"cpu%1"_s.arg(index));
        CpuCore c;
        c.index = index;
        c.capacity = int(std::min<qint64>(readNumber(core + u"/cpu_capacity"_s), 1 << 20));
        c.maxKHz = readNumber(core + u"/cpufreq/cpuinfo_max_freq"_s);
        // (thread_siblings_list: the older name of the same list.)
        QList<int> siblings = parseList(readLine(core + u"/topology/core_cpus_list"_s));
        if (siblings.isEmpty())
            siblings = parseList(readLine(core + u"/topology/thread_siblings_list"_s));
        c.firstSibling = siblings.contains(index) ? siblings.first() : -1;
        cores.append(c);
    }
    return cores;
}

QList<int> computeCores(const QList<CpuCore> &cores)
{
    if (cores.isEmpty() || std::ranges::any_of(cores, [](const CpuCore &c) { return c.capacity <= 0; }))
        return {}; // not told how fast they are (an older kernel): the caller's rule

    // One logical processor per physical core: an SMT sibling only halves its core's time.
    QList<CpuCore> physical;
    for (const CpuCore &core : cores) {
        if (core.firstSibling < 0 || core.firstSibling == core.index
            || !std::ranges::any_of(cores, [&](const CpuCore &c) { return c.index == core.firstSibling; }))
            physical.append(core);
    }

    const auto capacityOf = [](const CpuCore &c) { return qint64(c.capacity); };
    const auto clockOf = [](const CpuCore &c) { return c.maxKHz; };
    QList<CpuCore> kept;
    const bool alike =
            std::ranges::all_of(physical, [&](const CpuCore &c) { return c.capacity == physical[0].capacity; });
    if (!alike) {
        // A slow core has well under two thirds of the next faster one's capacity (an Arm
        // Cortex-A5xx next to an A7xx: a third); fast cores of two kinds differ by less.
        kept = aboveWidestGap(physical, capacityOf, 1.5);
    } else if (std::ranges::all_of(physical, [](const CpuCore &c) { return c.maxKHz > 0; })) {
        // Alike as far as the kernel says — one that does not know their capacities says 1024 for
        // each —, their clocks may still differ: slow cores run at three quarters of the fast
        // ones' clock or less (an A520 at 1.95 GHz beside A720s at 2.6), fast cores of two kinds
        // closer (Oryon cores at 3.53 and 4.32 GHz).
        kept = aboveWidestGap(physical, clockOf, 1.25);
    } else {
        kept = physical;
    }
    std::ranges::sort(kept, [](const CpuCore &a, const CpuCore &b) {
        if (a.capacity != b.capacity)
            return a.capacity > b.capacity;
        if (a.maxKHz != b.maxKHz)
            return a.maxKHz > b.maxKHz;
        return a.index < b.index;
    });
    QList<int> out;
    for (const CpuCore &core : std::as_const(kept)) {
        if (out.size() == MaxComputeThreads)
            break;
        out.append(core.index);
    }
    std::ranges::sort(out);
    return out;
}

QString coreList(const QList<int> &cores)
{
    QStringList parts;
    for (qsizetype i = 0; i < cores.size();) {
        qsizetype j = i;
        while (j + 1 < cores.size() && cores[j + 1] == cores[j] + 1)
            ++j;
        parts << (j == i ? QString::number(cores[i]) : u"%1-%2"_s.arg(cores[i]).arg(cores[j]));
        i = j + 1;
    }
    return parts.join(u',');
}

} // namespace brulion::ai
