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
    run = {"marks": {}, "marklist": [], "sync": [], "synced": [], "swap": [], "stalls": [], "stall_cpu": [],
           "actions": [], "model": [], "saved": [], "errors": [], "notes": [], "end": None, "spans": [],
           "cpu": ([], [], []), "io": []}
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
                # [start, duration] (older probes) or [start, duration, GUI CPU ms, instructions M]
                run["stalls"] = [(x[0], x[1]) for x in o["list"]]
                run["stall_cpu"] = [(x[2], x[3]) if len(x) > 3 else (None, None) for x in o["list"]]
            elif ev == "spans":
                names = o["names"]
                run["spans"] = [Span(x[0], names[x[1]], x[2], x[3], x[4], x[5], x[6], x[7], x[8], x[9], bool(x[10]))
                                for x in o["list"]]
            elif ev == "cpu":
                run["cpu"] = (o["t"], o["cpu"], o["insns"])
            elif ev == "io":
                run["io"] = [(t, d, kind) for t, d, kind in o["list"]]
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


class Span:
    """A signal span of the probe (GUI thread): start, name, nesting depth, inclusive and self wall
    time (ms), thread CPU (ms) and user-space instructions (millions), the rows a model signal
    names (-1: none), and whether it is the QML part of the emission (handlers, bindings)."""
    __slots__ = ("t", "name", "depth", "incl", "self", "icpu", "scpu", "iins", "sins", "rows", "qml")

    def __init__(self, t, name, depth, incl, self_, icpu, scpu, iins, sins, rows, qml):
        self.t, self.name, self.depth, self.incl, self.self = t, name, depth, incl, self_
        self.icpu, self.scpu, self.iins, self.sins, self.rows, self.qml = icpu, scpu, iins, sins, rows, qml

    @property
    def end(self):
        return self.t + self.incl


def frame_index_after(run, t, limit=5000):
    """Index of the first frame whose sync starts after t (None if none within limit)."""
    for i, (s, w) in enumerate(zip(run["sync"], run["swap"])):
        if s > t:
            return i if w - t <= limit else None
    return None


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
    # The GUI thread's work and the QObjects made up to each mark (probes that record them).
    for name in ("window", "first_frame", "content", "ready", "settled"):
        m = mk.get(name)
        if not m or "guiInsnsM" not in m:
            continue
        out[name + "_gui_insns_M"] = m["guiInsnsM"]
        out[name + "_gui_cpu_ms"] = m["guiCpuMs"]
        out[name + "_objects_created"] = m["objCreated"]
        out[name + "_objects_alive"] = m["objAlive"]
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
        frame, wait, gui, render = [], [], [], []
        split = bool(run.get("synced")) and len(run["synced"]) == len(run["swap"])
        for a in acts:
            i = frame_index_after(run, a["t1"], 2000)
            frame.append(None if i is None else run["swap"][i] - a["t0"])
            if i is not None and split:
                # key -> frame = gui (the press handled + the frame's sync) + wait (until that
                # sync starts: the rest of the event-loop turn, polish, timers, the render loop)
                # + render (after the sync: the render thread, the swap)
                s, d, w = run["sync"][i], run["synced"][i], run["swap"][i]
                gui.append((a["t1"] - a["t0"]) + (d - s))
                wait.append(s - a["t1"])
                render.append(w - d)
        out.update(dist("%s%s_handle" % (prefix, kind), handle))
        out.update(dist("%s%s_frame" % (prefix, kind), frame))
        if split:
            out.update(dist("%s%s_wait" % (prefix, kind), wait))
            out.update(dist("%s%s_gui" % (prefix, kind), gui))
            out.update(dist("%s%s_render" % (prefix, kind), render))
    return out


def m_typing(run):
    out = key_metrics(run, run["actions"], "")
    mk = run["marks"]
    saves = []
    save_max = []
    save_cpu = []
    for b in range(3):
        end = mk.get("burst_end:%d" % b)
        if not end:
            continue
        t = end["t"]
        last_key = max((a["t2"] for a in run["actions"] if a["t2"] <= t + 1), default=t)
        st = stalls_in(run, last_key + 30, t + 1700)
        saves.append(sum(d for _, d in st))
        save_max.append(max([d for _, d in st], default=0.0))
        cpu = [c for (s, _), (c, _) in zip(run["stalls"], run["stall_cpu"]) if last_key + 30 <= s < t + 1700]
        if all(c is not None for c in cpu):
            save_cpu.append(sum(cpu))
        sv = [s - last_key for s in run["saved"] if last_key < s < t + 1700]
        if sv:
            out["autosave%d_signal_after_ms" % b] = sv[0]
    out.update(dist("autosave_blocked", saves))
    out.update(dist("autosave_stall_max", save_max))
    out.update(dist("autosave_blocked_cpu", save_cpu))
    out.update(save_metrics(run))
    # Everything that blocked the GUI thread during the bursts, beyond the keys' own handling.
    out.update(mem(run, "typing_end"))
    return out


