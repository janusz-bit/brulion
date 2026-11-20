// Luau's type checker (3rdparty/luau/Analysis) over an extension's code: typecheck.h. Built when
// BRULION_LUAU_CHECK is on (desktop, Android); typecheck_none.cpp otherwise.
//
// The checker reports its own trouble with C++ exceptions (a time limit, an internal error): every
// one is caught here, at the edge, and becomes a problem of its own. It never runs a script: the
// old solver (Luau's default) has no user-defined type functions, and `require` resolves nothing.
#include "extensions/typecheck.h"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/Config.h"
#include "Luau/ConfigResolver.h"
#include "Luau/Error.h"
#include "Luau/FileResolver.h"
#include "Luau/Frontend.h"
#include "Luau/LinterConfig.h"

#include <QFile>
#include <QStringList>

#include <algorithm>
#include <exception>
#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::extensions {

namespace {

const std::string kModule = "extension";

/// The one module a check reads: the code being checked.
struct OneSource : Luau::FileResolver {
    std::string source;
    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName &name) override
    {
        if (name != kModule)
            return std::nullopt;
        return Luau::SourceCode{source, Luau::SourceCode::Script};
    }
    std::string getHumanReadableModuleName(const Luau::ModuleName &name) const override { return name; }
};

/// Nonstrict (the code says `--!strict` or `--!nocheck` itself), the lints that point at mistakes.
struct Settings : Luau::ConfigResolver {
    Luau::Config config;
    Settings()
    {
        config.mode = Luau::Mode::Nonstrict;
        config.enabledLint.warningMask = 0;
        using C = Luau::LintWarning::Code;
        for (const C code :
             {C::Code_GlobalUsedAsLocal, C::Code_BuiltinGlobalWrite, C::Code_PlaceholderRead, C::Code_UnreachableCode,
              C::Code_UnknownType, C::Code_ForRange, C::Code_UnbalancedAssignment, C::Code_DuplicateLocal,
              C::Code_FormatString, C::Code_TableLiteral, C::Code_UninitializedLocal, C::Code_DuplicateFunction,
              C::Code_DeprecatedApi, C::Code_TableOperations, C::Code_DuplicateCondition, C::Code_MisleadingAndOr,
              C::Code_CommentDirective, C::Code_IntegerParsing, C::Code_ComparisonPrecedence})
            config.enabledLint.enableWarning(code);
    }
    const Luau::Config &getConfig(const Luau::ModuleName &, const Luau::TypeCheckLimits &) const override
    {
        return config;
    }
};

/// A thread's checker: the builtins and the declarations of `b`, read once.
struct Checker {
    OneSource source;
    Settings settings;
    std::unique_ptr<Luau::Frontend> frontend;
    QString definitionsError;

    Checker()
    {
        Luau::FrontendOptions options;
        options.runLintChecks = true;
        frontend = std::make_unique<Luau::Frontend>(Luau::SolverMode::Old, &source, &settings, options);
        Luau::registerBuiltinGlobals(*frontend, frontend->globals);
        const std::string defs = apiDefinitions().toStdString();
        const Luau::LoadDefinitionFileResult loaded =
                frontend->loadDefinitionFile(frontend->globals, frontend->globals.globalScope, defs, "@brulion", false);
        if (!loaded.success)
            definitionsError = u"the declarations of b do not load"_s;
        Luau::freeze(frontend->globals.globalTypes);
    }
};

Checker &checker()
{
    thread_local Checker c;
    return c;
}

QString text(const std::string &s)
{
    return QString::fromStdString(s);
}

/// Whether an error of nonstrict code is a mistake whatever the code means: a name that is not
/// there, a syntax error, a call with too few arguments, `b:today()` for `b.today()`. The rest —
/// a value that may be nil, a type that does not fit an annotation — Luau reports for annotated
/// code that works (a `string?` of `string.match` returned as `string`), and only `--!strict` asks
/// for that.
bool mistakeWhateverTheMode(const Luau::TypeError &error)
{
    if (Luau::get<Luau::SyntaxError>(error) || Luau::get<Luau::UnknownSymbol>(error)
        || Luau::get<Luau::UnknownProperty>(error) || Luau::get<Luau::UnknownPropButFoundLikeProp>(error)
        || Luau::get<Luau::DeprecatedApiUsed>(error) || Luau::get<Luau::FunctionDoesNotTakeSelf>(error)
        || Luau::get<Luau::FunctionRequiresSelf>(error))
        return true;
    // Too many arguments are often the extra values of a call (`trim(s:gsub(…))`): dropped, fine.
    const Luau::CountMismatch *count = Luau::get<Luau::CountMismatch>(error);
    return count && count->context == Luau::CountMismatch::Arg && count->actual < count->expected;
}

} // namespace

bool typeCheckerAvailable()
{
    return true;
}

QString apiDefinitions()
{
    QFile f(u":/extensions/b.d.luau"_s);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

QList<TypeProblem> typeCheck(const QString &source, int timeLimitMs)
{
    QList<TypeProblem> out;
    try {
        Checker &c = checker();
        if (!c.definitionsError.isEmpty()) {
            out.append({1, 1, c.definitionsError, u"type"_s});
            return out;
        }
        c.source.source = source.toStdString();
        c.frontend->markDirty(kModule);
        Luau::FrontendOptions options;
        options.runLintChecks = true;
        options.moduleTimeLimitSec = std::max(0.05, timeLimitMs / 1000.0);
        const Luau::CheckResult result = c.frontend->check(kModule, options);
        const Luau::SourceModule *module = c.frontend->getSourceModule(kModule);
        const bool strict = module && module->mode == Luau::Mode::Strict;
        for (const Luau::TypeError &error : result.errors) {
            if (error.moduleName != kModule || (!strict && !mistakeWhateverTheMode(error)))
                continue;
            out.append({int(error.location.begin.line) + 1, int(error.location.begin.column) + 1,
                        text(Luau::toString(error, Luau::TypeErrorToStringOptions{c.frontend->fileResolver})),
                        u"type"_s});
        }
        for (const auto *list : {&result.lintResult.errors, &result.lintResult.warnings}) {
            for (const Luau::LintWarning &w : *list)
                out.append({int(w.location.begin.line) + 1, int(w.location.begin.column) + 1, text(w.text),
                            QString::fromLatin1(Luau::LintWarning::getName(w.code))});
        }
        if (!result.timeoutHits.empty())
            out.append({1, 1, u"the type checker ran out of time: the rest is not checked"_s, u"type"_s});
        c.frontend->clearModules({kModule}); // (no memory of the code kept between checks)
    } catch (const std::exception &e) {
        // A time limit, an internal error of the checker: the code itself may well be fine.
        checker().frontend->clearModules({kModule});
        out.append({1, 1, u"the type checker gave up: "_s + QString::fromUtf8(e.what()), u"type"_s});
    } catch (...) {
        out.append({1, 1, u"the type checker gave up"_s, u"type"_s});
    }
    std::ranges::stable_sort(out, [](const TypeProblem &a, const TypeProblem &b) {
        return a.line != b.line ? a.line < b.line : a.column < b.column;
    });
    if (out.size() > kMaxTypeProblems)
        out.resize(kMaxTypeProblems);
    return out;
}

QString describeTypeProblems(const QList<TypeProblem> &problems, const QString &name)
{
    QStringList lines;
    for (const TypeProblem &p : problems)
        lines << u"%1:%2:%3: %4"_s.arg(name).arg(p.line).arg(p.column).arg(p.message);
    return lines.join(u'\n');
}

} // namespace brulion::extensions
