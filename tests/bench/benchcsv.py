#!/usr/bin/env python3
"""Summarise, compare and check the scaling of QTest benchmark CSV output (scripts/bench.sh).

  benchcsv.py summarize DIR
        DIR/raw/<suite>[@filter].<rep>.csv -> DIR/summary.csv (+ a table on stdout)
  benchcsv.py compare BASE NEW [--pct P] [--all] [--csv OUT] [--filter REGEX]
        BASE/NEW: summary.csv files or result dirs. One line per row of NEW with the base and new
        median, the change in % and the noise (larger spread of the two). A row counts as
        changed when |change| > max(P, noise) (default P = 10). Without --all only changed,
        new and missing rows are listed. --csv writes every row with the verdict.
  benchcsv.py scaling SUMMARY [--csv OUT] [--all]
        Rows whose tags differ only in their first number ("inline html x2000" / "x5000" ...)
        form a series; prints the empirical exponent k of time ~ n^k between consecutive sizes
        (k ~ 1 linear, ~ 2 quadratic). Only super-linear steps unless --all.
  benchcsv.py check SUMMARY [--baseline FILE] [--limits FILE] [--pct P] [--factor F] [--csv OUT]
        The regression check of scripts/perf-check.sh. FILE rows (lines starting with '#' are
        comments): --baseline "suite,function,tag,unit,median,spread_pct" — fails when the new
        median > baseline median * (1 + max(P %, F * spread)) with spread the larger of the two
        runs' spreads (default P = 10, F = 3); --limits "kind,suite,function,row,limit,profiles,note"
        — kind "scaling": row is a series pattern ('#' for the number), fails when the exponent k
        between the series' two largest sizes exceeds limit; kind "max": row is a tag, fails when
        the median exceeds limit (in the row's unit); profiles: the bench profiles the limit applies
        to (--profile; default both). A checked row missing from SUMMARY fails.

QTest's CSV rows are  "function","tag","metric",value_per_iteration,total,iterations.
Times are reported in milliseconds per iteration (WalltimeNanoseconds are converted), memory
(BytesAllocated) in MiB. The summary has one row per (suite, function, tag, metric) with the
median, min and max over the repetitions and spread = (max - min) / median.
"""
import argparse
import collections
import csv
import math
import os
import re
import statistics
import sys


def unit_of(metric):
    if metric.startswith("Walltime"):
        return "ms"
    if metric == "BytesAllocated":
        return "MiB"
    return metric


def scaled(metric, value):
    if metric == "WalltimeNanoseconds":
        return value / 1e6
    if metric == "BytesAllocated":
        return value / (1024 * 1024)
    return value


def summarize(directory):
    raw = os.path.join(directory, "raw")
    rows = {}
    order = []
    for name in sorted(os.listdir(raw)):
        m = re.match(r"^(bench_[a-z]+)(?:@[^.]*)?\.(\d+)\.csv$", name)
        if not m:
            continue
        suite = m.group(1)
        with open(os.path.join(raw, name), newline="", encoding="utf-8") as f:
            for rec in csv.reader(f):
                if len(rec) < 6:
                    continue
                function, tag, metric = rec[0], rec[1], rec[2]
                try:
                    value = float(rec[3])
                    iterations = int(float(rec[5]))
                except ValueError:
                    continue
                key = (suite, function, tag, unit_of(metric))
                if key not in rows:
                    rows[key] = {"values": [], "iterations": []}
                    order.append(key)
                rows[key]["values"].append(scaled(metric, value))
                rows[key]["iterations"].append(iterations)
    out = os.path.join(directory, "summary.csv")
    with open(out, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["suite", "function", "tag", "unit", "n", "median", "min", "max", "spread_pct", "iterations"])
        for key in order:
            v = rows[key]["values"]
            med = statistics.median(v)
            spread = (max(v) - min(v)) / med * 100 if med else 0.0
            w.writerow([*key, len(v), f"{med:.6g}", f"{min(v):.6g}", f"{max(v):.6g}", f"{spread:.1f}",
                        int(statistics.median(rows[key]["iterations"]))])
    print_table(load(out))
    print(f"summary: {out}")


def load(path):
    if os.path.isdir(path):
        path = os.path.join(path, "summary.csv")
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def row_name(r):
    return f"{r['suite']}::{r['function']} {r['tag']}"


def print_table(rows):
    width = max((len(row_name(r)) for r in rows), default=20)
    for r in rows:
        print(f"{row_name(r):<{width}}  {float(r['median']):>12.4f} {r['unit']:<3}  ±{float(r['spread_pct']):5.1f}%  (n={r['n']})")


