#!/usr/bin/env python3
"""What the sections and the largest data segments of a .wasm are (no name section needed).

  wasm-sections.py brulion.wasm [--top 30]

Sections with sizes; data segments classified by content: QML compiled units ("qv4cdata"
header), TrueType/OpenType fonts (sfnt header + table directory), Qt resource trees, zstd/zlib
compressed blobs, text (UTF-8/UTF-16 strings), and the rest; the top segments with a sniff.
"""
import argparse
import collections
import re
import sys


def leb(b, i):
    r = s = 0
    while True:
        x = b[i]
        i += 1
        r |= (x & 0x7F) << s
        s += 7
        if not x & 0x80:
            return r, i


NAMES = {0: "custom", 1: "type", 2: "import", 3: "function", 4: "table", 5: "memory", 6: "global", 7: "export",
         8: "start", 9: "elem", 10: "code", 11: "data", 12: "datacount"}


def classify(seg):
    if seg[:8] == b"qv4cdata":
        return "QML compiled unit (qv4cdata)"
    if seg[:4] in (b"\x00\x01\x00\x00", b"OTTO", b"true") and len(seg) > 1000:
        n = int.from_bytes(seg[4:6], "big")
        tags = {seg[12 + 16 * k:16 + 16 * k] for k in range(min(n, 40))}
        if b"cmap" in tags or b"glyf" in tags or b"CFF " in tags:
            return "font (TrueType/OpenType)"
    if seg[:4] == b"\x28\xb5\x2f\xfd":
        return "zstd-compressed blob"
    if b"qv4cdata" in seg[:64]:
        return "QML compiled unit (qv4cdata, offset)"
    printable = sum(1 for c in seg[:4096] if 32 <= c < 127 or c in (9, 10, 13))
    zeros = seg[:4096].count(0)
    sample = min(len(seg), 4096)
    if sample and printable / sample > 0.85:
        return "text (UTF-8/ASCII)"
    if sample and zeros / sample > 0.35 and printable / sample > 0.35:
        return "text (UTF-16) or sparse tables"
    return "binary data (tables, vtables, …)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wasm")
    ap.add_argument("--top", type=int, default=30)
    a = ap.parse_args()
    b = open(a.wasm, "rb").read()
    assert b[:4] == b"\0asm"
    i = 8
    sections = []
    data = None
    while i < len(b):
        sid = b[i]
        size, j = leb(b, i + 1)
        name = NAMES.get(sid, str(sid))
        if sid == 0:
            nlen, k = leb(b, j)
            name = "custom:" + b[k:k + nlen].decode(errors="replace")
        sections.append((name, size))
        if sid == 11:
            data = (j, j + size)
        i = j + size
    total = len(b)
    print("file %d B" % total)
    for name, size in sections:
        print("  %-24s %12d B  %5.1f %%" % (name, size, 100.0 * size / total))
    if not data:
        return
    j, end = data
    count, j = leb(b, j)
    segs = []
    for _ in range(count):
        flags, j = leb(b, j)
        if flags in (0, 2):
            if flags == 2:
                _, j = leb(b, j)
            # offset expression: i32.const LEB end(0x0b)
            while b[j] != 0x0B:
                j += 1
            j += 1
        n, j = leb(b, j)
        segs.append(b[j:j + n])
        j += n
    cats = collections.defaultdict(lambda: [0, 0])
    for s in segs:
        c = classify(s)
        cats[c][0] += len(s)
        cats[c][1] += 1
    dtotal = sum(len(s) for s in segs)
    print("\ndata segments: %d, %d B" % (len(segs), dtotal))
    for c, (n, k) in sorted(cats.items(), key=lambda kv: -kv[1][0]):
        print("  %-40s %10d B %5.1f %%  (%d segments)" % (c, n, 100.0 * n / dtotal, k))
    print("\nlargest segments:")
    for s in sorted(segs, key=len, reverse=True)[:a.top]:
        sniff = re.sub(rb"[^\x20-\x7e]", b".", s[:48]).decode()
        print("  %9d B  %-34s %s" % (len(s), classify(s), sniff))


if __name__ == "__main__":
    main()
