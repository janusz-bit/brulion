#!/usr/bin/env python3
"""Quality gates for Brulion's C++: coverage, CRAP, duplication and mutation testing.

unclebob's crapper, dryer and mutator do not read C++; this is the same three gates for it, over
lizard's functions (cyclomatic complexity, line ranges) and Clang's source-based coverage.
Usage and the rules behind each number: scripts/quality/README.md.

    quality.py coverage --targets tst_gitcli --tests '^tst_gitcli$'    # inside `nix develop`
    quality.py crap     [--base main | --files F…] [--threshold 10]
    quality.py dry      [--base main | --files F…] [--threshold 0.8]
    quality.py mutate   --targets tst_gitcli --tests '^tst_gitcli$' [--base main | --files F…]
"""
import argparse
import bisect
import csv
import difflib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from typing import NamedTuple

# ---------------------------------------------------------------------------------------------
# The domain: functions, tokens, mutants
# ---------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Function:
    """One function as lizard sees it: lines start..end (1-based, inclusive) of `path`."""
    name: str
    path: str
    start: int
    end: int
    ccn: int


class Token(NamedTuple):  # a tuple: millions are made per run, a frozen dataclass is 7x slower
    kind: str   # ws, comment, pp, string, char, number, ident, op, other
    text: str
    start: int  # offset into the source
    end: int
    line: int   # 1-based line of its first character


@dataclass(frozen=True)
class Mutant:
    path: str
    line: int
    start: int
    end: int
    original: str
    replacement: str

    def describe(self):
        new = self.replacement if self.replacement else '(removed)'
        return f'{self.path}:{self.line}: {self.original!r} -> {new!r}'


TRIVIA = frozenset({'ws', 'comment', 'pp'})

# ---------------------------------------------------------------------------------------------
# Tokenizer (enough C++ to tell code from strings, comments and preprocessor lines)
# ---------------------------------------------------------------------------------------------

_OPS = sorted(['<=>', '<<=', '>>=', '->*', '...', '::', '->', '++', '--', '<<', '>>', '<=', '>=', '==',
               '!=', '&&', '||', '+=', '-=', '*=', '/=', '%=', '&=', '|=', '^=', '.*', '##'],
              key=len, reverse=True)
_PREFIX = r'(?:u8|u|U|L)?'
# A preprocessor line: `#` first on its line (after spaces), to its end with `\` continuations. The
# newline before it is part of it, so the whitespace alternative never swallows the line's start.
_TOKEN = re.compile('|'.join([
    r'(?P<pp>(?:\A|\n)[ \t]*#(?:\\\n|[^\n])*)',
    r'(?P<ws>[ \t\r\f\v]+|\n)',
    r'(?P<comment>//[^\n]*|/\*.*?(?:\*/|\Z))',
    r'(?P<string>' + _PREFIX + r'R"(?P<delim>[^ ()\\\t\n]{0,16})\(.*?(?:\)(?P=delim)"|\Z)[A-Za-z_]\w*'
    r'|' + _PREFIX + r'R"(?P<delim2>[^ ()\\\t\n]{0,16})\(.*?(?:\)(?P=delim2)"|\Z)'
    r'|' + _PREFIX + r'"(?:\\.|[^"\\\n])*(?:"[A-Za-z_]\w*|"|$))',
    r"(?P<char>" + _PREFIX + r"'(?:\\.|[^'\\\n])+')",
    r'(?P<number>\.?\d(?:[eEpP][+-]|[\w.\'])*)',
    r'(?P<ident>[A-Za-z_]\w*)',
    r'(?P<op>' + '|'.join(re.escape(o) for o in _OPS) + r'|[{}()\[\];,.<>+\-*/%&|^!~?:=#])',
    r'(?P<other>.)',
]), re.DOTALL)
_KINDS = {'delim': 'string', 'delim2': 'string'}