def compare(args):
    base_rows = load(args.base)
    new_rows = load(args.new)
    flt = re.compile(args.filter) if args.filter else None
    base = {(r["suite"], r["function"], r["tag"], r["unit"]): r for r in base_rows}
    seen = set()
    out = []
    for r in new_rows:
        key = (r["suite"], r["function"], r["tag"], r["unit"])
        if flt and not flt.search(row_name(r)):
            continue
        seen.add(key)
        b = base.get(key)
        nm = float(r["median"])
        if not b:
            out.append([*key, "", f"{nm:.6g}", "", "", "", "new"])
            continue
        bm = float(b["median"])
        noise = max(float(b["spread_pct"]), float(r["spread_pct"]))
        delta = (nm / bm - 1) * 100 if bm else (0.0 if nm == 0 else math.inf)
        verdict = ""
        # Below the clock resolution (QBENCHMARK reports 0 for sub-nanosecond work) nothing counts.
        if max(bm, nm) > 0 and abs(delta) > max(args.pct, noise):
            verdict = "faster" if delta < 0 else "slower"
        out.append([*key, f"{bm:.6g}", f"{nm:.6g}", f"{bm and nm / bm:.3f}", f"{delta:+.1f}", f"{noise:.1f}", verdict])
    for key, b in base.items():
        if key not in seen and (not flt or flt.search(row_name(b))):
            out.append([*key, b["median"], "", "", "", "", "missing"])

    width = max((len(f"{o[0]}::{o[1]} {o[2]}") for o in out), default=20)
    print(f"{'benchmark':<{width}}  {'base':>11} {'new':>11} {'unit':<4} {'change':>8} {'noise':>6}")
    changed = faster = slower = 0
    ratios = []
    for o in out:
        if o[5] and o[4] and float(o[4]) > 0 and float(o[5]) > 0:
            ratios.append(float(o[5]) / float(o[4]))
        if o[9]:
            changed += 1
            faster += o[9] == "faster"
            slower += o[9] == "slower"
        if not args.all and not o[9]:
            continue
        name = f"{o[0]}::{o[1]} {o[2]}"
        change = f"{o[7]}%" if o[7] else ""
        noise = f"±{o[8]}%" if o[8] else ""
        print(f"{name:<{width}}  {o[4] or '-':>11} {o[5] or '-':>11} {o[3]:<4} {change:>8} {noise:>6}  {o[9].upper()}")
    geo = math.exp(sum(math.log(x) for x in ratios) / len(ratios)) if ratios else float("nan")
    print(f"{len(out)} rows: {faster} faster, {slower} slower (|change| > max({args.pct:g}%, noise)), "
          f"{sum(1 for o in out if o[9] == 'new')} new, {sum(1 for o in out if o[9] == 'missing')} missing; "
          f"geometric mean new/base {geo:.3f}")
    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["suite", "function", "tag", "unit", "base_median", "new_median", "ratio", "change_pct",
                        "noise_pct", "verdict"])
            w.writerows(out)
        print(f"compare: {args.csv}")


def scaling(args):
    out = []
    for (suite, fn, pattern), pts in series(load(args.summary)).items():
        for (n1, t1), (n2, t2) in zip(pts, pts[1:]):
            if n2 == n1 or t1 <= 0 or t2 <= 0:
                continue
            k = math.log(t2 / t1) / math.log(n2 / n1)
            flag = "QUADRATIC?" if k > 1.6 else ("superlinear" if k > 1.25 else "")
            out.append([suite, fn, pattern, n1, n2, f"{t1:.4g}", f"{t2:.4g}", f"{k:.2f}", flag])
    for o in out:
        if o[-1] or args.all:
            print(f"{o[0]}::{o[1]} '{o[2]}' {o[3]}->{o[4]}: {o[5]} -> {o[6]} ms, k={o[7]} {o[8]}")
    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["suite", "function", "pattern", "n1", "n2", "t1_ms", "t2_ms", "exponent", "flag"])
            w.writerows(out)
        print(f"scaling: {args.csv}")


def read_commented_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(line for line in f if line.strip() and not line.startswith("#")))


def series(rows):
    """(suite, function, pattern) -> sorted [(n, median_ms)] for the ms rows with a number in the tag."""
    groups = collections.OrderedDict()
    for r in rows:
        if r["unit"] != "ms":
            continue
        m = re.search(r"(\d+)", r["tag"])
        if not m:
            continue
        key = (r["suite"], r["function"], r["tag"][: m.start()] + "#" + r["tag"][m.end():])
        groups.setdefault(key, []).append((int(m.group(1)), float(r["median"])))
    return {k: sorted(set(v)) for k, v in groups.items()}


