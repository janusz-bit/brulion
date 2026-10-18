#!/usr/bin/env python3
"""Where the bytes of a Brulion package go (Android APK, the app's native library, wasm objects).

  size-report.py apk <a.apk> [<b.apk>]          entries by category (+ comparison), top 20, the
                                                 download size if the stored (uncompressed)
                                                 entries were deflated (zlib -9) — the APK keeps
                                                 native libraries and dex uncompressed so they can
                                                 be mmapped; a store (Play) recompresses for
                                                 download, a direct download (GitHub, F-Droid) not
  size-report.py syms <lib.so> --nm <llvm-nm>    the final library's symbols by category
                                                 (QML compiled units / AOT code, qrc resources,
                                                 moc, core modules, app, Qt templates, std, …)
  size-report.py objs <build dir> --size <llvm-size> [--prefix src/app]
                                                 code+data of every object file of the build
                                                 (before --gc-sections/ICF, so an upper bound),
                                                 grouped: QML cache per file, qrc per resource,
                                                 core module, app area, autogen (moc), 3rdparty
Options: --md (markdown tables), --top N (default 20).
"""
import argparse
import collections
import os
import re
import subprocess
import sys
import zipfile
import zlib


def human(n):
    n = float(n)
    for unit in ("B", "KB", "MB", "GB"):
        if abs(n) < 1024 or unit == "GB":
            return ("%.0f %s" if unit == "B" else "%.2f %s") % (n, unit)
        n /= 1024


def table(rows, header, md):
    widths = [max(len(str(r[i])) for r in rows + [header]) for i in range(len(header))]
    out = []
    if md:
        out.append("| " + " | ".join(header) + " |")
        out.append("|" + "|".join("---" if i == len(header) - 1 or i == 0 else "---:" for i in range(len(header))) + "|")
        for r in rows:
            out.append("| " + " | ".join(str(c) for c in r) + " |")
    else:
        fmt = "  ".join("%-" + str(w) + "s" if i == 0 else "%" + str(w) + "s" for i, w in enumerate(widths))
        out.append(fmt % tuple(header))
        for r in rows:
            out.append(fmt % tuple(r))
    return "\n".join(out)


# ---- APK -------------------------------------------------------------------------------------

STYLES = ("Material", "Universal", "Imagine", "Fusion", "FluentWinUI3")


def apk_category(name):
    m = re.match(r"lib/[^/]+/(.+)\.so$", name)
    if m:
        lib = m.group(1)
        if lib.startswith("libbrulion"):
            return "app library (libbrulion)"
        if lib.startswith("libbenchprobe"):
            return "bench probe (measurement copy only)"
        if lib.startswith("libc++_shared"):
            return "C++ runtime (libc++_shared)"
        if "qmltooling" in lib:
            return "Qt plugins: QML debugging (qmltooling)"
        if any(s in lib for s in STYLES):
            return "Qt Quick Controls styles not used (%s)" % "/".join(STYLES)
        if "QuickDialogs2" in lib or "QtQuick_Dialogs" in lib:
            return "Qt Quick Dialogs (non-native file dialog impl)"
        if lib.startswith("libplugins_"):
            return "Qt plugins (platform, image formats, TLS, …)"
        if lib.startswith("libqml_"):
            return "QML plugins (module stubs)"
        if lib.startswith("libQt6"):
            return "Qt libraries (Core, Gui, Qml, Quick, …)"
        return "other native libraries"
    if name.startswith("classes") and name.endswith(".dex"):
        return "Java/Kotlin bytecode (classes.dex)"
    if name.startswith("assets/"):
        return "assets (Qt rcc bundle, dexopt profile)"
    if name.startswith("res/") or name == "resources.arsc":
        return "Android resources (icons, layouts)"
    if name.startswith("META-INF/"):
        return "signature + metadata"
    if name.startswith("kotlin/"):
        return "Kotlin metadata"
    return "other"


def apk_stats(path):
    z = zipfile.ZipFile(path)
    entries = []
    for i in z.infolist():
        data = z.read(i.filename) if i.compress_type == zipfile.ZIP_STORED else None
        deflated = len(zlib.compress(data, 9)) if data is not None else i.compress_size
        entries.append({"name": i.filename, "size": i.file_size, "stored": i.compress_size,
                        "deflated": deflated, "compressed": i.compress_type != zipfile.ZIP_STORED,
                        "cat": apk_category(i.filename)})
    return {"path": path, "file": os.path.getsize(path), "entries": entries}


