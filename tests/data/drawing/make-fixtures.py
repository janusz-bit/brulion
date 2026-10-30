#!/usr/bin/env python3
"""Writes the Excalidraw fixtures of tst_drawing the way excalidraw.com writes them, re-implemented
independently of Brulion's C++ (packages/excalidraw/data/encode.ts and scene/export.ts:
JSON.stringify(scene, null, 2) → pako.deflate → a byte string in {"version":"1","encoding":
"bstring","compressed":true,"encoded":…} → JSON.stringify → btoa → the SVG's payload comments).

excalidraw_shapes.* hold every kind of element Brulion draws; excalidraw_frame.svg a frame (an
element Brulion does not edit: the drawing is shown read-only). conflict_ours.svg and
conflict_theirs.svg are two versions of one drawing for the sync center's conflict of drawings
(tests/app/io/fixtures.sh): elements "a", "b", "c" and "a", "b", "d".

smiley_embedded_v2.svg and test_embedded_v1.svg are Excalidraw's own test fixtures
(excalidraw/excalidraw, packages/excalidraw/tests/fixtures, MIT licence), unchanged.

obsidian_*.excalidraw.md are files of the Obsidian Excalidraw plugin (zsviczian/obsidian-excalidraw-
plugin 2.x: src/constants/constants.ts FRONTMATTER, src/shared/ExcalidrawData.ts generateMDBase,
src/shared/excalidrawMarkdownParsing.ts getMarkdownDrawingSection, src/utils/sceneDataUtils.ts
compress): the frontmatter and the warning line, the back of the card, "# Excalidraw Data" with
"## Text Elements" (`raw ^id` entries), "## Element Links", "## Embedded Files", `%%`, and the scene
as JSON.stringify(scene, null, "\t") in "```json" or LZ-String's compressToBase64 of it in lines of 256
characters with an empty line between ("```compressed-json"). LZ-String (pieroxy/lz-string, MIT) is
ported below from its JavaScript, independently of Brulion's C++ (string keys, as the JavaScript),
and checked against lz-string's own test vectors (lzstring/, copied from its test/data) before any
file is written: the script fails when they differ. excalidraw_freedraw.excalidraw is a
`.excalidraw` file as excalidraw.com's "Save to disk" writes it (JSON.stringify(scene, null, 2)).

    python3 tests/data/drawing/make-fixtures.py   # rewrites the files below next to this script
"""
import base64
import json
import os
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- LZ-String (lz-string 1.5, libs/lz-string.js: _compress / _decompress with 6 bits a character) ----

LZ_KEY = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/="


def units_of(text):
    """A Python str as JavaScript's UTF-16 code units (lone surrogates kept)."""
    out = []
    for ch in text:
        o = ord(ch)
        if o >= 0x10000:
            o -= 0x10000
            out += [0xD800 + (o >> 10), 0xDC00 + (o & 0x3FF)]
        else:
            out.append(o)
    return out


def compress_to_base64(units):
    """LZString.compressToBase64 over a list of UTF-16 code units (lz-string 1.5, as its JS)."""
    dictionary = {}
    to_create = set()
    w = ()
    enlarge_in = 2
    dict_size = 3
    num_bits = 2
    data = []
    val = 0
    pos = 0

    def bits(value, n):
        nonlocal val, pos
        for _ in range(n):
            val = (val << 1) | (value & 1)
            if pos == 5:
                pos = 0
                data.append(LZ_KEY[val])
                val = 0
            else:
                pos += 1
            value >>= 1

    def output_w():
        nonlocal enlarge_in, num_bits
        if w in to_create:
            if w[0] < 256:
                bits(0, num_bits)
                bits(w[0], 8)
            else:
                bits(1, num_bits)
                bits(w[0], 16)
            enlarge_in -= 1
            if enlarge_in == 0:
                enlarge_in = 1 << num_bits
                num_bits += 1
            to_create.discard(w)
        else:
            bits(dictionary[w], num_bits)
        enlarge_in -= 1
        if enlarge_in == 0:
            enlarge_in = 1 << num_bits
            num_bits += 1

    for u in units:
        c = (u,)
        if c not in dictionary:
            dictionary[c] = dict_size
            dict_size += 1
            to_create.add(c)
        wc = w + c
        if wc in dictionary:
            w = wc
        else:
            output_w()
            dictionary[wc] = dict_size
            dict_size += 1
            w = c
    if w:
        output_w()
    bits(2, num_bits)
    while True:
        val <<= 1
        if pos == 5:
            data.append(LZ_KEY[val])
            break
        pos += 1
    res = "".join(data)
    return res + "=" * ((4 - len(res) % 4) % 4)


