#!/usr/bin/env python3
"""Turn the probe's raw runs (run-bench.py) into metrics.

  tests/bench/app/analyze.py <results dir> [--csv out.csv] [--json out.json] [--platform name]

Reads every <name>-<label>.runs.json with its per-run JSONL files, computes per-run metrics for
the scenario (see the metric functions below; times in ms, memory in MB) and aggregates them over
the measured runs (warm-up runs are skipped): median, min, max, p25, p75, n. Prints a table that
puts the labels (e.g. base / cur) side by side.
"""
import argparse
import csv
import glob
import json
import os
import statistics
import sys


def load(path):
    run = {"marks": {}, "marklist": [], "sync": [], "synced": [], "swap": [], "stalls": [], "actions": [],
           "model": [], "saved": [], "errors": [], "notes": [], "end": None}
    with open(path) as f:
        for line in f:
            try:
                o = json.loads(line)
            except ValueError:
                continue
            ev = o.get("ev")
            if ev == "mark":
                run["marks"].setdefault(o["name"], o)
                run["marklist"].append(o)
            elif ev == "frames":
                run["sync"], run["swap"] = o["sync"], o["swap"]
                run["synced"] = o.get("synced", [])
            elif ev == "stalls":
                run["stalls"] = [(a, d) for a, d in o["list"]]
            elif ev == "actions":
                run["actions"] = o["list"]
            elif ev == "signals":
                run["model"], run["saved"] = o["model"], o["saved"]
            elif ev == "error":
                run["errors"].append(o)
            elif ev == "note":
                run["notes"].append(o)
            elif ev == "end":
                run["end"] = o
    return run


def frame_after(run, t, limit=5000):
    """Swap time of the first frame whose sync starts after t (None if none within limit)."""
    for s, w in zip(run["sync"], run["swap"]):
        if s > t:
            return w if w - t <= limit else None
    return None


def stalls_in(run, a, b, min_dur=0):
    return [(s, d) for s, d in run["stalls"] if s >= a and s < b and d >= min_dur]


def stall_stats(run, a, b, prefix):
    st = stalls_in(run, a, b)
    durs = [d for _, d in st]
    span = max(1e-9, b - a)
    return {
        prefix + "stall_max": max(durs) if durs else 0.0,
        prefix + "stall_sum": sum(durs),
        prefix + "stalls_gt50": sum(1 for d in durs if d > 50),
        prefix + "stalls_gt100": sum(1 for d in durs if d > 100),
        prefix + "blocked_pct": 100.0 * sum(durs) / span,
    }


def mem(run, name):
    m = run["marks"].get("mem:" + name)
    if not m:
        return {}
    return {
        "rss_mb:" + name: m["VmRSS"] / 1024.0,
        "hwm_mb:" + name: m["VmHWM"] / 1024.0,
        "anon_mb:" + name: m["RssAnon"] / 1024.0,
        "heap_mb:" + name: (m["mallocInUseKB"] + m.get("mallocMmapKB", 0)) / 1024.0,
        "items:" + name: m.get("items", 0),
        "cpu_ms:" + name: m["cpuUserMs"] + m["cpuSysMs"],
    }


def pct(values, p):
    if not values:
        return None
    v = sorted(values)
    k = (len(v) - 1) * p / 100.0
    lo = int(k)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def dist(prefix, values):
    values = [v for v in values if v is not None]
    if not values:
        return {}
    return {prefix + "_med": statistics.median(values), prefix + "_p95": pct(values, 95),
            prefix + "_max": max(values), prefix + "_n": len(values)}


# ---- scenarios ----------------------------------------------------------------------------------

