// Internal: small Markdown line helpers of the calc module (fences, list markers, indentation),
// self-contained so calc does not depend on the markdown module. Not part of the public API.
#pragma once

#include "calc/calclocale.h"
#include "calc/evaluator_p.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringView>

#include <optional>

namespace brulion::calc {

struct CalcLineResult;
class Scope;

/// An opening code fence: "```calc", "~~~", "- ```calc" (Logseq list item).
struct FenceOpen {
    QChar marker;          ///< '`' or '~'
    qsizetype length = 0;  ///< number of fence characters (>= 3)
    qsizetype indent = 0;  ///< characters before the fence (indentation and list marker)
    QString lang;          ///< first word of the info string, lower-case
};

/// `^\s*((?:[-*+]|\d+[.)])\s+)?(`{3,}|~{3,})(.*)$`; a backtick fence whose info string
/// contains '`' is not a fence.
std::optional<FenceOpen> parseFenceOpen(QStringView line);
/// A line closing `open`: only fence characters of the same kind, at least as many.
bool isFenceClose(QStringView line, const FenceOpen &open);
/// Length of a list-item prefix `[ \t]*(?:[-*+]|\d{1,9}[.)])[ \t]+` followed by content, or -1.
qsizetype listItemPrefixLength(QStringView line);
/// Column width of leading whitespace (tabs advance to the next multiple of 4).
int columnWidth(QStringView prefix);
/// Leading spaces and tabs of a line.
QStringView leadingWhitespace(QStringView line);
/// Remove up to `n` leading spaces/tabs.
QStringView dedent(QStringView line, qsizetype n);

/// evaluateCalc() with an externally owned work budget (page scopes share one). When `assigned`
/// is set, the names the block assigned (including `ans`) are added to it.
QList<CalcLineResult> evaluateCalcInto(QStringView source, Scope &scope, CalcLocale locale, WorkBudget &budget,
                                       QSet<QString> *assigned = nullptr);

} // namespace brulion::calc
