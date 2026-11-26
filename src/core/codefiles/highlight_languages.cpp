// The languages of the syntax colours (highlight.h): what the lexer knows of each — comments,
// strings, keywords, types, constants and builtins. Words are written as one space-separated
// string each, split once.
#include "codefiles/highlight_p.h"

#include <QHash>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::codefiles::detail {

namespace {

QSet<QString> words(QStringView list)
{
    QSet<QString> out;
    for (const QStringView w : list.split(u' ', Qt::SkipEmptyParts))
        out.insert(w.toString());
    return out;
}

using Lexer = LanguageSpec::Lexer;

LanguageSpec cFamily(const QStringList &names)
{
    LanguageSpec s;
    s.names = names;
    s.lineComments = {u"//"_s};
    s.blockOpen = u"/*"_s;
    s.blockClose = u"*/"_s;
    s.upperConstants = true;
    return s;
}

const char16_t CKeywords[] = u"auto break case const continue default do else enum extern for goto if inline register "
                             u"restrict return sizeof static struct switch typedef union volatile while _Alignas "
                             u"_Alignof _Atomic _Generic _Noreturn _Static_assert _Thread_local";
const char16_t CTypes[] = u"char double float int long short signed unsigned void bool _Bool size_t ssize_t ptrdiff_t "
                          u"int8_t int16_t int32_t int64_t uint8_t uint16_t uint32_t uint64_t intptr_t uintptr_t "
                          u"wchar_t FILE";
const char16_t CppKeywords[] =
        u"alignas alignof and asm break case catch class co_await co_return co_yield concept const consteval "
        u"constexpr constinit const_cast continue decltype default delete do dynamic_cast else enum explicit "
        u"export extern final for friend goto if inline mutable namespace new noexcept not operator or "
        u"override private protected public reinterpret_cast requires return sizeof static static_assert "
        u"static_cast struct switch template this thread_local throw try typedef typeid typename union "
        u"using virtual volatile while import module Q_OBJECT Q_GADGET Q_PROPERTY Q_INVOKABLE Q_SIGNALS "
        u"Q_SLOTS Q_EMIT signals slots emit";
const char16_t CppTypes[] = u"auto bool char char8_t char16_t char32_t double float int long short signed unsigned "
                            u"void wchar_t size_t ssize_t ptrdiff_t int8_t int16_t int32_t int64_t uint8_t uint16_t "
                            u"uint32_t uint64_t intptr_t uintptr_t qint8 qint16 qint32 qint64 quint8 quint16 quint32 "
                            u"quint64 qsizetype qreal uchar ushort uint ulong";
const char16_t CConstants[] = u"true false nullptr NULL TRUE FALSE";
const char16_t CppBuiltins[] = u"std printf scanf malloc free memcpy memset strlen qDebug qWarning qInfo qCritical "
                               u"qFatal Q_ASSERT Q_UNUSED qPrintable";

const char16_t JsKeywords[] =
        u"async await break case catch class const continue debugger default delete do else export extends "
        u"finally for from function get if import in instanceof let new of return set static super switch "
        u"this throw try typeof var void while with yield as satisfies";
const char16_t TsExtra[] = u"abstract declare enum implements interface keyof namespace private protected public "
                           u"readonly type infer is asserts override accessor";
const char16_t JsConstants[] = u"true false null undefined NaN Infinity";
const char16_t JsBuiltins[] = u"console window document globalThis Math JSON Object Array String Number Boolean "
                              u"Promise Map Set WeakMap Symbol Date RegExp Error require module exports process";
const char16_t TsTypes[] = u"any boolean never number object string symbol unknown void bigint";

const char16_t PyKeywords[] = u"and as assert async await break class continue def del elif else except finally "
                              u"for from global if import in is lambda nonlocal not or pass raise return try "
                              u"while with yield match case";
const char16_t PyConstants[] = u"True False None NotImplemented Ellipsis __name__ __file__";
const char16_t PyBuiltins[] = u"print len range enumerate zip map filter sorted reversed open isinstance issubclass "
                              u"super self cls int str float bool list dict set tuple bytes object type min max "
                              u"sum abs any all iter next repr getattr setattr hasattr vars dir id hash input";

std::unique_ptr<QList<LanguageSpec>> makeLanguages()
{
    auto all = std::make_unique<QList<LanguageSpec>>();
    QList<LanguageSpec> &out = *all;

    { // C
        LanguageSpec s = cFamily({u"c"_s});
        s.hashPreprocessor = true;
        s.keywords = words(CKeywords);
        s.types = words(CTypes);
        s.constants = words(CConstants);
        s.builtins = words(CppBuiltins);
        s.typeDefiners = words(u"struct enum union");
        out.append(s);
    }
    { // C++, Objective-C, GLSL
        LanguageSpec s = cFamily({u"cpp"_s, u"objectivec"_s, u"glsl"_s});
        s.hashPreprocessor = true;
        s.cppRawStrings = true;
        s.keywords = words(CppKeywords);
        s.keywords.unite(words(u"@interface @implementation @end @property in out inout uniform varying "
                               u"attribute layout precision highp mediump lowp discard"));
        s.types = words(CppTypes);
        s.types.unite(words(u"vec2 vec3 vec4 mat2 mat3 mat4 ivec2 ivec3 ivec4 sampler2D id BOOL NSString"));
        s.constants = words(CConstants);
        s.constants.unite(words(u"YES NO nil"));
        s.builtins = words(CppBuiltins);
        s.typeDefiners = words(u"class struct enum union namespace concept");
        out.append(s);
    }
    { // C#
        LanguageSpec s = cFamily({u"cs"_s});
        s.hashPreprocessor = true;
        s.capitalTypes = true;
        s.upperConstants = false;
        s.keywords = words(u"abstract as base break case catch checked class const continue default delegate do "
                           u"else enum event explicit extern finally fixed for foreach goto if implicit in "
                           u"interface internal is lock namespace new operator out override params private "
                           u"protected public readonly record ref return sealed sizeof stackalloc static struct "
                           u"switch this throw try typeof unchecked unsafe using virtual volatile while async "
                           u"await var get set init where yield when");
        s.types = words(u"bool byte char decimal double float int long object sbyte short string uint ulong "
                        u"ushort void dynamic nint nuint");
        s.constants = words(u"true false null");
        s.typeDefiners = words(u"class struct enum interface record");
        out.append(s);
    }
    { // Java, Kotlin, Scala, Groovy, Dart
        LanguageSpec s = cFamily({u"java"_s, u"kotlin"_s, u"scala"_s, u"groovy"_s, u"dart"_s});
        s.decorators = true;
        s.capitalTypes = true;
        s.tripleQuotes = true;
        s.keywords = words(u"abstract assert break case catch class continue default do else enum extends final "
                           u"finally for if implements import instanceof interface native new package private "
                           u"protected public return static strictfp super switch synchronized this throw throws "
                           u"transient try volatile while var val fun object companion data sealed open override "
                           u"internal lateinit inline when is in out by init constructor typealias suspend "
                           u"def trait with yield match lazy implicit async await late required factory "
                           u"extension mixin library part show hide covariant operator get set");
        s.types = words(u"boolean byte char double float int long short void Int Long Double Float Boolean "
                        u"String Char Unit Any Nothing dynamic num");
        s.constants = words(u"true false null");
        s.functionDefiners = words(u"fun def");
        s.typeDefiners = words(u"class interface enum object trait typealias mixin");
        out.append(s);
    }
    { // Swift
        LanguageSpec s = cFamily({u"swift"_s});
        s.nestedBlocks = true;
        s.decorators = true;
        s.capitalTypes = true;
        s.tripleQuotes = true;
        s.quotes = u"\""_s;
        s.keywords = words(u"actor any as associatedtype async await break case catch class continue default "
                           u"defer deinit do else enum extension fallthrough fileprivate final for func guard if "
                           u"import in indirect init inout internal is lazy let mutating nonisolated open "
                           u"operator override private protocol public repeat rethrows return self Self some "
                           u"static struct subscript super switch throw throws try typealias var weak where while");
        s.constants = words(u"true false nil");
        s.functionDefiners = words(u"func");
        s.typeDefiners = words(u"class struct enum protocol extension actor typealias");
        out.append(s);
    }
    { // Go
        LanguageSpec s = cFamily({u"go"_s});
        s.backtickStrings = true;
        s.backtickEscapes = false;
        s.upperConstants = false;
        s.keywords = words(u"break case chan const continue default defer else fallthrough for func go goto if "
                           u"import interface map package range return select struct switch type var");
        s.types = words(u"bool byte complex64 complex128 error float32 float64 int int8 int16 int32 int64 rune "
                        u"string uint uint8 uint16 uint32 uint64 uintptr any comparable");
        s.constants = words(u"true false nil iota");
        s.builtins = words(u"append cap close complex copy delete imag len make new panic print println real "
                           u"recover min max clear fmt");
        s.functionDefiners = words(u"func");
        s.typeDefiners = words(u"type");
        out.append(s);
    }
    { // Rust
        LanguageSpec s = cFamily({u"rust"_s});
        s.nestedBlocks = true;
        s.rustRawStrings = true;
        s.rustAttributes = true;
        s.lifetimes = true;
        s.capitalTypes = true;
        s.keywords = words(u"as async await break const continue crate dyn else enum extern fn for if impl in let "
                           u"loop match mod move mut pub ref return self Self static struct super trait type "
                           u"union unsafe use where while macro_rules");
        s.types = words(u"bool char f32 f64 i8 i16 i32 i64 i128 isize str u8 u16 u32 u64 u128 usize");
        s.constants = words(u"true false None Some Ok Err");
        s.builtins = words(u"println print eprintln format vec panic assert assert_eq assert_ne todo unimplemented "
                           u"unreachable write writeln dbg matches");
        s.functionDefiners = words(u"fn");
        s.typeDefiners = words(u"struct enum trait type union impl");
        out.append(s);
    }
    { // Zig, WGSL, protobuf, GraphQL (C-like comments, plain keywords)
        LanguageSpec s = cFamily({u"zig"_s, u"wgsl"_s, u"protobuf"_s, u"hcl"_s});
        s.lineComments = {u"//"_s, u"#"_s};
        s.keywords = words(u"const var fn pub return if else while for switch break continue defer errdefer try "
                           u"catch struct enum union comptime inline test usingnamespace message service rpc "
                           u"returns option repeated optional required oneof map syntax package import "
                           u"resource variable output module provider data locals terraform let override");
        s.types = words(u"bool f16 f32 f64 i8 i16 i32 i64 u8 u16 u32 u64 usize isize void string bytes int32 "
                        u"int64 uint32 uint64 double float vec2 vec3 vec4 mat4x4");
        s.constants = words(u"true false null undefined");
        s.functionDefiners = words(u"fn");
        s.typeDefiners = words(u"struct enum union message service");
        out.append(s);
    }
    { // JavaScript, JSX
        LanguageSpec s = cFamily({u"js"_s, u"jsx"_s, u"javascript"_s});
        s.quotes = u"\"'"_s;
        s.backtickStrings = true;
        s.capitalTypes = true;
        s.keywords = words(JsKeywords);
        s.constants = words(JsConstants);
        s.builtins = words(JsBuiltins);
        s.functionDefiners = words(u"function");
        s.typeDefiners = words(u"class");
        out.append(s);
    }
    { // TypeScript, TSX, Vue's and Svelte's scripts are markup's
        LanguageSpec s = cFamily({u"ts"_s, u"tsx"_s, u"typescript"_s});
        s.backtickStrings = true;
        s.decorators = true;
        s.capitalTypes = true;
        s.keywords = words(JsKeywords);
        s.keywords.unite(words(TsExtra));
        s.types = words(TsTypes);
        s.constants = words(JsConstants);
        s.builtins = words(JsBuiltins);
        s.functionDefiners = words(u"function");
        s.typeDefiners = words(u"class interface type enum namespace");
        out.append(s);
    }
    { // QML
        LanguageSpec s = cFamily({u"qml"_s});
        s.backtickStrings = true;
        s.capitalTypes = true;
        s.keywords = words(JsKeywords);
        s.keywords.unite(words(u"property signal readonly required default alias pragma on component final"));
        s.colonProperties = true;
        s.types = words(u"bool int real double string url var list color point rect size date font");
        s.constants = words(JsConstants);
        s.builtins = words(u"Qt console Math parent qsTr qsTranslate JSON");
        s.functionDefiners = words(u"function signal");
        s.typeDefiners = words(u"component");
        out.append(s);
    }
    { // Python
        LanguageSpec s;
        s.names = {u"python"_s};
        s.lineComments = {u"#"_s};
        s.tripleQuotes = true;
        s.decorators = true;
        s.capitalTypes = true;
        s.upperConstants = true;
        s.keywords = words(PyKeywords);
        s.constants = words(PyConstants);
        s.builtins = words(PyBuiltins);
        s.functionDefiners = words(u"def");
        s.typeDefiners = words(u"class");
        out.append(s);
    }
    { // Julia, R, Elixir, Ruby, Perl, PHP (scripting languages with # comments)
        LanguageSpec s;
        s.names = {u"ruby"_s};
        s.lineComments = {u"#"_s};
        s.capitalTypes = true;
        s.keywords = words(u"BEGIN END alias and begin break case class def defined? do else elsif end ensure for if "
                           u"in module next not or redo rescue retry return self super then undef unless until "
                           u"when while yield require require_relative attr_accessor attr_reader private public "
                           u"protected lambda proc");
        s.constants = words(u"true false nil");
        s.builtins = words(u"puts print p raise");
        s.functionDefiners = words(u"def");
        s.typeDefiners = words(u"class module");
        out.append(s);
    }
    {
        LanguageSpec s;
        s.names = {u"php"_s, u"perl"_s};
        s.lineComments = {u"//"_s, u"#"_s};
        s.blockOpen = u"/*"_s;
        s.blockClose = u"*/"_s;
        s.sigils = true;
        s.keywords = words(u"abstract and array as break callable case catch class clone const continue declare "
                           u"default do echo else elseif empty enddeclare endfor endforeach endif endswitch "
                           u"endwhile extends final finally fn for foreach function global goto if implements "
                           u"include include_once instanceof insteadof interface isset list match namespace new "
                           u"or print private protected public readonly require require_once return static "
                           u"switch throw trait try unset use var while xor yield my our sub local package "
                           u"unless until last next redo elsif");
        s.constants = words(u"true false null TRUE FALSE NULL undef");
        s.functionDefiners = words(u"function sub fn");
        s.typeDefiners = words(u"class interface trait package");
        out.append(s);
    }
    {
        LanguageSpec s;
        s.names = {u"elixir"_s, u"julia"_s, u"r"_s};
        s.lineComments = {u"#"_s};
        s.tripleQuotes = true;
        s.capitalTypes = true;
        s.keywords = words(u"def defp defmodule defstruct defmacro defimpl defprotocol do end fn case cond if else "
                           u"unless when with for receive try catch rescue after raise import alias require use "
                           u"quote unquote function return while repeat break next in begin let local global "
                           u"module struct mutable abstract macro using export const elseif");
        s.constants = words(u"true false nil nothing missing TRUE FALSE NULL NA Inf NaN");
        s.functionDefiners = words(u"def defp defmacro function");
        s.typeDefiners = words(u"defmodule struct");
        out.append(s);
    }
    { // Shells
        LanguageSpec s;
        s.names = {u"sh"_s, u"bash"_s, u"zsh"_s, u"fish"_s, u"shell"_s};
        s.lineComments = {u"#"_s};
        s.hashCommentAtWordStart = true;
        s.variables = true;
        s.dashInNames = true;
        s.keywords = words(u"if then else elif fi case esac for select while until do done in function time coproc "
                           u"return break continue exit local export readonly declare typeset unset set shift "
                           u"source alias trap eval exec end switch begin and or not");
        s.builtins = words(u"echo printf read cd pwd test mkdir rm cp mv ls cat grep sed awk find xargs git nix "
                           u"sudo chmod chown touch tar curl wget");
        s.constants = words(u"true false");
        s.functionDefiners = words(u"function");
        out.append(s);
    }
    { // PowerShell
        LanguageSpec s;
        s.names = {u"powershell"_s};
        s.lineComments = {u"#"_s};
        s.blockOpen = u"<#"_s;
        s.blockClose = u"#>"_s;
        s.variables = true;
        s.dashInNames = true;
        s.caseInsensitive = true;
        s.keywords = words(u"begin break catch class continue data do dynamicparam else elseif end exit filter "
                           u"finally for foreach from function if in param process return switch throw trap try "
                           u"until using while");
        s.constants = words(u"$true $false $null");
        s.functionDefiners = words(u"function");
        out.append(s);
    }
    { // Batch files
        LanguageSpec s;
        s.names = {u"bat"_s};
        s.lineComments = {u"rem "_s, u"::"_s, u"REM "_s, u"Rem "_s};
        s.quotes = u"\""_s;
        s.caseInsensitive = true;
        s.keywords = words(u"echo set if else goto call exit for in do not exist defined errorlevel setlocal "
                           u"endlocal pushd popd shift");
        out.append(s);
    }
    { // CMake
        LanguageSpec s;
        s.names = {u"cmake"_s};
        s.lineComments = {u"#"_s};
        s.quotes = u"\""_s;
        s.variables = true;
        s.caseInsensitive = true;
        s.upperConstants = true;
        s.keywords = words(u"if elseif else endif foreach endforeach while endwhile function endfunction macro "
                           u"endmacro return break continue set unset option include project "
                           u"cmake_minimum_required cmake_parse_arguments add_library add_executable "
                           u"add_subdirectory add_test add_custom_command add_custom_target add_dependencies "
                           u"target_link_libraries target_include_directories target_compile_definitions "
                           u"target_compile_options target_sources set_property set_target_properties "
                           u"set_tests_properties find_package list string file message install configure_file "
                           u"get_filename_component get_target_property execute_process include_directories");
        s.constants = words(u"ON OFF TRUE FALSE YES NO");
        out.append(s);
    }
    { // Nix
        LanguageSpec s;
        s.names = {u"nix"_s};
        s.lineComments = {u"#"_s};
        s.blockOpen = u"/*"_s;
        s.blockClose = u"*/"_s;
        s.quotes = u"\""_s;
        s.nixStrings = true;
        s.dashInNames = true;
        s.keywords = words(u"let in with rec inherit if then else assert or import");
        s.constants = words(u"true false null");
        s.builtins = words(u"builtins lib pkgs derivation fetchurl fetchFromGitHub mkDerivation toString map "
                           u"throw abort");
        out.append(s);
    }
    { // Makefiles, Dockerfiles
        LanguageSpec s;
        s.names = {u"makefile"_s};
        s.lineComments = {u"#"_s};
        s.variables = true;
        s.keywords = words(u"ifeq ifneq ifdef ifndef else endif include define endef export override .PHONY");
        out.append(s);
    }
    {
        LanguageSpec s;
        s.names = {u"dockerfile"_s};
        s.lineComments = {u"#"_s};
        s.variables = true;
        s.caseInsensitive = true;
        s.keywords = words(u"from run cmd label maintainer expose env add copy entrypoint volume user workdir arg "
                           u"onbuild stopsignal healthcheck shell as");
        out.append(s);
    }
    { // SQL
        LanguageSpec s;
        s.names = {u"sql"_s};
        s.lineComments = {u"--"_s};
        s.blockOpen = u"/*"_s;
        s.blockClose = u"*/"_s;
        s.quotes = u"'\""_s;
        s.doubledQuoteEscapes = true;
        s.caseInsensitive = true;
        s.keywords = words(u"select from where insert into values update set delete create table index view drop "
                           u"alter add column primary key foreign references constraint unique not null default "
                           u"and or in is like between join inner left right outer full cross on as group by "
                           u"order having limit offset union all distinct case when then else end exists "
                           u"returning with recursive begin commit rollback transaction trigger if asc desc");
        s.types = words(u"int integer bigint smallint text varchar char boolean real double float numeric decimal "
                        u"date time timestamp blob json jsonb uuid serial");
        s.constants = words(u"true false null");
        s.builtins = words(u"count sum avg min max coalesce cast lower upper length substr now");
        out.append(s);
    }
    { // Haskell, OCaml
        LanguageSpec s;
        s.names = {u"haskell"_s};
        s.lineComments = {u"--"_s};
        s.blockOpen = u"{-"_s;
        s.blockClose = u"-}"_s;
        s.nestedBlocks = true;
        s.quotes = u"\""_s;
        s.capitalTypes = true;
        s.keywords = words(u"case class data default deriving do else foreign if import in infix infixl infixr "
                           u"instance let module newtype of then type where qualified as hiding");
        s.constants = words(u"True False Nothing Just");
        s.typeDefiners = words(u"data newtype type class");
        out.append(s);
    }
    {
        LanguageSpec s;
        s.names = {u"ocaml"_s};
        s.blockOpen = u"(*"_s;
        s.blockClose = u"*)"_s;
        s.nestedBlocks = true;
        s.quotes = u"\""_s;
        s.capitalTypes = true;
        s.keywords = words(u"and as assert begin class constraint do done downto else end exception external for "
                           u"fun function functor if in include inherit initializer lazy let match method module "
                           u"mutable new object of open or private rec sig struct then to try type val virtual "
                           u"when while with");
        s.constants = words(u"true false None Some");
        out.append(s);
    }
    { // Erlang, LaTeX (% comments)
        LanguageSpec s;
        s.names = {u"erlang"_s};
        s.lineComments = {u"%"_s};
        s.quotes = u"\""_s;
        s.keywords = words(u"after and andalso band begin bnot bor bsl bsr bxor case catch cond div end fun if let "
                           u"not of or orelse receive rem try when xor module export import record define");
        s.constants = words(u"true false undefined ok error");
        out.append(s);
    }
    {
        LanguageSpec s;
        s.names = {u"latex"_s};
        s.lineComments = {u"%"_s};
        s.quotes = QString();
        s.backslashCommands = true;
        out.append(s);
    }
    { // Clojure
        LanguageSpec s;
        s.names = {u"clojure"_s};
        s.lineComments = {u";"_s};
        s.quotes = u"\""_s;
        s.dashInNames = true;
        s.keywords = words(u"def defn defn- defmacro defprotocol defrecord deftype fn let if when cond do loop recur "
                           u"ns require import try catch finally throw case and or not");
        s.constants = words(u"true false nil");
        s.functionDefiners = words(u"defn defn- defmacro");
        out.append(s);
    }
    { // Vim script
        LanguageSpec s;
        s.names = {u"vim"_s};
        s.lineComments = {u"\""_s};
        s.quotes = u"'"_s;
        s.keywords = words(u"if else elseif endif for endfor while endwhile function endfunction return let set "
                           u"call execute map nnoremap inoremap vnoremap autocmd augroup command");
        out.append(s);
    }
    { // GraphQL
        LanguageSpec s;
        s.names = {u"graphql"_s};
        s.lineComments = {u"#"_s};
        s.quotes = u"\""_s;
        s.tripleQuotes = true;
        s.capitalTypes = true;
        s.keywords = words(u"query mutation subscription fragment on type interface union enum input scalar schema "
                           u"extend implements directive");
        s.constants = words(u"true false null");
        s.typeDefiners = words(u"type interface union enum input scalar");
        out.append(s);
    }
    { // Lua, Luau: core syntax's lexer
        LanguageSpec s;
        s.lexer = Lexer::Luau;
        s.names = {u"lua"_s, u"luau"_s};
        out.append(s);
    }
    { // CSS, SCSS, Less
        LanguageSpec s;
        s.lexer = Lexer::Css;
        s.names = {u"css"_s, u"scss"_s, u"less"_s};
        s.blockOpen = u"/*"_s;
        s.blockClose = u"*/"_s;
        s.dashInNames = true;
        s.constants = words(u"inherit initial unset none auto transparent currentColor revert");
        out.append(s);
    }
    for (const auto &[lexer, names] :
         {std::pair{Lexer::Markup, QStringList{u"html"_s, u"xml"_s, u"vue"_s, u"svelte"_s}},
          std::pair{Lexer::Json, QStringList{u"json"_s, u"jsonc"_s}},
          std::pair{Lexer::Yaml, QStringList{u"yaml"_s}}, std::pair{Lexer::Ini, QStringList{u"ini"_s, u"toml"_s, u"meson"_s}},
          std::pair{Lexer::Markdown, QStringList{u"markdown"_s}}, std::pair{Lexer::Diff, QStringList{u"diff"_s}},
          std::pair{Lexer::Gitignore, QStringList{u"gitignore"_s}}}) {
        LanguageSpec s;
        s.lexer = lexer;
        s.names = names;
        if (lexer == Lexer::Markup) {
            s.blockOpen = u"<!--"_s;
            s.blockClose = u"-->"_s;
        } else if (lexer == Lexer::Json) {
            s.lineComments = {u"//"_s};
            s.blockOpen = u"/*"_s;
            s.blockClose = u"*/"_s;
            s.constants = words(u"true false null");
        } else if (lexer == Lexer::Yaml) {
            s.lineComments = {u"#"_s};
            s.constants = words(u"true false null yes no on off True False Null Yes No On Off TRUE FALSE NULL ~");
        } else if (lexer == Lexer::Ini) {
            s.lineComments = {u"#"_s, u";"_s};
            s.tripleQuotes = true;
            s.constants = words(u"true false yes no on off");
        }
        out.append(s);
    }
    return all;
}

} // namespace

const LanguageSpec *languageSpec(QStringView name)
{
    static const std::unique_ptr<QList<LanguageSpec>> languages = makeLanguages();
    static const QHash<QString, int> byName = [] {
        QHash<QString, int> out;
        for (int i = 0; i < int(languages->size()); ++i) {
            for (const QString &n : languages->at(i).names)
                out.insert(n, i);
        }
        return out;
    }();
    const auto it = byName.constFind(name.toString().toLower());
    return it == byName.cend() ? nullptr : &languages->at(*it);
}

} // namespace brulion::codefiles::detail