def m_startup(run):
    mk = run["marks"]
    out = {}
    for name in ("preload", "qapp", "window", "first_frame", "vault_open", "ready", "settled"):
        if name in mk:
            out[name + "_ms"] = mk[name]["t"]
    if "content" in mk:
        out["content_ms"] = mk["content"]["t"]
        f = frame_after(run, mk["content"]["prevPoll"])
        if f is not None:
            out["content_frame_ms"] = f
    if "first_frame" in mk and "window" in mk:
        out["first_frame_after_window_ms"] = mk["first_frame"]["t"] - mk["window"]["t"]
    if "ready" in mk and "vault_open" in mk:
        out["index_ms"] = mk["ready"]["t"] - mk["vault_open"]["t"]
        start = out.get("content_frame_ms", mk["vault_open"]["t"])
        out.update(stall_stats(run, start, mk["ready"]["t"], "indexing_"))
        # The status spinner animates while indexing: the frame gaps are what the user sees.
        sw = [w for w in run["swap"] if start <= w <= mk["ready"]["t"]]
        gaps = [y - x for x, y in zip(sw, sw[1:])]
        if gaps:
            out["indexing_fps"] = len(sw) / max(1e-3, (mk["ready"]["t"] - start) / 1000.0)
            out["indexing_frame_gap_med"] = statistics.median(gaps)
            out["indexing_frame_gap_max"] = max(gaps)
    if "ready" in mk and "settled" in mk:
        out["after_ready_ms"] = mk["settled"]["t"] - mk["ready"]["t"]
        out.update(stall_stats(run, mk["ready"]["t"], mk["settled"]["t"] + 1, "after_ready_"))
    out.update(mem(run, "ready"))
    out.update(mem(run, "settled"))
    return out


def m_open_page(run):
    out = {}
    mk = run["marks"]
    for tag in ("1", "2"):
        ns = mk.get("nav_start:" + tag)
        if not ns:
            continue
        t0 = ns["t"]
        if "nav_returned:" + tag in mk:
            out["open%s_sync_ms" % tag] = mk["nav_returned:" + tag]["t"] - t0
        pc = mk.get("page_content:" + tag)
        if pc:
            out["open%s_content_ms" % tag] = pc["t"] - t0
            f = frame_after(run, pc["prevPoll"])
            if f is not None:
                out["open%s_frame_ms" % tag] = f - t0
        ps = mk.get("page_settled:" + tag)
        if ps:
            out["open%s_settled_ms" % tag] = ps["t"] - t0
            st = stalls_in(run, t0, ps["t"] + 1)
            out["open%s_stall_max" % tag] = max([d for _, d in st], default=0.0)
        out.update(mem(run, "page:" + tag))
    return out


KEY_KINDS = ("enter", "tab", "shift-tab", "undo", "redo", "backspace", "escape", "clear", "select-all",
             "select-text", "select-blocks", "copy")


def key_metrics(run, actions, prefix):
    out = {}
    groups = {}
    for a in actions:
        if a["kind"] != "key":
            continue
        kind = a["label"] if a["label"] in KEY_KINDS else "char"
        groups.setdefault(kind, []).append(a)
    for kind, acts in groups.items():
        if kind in ("tab", "shift-tab"):
            # Only the first Tab / Shift+Tab of a run changes the outline (one level deeper than
            # the block above at most; top level at least); the others are no-ops whose "frame"
            # would be the caret's next blink.
            acts = acts[:1]
        handle = [a["t1"] - a["t0"] for a in acts]
        frame = []
        for a in acts:
            f = frame_after(run, a["t1"], 2000)
            frame.append(None if f is None else f - a["t0"])
        out.update(dist("%s%s_handle" % (prefix, kind), handle))
        out.update(dist("%s%s_frame" % (prefix, kind), frame))
    return out


def m_typing(run):
    out = key_metrics(run, run["actions"], "")
    mk = run["marks"]
    saves = []
    save_max = []
    for b in range(3):
        end = mk.get("burst_end:%d" % b)
        if not end:
            continue
        t = end["t"]
        last_key = max((a["t2"] for a in run["actions"] if a["t2"] <= t + 1), default=t)
        st = stalls_in(run, last_key + 30, t + 1700)
        saves.append(sum(d for _, d in st))
        save_max.append(max([d for _, d in st], default=0.0))
        sv = [s - last_key for s in run["saved"] if last_key < s < t + 1700]
        if sv:
            out["autosave%d_signal_after_ms" % b] = sv[0]
    out.update(dist("autosave_blocked", saves))
    out.update(dist("autosave_stall_max", save_max))
    # Everything that blocked the GUI thread during the bursts, beyond the keys' own handling.
    out.update(mem(run, "typing_end"))
    return out


