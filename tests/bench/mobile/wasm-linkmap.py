#!/usr/bin/env python3
"""Code and data of the browser app by origin, from a wasm-ld map (wasm-linkmap.sh).

  wasm-linkmap.py <map> [--md] [--top 40] [--detail REGEX]

Every input chunk (a function, a data segment) of the CODE and DATA sections is attributed to
its object file / archive member and grouped: the app's own code (QML cache = qmlcachegen's
compiled QML per area, qrc resources, core modules, app areas, moc), Qt libraries per module,
Qt plugins, QML modules (static plugins + their resources: QML files, images), the unused Qt
Quick Controls styles (Material, Universal, Imagine, Fusion, FluentWinUI3), libc++ / libc /
compiler runtime. Sizes are BEFORE wasm-opt (code ~20 % larger than in the shipped .wasm, data
unchanged). --detail REGEX lists the largest chunks of the groups matching REGEX.
"""
import argparse
import collections
import re

STYLES = ("Material", "Universal", "Imagine", "Fusion", "FluentWinUI3")
LINE = re.compile(r"^\s+(?:-|[0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)\s{9}(\S.*?):\((.*)\)\s*$")
OUT = re.compile(r"^\s+(?:-|[0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+) (\S+)\s*$")


def group(path):
    p = path
    style = next((s for s in STYLES if re.search(r"(Controls2%s|Controls/%s\b|%sStyle)" % (s, s, s), p)), None)
    if style:
        return "Qt Quick Controls style not used: " + style
    m = re.search(r"/lib/libQt6(\w+)\.a\(", p)
    if m:
        return "Qt lib: " + m.group(1)
    m = re.search(r"/plugins/(\w+)/", p)
    if m:
        return "Qt plugins: " + m.group(1)
    m = re.search(r"/wasm_singlethread/qml/([\w/]+?)/(?:objects-Release|lib\w+\.a)", p)
    if m:
        mod = m.group(1).replace("/", ".")
        kind = "resources (QML files, images)" if "_resources_" in p or "/rcc/" in p else "plugin"
        return "QML module %s: %s" % (mod, kind)
    m = re.search(r"/wasm_singlethread/lib/objects-Release/(\w+?)_resources", p)
    if m:
        return "Qt lib resources: " + m.group(1)
    m = re.search(r"/wasm_singlethread/lib/(lib\w+)\.a\(", p)
    if m:
        return "Qt 3rdparty: " + m.group(1)
    m = re.search(r"sysroot/lib/wasm32-emscripten/(lib[\w+-]+)\.a\(", p)
    if m:
        lib = m.group(1)
        if lib.startswith("libc++") or lib.startswith("libc++abi"):
            return "C++ runtime (libc++, libc++abi)"
        return "emscripten system: " + re.sub(r"-(mt|noexcept|debug|asan|ww|legacysjlj|wasm-sjlj|optz|nogc)", "", lib)
    if "/qmlcache/" in p:
        m = re.search(r"_qml_(\w+?)_", p.split("/qmlcache/")[1])
        return "app QML cache: " + (m.group(1) if m else "loader")
    if "/rcc/qrc_" in p or "/.rcc/qrc_" in p:
        m = re.search(r"qrc_(\w+?)(?:_init)?\.cpp", p)
        n = m.group(1) if m else "?"
        if n.startswith("brulion_raw_qml") or n.startswith("qmake_Brulion"):
            n = "QML sources + qmldir"
        return "app qrc: " + n
    m = re.search(r"src/core/(\w+)/", p)
    if m:
        return "app core: " + m.group(1)
    if "_autogen" in p or "qmltyperegistrations" in p:
        return "app moc / QML type registration"
    m = re.search(r"src/(app|platform)/(?:CMakeFiles/[\w.]+\.dir/)?(\w+)/", p)
    if m and m.group(2) != "CMakeFiles":
        return "app %s: %s" % (m.group(1), m.group(2))
    if p.startswith("src/") or "/src/" in p:
        return "app: other"
    if "3rdparty" in p:
        return "app 3rdparty"
    if p.startswith("<internal>"):
        return "linker-generated"
    return "other: " + p.split("/")[-1][:40]


def fmt(n):
    return "%.2f MB" % (n / 1048576) if n >= 104858 else "%.0f KB" % (n / 1024)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--md", action="store_true")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--detail")
    a = ap.parse_args()
    sizes = collections.defaultdict(lambda: [0, 0])
    detail = collections.defaultdict(int)
    out = None
    totals = {}
    with open(a.map, errors="replace") as f:
        for line in f:
            m = OUT.match(line)
            if m and m.group(2) in ("CODE", "DATA", "TYPE", "IMPORT", "FUNCTION", "ELEM", "GLOBAL", "EXPORT"):
                out = m.group(2)
                totals[out] = int(m.group(1), 16)
                continue
            if out not in ("CODE", "DATA"):
                continue
            m = LINE.match(line)
            if not m:
                continue
            size = int(m.group(1), 16)
            g = group(m.group(2))
            sizes[g][0 if out == "CODE" else 1] += size
            if a.detail and re.search(a.detail, g):
                detail[(g, m.group(2).split("/")[-1][:70], m.group(3)[:70])] += size
    code = sum(v[0] for v in sizes.values())
    data = sum(v[1] for v in sizes.values())
    print("map sections: %s" % ", ".join("%s %s" % (k, fmt(v)) for k, v in totals.items()))
    print("attributed: code %s, data %s (pre-wasm-opt)\n" % (fmt(code), fmt(data)))
    # coarse buckets first
    coarse = collections.defaultdict(lambda: [0, 0])
    for g, (c, d) in sizes.items():
        k = g.split(":")[0]
        if g.startswith("Qt Quick Controls style not used"):
            k = "Qt Quick Controls styles not used (5)"
        elif g.startswith("QML module"):
            k = "QML modules (Qt): plugins + resources"
        coarse[k][0] += c
        coarse[k][1] += d
    rows = sorted(coarse.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))
    hdr = ["group", "code", "data", "total", "% of all"]
    tot = code + data

    def show(rows):
        if a.md:
            print("| " + " | ".join(hdr) + " |\n|---|---:|---:|---:|---:|")
            for g, (c, d) in rows:
                print("| %s | %s | %s | %s | %.1f |" % (g, fmt(c), fmt(d), fmt(c + d), 100.0 * (c + d) / tot))
        else:
            for g, (c, d) in rows:
                print("  %-58s %10s %10s %10s %5.1f %%" % (g, fmt(c), fmt(d), fmt(c + d), 100.0 * (c + d) / tot))

    show(rows)
    print("\n### Top %d groups\n" % a.top)
    show(sorted(sizes.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))[:a.top])
    if a.detail:
        print("\n### Largest chunks of %s\n" % a.detail)
        for (g, f, s), n in sorted(detail.items(), key=lambda kv: -kv[1])[:40]:
            print("  %9d  %-40s %-50s %s" % (n, g[:40], f, s))


if __name__ == "__main__":
    main()
