#!/usr/bin/env python3
"""Writes cmake/startup-order.txt: the order in which the linker lays out the app's functions.

The program is 72 MB of code, and a start runs a ninth of it, spread all over: the kernel reads
and maps whole pages (and their neighbours), so 32 MB of it were resident after a start. With the
functions a start runs laid out first, one after another, 9 MB are (docs/PERFORMANCE.md "Memory:
only what is in use"; cmake/BrulionSize.cmake: brulion_order_startup, lld's
--symbol-ordering-file).

The file lists, by their linker names:
  1. the functions a start of the sample notebook runs (the journal shown, settled);
  2. those a tour of the views (a page, Tasks, Calendar, All pages, Search, Graph, Tags,
     Settings, away and back) and some typing run on top;
  each group in the order the program has them anyway (neighbours stay neighbours), followed by
  the compiled QML units (qmlData) of the QML files the engine had loaded by then (the probe
  lists them) and of those the functions belong to. Everything else follows as the linker lays
  it out without the file. A name the program no longer has is
  skipped by the linker, a function added since is laid out with "everything else": a stale file
  orders less, it never breaks a build.

How: the app runs under callgrind (valgrind) — every function that ran is known, none is sampled —
driven by the benchmark probe (tests/bench/app), offscreen; a few minutes in all.

  nix develop -c scripts/startup-order.py --bin build-rel/src/app/brulion
  nix develop -c scripts/startup-order.py --check build-rel/src/app/brulion

--bin: an optimised build (Release, RelWithDebInfo) of the sources the order is for, with its
symbols. --check: how many of the file's names that build has, and how much of its code they are.
Run it again when the start changed a lot (new views shown at the start, a new Qt) and before a
release; valgrind comes from `nix shell nixpkgs#valgrind` when it is not installed.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORDER = os.path.join(ROOT, "cmake", "startup-order.txt")
PAGE = "Witaj w Brulionie.md"  # the sample notebook's first page (--locale pl)
NOW = "2026-10-14 10:00"       # the day the runs pretend it is (tests/bench/app/run-bench.py: NOW)

HEADER = """\
# The order of the app's functions in the program: what a start runs, then what a tour of the
# views and some typing run on top (lld's --symbol-ordering-file; cmake/BrulionSize.cmake).
# Written by scripts/startup-order.py — do not edit; a name the program no longer has is skipped.
"""


def text_symbols(binary):
    """name → (address, size) of the functions and read-only data the binary defines."""
    out = subprocess.run(["nm", "-S", "--defined-only", binary], capture_output=True, text=True, check=True).stdout
    functions, data = {}, {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        address, size, kind, name = int(parts[0], 16), int(parts[1], 16), parts[2], parts[3]
        if kind in "tTwW":
            # (A name several files define — a static function —: the first address counts.)
            functions.setdefault(name, (address, size))
        elif kind in "rR":
            data.setdefault(name, (address, size))
    return functions, data


def ran(callgrind_file, binary):
    """The linker names of the binary's functions in a callgrind output: the ones that ran."""
    want = os.path.basename(binary)
    objects, names = {}, {}
    current = callee = None
    found = set()
    ref = re.compile(r"^(c?ob|c?fn)=\((\d+)\)(?: (.*))?$")
    with open(callgrind_file, errors="replace") as f:
        for line in f:
            if not line.startswith(("ob=", "cob=", "fn=", "cfn=")):
                continue
            m = ref.match(line.rstrip("\n"))
            if not m:
                continue
            kind, ident, name = m.groups()
            if kind in ("ob", "cob"):
                if name:
                    objects[ident] = name
                if kind == "ob":
                    current, callee = objects.get(ident), None
                else:
                    callee = objects.get(ident)
                continue
            if name:
                names[ident] = name
            function = names.get(ident)
            where = current if kind == "fn" else (callee or current)
            if kind == "cfn":
                callee = None
            if function and where and os.path.basename(where) == want:
                # (callgrind numbers the cycles of recursive functions: "name'2".)
                found.add(function.split("'")[0])
    return found


