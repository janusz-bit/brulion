// A build without Luau's type checker (BRULION_LUAU_CHECK off: the browser): typecheck.h's
// functions with nothing to check with.
#include "extensions/typecheck.h"

#include <QFile>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

bool typeCheckerAvailable() { return false; }

QList<TypeProblem> typeCheck(const QString &source, int timeLimitMs)
{
    Q_UNUSED(source);
    Q_UNUSED(timeLimitMs);
    return {};
}

QString apiDefinitions()
{
    QFile f(u":/extensions/b.d.luau"_s);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QString describeTypeProblems(const QList<TypeProblem> &problems, const QString &name)
{
    QStringList lines;
    for (const TypeProblem &p : problems)
        lines << u"%1:%2:%3: %4"_s.arg(name).arg(p.line).arg(p.column).arg(p.message);
    return lines.join(u'\n');
}

} // namespace brulion::extensions