def decompress_from_base64(text):
    """LZString.decompressFromBase64 → code units (None as JS's null; "" → [] as JS's "")."""
    if text == "":
        return None
    vals = [LZ_KEY.index(ch) if ch in LZ_KEY else 0 for ch in text]
    length = len(vals)
    st = {"val": vals[0], "pos": 32, "index": 1}

    def read(n):
        out = 0
        for i in range(n):
            b = 1 if st["val"] & st["pos"] else 0
            st["pos"] >>= 1
            if st["pos"] == 0:
                st["pos"] = 32
                st["val"] = vals[st["index"]] if st["index"] < length else 0
                st["index"] += 1
            out |= b << i
        return out

    dictionary = [None, None, None]
    enlarge_in = 4
    num_bits = 3
    nxt = read(2)
    if nxt == 0:
        c = [read(8)]
    elif nxt == 1:
        c = [read(16)]
    else:
        return []
    dictionary.append(c)
    w = c
    result = list(c)
    while True:
        if st["index"] > length:
            return []
        code = read(num_bits)
        if code in (0, 1):
            dictionary.append([read(8 if code == 0 else 16)])
            code = len(dictionary) - 1
            enlarge_in -= 1
        elif code == 2:
            return result
        if enlarge_in == 0:
            enlarge_in = 1 << num_bits
            num_bits += 1
        if code < len(dictionary):
            entry = dictionary[code]
        elif code == len(dictionary):
            entry = w + [w[0]]
        else:
            return None
        result += entry
        dictionary.append(w + [entry[0]])
        enlarge_in -= 1
        w = entry
        if enlarge_in == 0:
            enlarge_in = 1 << num_bits
            num_bits += 1


def units_from_bin(data):
    """lz-string's test files: UTF-16BE, an odd last byte the high byte of a last unit."""
    out = [data[i] * 256 + data[i + 1] for i in range(0, len(data) - 1, 2)]
    if len(data) & 1:
        out.append(data[-1] * 256)
    return out




def check_lz_vectors():
    """lz-string's own vectors (test/data/<name>/{data,base64}.bin): this port must reproduce them."""
    folder = os.path.join(HERE, "lzstring")
    for name in ["hello_world", "repeated", "all_ascii", "tattoo"]:
        with open(os.path.join(folder, name + ".data.bin"), "rb") as f:
            data = units_from_bin(f.read())
        with open(os.path.join(folder, name + ".base64.bin"), "rb") as f:
            expected = "".join(chr(u) for u in units_from_bin(f.read()))
        if compress_to_base64(data) != expected or decompress_from_base64(expected) != data:
            raise SystemExit(f"LZ-String port differs from lz-string's vector {name}")


def lz_base64(text):
    return compress_to_base64(units_of(text))


check_lz_vectors()


def element(id_, type_, x, y, **extra):
    e = {
        "id": id_, "type": type_, "x": x, "y": y, "width": 100, "height": 40, "angle": 0,
        "strokeColor": "#1e1e1e", "backgroundColor": "transparent", "fillStyle": "solid",
        "strokeWidth": 2, "strokeStyle": "solid", "roughness": 1, "opacity": 100, "groupIds": [],
        "frameId": None, "index": "a0", "roundness": None, "seed": 1438920917, "version": 37,
        "versionNonce": 1786524155, "isDeleted": False, "boundElements": None,
        "updated": 1761568000000, "link": None, "locked": False,
    }
    e.update(extra)
    return e