def tokenize(src):
    """Every character of `src` in exactly one token, in order."""
    tokens = []
    line = 1
    for m in _TOKEN.finditer(src):
        kind = _KINDS.get(m.lastgroup, m.lastgroup)
        text = m.group(0)
        if kind == 'pp' and text.startswith('\n'):
            tokens.append(Token('ws', '\n', m.start(), m.start() + 1, line))
            line += 1
            tokens.append(Token('pp', text[1:], m.start() + 1, m.end(), line))
            line += text.count('\n') - 1
            continue
        tokens.append(Token(kind, text, m.start(), m.end(), line))
        line += text.count('\n')
    return tokens


def code_tokens(tokens):
    return [t for t in tokens if t.kind not in TRIVIA]

# ---------------------------------------------------------------------------------------------
# Inputs: git diffs, lizard's CSV, LCOV
# ---------------------------------------------------------------------------------------------


_HUNK = re.compile(r'^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@')


def changed_lines(diff_text):
    """{path: {new-side line numbers}} of a `git diff -U0`; a pure deletion marks the line after it."""
    result = {}
    path = None
    for line in diff_text.splitlines():
        if line.startswith('+++ '):
            target = line[4:]
            path = None if target == '/dev/null' else target[2:] if target.startswith('b/') else target
            continue
        m = _HUNK.match(line)
        if not (m and path):
            continue
        first, count = int(m.group(1)), int(m.group(2) if m.group(2) is not None else 1)
        lines = range(first, first + count) if count else [max(first, 1)]
        result.setdefault(path, set()).update(lines)
    return result


def parse_lizard_csv(text):
    functions = []
    for row in csv.reader(io.StringIO(text)):
        if len(row) < 11 or not row[1].isdigit():
            continue
        functions.append(Function(row[7], row[6], int(row[9]), int(row[10]), int(row[1])))
    return functions


def parse_lcov(text):
    """{source path: {line: hits}}; records of one file from several binaries add up."""
    files = {}
    current = None
    for line in text.splitlines():
        if line.startswith('SF:'):
            current = files.setdefault(line[3:], {})
        elif line.startswith('DA:') and current is not None:
            number, hits = line[3:].split(',')[:2]
            current[int(number)] = current.get(int(number), 0) + int(hits)
        elif line == 'end_of_record':
            current = None
    return files


def in_scope(functions, changed):
    """The functions with at least one changed line inside."""
    return [f for f in functions if any(f.start <= n <= f.end for n in changed.get(f.path, ()))]

# ---------------------------------------------------------------------------------------------
# CRAP
# ---------------------------------------------------------------------------------------------


def crap(ccn, coverage):
    """C.R.A.P. = comp² × (1 − cov)³ + comp (crap4j; coverage 0..1)."""
    return ccn * ccn * (1.0 - coverage) ** 3 + ccn


def function_coverage(fn, hits):
    """The share of `fn`'s instrumented lines that ran; None when none of its lines is instrumented."""
    lines = [hits[n] for n in range(fn.start, fn.end + 1) if n in hits]
    if not lines:
        return None
    return sum(1 for h in lines if h > 0) / len(lines)

# ---------------------------------------------------------------------------------------------
# Duplication
# ---------------------------------------------------------------------------------------------


_KEYWORDS = frozenset('''alignas alignof asm auto bool break case catch char char8_t char16_t char32_t
class co_await co_return co_yield concept const consteval constexpr constinit const_cast continue
decltype default delete do double dynamic_cast else enum explicit export extern float for friend goto
if inline int long mutable namespace new noexcept operator private protected public register
reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch
template this thread_local throw try typedef typeid typename union unsigned using virtual void
volatile wchar_t while override final Q_EMIT Q_SIGNALS Q_SLOTS Q_INVOKABLE'''.split())
_LITERAL_IDENTS = frozenset({'true', 'false', 'nullptr'})