def cmd_apk(args):
    stats = [apk_stats(p) for p in args.apk]
    out = []
    for s in stats:
        tot = sum(e["size"] for e in s["entries"])
        stored = sum(e["size"] for e in s["entries"] if not e["compressed"])
        defl = sum(e["deflated"] for e in s["entries"])
        out.append("## %s\n" % os.path.basename(s["path"]))
        out.append("APK file %s (%d B); %d entries, %s uncompressed, %s of it stored without compression; "
                   "all entries deflated (zlib -9): %s\n" % (human(s["file"]), s["file"], len(s["entries"]),
                                                           human(tot), human(stored), human(defl)))
    cats = collections.OrderedDict()
    for k, s in enumerate(stats):
        for e in s["entries"]:
            c = cats.setdefault(e["cat"], [[0, 0, 0] for _ in stats])
            c[k][0] += e["size"]
            c[k][1] += e["deflated"]
            c[k][2] += 1
    order = sorted(cats, key=lambda c: -cats[c][-1][0])
    header = ["category"]
    for s in stats:
        b = os.path.basename(s["path"]).replace(".apk", "")
        header += [b + " size", b + " deflated", b + " %", b + " n"]
    if len(stats) == 2:
        header += ["Δ size"]
    rows = []
    for c in order:
        r = [c]
        for k, s in enumerate(stats):
            tot = sum(e["size"] for e in s["entries"])
            r += [human(cats[c][k][0]), human(cats[c][k][1]), "%.1f" % (100.0 * cats[c][k][0] / tot), cats[c][k][2]]
        if len(stats) == 2:
            r += ["%+d B" % (cats[c][1][0] - cats[c][0][0])]
        rows.append(r)
    out.append(table(rows, header, args.md))
    s = stats[-1]
    out.append("\n### Top %d entries of %s\n" % (args.top, os.path.basename(s["path"])))
    rows = []
    for e in sorted(s["entries"], key=lambda e: -e["size"])[:args.top]:
        rows.append([e["name"], human(e["size"]), "deflate" if e["compressed"] else "stored", human(e["deflated"]),
                     e["cat"]])
    out.append(table(rows, ["entry", "size", "in APK", "deflated", "category"], args.md))
    print("\n".join(out))


# ---- symbols of the final library -------------------------------------------------------------

CORE = None


def core_modules():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.abspath(os.path.join(here, "..", "..", ".."))
    d = os.path.join(root, "src", "core")
    if os.path.isdir(d):
        return sorted(x for x in os.listdir(d) if os.path.isdir(os.path.join(d, x)))
    return ["calc", "dates", "index", "io", "markdown", "reminders", "sync", "text", "util", "vault"]


def sym_category(name, kind):
    if name.startswith("QmlCacheGeneratedCode::") and name.endswith("::qmlData"):
        return "QML compiled units (bytecode + metadata, qmlData)"
    if name.startswith("QmlCacheGeneratedCode::"):
        return "QML AOT-compiled functions (qmlcachegen C++)"
    if "QmlCacheGeneratedCode::" in name:
        return "QML AOT: RTTI, vtables, guards of the generated lambdas"
    if name in ("qt_resource_data", "qt_resource_name", "qt_resource_struct"):
        return "qrc resources (icons, logo, translations, QML sources, qmldir)"
    if (name.startswith("qt_meta_") or "staticMetaObject" in name or "::qt_static_metacall" in name
            or "::qt_metacall" in name or "::qt_metacast" in name or "::metaObject() const" in name
            or name.startswith("qt_incomplete_metaTypeArray") or "QMetaTypeForType" in name
            or "QMetaTypeInterfaceWrapper" in name or name.startswith("QtMocHelpers")):
        return "moc / meta-type tables"
    if name.startswith("qml_register_types") or name.startswith("QQmlPrivate::") or "qmlcache_loader" in name \
            or name.startswith("QmlCacheGeneratedCode"):
        return "QML type registration"
    if re.match(r"(mz_|tdefl_|tinfl_|miniz)", name):
        return "3rdparty miniz"
    m = re.match(r"(?:\(anonymous namespace\)::)?(?:[\w:<>]*\s)?brulion::(\w+)", name)
    if m:
        mod = m.group(1)
        if mod in CORE:
            return "brulion C++: core::" + mod
        if mod[0].islower():
            return "brulion C++: brulion::" + mod
        return "brulion C++: classes in namespace brulion (core + app, see objs)"
    if "brulion" in name:
        return "brulion C++: other"
    if name.startswith("std::") or name.startswith("void std::") or "std::__ndk1" in name.split("(")[0]:
        return "C++ std templates"
    if re.match(r"(?:\w+\s)?(Q\w+<|QtPrivate::|QArrayData|QHashPrivate|QMap|QList|QString|QtConcurrent)", name):
        return "Qt templates instantiated here"
    if name.startswith("(anonymous namespace)"):
        return "other (anonymous namespace)"
    return "other"


