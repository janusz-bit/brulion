# Quality gates for the C++ code

unclebob's [crapper](https://github.com/unclebob/crapper), [dryer](https://github.com/unclebob/dryer)
and [mutator](https://github.com/unclebob/mutator) read Clojure, Java, Go, TypeScript, Rust, Python and
Swift — not C++. `quality.py` gives Brulion's C++ the same three gates: the CRAP score of each function,
near-duplicate functions and mutation testing. Functions and their cyclomatic complexity come from
[lizard](https://github.com/terryyin/lizard) (fetched from the flake's nixpkgs when it is not on PATH),
coverage from Clang's source-based coverage (`llvm-profdata`, `llvm-cov` of the dev shell).

By default every command looks at the functions the branch changed: lines of `git diff <merge base
with main>` (committed or not) and untracked files, under `src/` (not `3rdparty/`, `tests/` or build
dirs). `--files F…` takes whole files instead, `--base REF` another base, `--only F…` only those files
of the diff (mutating each against its own tests), `--include-tests` the tests too. Run from the root of the worktree being checked (the script may live in another worktree).

## Coverage

```sh
nix develop -c python3 scripts/quality/quality.py coverage \
    --targets tst_gitcli tst_gitcli_repo --tests '^tst_gitcli'
```

Configures `build-cov` (once: Debug, Clang, `-fprofile-instr-generate -fcoverage-mapping`, otherwise as
`scripts/dev-build.sh`), builds the targets, runs the matching tests with `LLVM_PROFILE_FILE` and writes
`build-cov/quality-coverage.lcov` from every test executable (`--objects` adds programs the tests start,
such as `build-cov/src/cli/brulion-cli`). `--build DIR` and `--jobs N` (default 6) as usual.

## CRAP

```sh
nix develop -c python3 scripts/quality/quality.py crap [--all] [--threshold 10]
```

CRAP = CCN² × (1 − coverage)³ + CCN (crap4j's formula). Coverage of a function = the share of its
instrumented lines that ran; a function with no coverage data counts as uncovered. Prints the
functions at or above the threshold (or the ten worst; `--all` every one) and exits 1 when any is.
With full coverage CRAP equals CCN, so a function over 10 must be split, whatever its tests — lizard
counts `if`, loops, `case`, `catch`, `?:`, `&&` and `||`. Long `switch` tables are the usual exception
worth thinking about before splitting.

## Duplication

```sh
nix develop -c python3 scripts/quality/quality.py dry [--against src] [--threshold 0.8] [--min-tokens 40]
```

Each function becomes dryer's shape: keywords, operators, the names of called functions and qualified
names (`Kind::Added` — types and enum values) stay; other identifiers (locals, fields) and literals
become `ID` / `LIT`. Changed functions are compared with every function under `--against` (difflib's
ratio of the shapes); pairs at or above the threshold are printed and the exit status is 1. Shapes are
cached by file content under `~/.cache/brulion-quality/shapes`, shared by every worktree (the first run
over `src/` takes about a minute, later ones about half of that).

## Mutation testing

```sh
nix develop -c python3 scripts/quality/quality.py mutate \
    --targets tst_gitcli --tests '^tst_gitcli$' [--lcov build-cov/quality-coverage.lcov] [--max 80]
```

Uses an existing build dir (`--build`, default `build-clang`; build the targets once first). The tests
must pass unchanged, then each mutant is written into the source, the targets rebuilt and the tests run:
a failing build is "did not compile", a failing (or timed-out) test "killed", passing tests "survived".
Mutants, on changed lines of changed functions (`--whole-functions`: every line of them), never inside
strings, comments or preprocessor lines:

* `==` ↔ `!=`, `<` ↔ `<=`, `>` ↔ `>=`, `&&` ↔ `||`, binary `+` ↔ `-`, `+=` ↔ `-=` (operators with spaces
  around them, so template brackets and `T&&` stay), `!` removed, `true` ↔ `false`;
* decimal integers: 0 → 1, 1 → 0, n → n + 1;
* a call statement on a line of its own (`emitChanged(a);`) removed.

With `--lcov`, lines the tests never ran are listed as uncovered instead of mutated. At most `--max`
mutants, spread evenly over the list (the same ones each run). The exit status is 1 when a mutant
survived or a line is uncovered. A surviving mutant is either a missing test or an equivalent mutant
(the same behaviour, e.g. `x >= 0` → `x >= 1` where 0 is also the default) — say which in the report.

A mutated file is restored after every mutant; the original is kept in
`<build>/quality-mutate.journal` while it is changed, and the next run restores it first if a run was
killed half-way. Each mutant costs a rebuild and a test run (about 7 s for one core file and
tst_gitcli).

## Tests

```sh
python3 -I -m unittest discover -s scripts/quality -p 'test_*.py'
```