def _shape_words(kinds, texts):
    """dryer's shape: calls, keywords and operators stay; locals, fields and literals become kinds.

    Qualified names (`Change::Added`) stay too: they name types and enum values, not locals, so two
    tables over different enums do not look alike while a copy with renamed locals still does."""
    out = []
    last = len(texts) - 1
    for i, kind in enumerate(kinds):
        text = texts[i]
        if kind == 'ident':
            if text in _LITERAL_IDENTS:
                out.append('LIT')
            elif (text in _KEYWORDS or (i < last and texts[i + 1] in ('(', '::'))
                  or (i > 0 and texts[i - 1] == '::')):
                out.append(text)
            else:
                out.append('ID')
        elif kind in ('string', 'char', 'number'):
            out.append('LIT')
        else:
            out.append(text)
    return out


def normalize(tokens):
    code = code_tokens(tokens)
    return _shape_words([t.kind for t in code], [t.text for t in code])


def file_shape(src):
    """The shape words of every line of `src` (index 0 = line 1); a token counts on its first line.

    Straight from the regex's matches: making a Token per match costs 4–5× the matching itself."""
    kinds, texts, lines = [], [], []
    line = 1
    for m in _TOKEN.finditer(src):
        text = m.group()
        kind = _KINDS.get(m.lastgroup, m.lastgroup)
        if kind not in TRIVIA:
            kinds.append(kind)
            texts.append(text)
            lines.append(line)
        line += text.count('\n')
    shape = [[] for _ in range(src.count('\n') + 1)]
    for word, n in zip(_shape_words(kinds, texts), lines):
        shape[n - 1].append(word)
    return shape


def shape_of(lines, start, end):
    return [word for line in lines[start - 1:end] for word in line]


SHAPE_VERSION = '1'


def cached_file_shape(src, cache_dir):
    """file_shape(src), kept under `cache_dir` by the content's hash: shared by every worktree."""
    import hashlib
    key = hashlib.sha1((SHAPE_VERSION + '\0' + src).encode('utf-8', 'surrogateescape')).hexdigest()
    path = os.path.join(cache_dir, key + '.json')
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        pass
    shape = file_shape(src)
    os.makedirs(cache_dir, exist_ok=True)
    tmp = f'{path}.{os.getpid()}.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump(shape, f, separators=(',', ':'))
    os.replace(tmp, path)
    return shape


def shape_cache_dir():
    base = os.environ.get('XDG_CACHE_HOME') or os.path.join(os.path.expanduser('~'), '.cache')
    return os.path.join(base, 'brulion-quality', 'shapes')


def similarity(a, b):
    if not a or not b or 2 * min(len(a), len(b)) / (len(a) + len(b)) == 0:
        return 0.0
    matcher = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return matcher.ratio()


def similar_pairs(scoped, everything, shapes, threshold, min_tokens):
    """Pairs (score, a, b), a in scope, b anywhere else, with similarity >= threshold."""
    from collections import Counter
    pairs = {}
    scoped = [(a, shapes[a], Counter(shapes[a])) for a in scoped if len(shapes.get(a) or ()) >= min_tokens]
    for b in everything:
        sb = shapes.get(b)
        if not sb or len(sb) < min_tokens:
            continue
        matcher = None
        cb = None
        for a, sa, ca in scoped:
            total = len(sa) + len(sb)
            if b == a or 2 * min(len(sa), len(sb)) / total < threshold:
                continue
            cb = cb or Counter(sb)
            # difflib's quick_ratio (the bags' overlap), from counts made once per function.
            small, big = (ca, cb) if len(ca) <= len(cb) else (cb, ca)
            if 2 * sum(min(n, big.get(w, 0)) for w, n in small.items()) / total < threshold:
                continue
            if matcher is None:
                matcher = difflib.SequenceMatcher(None, autojunk=False)
                matcher.set_seq2(sb)  # the expensive index, once per b
            matcher.set_seq1(sa)
            score = matcher.ratio()
            if score >= threshold:
                key = tuple(sorted([(a.path, a.start), (b.path, b.start)]))
                pairs[key] = (score, a, b)
    return sorted(pairs.values(), key=lambda p: -p[0])

