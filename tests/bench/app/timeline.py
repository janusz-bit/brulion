#!/usr/bin/env python3
"""Print one run's events in time order (marks, synthetic input, stalls > 12 ms, model signals,
saves, and frame gaps > 50 ms) — for reading what happened around a slow moment.

  tests/bench/app/timeline.py run.jsonl [--from ms] [--to ms] [--frames]
"""
import argparse
import json


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("--from", dest="a", type=float, default=0)
    ap.add_argument("--to", dest="b", type=float, default=1e12)
    ap.add_argument("--frames", action="store_true", help="every frame, not only gaps")
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
            for s, d in o["list"]:
                ev.append((s, "STALL  %.1f ms (ends %.1f)" % (d, s + d)))
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