def phase_bounds(run):
    phases = [(m["name"][6:], m["t"]) for m in run["marklist"] if m["name"].startswith("phase:")]
    end = run["marklist"][-1]["t"] if run["marklist"] else 1e12
    bounds = {}
    for i, (name, t) in enumerate(phases):
        bounds[name] = (t, phases[i + 1][1] if i + 1 < len(phases) else end)
    return bounds


def m_select_copy(run):
    """select-copy: Ctrl+A twice (every block), Ctrl+C, Escape — key handling and key -> frame,
    the blocks selected and the characters copied (a check that it really happened)."""
    out = key_metrics(run, run["actions"], "")
    sel = [n["count"] for n in run["notes"] if n.get("what") == "selection"]
    clip = [n["chars"] for n in run["notes"] if n.get("what") == "clipboard"]
    if sel:
        out["selected_blocks"] = min(sel)
    if clip:
        out["copied_chars"] = min(clip)
    out.update(mem(run, "select_end"))
    return out


def m_search(run):
    out = {}
    for phase, (a, b) in phase_bounds(run).items():
        acts = [x for x in run["actions"] if a <= x["t0"] < b and x["kind"] == "key" and x["label"] not in KEY_KINDS]
        out.update(key_metrics(run, acts, phase + "_"))
        results, blocked = [], []
        starts = sorted(y["t0"] for y in run["actions"])
        for i, x in enumerate(acts):
            # Up to the next input of any kind (the clear keys after a phase too).
            nxt = min([t for t in starts if t > x["t0"]] + [b])
            if phase == "fast" and i + 1 < len(acts):
                continue  # the debounce collapses the burst: only the last key's results count
            ev = [m for m in run["model"] if x["t1"] < m < nxt]
            if not ev:
                continue  # the results did not change (e.g. a longer prefix of the same hits)
            f = frame_after(run, ev[0], 3000)
            if f is not None:
                results.append(f - x["t0"])
            blocked.append(sum(d for _, d in stalls_in(run, x["t2"] + 20, nxt)))
        out.update(dist(phase + "_results", results))
        out.update(dist(phase + "_query_blocked", blocked))
    return out


def m_switcher(run):
    out = {}
    mk = run["marks"]
    for r in ("0", "1"):
        if "switcher_open:" + r in mk and "switcher_focus:" + r in mk:
            o = mk["switcher_open:" + r]["t"]
            out["open%s_ms" % r] = mk["switcher_focus:" + r]["t"] - o
            f = frame_after(run, mk["switcher_focus:" + r]["prevPoll"])
            if f is not None:
                out["open%s_frame_ms" % r] = f - o
    for phase, (a, b) in phase_bounds(run).items():
        acts = [x for x in run["actions"] if a <= x["t0"] < b]
        out.update(key_metrics(run, acts, phase + "_"))
    return out


def app_state(run, out):
    """Whether the app was active (in front) during the scenario: 1 yes, 0 no (Qt::ApplicationActive = 4)."""
    m = run["marks"].get("app_state")
    if m:
        out["app_active_before"] = 1 if m.get("before") == 4 else 0
        out["app_active"] = 1 if m.get("after") == 4 else 0


def m_views(run):
    out = {}
    app_state(run, out)
    mk = run["marks"]
    for name, m in mk.items():
        if not name.startswith("nav_start:"):
            continue
        tag = name[len("nav_start:"):]
        t0 = m["t"]
        key = tag.replace(":", "_r")
        if "nav_returned:" + tag in mk:
            out[key + "_sync_ms"] = mk["nav_returned:" + tag]["t"] - t0
            f = frame_after(run, mk["nav_returned:" + tag]["t"])
            if f is not None:
                out[key + "_frame_ms"] = f - t0
        s = mk.get("view_settled:" + tag)
        if s:
            out[key + "_settled_ms"] = s["t"] - t0
            out[key + "_stall_max"] = max([d for _, d in stalls_in(run, t0 - 1, s["t"] + 1)], default=0.0)
            if tag.startswith("graph"):
                # The graph animates its layout: frames shown between the navigation and settled.
                out.update(frame_intervals(run, t0, s["t"], key + "_"))
        if "mem:view:" + tag in mk:
            out[key + "_rss_mb"] = mk["mem:view:" + tag]["VmRSS"] / 1024.0
    return out