def qml_units(functions, data):
    """The qmlData symbols of the QML files `functions` belong to (qmlcachegen's namespaces)."""
    unit = re.compile(r"^_ZN21QmlCacheGeneratedCode(\d+)")
    by_namespace = {}
    for name in data:
        m = unit.match(name)
        if m and name.endswith("7qmlDataE"):
            start = m.end()
            by_namespace[name[start:start + int(m.group(1))]] = name
    out = set()
    inside = re.compile(r"21QmlCacheGeneratedCode(\d+)")
    for name in functions:
        m = inside.search(name)
        if m:
            namespace = name[m.end():m.end() + int(m.group(1))]
            if namespace in by_namespace:
                out.add(by_namespace[namespace])
    return out


def loaded_units(events, data):
    """The qmlData symbols of the QML files the engine had loaded after the start, and at any
    later mark of a memory scenario run with BENCH_QML_TYPES=1 (the probe's events file)."""
    by_namespace = {}
    unit = re.compile(r"^_ZN21QmlCacheGeneratedCode(\d+)")
    for name in data:
        m = unit.match(name)
        if m and name.endswith("7qmlDataE"):
            by_namespace[name[m.end():m.end() + int(m.group(1))]] = name
    first, later = set(), set()
    with open(events) as f:
        for line in f:
            if '"qmlTypeNames"' not in line:
                continue
            event = json.loads(line)
            names = {by_namespace.get("_qt_qml_Brulion_" + re.sub(r"[^A-Za-z0-9]", "_", file))
                     for file in event.get("qmlTypeNames", [])} - {None}
            if event.get("name") in ("mem:ready", "mem:settled", "mem:start"):
                first |= names
            later |= names
    return first, later - first


def in_program_order(names, symbols):
    return sorted((n for n in names if n in symbols), key=lambda n: symbols[n][0])


def run(cmd, **kwargs):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, **kwargs)


def events_file(work, name):
    return os.path.join(work, "runs", "%s-order-00.jsonl" % name)


def record(binary, probe, vault, work, name, scenario, env):
    """One scenario of the benchmark probe under callgrind; the callgrind output's path."""
    out = os.path.join(work, "callgrind-" + name)
    os.makedirs(out, exist_ok=True)
    wrap = ("valgrind --tool=callgrind --trace-children=yes --demangle=no "
            "--callgrind-out-file=%s/out.%%p" % out)
    cmd = [sys.executable, os.path.join(ROOT, "tests/bench/app/run-bench.py"), "--bin", binary, "--probe", probe,
           "--label", "order", "--out", os.path.join(work, "runs"), "--workdir", os.path.join(work, "run-" + name),
           "--start", "first", "--runs", "1", "--vault", vault, "--scenario", scenario, "--name", name,
           "--platform", "offscreen", "--timeout", "5400", "--wrap", wrap, "--env", "BENCH_TIMEOUT_MS=5300000"]
    for item in env:
        cmd += ["--env", item]
    clean = {k: v for k, v in os.environ.items() if k not in ("DISPLAY", "WAYLAND_DISPLAY")}
    run(cmd, env=clean, check=True)
    files = [os.path.join(out, f) for f in os.listdir(out) if os.path.getsize(os.path.join(out, f)) > 0]
    if not files:
        sys.exit("startup-order: callgrind wrote nothing for " + name)
    # The probe says how the scenario ended (0: all of its steps ran).
    events = events_file(work, name)
    ended = [line for line in open(events) if '"ev":"end"' in line] if os.path.exists(events) else []
    if not ended or '"code":0' not in ended[-1]:
        sys.exit("startup-order: the scenario %s did not run to its end (%s)"
                 % (name, ended[-1].strip() if ended else "no end event in " + events))
    return max(files, key=os.path.getsize)