# The signals whose inclusive time save_metrics() reports as sig_<name>_ms (C++ and QML parts).
SAVE_SIGNALS = ("Vault.changed", "Workspace.revisionChanged", "Workspace.fileChanged", "Workspace.filesChanged",
                "Workspace.fileListChanged", "EditorHost.indexChanged", "EditorHost.revisionChanged",
                "EditorHost.fileChanged", "EditorHost.filesChanged", "EditorHost.fileListChanged",
                "VaultIndex.changed", "PageController.saved", "PageController.dirtyChanged")
SAVE_TIMER = "timer:save"
FANOUT_MS = 1700
TOP_SPANS = 8


def cpu_at(run, t):
    """The GUI thread's (CPU ms, instructions) at the last lag tick at or before t (None outside the
    recorded series)."""
    ts, cpu, ins = run["cpu"]
    if not ts or t < ts[0]:
        return None
    lo, hi = 0, len(ts) - 1
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if ts[mid] <= t:
            lo = mid
        else:
            hi = mid - 1
    return ts[lo], cpu[lo], ins[lo]


def idle_rate(run):
    """The GUI thread's idle CPU (ms per ms) and instructions (per ms) in the 1 s of idle editing
    before the first burst: the median of the lag ticks' rates (robust to a slice of other work
    there, e.g. a kept view built ahead)."""
    a, b = run["marks"].get("idle_ref_start"), run["marks"].get("idle_ref_end")
    ts, cpu, ins = run["cpu"]
    if not a or not b or not ts:
        return None
    idx = [i for i, t in enumerate(ts) if a["t"] <= t <= b["t"]]
    rates_c, rates_i = [], []
    for i, j in zip(idx, idx[1:]):
        dt = ts[j] - ts[i]
        if dt > 0:
            rates_c.append((cpu[j] - cpu[i]) / dt)
            rates_i.append((ins[j] - ins[i]) / dt)
    if not rates_c:
        return None
    return statistics.median(rates_c), statistics.median(rates_i)


def save_metrics(run):
    """The autosave of each burst from the probe's signal spans (BENCH_SPANS) and file system calls:
    the save is the "timer:save" span (PageController's parentless single-shot save timer: the whole
    synchronous save, Vault::changed and the direct fan-out included); its fan-out window runs from
    the save's start to FANOUT_MS after its end. Per burst `<metric>_b<n>`, the run's median as
    `<metric>`."""
    out = {}
    mk = run["marks"]
    spans = run["spans"]
    if not spans and not run["io"]:
        return out
    rate = idle_rate(run)
    # Without a counter (perf_event_paranoid > 2, e.g. Ubuntu's 4) the probe's instructions are 0.
    counters = next((n for n in run["notes"] if n.get("what") == "counters"), {})
    insns = bool(counters.get("insns", ["?"]))
    if rate:
        out["idle_ref_cpu_ms_per_s"] = rate[0] * 1000
    per = {}
    top = {}
    for b in range(3):
        start, end = mk.get("burst_start:%d" % b), mk.get("burst_end:%d" % b)
        if not start or not end:
            continue
        m = {}
        kept = mk.get("kept_views:%d" % b)
        if kept:
            m["kept_views"] = kept.get("n", 0)
        win = mk.get("window_end:%d" % b)
        if win:
            m["window_emissions"] = win.get("emissions", 0)
            m["window_spans"] = win.get("spans", 0)
        saves = [x for x in spans if x.name == SAVE_TIMER and not x.qml and start["t"] <= x.t < end["t"] + FANOUT_MS]
        if saves:
            inside = lambda t: any(x.t <= t <= x.end for x in saves)  # noqa: E731
            m["saves"] = len(saves)
            m["save_sync_ms"] = sum(x.incl for x in saves)
            m["save_cpu_ms"] = sum(x.icpu for x in saves)
            if insns:
                m["save_insns_M"] = sum(x.iins for x in saves)
            vc = [x for x in spans if x.name == "Vault.changed" and inside(x.t)]
            m["save_vault_changed_ms"] = sum(x.incl for x in vc)
            write = 0.0
            for x in saves:  # the save up to Vault::changed: serialising, the compare-and-swap, the write
                first = min((v.t for v in vc if x.t <= v.t <= x.end), default=x.end)
                write += first - x.t
            m["save_write_ms"] = write
            fs = [(t, d) for t, d, k in run["io"] if k in ("fsync", "fdatasync") and inside(t)]
            m["save_fsync_ms"] = sum(d for _, d in fs)
            m["save_fsync_n"] = len(fs)
            rn = [(t, d) for t, d, k in run["io"] if k.startswith("rename") and inside(t)]
            m["save_rename_ms"] = sum(d for _, d in rn)
            # A save in the background (PageController.saveStarted inside the timer): its result
            # is merged later from the event loop — the vault's merge, the editor's `saved`, the
            # index update and the direct fan-out of Vault.changed. Its outermost spans outside the
            # save timer: what the merge costs the GUI thread.
            merge = [x for x in spans if not x.qml and x.depth == 0 and not inside(x.t)
                     and x.name in ("Vault.changed", "PageController.saved", "PageController.dirtyChanged")
                     and saves[0].t <= x.t < max(s.end for s in saves) + FANOUT_MS]
            if merge:
                m["save_merge_ms"] = sum(x.incl for x in merge)
                m["save_merge_cpu_ms"] = sum(x.icpu for x in merge)
                if insns:
                    m["save_merge_insns_M"] = sum(x.iins for x in merge)
            fa, save_end = saves[0].t, max(x.end for x in saves)
            fb = save_end + FANOUT_MS
            if win:
                fb = min(fb, win["t"])  # the spans and the next burst: only up to the window's end
            m["fanout_window_ms"] = fb - save_end
            fan = [x for x in spans if fa <= x.t < fb]
            for name in SAVE_SIGNALS:
                hit = [x for x in fan if x.name == name]
                if hit:
                    m["sig_%s_ms" % name] = sum(x.incl for x in hit)
            m["fanout_model_resets"] = sum(1 for x in fan if x.name.startswith("model:") and not x.qml
                                           and x.name.endswith((".modelReset", ".layoutChanged")))
            m["fanout_rows_changed"] = sum(max(0, x.rows) for x in fan if x.name.startswith("model:") and not x.qml
                                           and x.name.endswith((".dataChanged", ".rowsInserted", ".rowsRemoved",
                                                                ".rowsMoved")))
            c0, c1 = cpu_at(run, save_end), cpu_at(run, fb)
            if c0 and c1 and rate:
                dt = c1[0] - c0[0]
                m["fanout_deferred_cpu_ms"] = (c1[1] - c0[1]) - rate[0] * dt
                if insns:
                    m["fanout_insns_M"] = ((c1[2] - c0[2]) - rate[1] * dt) / 1e6
            for x in fan:
                key = x.name + (" (qml)" if x.qml else "")
                agg = top.setdefault(key, [0.0, 0.0, 0])
                agg[0] += x.self
                agg[1] += x.incl
                agg[2] += 1
        per[b] = m
        for k, v in m.items():
            out["%s_b%d" % (k, b)] = v
    for k in sorted({k for m in per.values() for k in m}):
        vals = [m[k] for m in per.values() if k in m]
        if vals:
            out[k] = statistics.median(vals)
    # Where the fan-out's time goes: the spans with the most self time over every burst's window.
    for i, (name, (self_ms, incl_ms, n)) in enumerate(sorted(top.items(), key=lambda kv: -kv[1][0])[:TOP_SPANS]):
        out["top%d_span" % i] = "%s self %.1f incl %.1f ms n%d" % (name, self_ms, incl_ms, n)
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


