#!/usr/bin/env python3
"""Summarise a qmlprofiler trace (.qtd): where the QML time goes.

  qmlprofiler -o trace.qtd -- env LD_PRELOAD=… brulion …   (a build with -DQT_QML_DEBUG; the
  probe quits the app at the end of its scenario, then qmlprofiler writes the trace)
  tests/bench/app/qtd-summary.py trace.qtd [--top 40] [--from-ms A --to-ms B]

For every event type (Creating, Binding, Javascript, HandlingSignal, Compiling, …) the total
time, and per source location (file:line + details) the inclusive time and the call count,
largest first. "Self" time is the inclusive time minus that of ranges nested directly inside
(same thread). Times are from the start of the trace (qmlprofiler's own clock).
"""
import argparse
import collections
import xml.etree.ElementTree as ET


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("trace")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--from-ms", type=float, default=0)
    ap.add_argument("--to-ms", type=float, default=1e15)
    args = ap.parse_args()
    root = ET.parse(args.trace).getroot()
    trace_start = int(root.get("traceStart", "0"))
    events = {}
    for ev in root.iter("event"):
        idx = int(ev.get("index"))
        d = {c.tag: (c.text or "") for c in ev}
        events[idx] = d
    ranges = []
    for r in root.iter("range"):
        start = int(r.get("startTime")) - trace_start
        dur = int(r.get("duration", "0"))
        idx = int(r.get("eventIndex"))
        if dur <= 0:
            continue
        if not (args.from_ms * 1e6 <= start <= args.to_ms * 1e6):
            continue
        ranges.append((start, dur, idx))
    ranges.sort(key=lambda x: (x[0], -x[1]))
    # Self time: subtract direct children (stack of open ranges).
    self_time = collections.Counter()
    incl = collections.Counter()
    count = collections.Counter()
    by_type = collections.Counter()
    stack = []
    for start, dur, idx in ranges:
        end = start + dur
        while stack and stack[-1][1] <= start:
            stack.pop()
        e = events.get(idx, {})
        key = (e.get("type", "?"), e.get("filename", "").replace("qrc:/qt/qml/Brulion/", ""), e.get("line", ""),
               (e.get("details", "") or "")[:80])
        if stack and end <= stack[-1][1]:
            parent = stack[-1][2]
            self_time[parent] -= dur
        else:
            by_type[key[0]] += dur  # top-level ranges only: no double counting per type
        incl[key] += dur
        self_time[key] += dur
        count[key] += 1
        stack.append((start, end, key))
    print("=== top-level time per event type (ms)")
    for t, d in by_type.most_common():
        print("%10.1f  %s" % (d / 1e6, t))
    print("\n=== self time per location (ms, calls, inclusive ms)")
    for key, d in self_time.most_common(args.top):
        t, f, line, det = key
        print("%10.1f  %6d  %10.1f  %-14s %s:%s %s" % (d / 1e6, count[key], incl[key] / 1e6, t, f, line, det))
    print("\n=== inclusive time per location (ms, calls) — Creating = component instantiation")
    for key, d in incl.most_common(args.top):
        t, f, line, det = key
        print("%10.1f  %6d  %-14s %s:%s %s" % (d / 1e6, count[key], t, f, line, det))


if __name__ == "__main__":
    main()