def wave(n, dx, amp):
    import math
    return [[round(i * dx, 2), round(amp * math.sin(i / 3), 2)] for i in range(n)]


FREEDRAW_PRESSURE = element(
    "Qk8_3vL1nYbXc0Rr2mT9a", "freedraw", 412.5, 230.25, width=120.5, height=40.75,
    points=wave(24, 5.25, 18), pressures=[round(0.2 + 0.03 * i, 3) for i in range(24)],
    simulatePressure=False, lastCommittedPoint=[120.75, 0])
FREEDRAW_SIMULATED = element(
    "pL0_aZ9yWq3e4R5t6Y7u8", "freedraw", 400, 320, strokeColor="#e03131", strokeWidth=1,
    opacity=80, points=wave(30, 4, 10), pressures=[], simulatePressure=True,
    lastCommittedPoint=None, customData={"note": "kept"})
FREEDRAW_DELETED = element(
    "deleted_stroke_0000001", "freedraw", 0, 0, isDeleted=True, points=[[0, 0], [10, 10]],
    pressures=[], simulatePressure=True)
RECTANGLE = element("rect_00000000000000001", "rectangle", 380, 200, width=200, height=180,
                    roundness={"type": 3})
TEXT = element("text_00000000000000001", "text", 420, 410, text="Zażółć gęślą jaźń",
               originalText="Zażółć gęślą jaźń", fontSize=20, fontFamily=5, textAlign="left",
               verticalAlign="top", containerId=None, autoResize=True, lineHeight=1.25)


BASE62 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"


def scene(elements, files=None):
    # Excalidraw's fractional indices, rising in the array's order ("a0", "a1", …).
    elements = [dict(e, index="a" + BASE62[i]) for i, e in enumerate(elements)]
    return {
        "type": "excalidraw", "version": 2, "source": "https://excalidraw.com",
        "elements": elements,
        "appState": {"gridSize": 20, "gridStep": 5, "gridModeEnabled": False,
                     "viewBackgroundColor": "#ffffff"},
        "files": files or {},
    }


def png_2x2():
    """A 2 × 2 RGB PNG (red, green / blue, white)."""
    import struct

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xffffffff)

    raw = b"\x00\xff\x00\x00\x00\xff\x00" + b"\x00\x00\x00\xff\xff\xff\xff"
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def shape(id_, type_, x, y, w, h, **extra):
    return element(id_, type_, x, y, width=w, height=h, **extra)


def linear(id_, type_, x, y, points, **extra):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    e = element(id_, type_, x, y, width=max(xs) - min(xs), height=max(ys) - min(ys), points=points,
                lastCommittedPoint=None, startBinding=None, endBinding=None, startArrowhead=None,
                endArrowhead=None)
    e.update(extra)
    return e


def text(id_, x, y, w, h, value, **extra):
    e = element(id_, "text", x, y, width=w, height=h, text=value, originalText=value, fontSize=20,
                fontFamily=5, textAlign="left", verticalAlign="top", containerId=None, autoResize=True,
                lineHeight=1.25)
    e.update(extra)
    return e