def frame_intervals(run, a, b, prefix):
    """Frames swapped in [a, b], the intervals between consecutive ones and — when the probe
    recorded the end of each sync — the sync (updatePaintNode of every item; the GUI thread waits)
    and the rest (render + swap) per frame."""
    sw = [w for w in run["swap"] if a <= w <= b]
    iv = [y - x for x, y in zip(sw, sw[1:])]
    out = {prefix + "frames": len(sw)}
    if iv:
        out[prefix + "frame_p95_ms"] = pct(iv, 95)
        out[prefix + "frame_max_ms"] = max(iv)
    if run.get("synced") and len(run["synced"]) == len(run["swap"]):
        frames = [(s, d, w) for s, d, w in zip(run["sync"], run["synced"], run["swap"]) if a <= w <= b]
        out.update(dist(prefix + "sync", [d - s for s, d, _ in frames]))
        out.update(dist(prefix + "render", [w - d for _, d, w in frames]))
    return out


def m_graph(run):
    """The graph scenario: open, pan and zoom while the layout runs, pan again once settled."""
    out = {}
    app_state(run, out)
    mk = run["marks"]
    nav = mk.get("nav_start:graph")
    if not nav:
        return out
    t0 = nav["t"]
    if "nav_returned:graph" in mk:
        out["sync_ms"] = mk["nav_returned:graph"]["t"] - t0
        f = frame_after(run, mk["nav_returned:graph"]["t"])
        if f is not None:
            out["frame_ms"] = f - t0
    done = mk.get("graph_layout_done")
    if done:
        out["layout_ms"] = done["t"] - t0  # GraphModel.running false: the layout has cooled down
        out.update(stall_stats(run, t0, done["t"], "run_"))
        out.update(frame_intervals(run, t0, done["t"], "run_"))
    s = mk.get("graph_settled")
    if s:
        out["settled_ms"] = s["t"] - t0
    for phase in ("graph_run_pan", "graph_settled_pan"):
        p = phase[len("graph_"):]
        moves = [x for x in run["actions"] if x["kind"] == "mouse" and x["label"] == phase]
        frames = []
        for x in moves:
            f = frame_after(run, x["t1"], 2000)
            frames.append(None if f is None else f - x["t0"])
        out.update(dist(p + "_move_frame", frames))
        out.update(dist(p + "_move_handle", [x["t1"] - x["t0"] for x in moves]))
        a, b = mk.get(phase + ":start"), mk.get(phase + ":end")
        if a and b:
            out.update(frame_intervals(run, a["t"], b["t"], p + "_"))
            out.update(stall_stats(run, a["t"], b["t"], p + "_"))
            if "cx" in a and "cx" in b and a.get("zoom"):
                # How far the drag moved the camera, in screen pixels (≈ the drag's length if it panned).
                out[p + "_moved_px"] = ((b["cx"] - a["cx"]) ** 2 + (b["cy"] - a["cy"]) ** 2) ** 0.5 * a["zoom"]
    zooms = [x for x in run["actions"] if x["kind"] == "wheel" and x["label"] == "graph_zoom"]
    zf = []
    for x in zooms:
        f = frame_after(run, x["t1"], 2000)
        zf.append(None if f is None else f - x["t0"])
    out.update(dist("zoom_frame", zf))
    out.update(mem(run, "graph"))
    return out