def check(args):
    rows = load(args.summary)
    by_key = {(r["suite"], r["function"], r["tag"], r["unit"]): r for r in rows}
    out = []  # [status, name, value, limit, detail]
    if args.baseline:
        for b in read_commented_csv(args.baseline):
            key = (b["suite"], b["function"], b["tag"], b["unit"])
            name = f"{b['suite']}::{b['function']} {b['tag']}"
            r = by_key.get(key)
            if not r:
                out.append(["MISSING", name, "", "", "row not in this run"])
                continue
            bm, nm = float(b["median"]), float(r["median"])
            spread = max(float(b["spread_pct"]), float(r["spread_pct"]))
            tol = max(args.pct, args.factor * spread)
            limit = bm * (1 + tol / 100)
            change = (nm / bm - 1) * 100 if bm else 0.0
            status = "FAIL" if nm > limit else ("faster" if nm < bm * (1 - tol / 100) else "ok")
            out.append([status, name, f"{nm:.4g} {b['unit']}", f"{limit:.4g}",
                        f"baseline {bm:.4g}, {change:+.1f} %, tolerance {tol:.0f} %"])
    if args.limits:
        groups = series(rows)
        for l in read_commented_csv(args.limits):
            if args.profile and args.profile not in (l.get("profiles") or "full small").split():
                continue
            limit = float(l["limit"])
            if l["kind"] == "scaling":
                name = f"{l['suite']}::{l['function']} '{l['row']}'"
                pts = groups.get((l["suite"], l["function"], l["row"]), [])
                if len(pts) < 2:
                    out.append(["MISSING", name, "", f"k <= {limit:g}", "fewer than two sizes in this run"])
                    continue
                (n1, t1), (n2, t2) = pts[-2], pts[-1]
                k = math.log(t2 / t1) / math.log(n2 / n1) if t1 > 0 and t2 > 0 else 0.0
                out.append(["FAIL" if k > limit else "ok", name, f"k = {k:.2f}", f"k <= {limit:g}",
                            f"{n1} -> {n2}: {t1:.4g} -> {t2:.4g} ms"])
            elif l["kind"] == "max":
                name = f"{l['suite']}::{l['function']} {l['row']}"
                r = next((x for x in rows if (x["suite"], x["function"], x["tag"]) ==
                          (l["suite"], l["function"], l["row"])), None)
                if not r:
                    out.append(["MISSING", name, "", f"<= {limit:g}", "row not in this run"])
                    continue
                v = float(r["median"])
                out.append(["FAIL" if v > limit else "ok", name, f"{v:.4g} {r['unit']}", f"<= {limit:g}",
                            l.get("note", "")])
            else:
                out.append(["MISSING", f"{l['suite']}::{l['function']} {l['row']}", "", "",
                            f"unknown kind {l['kind']!r}"])
    width = max((len(o[1]) for o in out), default=20)
    for o in out:
        print(f"{o[0]:<7} {o[1]:<{width}}  {o[2]:>14}  limit {o[3]:<10} {o[4]}")
    failed = sum(1 for o in out if o[0] in ("FAIL", "MISSING"))
    faster = sum(1 for o in out if o[0] == "faster")
    print(f"perf-check: {len(out)} checks, {failed} failed"
          + (f", {faster} clearly faster than the baseline (ratchet it: scripts/perf-check.sh -u)" if faster else ""))
    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["status", "check", "value", "limit", "detail"])
            w.writerows(out)
    return 1 if failed else 0


def main(argv):
    if len(argv) >= 2 and argv[0] == "summarize":
        summarize(argv[1])
        return 0
    ap = argparse.ArgumentParser(prog="benchcsv.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("compare")
    c.add_argument("base")
    c.add_argument("new")
    c.add_argument("pct_pos", nargs="?", type=float, help=argparse.SUPPRESS)  # old positional PCT
    c.add_argument("--pct", type=float, default=10.0)
    c.add_argument("--all", action="store_true")
    c.add_argument("--csv")
    c.add_argument("--filter")
    s = sub.add_parser("scaling")
    s.add_argument("summary")
    s.add_argument("--csv")
    s.add_argument("--all", action="store_true")
    k = sub.add_parser("check")
    k.add_argument("summary")
    k.add_argument("--baseline")
    k.add_argument("--limits")
    k.add_argument("--pct", type=float, default=10.0)
    k.add_argument("--factor", type=float, default=3.0)
    k.add_argument("--csv")
    k.add_argument("--profile", help="skip limits whose 'profiles' column does not name this profile")
    if not argv or argv[0] not in ("compare", "scaling", "check"):
        print(__doc__, file=sys.stderr)
        return 2
    args = ap.parse_args(argv)
    if args.cmd == "compare":
        if args.pct_pos is not None:
            args.pct = args.pct_pos
        compare(args)
    elif args.cmd == "check":
        return check(args)
    else:
        scaling(args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