# Every kind of element Brulion draws (tst_drawing_elements readElements): shapes with a label,
# fills and dashes, lines and arrows with bindings, an elbow arrow, rotation, text, a group,
# pictures, filled and rotated freehand lines.
SHAPES = [
    shape("prostokat", "rectangle", 40, 40, 200, 100, roundness={"type": 3},
          boundElements=[{"id": "etykieta", "type": "text"}, {"id": "strzalka", "type": "arrow"}]),
    text("etykieta", 52.5, 77.5, 175, 25, "Zażółć gęślą jaźń", containerId="prostokat",
         textAlign="center", verticalAlign="middle"),
    shape("elipsa", "ellipse", 300, 40, 160, 100, backgroundColor="#a5d8ff", fillStyle="hachure",
          strokeStyle="dashed"),
    shape("romb", "diamond", 520, 40, 120, 120, roundness={"type": 2},
          boundElements=[{"id": "strzalka", "type": "arrow"}]),
    linear("linia", "line", 40, 220, [[0, 0], [80, -40], [160, 0]], roundness={"type": 2},
           strokeStyle="dotted"),
    linear("strzalka", "arrow", 240, 90, [[0, 0], [280, 10]], endArrowhead="arrow",
           startBinding={"elementId": "prostokat", "focus": 0, "gap": 1},
           endBinding={"elementId": "romb", "focus": 0.1, "gap": 2},
           boundElements=[{"id": "podpis", "type": "text"}], elbowed=False),
    text("podpis", 350, 80, 60, 25, "tak", containerId="strzalka", textAlign="center",
         verticalAlign="middle"),
    linear("lokiec", "arrow", 300, 200, [[0, 0], [100, 0], [100, 80]], endArrowhead="triangle",
           elbowed=True),
    shape("obrocony", "rectangle", 460, 220, 120, 60, angle=0.5),
    text("tekst", 40, 320, 220, 50, "Dwie linie: ąćęłńóśźż\nI emoji 🙂", fontFamily=2),
    shape("grupa1", "rectangle", 300, 320, 40, 40, groupIds=["grupaA"]),
    shape("grupa2", "ellipse", 360, 320, 40, 40, groupIds=["grupaA"]),
    element("obrazek", "image", 440, 320, width=40, height=40, fileId="plik-png", status="saved",
            scale=[1, 1]),
    element("bez-pliku", "image", 500, 320, width=40, height=40, fileId="brak", status="saved",
            scale=[1, 1]),
    element("wypelniona", "freedraw", 40, 420, width=60, height=40,
            points=[[0, 0], [60, 0], [30, 40], [0, 0]], pressures=[], simulatePressure=True,
            backgroundColor="#ffc9c9", lastCommittedPoint=None),
    element("obrocona", "freedraw", 160, 420, width=80, height=20, angle=1.0,
            points=wave(12, 7, 10), pressures=[], simulatePressure=True, lastCommittedPoint=None),
]
SHAPE_FILES = {
    "plik-png": {"id": "plik-png", "mimeType": "image/png", "created": 1761568000000,
                 "dataURL": "data:image/png;base64," + base64.b64encode(png_2x2()).decode("ascii")},
}
FRAME = element("ramka", "frame", 0, 0, width=300, height=200, name="Ramka")


def payload(scene_text, compressed=True):
    data = scene_text.encode("utf-8")
    encoded = zlib.compress(data, 6) if compressed else data
    wrapper = json.dumps({"version": "1", "encoding": "bstring", "compressed": compressed,
                          "encoded": encoded.decode("latin-1")},
                         ensure_ascii=False, separators=(",", ":"))
    return base64.b64encode(wrapper.encode("latin-1")).decode("ascii")


def svg(scene_text, compressed=True, width=240, height=230):
    return (f'<svg version="1.1" xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
            f'width="{width}" height="{height}"><!-- svg-source:excalidraw --><metadata>'
            '<!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->'
            f'<!-- payload-start -->{payload(scene_text, compressed)}<!-- payload-end --></metadata>'
            '<defs><style class="style-fonts">\n      </style></defs>'
            f'<rect x="0" y="0" width="{width}" height="{height}" fill="#ffffff"></rect>'
            '<g stroke-linecap="round" transform="translate(10 10) rotate(0 60 20)">'
            '<path d="M0,0 L120,40" stroke="#1e1e1e" stroke-width="2" fill="none"></path></g></svg>')


def write(name, text):
    with open(os.path.join(HERE, name), "w", encoding="utf-8", newline="") as f:
        f.write(text)


freedraw_scene = json.dumps(scene([FREEDRAW_PRESSURE, FREEDRAW_SIMULATED, FREEDRAW_DELETED]),
                            indent=2, ensure_ascii=False)
mixed_scene = json.dumps(scene([FREEDRAW_PRESSURE, RECTANGLE, TEXT]), indent=2, ensure_ascii=False)

