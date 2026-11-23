#include "chattools.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <array>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

enum class Part { Read, Edit, Delete, Move, Search, Execute, Fetch, Other };
constexpr int PartCount = int(Part::Other) + 1;
/// The kinds named in a summary; the rest are counted as other steps.
constexpr int MaxNamed = 3;

Part partOf(const QString &kind)
{
    if (kind == u"read")
        return Part::Read;
    if (kind == u"edit")
        return Part::Edit;
    if (kind == u"delete")
        return Part::Delete;
    if (kind == u"move")
        return Part::Move;
    if (kind == u"search")
        return Part::Search;
    if (kind == u"execute")
        return Part::Execute;
    if (kind == u"fetch")
        return Part::Fetch;
    return Part::Other; // think, switch_mode, other, a kind of a newer protocol
}

/// A part of the summary: what `n` calls of one kind did (a part after the first, lower case).
QString partText(Part part, int n, bool alone)
{
    switch (part) {
    case Part::Read: return QCoreApplication::translate("ChatTools", "read %n file(s)", nullptr, n);
    case Part::Edit: return QCoreApplication::translate("ChatTools", "edited %n file(s)", nullptr, n);
    case Part::Delete: return QCoreApplication::translate("ChatTools", "deleted %n file(s)", nullptr, n);
    case Part::Move: return QCoreApplication::translate("ChatTools", "moved %n file(s)", nullptr, n);
    case Part::Search: return QCoreApplication::translate("ChatTools", "searched %n time(s)", nullptr, n);
    case Part::Execute: return QCoreApplication::translate("ChatTools", "ran %n command(s)", nullptr, n);
    case Part::Fetch: return QCoreApplication::translate("ChatTools", "fetched %n web page(s)", nullptr, n);
    case Part::Other:
        return alone ? QCoreApplication::translate("ChatTools", "took %n step(s)", nullptr, n)
                     : QCoreApplication::translate("ChatTools", "%n other step(s)", nullptr, n);
    }
    return QString();
}

} // namespace

QString toolSummary(const QList<ToolStep> &steps)
{
    std::array<int, PartCount> counts{};
    QList<Part> order; // the kinds in the order the turn first did them
    int failed = 0;
    for (const ToolStep &step : steps) {
        if (step.status == u"failed") {
            ++failed; // (not counted as done)
            continue;
        }
        const Part part = partOf(step.kind);
        if (counts[size_t(part)]++ == 0)
            order.append(part);
    }
    // Other steps last; beyond the first few kinds named, the rest are other steps too.
    if (order.removeOne(Part::Other))
        order.append(Part::Other);
    int others = 0;
    while (order.size() > MaxNamed)
        others += counts[size_t(order.takeLast())];
    QStringList parts;
    for (const Part part : order)
        parts << partText(part, counts[size_t(part)], order.size() == 1 && others == 0);
    if (others > 0)
        parts << partText(Part::Other, others, parts.isEmpty());
    if (failed > 0)
        parts << QCoreApplication::translate("ChatTools", "%n step(s) failed", nullptr, failed);
    QString text = parts.join(u" · "_s);
    if (!text.isEmpty())
        text[0] = text.at(0).toUpper();
    return text;
}

QString toolSummary(const QVariantList &tools)
{
    QList<ToolStep> steps;
    steps.reserve(tools.size());
    for (const QVariant &tool : tools) {
        const QVariantMap call = tool.toMap();
        steps.append({call.value(u"kind"_s).toString(), call.value(u"status"_s).toString()});
    }
    return toolSummary(steps);
}

QString toolsAnswer(const QList<ToolStep> &steps, const QStringList &titles)
{
    QStringList lines;
    for (const QString &title : titles) {
        if (!title.trimmed().isEmpty())
            lines << u"- "_s + title.trimmed();
    }
    const QString summary = toolSummary(steps);
    if (lines.isEmpty())
        return summary.isEmpty() ? QCoreApplication::translate("ChatTools", "Done.") : summary;
    return (summary.isEmpty() ? QCoreApplication::translate("ChatTools", "Done:") : summary) + u"\n\n"_s
           + lines.join(u'\n');
}

} // namespace brulion::shell