def m_journal_scroll(run):
    mk = run["marks"]
    out = {}
    if "scroll_start" not in mk or "scroll_end" not in mk:
        return out
    a, b = mk["scroll_start"]["t"], mk["scroll_end"]["t"] + 300
    sw = [w for w in run["swap"] if a <= w <= b]
    iv = [y - x for x, y in zip(sw, sw[1:])]
    out.update(dist("frame_interval", iv))
    out["frames"] = len(sw)
    out["frames_gt33"] = sum(1 for x in iv if x > 33.4)
    out["frames_gt50"] = sum(1 for x in iv if x > 50)
    out.update(stall_stats(run, a, b, "scroll_"))
    wheels = [x for x in run["actions"] if x["kind"] == "wheel"]
    out.update(dist("wheel_handle", [x["t1"] - x["t0"] for x in wheels]))
    out.update(mem(run, "scrolled"))
    return out


def linfit(xs, ys):
    if len(xs) < 2:
        return None
    mx, my = statistics.mean(xs), statistics.mean(ys)
    den = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else None


def m_switch_pages(run):
    mk = run["marks"]
    out = {}
    times = []
    for i in range(200):
        a, b = mk.get("nav_start:%d" % i), mk.get("page_content:%d" % i)
        if a and b:
            f = frame_after(run, b["prevPoll"])
            times.append((f if f is not None else b["t"]) - a["t"])
    out.update(dist("nav_frame", times))
    xs, heap, rss, items = [], [], [], []
    for n in range(20, 201, 20):
        m = mk.get("mem:switch:%d" % n)
        if m:
            xs.append(n)
            heap.append((m["mallocInUseKB"] + m.get("mallocMmapKB", 0)) / 1024.0)
            rss.append(m["VmRSS"] / 1024.0)
            items.append(m.get("items", 0))
    if xs:
        out["heap_mb_at20"], out["heap_mb_at200"] = heap[0], heap[-1]
        out["rss_mb_at20"], out["rss_mb_at200"] = rss[0], rss[-1]
        out["items_at20"], out["items_at200"] = items[0], items[-1]
        s = linfit(xs, heap)
        if s is not None:
            out["heap_mb_per_100_navs"] = s * 100
        s = linfit(xs, rss)
        if s is not None:
            out["rss_mb_per_100_navs"] = s * 100
    out.update(mem(run, "switch:0"))
    out.update(mem(run, "switch:end"))
    return out


def m_idle(run):
    out = {}
    mk = run["marks"]
    for name, m in mk.items():
        if not name.startswith("idle_start:"):
            continue
        tag = name.split(":", 1)[1]
        e = mk.get("idle_end:" + tag)
        if not e:
            continue
        dur = (e["t"] - m["t"]) / 1000.0
        cpu = (e["cpuUserMs"] + e["cpuSysMs"]) - (m["cpuUserMs"] + m["cpuSysMs"])
        out["cpu_pct"] = 100.0 * cpu / 1000.0 / dur
        out["cpu_ms_per_s"] = cpu / dur
        out["frames_per_s"] = e.get("frames", 0) / dur
        wake = {}
        for tid, t in e["threads"].items():
            s = m["threads"].get(tid, {"vcsw": 0, "ivcsw": 0, "utime": 0, "stime": 0})
            w = (t["vcsw"] + t["ivcsw"]) - (s["vcsw"] + s["ivcsw"])
            name = t["name"]
            wake[name] = wake.get(name, 0) + w
        out["wakeups_per_s"] = sum(wake.values()) / dur
        top = sorted(wake.items(), key=lambda kv: -kv[1])[:4]
        for i, (n, w) in enumerate(top):
            out["top%d_thread" % i] = "%s:%.1f/s" % (n, w / dur)
        out["rss_mb"] = e["VmRSS"] / 1024.0
        events = sorted(e.get("events", {}).items(), key=lambda kv: -kv[1])[:6]
        for i, (k, n) in enumerate(events):
            out["event%d" % i] = "%.2f/s %s" % (n / dur, k)
    return out


def m_workload(run):
    mk = run["marks"]
    out = {}
    for n in (0, 4, 8, 12):
        m = mk.get("mem:workload:%d" % n)
        if m:
            out["rss_mb_c%d" % n] = m["VmRSS"] / 1024.0
            out["heap_mb_c%d" % n] = (m["mallocInUseKB"] + m.get("mallocMmapKB", 0)) / 1024.0
            out["items_c%d" % n] = m.get("items", 0)
    out.update(mem(run, "settled"))
    return out


