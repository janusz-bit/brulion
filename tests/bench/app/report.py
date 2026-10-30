#!/usr/bin/env python3
"""The headline numbers of a results directory, BASELINE vs CURRENT, as a Markdown table and CSV.

  tests/bench/app/report.py <results dir> --platform "laptop x86_64, Xvfb+llvmpipe" \
      [--base base --cur cur] [--csv summary.csv] [--md summary.md]

Medians over the runs (per-run medians for per-key metrics); `Δ` = current vs baseline.
"""
import argparse
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze  # noqa: E402

KEY = [
    ("startup", "startup-demo-warm", ["first_frame_ms", "content_frame_ms", "settled_ms", "rss_mb:settled"]),
    ("startup", "startup-demo-cold", ["first_frame_ms", "content_frame_ms", "settled_ms"]),
    ("startup", "startup-demo-first", ["first_frame_ms", "content_frame_ms", "settled_ms"]),
    ("startup", "startup-big-warm", ["first_frame_ms", "content_frame_ms", "ready_ms", "settled_ms", "index_ms",
                                     "indexing_stall_max", "indexing_fps", "indexing_frame_gap_max", "after_ready_stall_max",
                                     "rss_mb:ready", "rss_mb:settled",
                                     "heap_mb:settled", "cpu_ms:settled"]),
    ("startup", "startup-big-cold", ["first_frame_ms", "content_frame_ms", "ready_ms", "settled_ms"]),
    ("startup", "startup-big-first", ["first_frame_ms", "content_frame_ms", "ready_ms", "settled_ms"]),
    ("open page", "open-outline5000", ["open1_sync_ms", "open1_frame_ms", "open1_settled_ms", "open1_stall_max",
                                       "open2_frame_ms", "open2_settled_ms", "rss_mb:page:1"]),
    ("open page", "open-document5000", ["open1_sync_ms", "open1_frame_ms", "open1_settled_ms", "open1_stall_max",
                                        "open2_frame_ms", "open2_settled_ms", "rss_mb:page:1"]),
    ("typing", "typing-small", ["char_handle_med", "char_frame_med", "char_frame_p95", "enter_frame_med",
                                "tab_frame_med", "undo_frame_med", "autosave_blocked_med"]),
    ("typing", "typing-outline5000", ["char_handle_med", "char_frame_med", "char_frame_p95", "enter_frame_med",
                                      "tab_frame_med", "shift-tab_frame_med", "undo_frame_med", "redo_frame_med",
                                      "autosave_blocked_med", "autosave_stall_max_med"]),
    ("typing", "typing-document5000", ["char_handle_med", "char_frame_med", "char_frame_p95", "enter_frame_med",
                                       "undo_frame_med", "redo_frame_med", "autosave_blocked_med",
                                       "autosave_stall_max_med"]),
    ("search", "search-10k", ["slow_char_frame_med", "slow_results_med", "slow_query_blocked_med",
                              "fast_results_med", "many_results_med", "many_query_blocked_med"]),
    ("switcher", "switcher-10k", ["open0_frame_ms", "slow_char_handle_med", "slow_char_frame_med",
                                  "fast_char_frame_med", "slow_backspace_frame_med"]),
    ("views", "views-10k", ["tasks_r0_frame_ms", "tasks_r0_settled_ms", "calendar_r0_frame_ms", "calendar_r0_settled_ms",
                            "agenda_r0_frame_ms", "agenda_r0_settled_ms", "graph_r0_frame_ms", "graph_r0_settled_ms",
                            "graph_r0_stall_max", "graph_r0_frames", "graph_r0_frame_p95_ms", "graph_r0_sync_med",
                            "graph_r0_render_med",
                            "allPages_r0_frame_ms", "tags_r0_frame_ms", "tasks_r1_frame_ms", "calendar_r1_frame_ms",
                            "journals-after-tasks_r0_settled_ms"]),
    ("graph", "graph-10k", ["app_active_before", "app_active", "frame_ms", "layout_ms", "settled_ms", "run_stall_max",
                            "run_frames", "run_frame_p95_ms", "run_sync_med", "run_render_med",
                            "run_frame_max_ms", "run_pan_moved_px", "run_pan_move_frame_med", "run_pan_move_frame_p95",
                            "run_pan_frame_p95_ms", "run_pan_stall_max", "zoom_frame_med", "settled_pan_moved_px",
                            "settled_pan_move_frame_med",
                            "settled_pan_move_frame_p95", "settled_pan_frame_p95_ms", "rss_mb:graph"]),
    ("journal", "journal-scroll-10k", ["frame_interval_med", "frame_interval_p95", "frame_interval_max", "frames_gt33",
                                       "scroll_stall_max"]),
    ("navigation", "switch-pages-10k", ["nav_frame_med", "nav_frame_p95", "nav_frame_max", "heap_mb_per_100_navs",
                                        "rss_mb_per_100_navs", "rss_mb_at200", "items_at200"]),
    ("idle", "idle-demo", ["cpu_pct", "wakeups_per_s", "frames_per_s"]),
    ("idle", "idle-10k", ["cpu_pct", "wakeups_per_s", "frames_per_s", "rss_mb"]),
    ("idle", "idle-edit-small", ["cpu_pct", "wakeups_per_s", "frames_per_s"]),
    ("memory", "workload-10k", ["rss_mb_c0", "rss_mb_c12", "heap_mb_c0", "heap_mb_c12", "items_c0", "items_c12"]),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--platform", required=True)
    ap.add_argument("--base", default="base")
    ap.add_argument("--cur", default="cur")
    ap.add_argument("--csv")
    ap.add_argument("--md")
    args = ap.parse_args()
    rows = analyze.analyze_dir(args.dir)
    by = {(r["name"], r["metric"], r["label"]): r for r in rows}
    out = []
    for area, name, metrics in KEY:
        for m in metrics:
            b, c = by.get((name, m, args.base)), by.get((name, m, args.cur))
            if not b and not c:
                continue
            bv = b.get("median") if b else None
            cv = c.get("median") if c else None
            delta = (100.0 * (cv - bv) / bv) if (bv and cv is not None) else None
            spread = lambda r: "%s–%s" % (analyze.fmt(r.get("min")), analyze.fmt(r.get("max"))) if r and r.get("median") is not None else ""
            out.append({"area": area, "scenario": name, "platform": args.platform, "metric": m,
                        "baseline": bv, "current": cv, "delta_pct": delta,
                        "baseline_range": spread(b), "current_range": spread(c),
                        "n": "%s/%s" % (b["n"] if b else 0, c["n"] if c else 0)})
    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(out[0].keys()) if out else ["area"])
            w.writeheader()
            for r in out:
                w.writerow({k: analyze.fmt(v) if isinstance(v, float) else v for k, v in r.items()})
    lines = ["| scenario | metric | baseline (v0.1.1) | current | Δ | n |", "|---|---|---|---|---|---|"]
    for r in out:
        lines.append("| %s | %s | %s (%s) | %s (%s) | %s | %s |" % (
            r["scenario"], r["metric"], analyze.fmt(r["baseline"]), r["baseline_range"], analyze.fmt(r["current"]),
            r["current_range"], "" if r["delta_pct"] is None else "%+.0f%%" % r["delta_pct"], r["n"]))
    text = "Platform: %s\n\n" % args.platform + "\n".join(lines) + "\n"
    if args.md:
        open(args.md, "w").write(text)
    print(text)


if __name__ == "__main__":
    main()