write("excalidraw_freedraw.svg", svg(freedraw_scene))
write("excalidraw_freedraw_uncompressed.svg", svg(freedraw_scene, compressed=False))
write("excalidraw_mixed.svg", svg(mixed_scene))
write("excalidraw_mixed.excalidraw", mixed_scene)
shapes_scene = json.dumps(scene(SHAPES, SHAPE_FILES), indent=2, ensure_ascii=False)
write("excalidraw_shapes.svg", svg(shapes_scene))
write("excalidraw_shapes.excalidraw", shapes_scene)
write("excalidraw_frame.svg", svg(json.dumps(scene([RECTANGLE, FRAME]), indent=2, ensure_ascii=False)))

# The sync center's conflict of a drawing: what both versions have ("a" a line, "b" a box), what
# only this device drew ("c", blue) and what only the other one drew ("d", a red ellipse).
CONFLICT_A = element("a", "freedraw", 40, 60, width=200, height=40, points=wave(40, 5, 16), pressures=[],
                     simulatePressure=True, lastCommittedPoint=None, roughness=0)
CONFLICT_B = shape("b", "rectangle", 60, 120, 160, 80, roughness=0, roundness={"type": 3})
CONFLICT_C = element("c", "freedraw", 260, 80, width=120, height=30, strokeColor="#1971c2", strokeWidth=2,
                     points=wave(30, 4, 12), pressures=[], simulatePressure=True, lastCommittedPoint=None,
                     roughness=0)
CONFLICT_D = shape("d", "ellipse", 260, 150, 120, 70, strokeColor="#e03131", backgroundColor="#ffc9c9",
                   roughness=0)
write("conflict_ours.svg",
      svg(json.dumps(scene([CONFLICT_A, CONFLICT_B, CONFLICT_C]), indent=2, ensure_ascii=False), width=400,
          height=260))
write("conflict_theirs.svg",
      svg(json.dumps(scene([CONFLICT_A, CONFLICT_B, CONFLICT_D]), indent=2, ensure_ascii=False), width=400,
          height=260))


# ---- The Obsidian Excalidraw plugin's files (`*.excalidraw.md`) ----

PLUGIN_FRONTMATTER = (
    "---\n\nexcalidraw-plugin: parsed\ntags: [excalidraw]\n\n---\n"
    "==⚠  Switch to EXCALIDRAW VIEW in the MORE OPTIONS menu of this document. ⚠== You can decompress "
    "Drawing data with the command palette: 'Decompress current Excalidraw file'. For more info check "
    "in plugin settings under 'Saving'\n\n")
PLUGIN_SOURCE = "https://github.com/zsviczian/obsidian-excalidraw-plugin/releases/tag/2.2.5"


def plugin_scene(elements):
    elements = [dict(e, index="a" + BASE62[i]) for i, e in enumerate(elements)]
    return {
        "type": "excalidraw", "version": 2, "source": PLUGIN_SOURCE, "elements": elements,
        "appState": {"theme": "light", "viewBackgroundColor": "#ffffff", "currentItemStrokeColor": "#1e1e1e",
                     "gridSize": None, "scrollX": 0, "scrollY": 0, "zoom": {"value": 1}},
        "files": {},
    }


def plugin_compress(text):
    """sceneDataUtils.ts compress(): compressToBase64 in lines of 256 with an empty line between."""
    b64 = lz_base64(text)
    return "\n\n".join(b64[i:i + 256] for i in range(0, len(b64), 256))


def plugin_md(elements, note="", texts=(), links=(), embedded=(), compressed=True, commented=False):
    """FRONTMATTER + the back of the card + generateMDBase + getMarkdownDrawingSection."""
    md = PLUGIN_FRONTMATTER + note
    md += ("%%\n" if commented else "") + "# Excalidraw Data\n\n## Text Elements\n"
    md += "".join(f"{raw} ^{id_}\n\n" for id_, raw in texts)
    if links:
        md += "## Element Links\n" + "".join(f"{id_}: {link}\n\n" for id_, link in links)
    if embedded:
        md += "## Embedded Files\n" + "".join(f"{id_}: {link}\n\n" for id_, link in embedded)
    md += "" if commented else "%%\n"
    scene_text = json.dumps(plugin_scene(elements), indent="\t", ensure_ascii=False)
    if compressed:
        return md + "## Drawing\n```compressed-json\n" + plugin_compress(scene_text) + "\n```\n%%"
    return md + "## Drawing\n```json\n" + scene_text + "\n```\n%%"