def cmd_syms(args):
    global CORE
    CORE = set(core_modules())
    res = subprocess.run([args.nm, "-S", "--size-sort", "-C", "--defined-only", args.lib], capture_output=True,
                         text=True, check=True).stdout
    cats = collections.defaultdict(lambda: [0, 0, 0])  # text, data, count
    per_qml = collections.defaultdict(int)
    for line in res.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4:
            continue
        _, size, kind, name = parts
        size = int(size, 16)
        c = sym_category(name, kind)
        cats[c][0 if kind in "tTwW" else 1] += size
        cats[c][2] += 1
        m = re.match(r"QmlCacheGeneratedCode::_qt_qml_Brulion_qml_(\w+?)_qml::", name)
        if m:
            per_qml[m.group(1)] += size
    rows = []
    total = sum(v[0] + v[1] for v in cats.values())
    for c, v in sorted(cats.items(), key=lambda kv: -(kv[1][0] + kv[1][1])):
        rows.append([c, human(v[0]), human(v[1]), human(v[0] + v[1]), "%.1f" % (100.0 * (v[0] + v[1]) / total), v[2]])
    print("symbols with a size: %s (the stripped library also has relocations, unwind tables, "
          "dynamic symbols)\n" % human(total))
    print(table(rows, ["category", "code", "data", "total", "%", "symbols"], args.md))
    print("\n### QML files by compiled size (unit data + AOT code), top %d\n" % args.top)
    rows = [[k.replace("_", "/", 1), human(v)] for k, v in sorted(per_qml.items(), key=lambda kv: -kv[1])[:args.top]]
    print(table(rows, ["QML file", "size"], args.md))
    dev = sum(v for k, v in per_qml.items() if k.startswith("dev_"))
    print("\nQML total %s in %d files; dev/ (Gallery, playground) %s" % (human(sum(per_qml.values())), len(per_qml),
                                                                          human(dev)))


# ---- object files ---------------------------------------------------------------------------

def obj_category(rel):
    m = re.search(r"\.rcc/qmlcache/\w+?_qml_(\w+)_qml\.cpp\.o$", rel)
    if m:
        return "QML cache: " + m.group(1).split("_")[0], m.group(1)
    if "qmlcache_loader" in rel:
        return "QML cache: loader", None
    m = re.search(r"qrc_(\w+)\.cpp\.o$", rel)
    if m:
        n = m.group(1)
        if n.startswith("brulion_raw_qml") or n.startswith("qmake_"):
            return "qrc: QML sources + qmldir", n
        return "qrc: " + n, n
    m = re.search(r"src/core/(\w+)/", rel)
    if m:
        return "core: " + m.group(1), None
    if "_autogen" in rel or "qmltyperegistrations" in rel or "_qmltyperegistrations" in rel:
        return "autogen (moc, QML type registrations)", None
    if "3rdparty" in rel:
        return "3rdparty", None
    m = re.search(r"src/(app|platform)/(?:CMakeFiles/[\w.]+\.dir/)?(\w+)/", rel)
    if m and m.group(2) != "CMakeFiles":
        return "%s: %s" % (m.group(1), m.group(2)), None
    m = re.search(r"src/app/CMakeFiles/brulion\.dir/(\w+)\.cpp\.o$", rel)
    if m:
        return "app: main", None
    return "other", None


def cmd_objs(args):
    objs = []
    for dp, _, fs in os.walk(args.build):
        for f in fs:
            if f.endswith(".o"):
                objs.append(os.path.join(dp, f))
    cats = collections.defaultdict(lambda: [0, 0])
    detail = collections.defaultdict(int)
    for i in range(0, len(objs), 200):
        chunk = objs[i:i + 200]
        res = subprocess.run([args.size, "-A"] + chunk, capture_output=True, text=True).stdout
        cur = None
        for line in res.splitlines():
            if line.endswith(":") and " :" in line:
                cur = line.split(" :")[0].strip()
                continue
            parts = line.split()
            if cur is None or len(parts) < 2 or not parts[1].isdigit():
                continue
            sec, size = parts[0], int(parts[1])
            if sec == "Total" or sec.startswith(".debug") or sec in (".comment", ".note.GNU-stack", ".llvm_addrsig", ".group",
                                                   ".symtab", ".strtab", ".shstrtab") or sec.startswith(".rela"):
                continue
            rel = os.path.relpath(cur, args.build)
            c, d = obj_category(rel)
            code = sec.startswith(".text")
            cats[c][0 if code else 1] += size
            if d:
                detail[(c, d)] += size
    total = sum(a + b for a, b in cats.values())
    rows = [[c, human(v[0]), human(v[1]), human(v[0] + v[1]), "%.1f" % (100.0 * (v[0] + v[1]) / total)]
            for c, v in sorted(cats.items(), key=lambda kv: -(kv[1][0] + kv[1][1]))]
    print("object files: %d, code+data %s (before linking: duplicates of inline/template code are counted "
          "in every object)\n" % (len(objs), human(total)))
    print(table(rows[:args.top * 2], ["group", "code", "data", "total", "%"], args.md))
    rows = [[c + " / " + d, human(v)] for (c, d), v in sorted(detail.items(), key=lambda kv: -kv[1])[:args.top]
            if c.startswith("qrc")]
    if rows:
        print("\n### qrc resources\n")
        print(table(rows, ["resource", "size"], args.md))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("apk")
    a.add_argument("apk", nargs="+")
    s = sub.add_parser("syms")
    s.add_argument("lib")
    s.add_argument("--nm", default="llvm-nm")
    o = sub.add_parser("objs")
    o.add_argument("build")
    o.add_argument("--size", default="llvm-size")
    for p in (a, s, o):
        p.add_argument("--md", action="store_true")
        p.add_argument("--top", type=int, default=20)
    args = ap.parse_args()
    {"apk": cmd_apk, "syms": cmd_syms, "objs": cmd_objs}[args.cmd](args)


if __name__ == "__main__":
    main()
