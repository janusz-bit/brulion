#!/usr/bin/env python3
"""Writes the Excalidraw fixtures of tst_drawing the way excalidraw.com writes them, re-implemented
independently of Brulion's C++ (packages/excalidraw/data/encode.ts and scene/export.ts:
JSON.stringify(scene, null, 2) → pako.deflate → a byte string in {"version":"1","encoding":
"bstring","compressed":true,"encoded":…} → JSON.stringify → btoa → the SVG's payload comments).

excalidraw_shapes.* hold every kind of element Brulion draws; excalidraw_frame.svg a frame (an
element Brulion does not edit: the drawing is shown read-only).

smiley_embedded_v2.svg and test_embedded_v1.svg are Excalidraw's own test fixtures
(excalidraw/excalidraw, packages/excalidraw/tests/fixtures, MIT licence), unchanged.

    python3 tests/data/drawing/make-fixtures.py   # rewrites the files below next to this script
"""
import base64
import json
import os
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))


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