def plugin_text(id_, x, y, value, **extra):
    e = text(id_, x, y, 200, 25 * (value.count("\n") + 1), value, rawText=value)
    e.update(extra)
    return e


NOTE = "Notatka z tyłu: Zażółć gęślą jaźń\n\n"
PLUGIN_RECT = shape("Rr5Kd9mq", "rectangle", 380, 200, 200, 120, roundness={"type": 3},
                    link="[[Plan ogrodu]]")
PLUGIN_TEXT = plugin_text("Zt8xQw1a", 400, 340, "Zażółć gęślą jaźń")
PLUGIN_TEXT2 = plugin_text("Dw2Lin3s", 400, 400, "Pierwsza linia\nDruga: ąćęłńóśźż")
PLUGIN_IMAGE = element("Im4ge0Ab", "image", 620, 200, width=80, height=60, fileId="a1b2c3d4e5f60718",
                       status="saved", scale=[1, 1])
PLUGIN_FRAME = element("Fr4me0Ab", "frame", 360, 180, width=400, height=300, name="Ramka")

write("obsidian_freedraw.excalidraw.md",
      plugin_md([FREEDRAW_PRESSURE, FREEDRAW_SIMULATED, FREEDRAW_DELETED], note=NOTE))
write("obsidian_mixed.excalidraw.md",
      plugin_md([FREEDRAW_PRESSURE, PLUGIN_RECT, PLUGIN_TEXT, PLUGIN_TEXT2, PLUGIN_IMAGE], note=NOTE,
                texts=[("Zt8xQw1a", "Zażółć gęślą jaźń"), ("Dw2Lin3s", "Pierwsza linia\nDruga: ąćęłńóśźż")],
                links=[("Rr5Kd9mq", "[[Plan ogrodu]]")],
                embedded=[("a1b2c3d4e5f60718", "[[Pasted Image 20261028143205.png]]")]))
write("obsidian_frame.excalidraw.md",
      plugin_md([PLUGIN_FRAME, FREEDRAW_PRESSURE, PLUGIN_TEXT], note=NOTE, texts=[("Zt8xQw1a", "Zażółć gęślą jaźń")]))
write("obsidian_json.excalidraw.md",
      plugin_md([FREEDRAW_PRESSURE, FREEDRAW_SIMULATED], note=NOTE, compressed=False))
write("obsidian_crlf.excalidraw.md",
      plugin_md([FREEDRAW_PRESSURE, FREEDRAW_SIMULATED, PLUGIN_TEXT], note=NOTE,
                texts=[("Zt8xQw1a", "Zażółć gęślą jaźń")]).replace("\n", "\r\n"))
write("obsidian_commented.excalidraw.md",
      plugin_md([FREEDRAW_PRESSURE, PLUGIN_TEXT], note=NOTE, texts=[("Zt8xQw1a", "Zażółć gęślą jaźń")],
                commented=True))
# The plugin's oldest form: the scene's JSON on one line under "# Drawing", no code block.
write("obsidian_legacy.excalidraw.md",
      "---\nexcalidraw-plugin: parsed\n---\n# Text Elements\n# Drawing\n"
      + json.dumps(plugin_scene([FREEDRAW_PRESSURE]), ensure_ascii=False, separators=(",", ":")) + "\n%%")
# excalidraw.com's "Save to disk".
write("excalidraw_freedraw.excalidraw",
      json.dumps(scene([FREEDRAW_PRESSURE, FREEDRAW_SIMULATED]), indent=2, ensure_ascii=False) + "\n")