def m_page_scroll(run):
    """page-scroll: the opening as open-page's first round, then the wheel's frames as journal-scroll."""
    out = {k: v for k, v in m_open_page(run).items() if k.startswith("open1_") or k.endswith(":page:1")}
    out.update(m_journal_scroll(run))
    return out


def m_page_resize(run):
    """page-resize: per resize the request → the frame after the window has the new width, and the
    longest GUI stall within 500 ms of the request."""
    out = {k: v for k, v in m_open_page(run).items() if k.startswith("open1_") or k.endswith(":page:1")}
    acts = [x for x in run["actions"] if x["kind"] == "resize" and x["t2"] > 0]
    frames, stalls = [], []
    for a in acts:
        f = frame_after(run, a["t2"])
        if f is not None:
            frames.append(f - a["t0"])
        stalls.append(max([d for _, d in stalls_in(run, a["t0"], a["t0"] + 500)], default=0.0))
    out.update(dist("resize_frame", frames))
    out.update(dist("resize_stall", stalls))
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
    "startup": m_startup, "open-page": m_open_page, "typing": m_typing, "source-typing": m_typing,
    "select-copy": m_select_copy, "search": m_search,
    "switcher": m_switcher, "views": m_views, "graph": m_graph, "journal-scroll": m_journal_scroll,
    "page-scroll": m_page_scroll, "page-resize": m_page_resize,
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
    print("%-28s %-40s " % ("name", "metric") + " ".join("%-26s" % l for l in labels))
    for (name, metric), by in table.items():
        cells = []
        for l in labels:
            r = by.get(l)
            if not r:
                cells.append("%-26s" % "")
            elif r.get("median") is None:
                text = ",".join(str(v) for v in r["values"])
                cells.append("%-26s" % (text if metric.startswith("top") else text[:26]))
            else:
                cells.append("%-26s" % ("%s [%s-%s] n%d" % (fmt(r["median"]), fmt(r["min"]), fmt(r["max"]), r["n"])))
        print("%-28s %-40s " % (name[:28], metric[:40]) + " ".join(cells))


if __name__ == "__main__":
    main()
