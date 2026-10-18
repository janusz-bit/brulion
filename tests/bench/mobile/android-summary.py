#!/usr/bin/env python3
"""Tables from the Android measurements of android-measure.sh / android-batch.sh.

  android-summary.py <out dir> [--md]

  * startup-<label>-<kind>.csv  (launcher starts of the release APK): median [min–max] of
    TotalTime (am start -W), "Displayed", PSS / RSS / Java / native / graphics after the start,
    the process's CPU time, warm-up run excluded;
  * meminfo-*.txt  (single snapshots, e.g. after navigation): PSS, RSS and the App Summary;
  * idle-*.txt  (CPU and wakeups while left alone).
The probe runs (*.runs.json + JSONL) are summarised by tests/bench/app/analyze.py.
"""
import argparse
import csv
import glob
import os
import re
import statistics


def med(vals):
    vals = [v for v in vals if v is not None]
    if not vals:
        return "–"
    m = statistics.median(vals)
    return "%s [%s–%s]" % (fmt(m), fmt(min(vals)), fmt(max(vals))) if len(vals) > 1 else fmt(m)


def fmt(v):
    if v is None:
        return "–"
    if abs(v) >= 100:
        return "%.0f" % v
    return "%.1f" % v


def num(s):
    try:
        return float(s)
    except (TypeError, ValueError):
        return None


def table(rows, header, md):
    if md:
        out = ["| " + " | ".join(header) + " |", "|" + "|".join(["---"] + ["---:"] * (len(header) - 1)) + "|"]
        out += ["| " + " | ".join(str(c) for c in r) + " |" for r in rows]
        return "\n".join(out)
    w = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
    return "\n".join("  ".join(str(c).ljust(w[i]) for i, c in enumerate(r)) for r in [header] + rows)


def parse_meminfo(path):
    t = open(path, errors="replace").read()
    d = {}
    m = re.search(r"TOTAL PSS:\s+(\d+)\s+TOTAL RSS:\s+(\d+)", t)
    if m:
        d["pss"], d["rss"] = int(m.group(1)), int(m.group(2))
    for key, rx in (("java", r"Java Heap:\s+(\d+)"), ("native", r"Native Heap:\s+(\d+)"), ("code", r"\n\s*Code:\s+(\d+)"),
                    ("stack", r"\n\s*Stack:\s+(\d+)"), ("graphics", r"\n\s*Graphics:\s+(\d+)"),
                    ("other", r"Private Other:\s+(\d+)"), ("system", r"\n\s*System:\s+(\d+)")):
        m = re.search(rx, t)
        if m:
            d[key] = int(m.group(1))
    m = re.search(r"\n\s*\.so mmap\s+(\d+)", t)
    if m:
        d["so_pss"] = int(m.group(1))
    m = re.search(r"\n\s*Native Heap\s+(\d+)", t)
    if m:
        d["native_pss"] = int(m.group(1))
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--md", action="store_true")
    a = ap.parse_args()
    rows = []
    for path in sorted(glob.glob(os.path.join(a.dir, "startup-*.csv"))):
        name = os.path.basename(path)[len("startup-"):-4]
        runs = [r for r in csv.DictReader(open(path)) if r["run"] != "warmup"]
        mb = lambda k: [num(r[k]) / 1024 if num(r[k]) is not None else None for r in runs]
        rows.append([name, len(runs), "/".join(sorted({r["launch_state"] for r in runs})),
                     med([num(r["total_ms"]) for r in runs]), med([num(r["displayed_ms"]) for r in runs]),
                     med(mb("pss_kb")), med(mb("rss_kb")), med(mb("java_kb")), med(mb("native_kb")),
                     med(mb("graphics_kb")), med(mb("code_kb")), med([num(r["cpu_ms"]) for r in runs])])
    if rows:
        print("### Launcher starts (release APK; ms, MB after the settle time)\n")
        print(table(rows, ["start", "n", "state", "TotalTime", "Displayed", "PSS", "RSS", "Java", "native",
                           "graphics", "code", "CPU ms"], a.md))
    rows = []
    for path in sorted(glob.glob(os.path.join(a.dir, "meminfo-*.txt"))):
        name = os.path.basename(path)[len("meminfo-"):-4]
        if re.search(r"-(warm|hot|cold)-\d+$", name) or re.search(r"-\d\d$|-warmup$", name):
            continue
        d = parse_meminfo(path)
        if not d:
            continue
        rows.append([name] + ["%.1f" % (d[k] / 1024) if k in d else "–" for k in
                              ("pss", "rss", "java", "native", "graphics", "code", "other", "system")])
    if rows:
        print("\n### Memory snapshots (dumpsys meminfo, MB)\n")
        print(table(rows, ["when", "PSS", "RSS", "Java", "native", "graphics", "code", "other", "system"], a.md))
    for path in sorted(glob.glob(os.path.join(a.dir, "idle-*.txt"))):
        print("\n### %s\n" % os.path.basename(path))
        lines = open(path).read().splitlines()
        print("```")
        for l in lines[:40]:
            print(l)
        print("```")


if __name__ == "__main__":
    main()
