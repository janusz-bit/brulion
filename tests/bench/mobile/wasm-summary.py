#!/usr/bin/env python3
"""Markdown tables from wasm-bench.mjs results (<label>-summary.json files in a directory).

  wasm-summary.py <dir> [--labels cur,base]

One table per group (empty, 1k, 1k-cpu4, net20-gz, net20-br) and load kind (first visit, repeat
visit, reload after IDBFS write-back), labels side by side as median [min–max]; then the IDBFS
write-back timings and the idle measurement.
"""
import argparse
import glob
import json
import os
import statistics

METRICS = [
    ("wasm_download_end_ms", "wasm downloaded (ms)"),
    ("compile_end_ms", "wasm compiled (streaming, ms)"),
    ("module_ready_ms", "module instantiated, main() ran (ms)"),
    ("idbfs_load_ms", "IDBFS load (storage → ui stage, ms)"),
    ("shell_done_ms", "app UI created, loader hidden (ms)"),
    ("first_draw_ms", "first WebGL draw (ms)"),
    ("settled_ms", "settled: main thread quiet (ms)"),
    ("main_thread_busy_ms", "main thread busy in total (ms)"),
    ("longtask_max_ms", "longest task (ms)"),
    ("longtask_total_ms", "long tasks in total (ms)"),
    ("wasm_heap_MB", "wasm linear memory (MB)"),
    ("js_heap_MB", "JS heap (MB)"),
    ("renderer_rss_MB", "renderer process RSS (MB)"),
    ("gpu_rss_MB", "GPU process RSS (MB, SwiftShader)"),
    ("wasm_transfer_B", "bytes transferred for the .wasm"),
]


def cell(m):
    if not m:
        return "–"
    f = (lambda v: "%.0f" % v) if abs(m["median"]) >= 10 else (lambda v: "%.1f" % v)
    if m["n"] == 1 or m["min"] == m["max"]:
        return f(m["median"])
    return "%s [%s–%s]" % (f(m["median"]), f(m["min"]), f(m["max"]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--labels", default="cur,base")
    a = ap.parse_args()
    labels = a.labels.split(",")
    data = {}
    for f in glob.glob(os.path.join(a.dir, "*-summary.json")):
        name = os.path.basename(f)[:-len("-summary.json")]
        lab = next((l for l in labels if name.startswith(l + "-")), None)
        if not lab:
            continue
        data[(lab, name[len(lab) + 1:])] = json.load(open(f))
    groups = sorted({g for _, g in data})
    for g in groups:
        kinds = []
        for lab in labels:
            for k in (data.get((lab, g)) or {}).get("kinds", {}):
                if k not in kinds:
                    kinds.append(k)
        for kind in kinds:
            ns = [str(max((v["n"] for v in data[(l, g)]["kinds"].get(kind, {}).values()), default=0))
                  for l in labels if (l, g) in data]
            print("\n### %s — %s (n = %s)\n" % (g, kind, "/".join(ns)))
            print("| metric | " + " | ".join(labels) + " |")
            print("|---|" + "---:|" * len(labels))
            for key, title in METRICS:
                row = [cell((data.get((l, g)) or {}).get("kinds", {}).get(kind, {}).get(key)) for l in labels]
                if all(c == "–" for c in row):
                    continue
                print("| %s | %s |" % (title, " | ".join(row)))
    for lab in labels:
        d = data.get((lab, "1k"))
        if not d:
            continue
        if d.get("idbfs"):
            print("\n### IDBFS write-back with 1 000 notes, %s (3 rounds, ms)\n" % lab)
            print("| one changed note | nothing changed | all 1 001 notes touched |")
            print("|---:|---:|---:|")
            for r in d["idbfs"]:
                print("| %.0f | %.0f | %.0f |" % (r["one_note_ms"], r["nothing_changed_ms"], r["all_notes_ms"]))
            ones = [r["one_note_ms"] for r in d["idbfs"]]
            print("\nmedian one note %.0f ms" % statistics.median(ones))
        if d.get("idle"):
            print("\nidle %s: %s" % (lab, json.dumps(d["idle"])))


if __name__ == "__main__":
    main()
