#!/usr/bin/env python3
"""Run one benchmark scenario of the real app N times with the LD_PRELOAD probe.

  tests/bench/app/run-bench.py --bin build/src/app/brulion --probe libbenchprobe.so \
      --vault <pristine vault> --scenario startup --runs 7 --label cur --out results/ \
      [--start first|warm|cold] [--platform xvfb|offscreen] [--size 1280x800] \
      [--route '{"view":"page","path":"Bench/Konspekt 5000.md"}'] [--env BENCH_PAGE=…] [--name startup-10k]

Every run gets a working copy of the vault (the pristine one is never written) at the same path,
so the app's device state (XDG data: history, recent pages) and caches (fontconfig) in the run's
XDG home carry over between runs, as for a user who starts the app again:
  --start first   a fresh XDG home for every run (the very first start: caches, device state)
  --start warm    one XDG home for all runs; one discarded warm-up run first (default)
  --start cold    like warm, but before each run the page cache of the binary, every shared
                  library / font / QML file the app maps and the vault's files is dropped
                  (posix_fadvise DONTNEED after sync; pages other processes map stay resident)
Output: <out>/<name>-<label>-NN.jsonl per run (probe events) and <name>-<label>.runs.json.
Platform: xvfb starts one Xvfb (1920x1200x24, GLX → Mesa llvmpipe) for the batch; offscreen uses
QT_QPA_PLATFORM=offscreen + QT_QUICK_BACKEND=software (no X server; the Pi).
"""
import argparse
import json
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time

NOW = "2026-10-14 10:00"