# ---------------------------------------------------------------------------------------------
# Mutants
# ---------------------------------------------------------------------------------------------


_SWAPS = {'==': '!=', '!=': '==', '<': '<=', '<=': '<', '>': '>=', '>=': '>', '&&': '||', '||': '&&',
          '+': '-', '-': '+', '+=': '-=', '-=': '+=', 'true': 'false', 'false': 'true'}
_SPACED = frozenset({'<', '>', '&&', '||', '+', '-', '<=', '>=', '==', '!=', '+=', '-='})
_CALL_STATEMENT = re.compile(r'[A-Za-z_][\w:]*(?:(?:\.|->)[A-Za-z_]\w*)*\((?:[^;]*)\);')


def _spaced(src, t):
    return t.start > 0 and t.end < len(src) and src[t.start - 1] in ' \t' and src[t.end] in ' \t\n'


def _operator_mutants(src, code, i):
    t = code[i]
    if t.text in _SWAPS and (t.kind == 'ident' or t.kind == 'op'):
        if t.text in _SPACED and not _spaced(src, t):
            return []
        return [(t, _SWAPS[t.text])]
    if t.text == '!' and t.kind == 'op' and i + 1 < len(code) and code[i + 1].text != '=':
        return [(t, '')]
    return []


def _number_mutants(t):
    if t.kind != 'number' or not t.text.isdigit():
        return []
    value = int(t.text)
    return [(t, '1' if value == 0 else '0' if value == 1 else str(value + 1))]


def _statement_mutants(src, tokens):
    """Whole call statements on a line of their own: `foo(a, b);` removed."""
    found = []
    for t in tokens:
        if t.kind != 'ws' or '\n' not in t.text:
            continue
        line_start = t.end
        line_end = src.find('\n', line_start)
        line_end = len(src) if line_end < 0 else line_end
        text = src[line_start:line_end].strip()
        if _CALL_STATEMENT.fullmatch(text) and text.count('(') == text.count(')'):
            offset = src.index(text, line_start)
            found.append((offset, offset + len(text), text))
    return found


def mutants(src, tokens, lines, path=''):
    """The mutants of `src` on `lines` (code only: never strings, comments or preprocessor lines)."""
    code = code_tokens(tokens)
    result = []
    for i, t in enumerate(code):
        if t.line not in lines:
            continue
        for token, replacement in _operator_mutants(src, code, i) + _number_mutants(t):
            result.append(Mutant(path, token.line, token.start, token.end, token.text, replacement))
    line_of = _line_index(src)
    for start, end, text in _statement_mutants(src, tokens):
        line = line_of(start)
        if line in lines:
            result.append(Mutant(path, line, start, end, text, ''))
    return sorted(result, key=lambda m: (m.start, m.replacement))


def _line_index(src):
    starts = [0] + [i + 1 for i, c in enumerate(src) if c == '\n']
    return lambda offset: bisect.bisect_right(starts, offset)


def apply(src, mutant):
    return src[:mutant.start] + mutant.replacement + src[mutant.end:]


def sample(items, limit):
    """At most `limit` items spread evenly over the list (the same ones every run)."""
    if limit <= 0 or len(items) <= limit:
        return list(items)
    step = len(items) / limit
    return [items[int(i * step)] for i in range(limit)]

# ---------------------------------------------------------------------------------------------
# The journal: a mutated file is restored even after a crash (running again restores first)
# ---------------------------------------------------------------------------------------------


def write_journal(journal, path, original):
    tmp = journal + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        json.dump({'path': path, 'original': original}, f)
    os.replace(tmp, journal)


def restore_from_journal(journal):
    if not os.path.exists(journal):
        return False
    with open(journal, encoding='utf-8') as f:
        entry = json.load(f)
    _write(entry['path'], entry['original'])
    os.remove(journal)
    return True

