#include "speech/load.h"

#include <QElapsedTimer>
#include <QFile>
#include <QList>

#include <algorithm>
#include <cmath>

namespace brulion::speech {

namespace {

QByteArray readSmall(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    // (A /proc file has no size: read what comes, enough for the first lines.)
    return f.read(4096);
}

qint64 monotonicMs()
{
    static QElapsedTimer clock;
    if (!clock.isValid())
        clock.start();
    return clock.elapsed();
}

} // namespace

std::optional<CpuTimes> parseProcStat(QByteArrayView text)
{
    const qsizetype eol = text.indexOf('\n');
    const QByteArrayView line = eol < 0 ? text : text.first(eol);
    const QList<QByteArray> fields = line.toByteArray().simplified().split(' ');
    if (fields.size() < 5 || fields.first() != "cpu")
        return std::nullopt;
    // user nice system idle iowait irq softirq steal [guest guest_nice: inside user and nice]
    qint64 values[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    const qsizetype n = std::min<qsizetype>(8, fields.size() - 1);
    for (qsizetype i = 0; i < n; ++i) {
        bool ok = false;
        values[i] = fields.at(i + 1).toLongLong(&ok);
        if (!ok || values[i] < 0)
            return std::nullopt;
    }
    CpuTimes t;
    t.busy = values[0] + values[1] + values[2] + values[5] + values[6] + values[7];
    t.total = t.busy + values[3] + values[4];
    return t;
}

qint64 parseProcessTime(QByteArrayView text)
{
    // "1234 (name (with) spaces) S 1 …": the fields after the name's last ')'.
    const qsizetype close = text.lastIndexOf(')');
    if (close < 0)
        return -1;
    const QList<QByteArray> fields = text.sliced(close + 1).toByteArray().simplified().split(' ');
    // fields[0] is the state (field 3); utime and stime are fields 14 and 15.
    if (fields.size() < 13)
        return -1;
    bool okUser = false;
    bool okSystem = false;
    const qint64 user = fields.at(11).toLongLong(&okUser);
    const qint64 system = fields.at(12).toLongLong(&okSystem);
    if (!okUser || !okSystem || user < 0 || system < 0)
        return -1;
    return user + system;
}

CpuSample sampleCpu(const QString &statFile, const QString &selfFile)
{
    CpuSample s;
    s.atMs = monotonicMs();
    s.all = parseProcStat(readSmall(statFile));
    s.self = parseProcessTime(readSmall(selfFile));
    return s;
}

double othersBusy(const CpuSample &before, const CpuSample &after, int cpus)
{
    if (!before.isValid() || !after.isValid() || cpus <= 0)
        return -1;
    const qint64 total = after.all->total - before.all->total;
    const qint64 busy = after.all->busy - before.all->busy;
    const qint64 self = after.self - before.self;
    if (total <= 0 || busy < 0 || self < 0)
        return -1;
    const double others = double(std::max<qint64>(0, busy - self)) / double(total);
    return std::clamp(others, 0.0, 1.0) * cpus;
}

int speechThreads(int fastCores, int cpus, double busy)
{
    int n = fastCores > 0 ? fastCores : std::max(1, cpus / 2);
    n = std::min(n, MaxSpeechThreads);
    if (busy >= 0 && cpus > 0) {
        const int free = cpus - int(std::lround(busy));
        n = std::min(n, free);
    }
    return std::max(1, n);
}

} // namespace brulion::speech