SCENARIOS = {
    "startup": m_startup, "open-page": m_open_page, "typing": m_typing, "select-copy": m_select_copy,
    "search": m_search,
    "switcher": m_switcher, "views": m_views, "graph": m_graph, "journal-scroll": m_journal_scroll,
    "switch-pages": m_switch_pages, "idle": m_idle, "idle-edit": m_idle, "workload": m_workload,
}


def analyze_dir(d):
    rows = []
    for meta_path in sorted(glob.glob(os.path.join(d, "*.runs.json"))):
        meta = json.load(open(meta_path))
        fn = SCENARIOS.get(meta["scenario"])
        per_run = []
        for r in meta["runs"]:
            if r.get("warmup"):
                continue
            path = os.path.join(d, r["file"])
            if not os.path.exists(path):
                continue
            run = load(path)
            m = {}
            if meta["scenario"] != "startup" and fn is not m_startup:
                m.update({"startup_" + k: v for k, v in m_startup(run).items()
                          if k in ("first_frame_ms", "content_frame_ms", "ready_ms", "settled_ms")})
            m.update(fn(run) if fn else {})
            m["exit"] = r["exit"]
            m["errors"] = len(run["errors"])
            per_run.append(m)
        keys = sorted({k for m in per_run for k in m})
        for k in keys:
            vals = [m[k] for m in per_run if k in m]
            num = [v for v in vals if isinstance(v, (int, float)) and not isinstance(v, bool)]
            row = {"name": meta["name"], "label": meta["label"], "scenario": meta["scenario"],
                   "start": meta["start"], "platform": meta["platform"], "host": meta.get("host", ""),
                   "metric": k, "n": len(vals)}
            if num and len(num) == len(vals):
                row.update({"median": statistics.median(num), "min": min(num), "max": max(num),
                            "p25": pct(num, 25), "p75": pct(num, 75), "values": num})
            else:
                row.update({"median": None, "values": vals})
            rows.append(row)
    return rows


def fmt(v):
    if v is None:
        return "-"
    if isinstance(v, float):
        return "%.1f" % v
    return str(v)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dirs", nargs="+")
    ap.add_argument("--csv")
    ap.add_argument("--json")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    rows = []
    for d in args.dirs:
        rows += analyze_dir(d)
    if args.json:
        json.dump(rows, open(args.json, "w"), indent=1, ensure_ascii=False)
    if args.csv:
        with open(args.csv, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["name", "scenario", "start", "platform", "host", "label", "metric", "n", "median", "min", "max", "p25", "p75", "values"])
            for r in rows:
                w.writerow([r["name"], r["scenario"], r["start"], r["platform"], r["host"], r["label"], r["metric"], r["n"],
                            fmt(r.get("median")), fmt(r.get("min")), fmt(r.get("max")), fmt(r.get("p25")), fmt(r.get("p75")),
                            " ".join(fmt(v) for v in r["values"])])
    if args.quiet:
        return
    # Side-by-side table: name/metric x label.
    table = {}
    labels = []
    for r in rows:
        if r["label"] not in labels:
            labels.append(r["label"])
        table.setdefault((r["name"], r["metric"]), {})[r["label"]] = r
    print("%-28s %-34s " % ("name", "metric") + " ".join("%-26s" % l for l in labels))
    for (name, metric), by in table.items():
        cells = []
        for l in labels:
            r = by.get(l)
            if not r:
                cells.append("%-26s" % "")
            elif r.get("median") is None:
                cells.append("%-26s" % (",".join(str(v) for v in r["values"])[:26]))
            else:
                cells.append("%-26s" % ("%s [%s-%s] n%d" % (fmt(r["median"]), fmt(r["min"]), fmt(r["max"]), r["n"])))
        print("%-28s %-34s " % (name[:28], metric[:34]) + " ".join(cells))


if __name__ == "__main__":
    main()