# ---------------------------------------------------------------------------------------------
# The world: git, lizard, cmake, ctest
# ---------------------------------------------------------------------------------------------


SOURCE_SUFFIXES = ('.cpp', '.cc', '.cxx', '.h', '.hpp')
DEFAULT_EXCLUDE = ('3rdparty/', 'tests/', 'build')


def _read(path):
    with open(path, encoding='utf-8', errors='surrogateescape') as f:
        return f.read()


def _write(path, text):
    # Keep the mtime moving forward even when a mutant and the original are written within one tick.
    with open(path, 'w', encoding='utf-8', errors='surrogateescape', newline='') as f:
        f.write(text)


def _run(cmd, cwd=None, env=None, timeout=None, check=True):
    return subprocess.run(cmd, cwd=cwd, env=env, timeout=timeout, check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def git_root():
    return _run(['git', 'rev-parse', '--show-toplevel']).stdout.strip()


def lizard_program(root):
    found = shutil.which('lizard')
    if found:
        return found
    out = _run(['nix', 'build', '--no-link', '--print-out-paths', '--inputs-from', root,
                'nixpkgs#python3Packages.lizard'], cwd=root).stdout.split()[-1]
    return os.path.join(out, 'bin', 'lizard')


def lizard_functions(root, paths):
    if not paths:
        return []
    out = _run([lizard_program(root), '--csv', '-l', 'cpp', '-t', '8'] + list(paths), cwd=root, check=False)
    return parse_lizard_csv(out.stdout)


def is_source(path, include_tests=False):
    if not path.endswith(SOURCE_SUFFIXES):
        return False
    excluded = DEFAULT_EXCLUDE if not include_tests else tuple(e for e in DEFAULT_EXCLUDE if e != 'tests/')
    return not any(path.startswith(e) for e in excluded)


def scope_lines(root, args):
    """{path: lines} the run looks at: --files (whole files) or the diff against --base."""
    if args.files:
        result = {}
        for path in args.files:
            rel = os.path.relpath(os.path.abspath(path), root)
            count = _read(os.path.join(root, rel)).count('\n') + 1
            result[rel] = set(range(1, count + 1))
        return result
    base = _run(['git', 'merge-base', args.base, 'HEAD'], cwd=root).stdout.strip()
    diff = _run(['git', 'diff', '-U0', '--no-color', '--no-ext-diff', base, '--'], cwd=root).stdout
    untracked = _run(['git', 'ls-files', '--others', '--exclude-standard'], cwd=root).stdout.split()
    lines = changed_lines(diff)
    for path in untracked:
        if os.path.isfile(os.path.join(root, path)):
            lines[path] = set(range(1, _read(os.path.join(root, path)).count('\n') + 2))
    return only_paths({p: ls for p, ls in lines.items() if is_source(p, args.include_tests)}, args.only)


def only_paths(lines, only):
    """`lines` limited to the paths `only` names (all of them when it names none)."""
    if not only:
        return lines
    wanted = {os.path.normpath(p) for p in only}
    return {p: ls for p, ls in lines.items() if os.path.normpath(p) in wanted}


def scoped_functions(root, args):
    lines = scope_lines(root, args)
    functions = lizard_functions(root, sorted(p for p in lines if os.path.exists(os.path.join(root, p))))
    return in_scope(functions, lines), lines


def _abs(build, root):
    return build if os.path.isabs(build) else os.path.join(root, build)

# ---------------------------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------------------------


COVERAGE_FLAGS = '-fprofile-instr-generate -fcoverage-mapping'


def _need(program):
    if not shutil.which(program):
        sys.exit(f'quality: {program} not found — run inside `nix develop`')


def _configure_coverage(root, build):
    if os.path.exists(os.path.join(build, 'build.ninja')):
        return
    cxx = shutil.which('c++')
    env = dict(os.environ)
    bindir = os.path.dirname(cxx)
    env['CC'], env['CXX'] = os.path.join(bindir, 'clang'), os.path.join(bindir, 'clang++')
    link = '-fprofile-instr-generate'
    subprocess.run(['cmake', '-S', root, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Debug',
                    '-DCMAKE_CXX_COMPILER_LAUNCHER=ccache', '-DBRULION_WERROR=ON',
                    f'-DCMAKE_CXX_FLAGS={COVERAGE_FLAGS}', f'-DCMAKE_C_FLAGS={COVERAGE_FLAGS}',
                    f'-DCMAKE_EXE_LINKER_FLAGS={link}', f'-DCMAKE_SHARED_LINKER_FLAGS={link}',
                    f'-DCMAKE_MODULE_LINKER_FLAGS={link}'], env=env, check=True)


def test_programs(build, tests):
    """The executables ctest runs for the tests matching `tests` (their first argument)."""
    out = _run(['ctest', '--test-dir', build, '--show-only=json-v1', '-R', tests]).stdout
    programs = []
    for test in json.loads(out).get('tests', []):
        command = test.get('command') or []
        if command and os.path.isfile(command[0]) and command[0].startswith(build):
            programs.append(command[0])
    return sorted(set(programs))


def cmd_coverage(args):
    for program in ('cmake', 'ctest', 'llvm-profdata', 'llvm-cov', 'c++'):
        _need(program)
    root = git_root()
    build = _abs(args.build, root)
    _configure_coverage(root, build)
    subprocess.run(['cmake', '--build', build, '-j', str(args.jobs), '--target'] + args.targets, check=True)
    prof = os.path.join(build, 'quality-prof')
    shutil.rmtree(prof, ignore_errors=True)
    os.makedirs(prof)
    env = dict(os.environ, LLVM_PROFILE_FILE=os.path.join(prof, '%p-%m.profraw'),
               QT_QPA_PLATFORM=os.environ.get('QT_QPA_PLATFORM', 'offscreen'))
    tests = subprocess.run(['ctest', '--test-dir', build, '--output-on-failure', '-j', str(args.jobs),
                            '-R', args.tests], env=env)
    raws = [os.path.join(prof, f) for f in os.listdir(prof) if f.endswith('.profraw')]
    if not raws:
        sys.exit('quality: no profile written — did the tests run?')
    merged = os.path.join(prof, 'merged.profdata')
    subprocess.run(['llvm-profdata', 'merge', '-sparse', '-o', merged] + raws, check=True)
    programs = test_programs(build, args.tests) + [_abs(o, root) for o in args.objects]
    if not programs:
        sys.exit('quality: no test executables found for ' + args.tests)
    cmd = ['llvm-cov', 'export', '-format=lcov', '-instr-profile=' + merged, programs[0]]
    for program in programs[1:]:
        cmd += ['-object', program]
    lcov = os.path.join(build, 'quality-coverage.lcov')
    with open(lcov, 'w', encoding='utf-8') as f:
        subprocess.run(cmd, stdout=f, check=True)
    print(f'coverage: {lcov} ({len(programs)} programs, tests exit {tests.returncode})')
    return tests.returncode


def _hits_for(lcov, root, path):
    return lcov.get(os.path.join(root, path)) or lcov.get(os.path.realpath(os.path.join(root, path)))


def cmd_crap(args):
    root = git_root()
    functions, _ = scoped_functions(root, args)
    lcov_path = _abs(args.lcov, root)
    lcov = parse_lcov(_read(lcov_path)) if os.path.exists(lcov_path) else {}
    if not lcov:
        print(f'crap: no coverage at {lcov_path} — every function counts as uncovered '
              '(quality.py coverage writes it)')
    rows = []
    for fn in functions:
        hits = _hits_for(lcov, root, fn.path) or {}
        cov = function_coverage(fn, hits)
        rows.append((crap(fn.ccn, cov or 0.0), fn, cov))
    rows.sort(key=lambda r: -r[0])
    over = [r for r in rows if r[0] >= args.threshold]
    print(f'{"CRAP":>7} {"CCN":>4} {"cov":>5}  function')
    for score, fn, cov in rows if args.all else over or rows[:10]:
        shown = 'n/a' if cov is None else f'{cov * 100:.0f}%'
        print(f'{score:7.1f} {fn.ccn:4d} {shown:>5}  {fn.name}  {fn.path}:{fn.start}')
    print(f'crap: {len(rows)} functions, {len(over)} at or above {args.threshold:g}')
    return 1 if over else 0


def cmd_dry(args):
    root = git_root()
    scoped, _ = scoped_functions(root, args)
    against = [p for p in args.against]
    everything = lizard_functions(root, against)
    by_path = {}
    for fn in everything + scoped:
        by_path.setdefault(fn.path, set()).add(fn)
    shapes = {}
    cache = shape_cache_dir()
    for path, fns in by_path.items():
        full = os.path.join(root, path)
        if not os.path.exists(full) or not is_source(path, include_tests=True):
            continue
        lines = cached_file_shape(_read(full), cache)
        for fn in fns:
            shapes[fn] = shape_of(lines, fn.start, fn.end)
    everything = [f for f in set(everything) | set(scoped) if is_source(f.path, args.include_tests)]
    pairs = similar_pairs(scoped, everything, shapes, args.threshold, args.min_tokens)
    for score, a, b in pairs:
        print(f'{score:.2f}  {a.name}  {a.path}:{a.start}\n      {b.name}  {b.path}:{b.start}')
    print(f'dry: {len(scoped)} functions in scope, {len(pairs)} pairs at or above {args.threshold:g}')
    return 1 if pairs else 0


def _build_and_test(build, args, timeout):
    built = _run(['cmake', '--build', build, '-j', str(args.jobs), '--target'] + args.targets, check=False)
    if built.returncode != 0:
        return 'invalid', built.stdout
    env = dict(os.environ, QT_QPA_PLATFORM=os.environ.get('QT_QPA_PLATFORM', 'offscreen'))
    try:
        tested = _run(['ctest', '--test-dir', build, '-j', str(args.jobs), '--stop-on-failure',
                       '--timeout', str(timeout), '-R', args.tests], env=env, timeout=timeout * 4, check=False)
    except subprocess.TimeoutExpired:
        return 'killed', 'timeout'
    if 'No tests were found' in tested.stdout:
        sys.exit('quality: no tests match ' + args.tests)
    return ('survived' if tested.returncode == 0 else 'killed'), tested.stdout


def _mutation_plan(root, args):
    functions, lines = scoped_functions(root, args)
    lcov = parse_lcov(_read(_abs(args.lcov, root))) if args.lcov else {}
    plan, uncovered = [], []
    for path in sorted({f.path for f in functions}):
        src = _read(os.path.join(root, path))
        hits = _hits_for(lcov, root, path) if lcov else None
        wanted = set()
        for fn in (f for f in functions if f.path == path):
            span = range(fn.start, fn.end + 1)
            wanted.update(span if args.whole_functions else (n for n in span if n in lines.get(path, ())))
        if hits is not None:
            uncovered += [(path, n) for n in sorted(wanted) if hits.get(n) == 0]
            wanted = {n for n in wanted if hits.get(n, 1) > 0}
        plan += mutants(src, tokenize(src), wanted, path)
    return sample(plan, args.max), uncovered


def cmd_mutate(args):
    for program in ('cmake', 'ctest'):
        _need(program)
    root = git_root()
    build = _abs(args.build, root)
    journal = os.path.join(build, 'quality-mutate.journal')
    if restore_from_journal(journal):
        print('mutate: restored a file a crashed run left mutated')
    plan, uncovered = _mutation_plan(root, args)
    print(f'mutate: {len(plan)} mutants, {len(uncovered)} uncovered lines')
    started = time.monotonic()
    baseline, log = _build_and_test(build, args, args.timeout)
    if baseline != 'survived':
        print(log[-4000:])
        sys.exit(f'mutate: the tests must pass before mutating (baseline: {baseline})')
    test_seconds = max(1, int(time.monotonic() - started))
    results = {'killed': [], 'survived': [], 'invalid': []}
    for n, m in enumerate(plan, 1):
        path = os.path.join(root, m.path)
        original = _read(path)
        write_journal(journal, path, original)
        try:
            _write(path, apply(original, m))
            outcome, _ = _build_and_test(build, args, max(args.timeout, test_seconds * 3))
        finally:
            _write(path, original)
            os.remove(journal)
        results[outcome].append(m)
        print(f'[{n}/{len(plan)}] {outcome:8} {m.describe()}', flush=True)
    _run(['cmake', '--build', build, '-j', str(args.jobs), '--target'] + args.targets, check=False)
    for path, line in uncovered:
        print(f'uncovered  {path}:{line}')
    for m in results['survived']:
        print(f'SURVIVED   {m.describe()}')
    valid = len(results['killed']) + len(results['survived'])
    score = 100.0 * len(results['killed']) / valid if valid else 100.0
    print(f'mutate: {len(results["killed"])} killed, {len(results["survived"])} survived, '
          f'{len(results["invalid"])} did not compile, {len(uncovered)} uncovered lines — score {score:.0f}%')
    return 1 if results['survived'] or uncovered else 0


def _scope_options(p):
    p.add_argument('--base', default='main', help='diff against the merge base with this ref (default main)')
    p.add_argument('--files', nargs='*', default=[], help='whole files instead of the diff')
    p.add_argument('--include-tests', action='store_true', help='tests/ count too')
    p.add_argument('--only', nargs='*', default=[], help='of the diff, only these files')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    jobs = max(1, min(6, os.cpu_count() or 1))

    p = sub.add_parser('coverage', help='instrumented build + tests → LCOV')
    p.add_argument('--build', default='build-cov')
    p.add_argument('--targets', nargs='+', required=True)
    p.add_argument('--tests', required=True, help='ctest -R regex')
    p.add_argument('--objects', nargs='*', default=[], help='more instrumented programs the tests start')
    p.add_argument('--jobs', type=int, default=jobs)
    p.set_defaults(run=cmd_coverage)

    p = sub.add_parser('crap', help='CRAP per function in scope')
    _scope_options(p)
    p.add_argument('--lcov', default='build-cov/quality-coverage.lcov')
    p.add_argument('--threshold', type=float, default=10.0)
    p.add_argument('--all', action='store_true', help='list every function, not only those over')
    p.set_defaults(run=cmd_crap)

    p = sub.add_parser('dry', help='near-duplicate functions')
    _scope_options(p)
    p.add_argument('--against', nargs='*', default=['src'], help='where duplicates are looked for')
    p.add_argument('--threshold', type=float, default=0.8)
    p.add_argument('--min-tokens', type=int, default=40, help='shorter functions are never compared')
    p.set_defaults(run=cmd_dry)

    p = sub.add_parser('mutate', help='mutation testing of the lines in scope')
    _scope_options(p)
    p.add_argument('--build', default='build-clang', help='a configured build dir (not the coverage one)')
    p.add_argument('--targets', nargs='+', required=True)
    p.add_argument('--tests', required=True, help='ctest -R regex')
    p.add_argument('--lcov', default='', help='skip lines this coverage never ran (they are reported)')
    p.add_argument('--whole-functions', action='store_true', help='every line of a changed function')
    p.add_argument('--max', type=int, default=80, help='at most this many mutants, spread evenly')
    p.add_argument('--timeout', type=int, default=120, help='seconds per test')
    p.add_argument('--jobs', type=int, default=jobs)
    p.set_defaults(run=cmd_mutate)

    args = parser.parse_args(argv)
    return args.run(args)


if __name__ == '__main__':
    sys.exit(main())