def start_xvfb():
    for n in range(140, 200):
        if os.path.exists("/tmp/.X11-unix/X%d" % n) or os.path.exists("/tmp/.X%d-lock" % n):
            continue
        xvfb = shutil.which("Xvfb")
        if not xvfb:
            sys.exit("run-bench: Xvfb not found (nix shell nixpkgs#xorg.xvfb)")
        p = subprocess.Popen([xvfb, ":%d" % n, "-screen", "0", "1920x1200x24", "-nolisten", "tcp"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists("/tmp/.X11-unix/X%d" % n):
                return p, ":%d" % n
            if p.poll() is not None:
                break
            time.sleep(0.05)
        p.kill()
    sys.exit("run-bench: could not start Xvfb")


def evict(paths):
    """Drop the page cache of these files (and every file under these directories)."""
    files = []
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                files += [os.path.join(root, n) for n in names]
        elif os.path.isfile(p):
            files.append(p)
    os.sync()
    total = 0
    for f in files:
        try:
            fd = os.open(f, os.O_RDONLY)
        except OSError:
            continue
        try:
            total += os.fstat(fd).st_size
            os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
        except OSError:
            pass
        finally:
            os.close(fd)
    return len(files), total


def mapped_files(pid):
    out = set()
    try:
        with open("/proc/%d/maps" % pid) as f:
            for line in f:
                parts = line.split(None, 5)
                if len(parts) == 6 and parts[5].startswith("/"):
                    path = parts[5].strip()
                    if not path.endswith("(deleted)") and os.path.isfile(path):
                        out.add(path)
    except OSError:
        pass
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", required=True)
    ap.add_argument("--probe", required=True)
    ap.add_argument("--vault", required=True, help="pristine vault (copied for every run)")
    ap.add_argument("--scenario", default="startup")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--label", required=True)
    ap.add_argument("--name", default=None)
    ap.add_argument("--out", required=True)
    ap.add_argument("--start", choices=["first", "warm", "cold"], default="warm")
    ap.add_argument("--platform", choices=["xvfb", "offscreen"], default="xvfb")
    ap.add_argument("--size", default="1280x800")
    ap.add_argument("--route", default=None)
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("--workdir", default=None, help="where vault copies and XDG homes live")
    ap.add_argument("--wrap", default="", help="command prefix for the app (e.g. 'perf record -g -o x --')")
    ap.add_argument("--wrap-env", action="store_true",
                    help="keep LD_PRELOAD in the wrapper's environment (a Qt tool such as qmlprofiler that "
                         "starts the app itself; the probe only activates inside brulion)")
    ap.add_argument("--app-arg", action="append", default=[])
    args = ap.parse_args()

    name = args.name or args.scenario
    os.makedirs(args.out, exist_ok=True)
    work = args.workdir or tempfile.mkdtemp(prefix="brulion-bench-")
    os.makedirs(work, exist_ok=True)
    vault = os.path.join(work, "Notatnik testowy")
    home = None
    xvfb = None
    display = None
    if args.platform == "xvfb":
        xvfb, display = start_xvfb()
    mapped = set()
    results = []
    try:
        total_runs = args.runs + (1 if args.start in ("warm", "cold") else 0)
        for i in range(total_runs):
            warmup = args.start in ("warm", "cold") and i == 0
            if args.start == "first" or home is None:
                if home:
                    shutil.rmtree(home, ignore_errors=True)
                home = tempfile.mkdtemp(prefix="home-", dir=work)
            if os.path.exists(vault):
                shutil.rmtree(vault)
            shutil.copytree(args.vault, vault, symlinks=True)
            evicted = None
            if args.start == "cold" and not warmup:
                evicted = evict(sorted(mapped | {os.path.realpath(args.bin), vault}))
                time.sleep(0.5)
            env = dict(os.environ)
            env.update({
                "XDG_CONFIG_HOME": os.path.join(home, "config"),
                "XDG_DATA_HOME": os.path.join(home, "data"),
                "XDG_CACHE_HOME": os.path.join(home, "cache"),
                "XDG_DATA_DIRS": os.environ.get("PROBE_XDG_DATA_DIRS", "/run/current-system/sw/share"),
                "LD_PRELOAD": os.path.abspath(args.probe),
                "BENCH_SCENARIO": args.scenario,
                "QT_FORCE_STDERR_LOGGING": "1",
            })
            env.pop("QT_QPA_PLATFORM", None)
            if args.platform == "xvfb":
                env["DISPLAY"] = display
                env.pop("WAYLAND_DISPLAY", None)
                env["QT_QPA_PLATFORM"] = "xcb"
            else:
                env["QT_QPA_PLATFORM"] = "offscreen"
                env["QT_QUICK_BACKEND"] = "software"
            for kv in args.env:
                k, _, v = kv.partition("=")
                env[k] = v
            tag = "warmup" if warmup else "%02d" % (i - (1 if args.start in ("warm", "cold") else 0))
            out = os.path.join(args.out, "%s-%s-%s.jsonl" % (name, args.label, tag))
            env["BENCH_OUT"] = out
            cmd = [args.bin, "--no-single-instance", "--vault", vault, "--size", args.size,
                   "--locale", "pl", "--now", NOW]
            if args.route:
                cmd += ["--route", args.route]
            cmd += args.app_arg
            if args.wrap and args.wrap_env:
                cmd = args.wrap.split() + cmd
            elif args.wrap:
                # A profiler in front (perf record …): only the app itself loads the probe.
                env.pop("LD_PRELOAD")
                cmd = args.wrap.split() + ["env", "LD_PRELOAD=" + os.path.abspath(args.probe)] + cmd
            log = open(out + ".log", "w")
            t0 = time.monotonic_ns()
            env["BENCH_T0"] = str(t0)
            p = subprocess.Popen(cmd, env=env, stdout=log, stderr=subprocess.STDOUT)
            code = None
            deadline = time.time() + args.timeout
            sampled = False
            while time.time() < deadline:
                code = p.poll()
                if code is not None:
                    break
                if args.start == "cold" and warmup and time.monotonic_ns() - t0 > 0.7e9:
                    mapped |= mapped_files(p.pid)
                    sampled = True
                time.sleep(0.05)
            if code is None:
                p.send_signal(signal.SIGTERM)
                try:
                    code = p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    code = p.wait()
                code = "timeout"
            wall = (time.monotonic_ns() - t0) / 1e6
            log.close()
            r = {"run": tag, "file": os.path.basename(out), "exit": code, "wall_ms": wall, "warmup": warmup}
            if evicted:
                r["evicted_files"], r["evicted_bytes"] = evicted
            results.append(r)
            print("run-bench: %s %s %s exit=%s %.0f ms" % (name, args.label, tag, code, wall), file=sys.stderr)
            time.sleep(0.3)
    finally:
        if xvfb:
            xvfb.terminate()
            try:
                xvfb.wait(5)
            except subprocess.TimeoutExpired:
                xvfb.kill()
        if not args.workdir:
            shutil.rmtree(work, ignore_errors=True)
        elif home:
            pass
    meta = {"name": name, "label": args.label, "scenario": args.scenario, "start": args.start,
            "platform": args.platform, "size": args.size, "route": args.route, "env": args.env,
            "bin": os.path.realpath(args.bin), "runs": results, "host": os.uname().nodename,
            "machine": os.uname().machine}
    with open(os.path.join(args.out, "%s-%s.runs.json" % (name, args.label)), "w") as f:
        json.dump(meta, f, indent=1)


if __name__ == "__main__":
    main()
