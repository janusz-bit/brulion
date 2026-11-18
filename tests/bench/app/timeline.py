#!/usr/bin/env python3
"""Print one run's events in time order (marks, synthetic input, stalls > 12 ms with their CPU,
model signals, saves, signal spans of the typing windows, file system calls of the GUI thread, and
frame gaps > 50 ms) — for reading what happened around a slow moment.

  tests/bench/app/timeline.py run.jsonl [--from ms] [--to ms] [--frames] [--span-ms 1]
"""
import argparse
import json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--from", dest="a", type=float, default=0)
    ap.add_argument("--to", dest="b", type=float, default=1e12)
    ap.add_argument("--frames", action="store_true", help="every frame, not only gaps")
    ap.add_argument("--span-ms", type=float, default=1.0, help="signal spans at least this long (inclusive)")
    args = ap.parse_args()
    ev = []
    swaps = []
    for line in open(args.run):
        o = json.loads(line)
        k = o["ev"]
        if k == "mark":
            extra = {x: o[x] for x in ("VmRSS", "items") if x in o}
            ev.append((o["t"], "mark   %s %s" % (o["name"], extra if extra else "")))
        elif k == "stalls":
            for x in o["list"]:
                s, d = x[0], x[1]
                cpu = " cpu %.1f ms, %.1f M instructions" % (x[2], x[3]) if len(x) > 3 else ""
                ev.append((s, "STALL  %.1f ms (ends %.1f)%s" % (d, s + d, cpu)))
        elif k == "spans":
            for x in o["list"]:
                if x[3] >= args.span_ms:
                    ev.append((x[0], "span   %s%s%s incl %.2f self %.2f ms cpu %.2f ms %.2f M insns%s" % (
                        "  " * x[2], o["names"][x[1]], " (qml)" if x[10] else "", x[3], x[4], x[5], x[7],
                        "" if x[9] < 0 else " rows %d" % x[9])))
        elif k == "io":
            for t, d, kind in o["list"]:
                ev.append((t, "io     %s %.2f ms" % (kind, d)))
        elif k == "actions":
            for a in o["list"]:
                ev.append((a["t0"], "input  %s %r handled %.1f ms" % (a["kind"], a["label"], a["t1"] - a["t0"])))
        elif k == "signals":
            for t in o["model"]:
                ev.append((t, "model  changed"))
            for t in o["saved"]:
                ev.append((t, "saved"))
        elif k == "frames":
            swaps = list(zip(o["sync"], o["swap"]))
        elif k in ("error", "note"):
            ev.append((0, "%s %s" % (k, o)))
    prev = None
    for s, w in swaps:
        if args.frames or (prev is not None and w - prev > 50):
            ev.append((w, "frame  sync %.1f swap %.1f%s" % (s, w, "" if prev is None else " (gap %.1f)" % (w - prev))))
        prev = w
    for t, text in sorted(ev):
        if args.a <= t <= args.b:
            print("%10.1f  %s" % (t, text))


if __name__ == "__main__":
    main()