def check(binary):
    functions, data = text_symbols(binary)
    listed = [line.strip() for line in open(ORDER) if line.strip() and not line.startswith("#")]
    known = [n for n in listed if n in functions or n in data]
    code = sum(functions[n][1] for n in listed if n in functions)
    total = sum(size for _, size in functions.values())
    print("%s: %d names, %d of them in %s (%.0f %%); their code: %.1f of %.1f MB"
          % (os.path.relpath(ORDER, ROOT), len(listed), len(known), binary,
             100.0 * len(known) / max(1, len(listed)), code / 1e6, total / 1e6))
    return 0 if len(known) * 2 >= len(listed) else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", help="an optimised build of the app with its symbols")
    ap.add_argument("--check", metavar="BIN", help="only report how much of the file that build has")
    ap.add_argument("--out", default=ORDER)
    ap.add_argument("--work", help="where the runs' files go (default: a temporary folder, removed)")
    args = ap.parse_args()
    if args.check:
        return check(os.path.abspath(args.check))
    if not args.bin:
        ap.error("--bin or --check")
    binary = os.path.abspath(args.bin)
    if not shutil.which("valgrind"):
        # (Nix-only tooling: the same command again inside a shell that has valgrind.)
        os.execvp("nix", ["nix", "shell", "nixpkgs#valgrind", "-c", sys.executable] + sys.argv)

    work = os.path.abspath(args.work) if args.work else tempfile.mkdtemp(prefix="startup-order-")
    os.makedirs(work, exist_ok=True)
    try:
        probe_dir = os.path.join(work, "probe")
        run([os.path.join(ROOT, "tests/bench/app/build-probe.sh"), probe_dir], check=True)
        probe = os.path.join(probe_dir, "libbenchprobe.so")
        # The sample notebook, seeded by the app itself (its own profile and folders).
        vault = os.path.join(work, "vault")
        home = os.path.join(work, "home")
        seed_env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
                        XDG_CONFIG_HOME=home + "/config", XDG_DATA_HOME=home + "/data",
                        XDG_CACHE_HOME=home + "/cache", XDG_STATE_HOME=home + "/state")
        # (Seeded on the runs' own day: the sample journal's days are around "today".)
        run([binary, "--no-single-instance", "--profile", "startup-order", "--demo", "--vault", vault,
             "--locale", "pl", "--now", NOW, "--screenshot", os.path.join(work, "seed.png")],
            env=seed_env, check=True)
        tour_env = ["BENCH_SETTLE_MS=1000", "BENCH_REST_MS=500", "BENCH_PAGE=" + PAGE, "BENCH_QML_TYPES=1"]
        start_files = [record(binary, probe, vault, work, "start", "startup", [])]
        tour_files = [record(binary, probe, vault, work, "tour", "memory", tour_env),
                      record(binary, probe, vault, work, "typing", "typing", ["BENCH_PAGE=" + PAGE])]

        functions, data = text_symbols(binary)
        start = set().union(*(ran(f, binary) for f in start_files))
        tour = set().union(*(ran(f, binary) for f in tour_files)) - start
        first = in_program_order(start, functions)
        second = in_program_order(tour, functions)
        loaded_first, loaded_later = loaded_units(events_file(work, "tour"), data)
        units_first = in_program_order(qml_units(first, data) | loaded_first, data)
        units_second = in_program_order((qml_units(second, data) | loaded_later) - set(units_first), data)
        if len(first) < 1000:
            sys.exit("startup-order: only %d functions of the start found in %s — is it stripped?"
                     % (len(first), binary))
        with open(args.out, "w") as f:
            f.write(HEADER)
            f.write("\n".join(first + units_first + second + units_second) + "\n")
        size = lambda names, table: sum(table[n][1] for n in names) / 1e6
        print("%s: the start %d functions (%.1f MB) and %d QML units (%.1f MB); the tour %d more (%.1f MB) "
              "and %d units (%.1f MB)" % (os.path.relpath(args.out, ROOT), len(first), size(first, functions),
                                         len(units_first), size(units_first, data), len(second),
                                         size(second, functions), len(units_second), size(units_second, data)))
    finally:
        if not args.work:
            shutil.rmtree(work, ignore_errors=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
