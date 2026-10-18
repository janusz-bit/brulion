#!/usr/bin/env python3
"""Attribute GUI-thread stalls to code: slice a perf recording by the probe's time windows.

  perf record -k CLOCK_MONOTONIC -F 999 -g --call-graph dwarf,16384 -o x.data -- env LD_PRELOAD=… brulion …
  perf script -i x.data -F tid,time,ip,sym,dso > x.txt   (perf-scenario.sh does both)
  tests/bench/app/perf-slice.py x.txt run.jsonl --window after_ready [--window autosave …] [--top 40]

Windows (from the probe's marks and stalls, GUI thread only):
  all            the whole run
  startup        exec .. content frame
  indexing       content .. ready
  after_ready    ready .. settled
  stalls:<ms>    every stall longer than <ms> (stalls:<ms>@<mark>..<mark>: only between two marks)
  autosave       the stalls 30..1700 ms after each typing burst's last key
  keys           the synchronous handling of every synthetic key (t0..t2)
  after:<label>:<ms> / after1:<label>:<ms>   each key action with that label (or the first one)
                 up to <ms> after it: the handling plus the polish, layout and rendering after it
  mark:<a>..<b>  from mark a to mark b (mark names may contain ':')
For every window: number of samples, the top self symbols and the top inclusive symbols
(a frame counted once per sample), and the top inclusive symbols of the app's own code
(brulion::…, QML-compiled functions) — what to fix.
"""
import argparse
import collections
import json
import re
import sys


def read_probe(path):
    marks, stalls, actions, start = {}, [], [], None
    for line in open(path):
        o = json.loads(line)
        if o["ev"] == "start":
            start = o
        elif o["ev"] == "mark":
            marks.setdefault(o["name"], o)
        elif o["ev"] == "stalls":
            stalls = o["list"]
        elif o["ev"] == "actions":
            actions = o["list"]
    return start, marks, stalls, actions


def read_samples(path, tid):
    """Yield (time_s, [frames leaf→root]) for samples of thread `tid`."""
    head = re.compile(r"^\s*(\d+)\s+(\d+\.\d+):")
    cur = None
    frames = []
    with open(path, errors="replace") as f:
        for line in f:
            if not line.strip():
                if cur is not None and cur[0] == tid:
                    yield cur[1], frames
                cur, frames = None, []
                continue
            m = head.match(line)
            if m and not line.startswith("\t") and not line.startswith(" " * 8):
                if cur is not None and cur[0] == tid:
                    yield cur[1], frames
                cur, frames = (int(m.group(1)), float(m.group(2))), []
                continue
            parts = line.strip().split(None, 1)
            if len(parts) == 2:
                sym = parts[1]
                sym = re.sub(r"\+0x[0-9a-f]+", "", sym)
                frames.append(sym)
    if cur is not None and cur[0] == tid:
        yield cur[1], frames


def short(sym, width=150):
    sym = sym.replace("(/nix/store/", "(").replace("brulion::", "b::")
    return sym if len(sym) <= width else sym[:width] + "…"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("script")
    ap.add_argument("probe")
    ap.add_argument("--window", action="append", default=[])
    ap.add_argument("--top", type=int, default=30)
    args = ap.parse_args()
    start, marks, stalls, actions = read_probe(args.probe)
    t0 = int(start["t0ns"]) / 1e9
    pid = start["pid"]

    def abs_t(ms):
        return t0 + ms / 1000.0

    windows = {}
    for w in args.window or ["all"]:
        spans = []
        if w == "all":
            spans = [(0, 1e12)]
        elif w == "startup":
            spans = [(marks["exec"]["t"], marks.get("content", marks["ready"])["t"])]
        elif w == "indexing":
            spans = [(marks["content"]["t"], marks["ready"]["t"])]
        elif w == "after_ready":
            spans = [(marks["ready"]["t"], marks["settled"]["t"] + 1)]
        elif w.startswith("stalls:"):
            # stalls:<ms> or stalls:<ms>@<mark a>:<mark b> (only stalls starting between the marks)
            spec, _, rng = w[len("stalls:"):].partition("@")
            lim = float(spec)
            lo, hi = 0.0, 1e12
            if rng:
                a, b = rng.split("..", 1)
                lo, hi = marks[a]["t"], marks[b]["t"]
            spans = [(s, s + d) for s, d in stalls if d >= lim and lo <= s <= hi]
        elif w == "autosave":
            for b in range(3):
                e = marks.get("burst_end:%d" % b)
                if not e:
                    continue
                last = max((a["t2"] for a in actions if a["t2"] <= e["t"] + 1), default=e["t"])
                spans += [(s, s + d) for s, d in stalls if last + 30 <= s < e["t"] + 1700]
        elif w == "keys":
            spans = [(a["t0"], a["t2"]) for a in actions if a["kind"] == "key"]
        elif w.startswith("after:") or w.startswith("after1:"):
            # after:<label>:<ms> — from each key action with that label (first one only: after1:)
            # up to <ms> later: its handling plus the polish / layout / rendering that follow.
            _, label, ms = w.split(":", 2)
            acts = [a for a in actions if a["label"] == label]
            if w.startswith("after1:"):
                acts = acts[:1]
            spans = [(a["t0"], a["t0"] + float(ms)) for a in acts]
        elif w.startswith("mark:"):
            a, b = w[len("mark:"):].split("..", 1)
            spans = [(marks[a]["t"], marks[b]["t"])]
        windows[w] = [(abs_t(a), abs_t(b)) for a, b in spans]

    stats = {w: {"n": 0, "self": collections.Counter(), "incl": collections.Counter(), "own": collections.Counter()}
             for w in windows}
    for t, frames in read_samples(args.script, pid):
        for w, spans in windows.items():
            if any(a <= t <= b for a, b in spans):
                st = stats[w]
                st["n"] += 1
                if frames:
                    st["self"][frames[0]] += 1
                seen = set()
                for fr in frames:
                    if fr in seen:
                        continue
                    seen.add(fr)
                    st["incl"][fr] += 1
                    if "brulion::" in fr or "QML" in fr or "qml_" in fr.lower() and "brulion" in fr:
                        st["own"][fr] += 1
    for w, st in stats.items():
        spans = windows[w]
        dur = sum(b - a for a, b in spans) * 1000
        print("=== window %s: %d spans, %.0f ms, %d GUI-thread samples" % (w, len(spans), dur, st["n"]))
        if not st["n"]:
            continue
        for title, key in (("self", "self"), ("inclusive", "incl"), ("inclusive, app code", "own")):
            print("--- top %s" % title)
            for sym, n in st[key].most_common(args.top):
                print("%6.1f%%  %s" % (100.0 * n / st["n"], short(sym)))
        print()


if __name__ == "__main__":
    main()
