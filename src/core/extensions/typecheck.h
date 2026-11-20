#pragma once
// Luau's type checker over an extension's code (docs/EXTENSIONS.md "Types"): what is wrong before
// it runs — an unknown global, a function of `b` that is not there (`b.raed`), a call with too few
// arguments, `b:today()` for `b.today()`, a syntax error; with `--!strict` every type that does not
// fit — and lint warnings (code that cannot be reached, a local used before it is given a value …),
// each with the page's line. Checked against the declarations of `b` (builtin/b.d.luau,
// apiDefinitions()). Nonstrict code (no `--!strict`) gets only what is a mistake whatever it
// means: Luau's nonstrict mode also reports annotated code that works (a `string?` returned as
// `string`), which is no warning for a page that never asked for types.
//
//   if (typeCheckerAvailable())
//       for (const TypeProblem &p : typeCheck(page.source)) … p.line, p.message
//
// Luau's Analysis library (3rdparty/luau/Analysis) is in the desktop and Android builds; the
// browser build has none (BRULION_LUAU_CHECK, src/core/extensions/CMakeLists.txt: the type
// checker reports its own errors with C++ exceptions, which the browser's Qt does not catch, and
// it adds megabytes to the download — docs/EXTENSIONS.md "Types"). There typeCheck() gives nothing.
//
// Thread-safe: each thread checks with a checker of its own (made on its first check). Bounded: a
// time limit per check, at most kMaxTypeProblems problems; a check that runs out of time says so.

#include <QList>
#include <QString>

namespace brulion::extensions {

struct TypeProblem
{
    int line = 0; ///< 1-based, as the code's lines (an extension page's own lines)
    int column = 0; ///< 1-based
    QString message; ///< Luau's words: "Unknown global 'x'", "Key 'raed' not found in table 'b'"
    QString kind; ///< "type" (the checker) | "lint" (a lint warning: its name, "LocalUnused" …)
};

constexpr int kMaxTypeProblems = 50;

/// Whether this build has the type checker.
bool typeCheckerAvailable();

/// The problems of `source` (Luau; the line numbers are its own), sorted by line; at most
/// kMaxTypeProblems. Nothing when the build has no type checker.
QList<TypeProblem> typeCheck(const QString &source, int timeLimitMs = 2000);

/// The declarations of `b` the checker reads (a Luau definition file).
QString apiDefinitions();

/// "14:5: Unknown global 'x'" lines of `problems`, as the console and the command line show them.
QString describeTypeProblems(const QList<TypeProblem> &problems, const QString &name);

} // namespace brulion::extensions
