#!/usr/bin/env python3
"""A notebook full of photos for the app's memory benchmarks (tests/bench/app/README.md, scenario
`pictures`).

  tests/bench/app/make-picture-vault.py <dir> [--photos 60] [--today 2026-10-14] [--seed 7]
                                              [--width 4000] [--height 3000]

Needs ImageMagick's `magick` on PATH (`nix shell nixpkgs#imagemagick -c …`). Writes:
  * assets/zdjęcie-NNN.jpg — `--photos` camera-like JPEGs (12 MP by default; every fifth one
    upright, every seventh one turned by its EXIF orientation), plasma clouds with noise so that
    they compress as photos do (2–4 MB each);
  * assets/zrzut-NN.png — ten phone screenshots (1080 × 2400, flat areas and text-like bars);
  * Album.md — a document with 40 photos, a caption under each;
  * Zdjęcia/Strona NN.md — 30 pages of four photos each among paragraphs;
  * journals/YYYY-MM-DD.md — 20 days with two photos each, 10 days of text;
  * Tekst/Notatka NN.md — 30 pages without a picture (page switching between the picture pages);
  * <dir>.pictures.txt, <dir>.pages.txt, <dir>.album.txt — what the scenario opens.
Deterministic for a seed (ImageMagick's `-seed`). Prints a JSON summary.
"""
import argparse
import datetime as dt
import json
import os
import random
import shutil
import subprocess
import sys

WORDS = (
    "rano spacer nad rzeką mgła nad łąką zażółć gęślą jaźń stary most w Krakowie światło "
    "po deszczu targ z jabłkami śnieg na dachu wieczór przy ognisku pociąg do Gdańska "
    "żaglówka na jeziorze góry w chmurach kot na parapecie kawa i książka źródło w lesie"
).split()


def sentence(rng, lo=6, hi=18):
    words = [rng.choice(WORDS) for _ in range(rng.randint(lo, hi))]
    text = " ".join(words)
    return text[0].upper() + text[1:] + "."


def write(path, lines):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = "\n".join(lines) + "\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(data)
    return len(data.encode("utf-8"))


def magick(args):
    subprocess.run(["magick"] + args, check=True)


def make_photo(path, width, height, seed, orientation):
    # Clouds drawn small and scaled up, then sensor-like noise: a file of a few megabytes, as a
    # camera writes it (a flat picture would compress to nothing and decode faster than a photo).
    args = ["-seed", str(seed), "-size", "%dx%d" % (max(1, width // 8), max(1, height // 8)), "plasma:fractal",
            "-resize", "%dx%d!" % (width, height), "-seed", str(seed + 1), "-attenuate", "0.35", "+noise", "Gaussian",
            "-sampling-factor", "4:2:0", "-quality", "88"]
    magick(args + [path])
    if orientation:
        set_exif_orientation(path, orientation)


def set_exif_orientation(path, orientation):
    """An Exif segment that holds the orientation alone, right after the JPEG's start (ImageMagick
    writes none for a picture it made itself, whatever -orient says)."""
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"\xff\xd8":
        sys.exit("make-picture-vault: %s is not a JPEG" % path)
    # TIFF, little-endian: one directory of one entry — tag 0x0112 (Orientation), SHORT, 1 value.
    tiff = b"II*\x00" + (8).to_bytes(4, "little") + (1).to_bytes(2, "little")
    tiff += (0x0112).to_bytes(2, "little") + (3).to_bytes(2, "little") + (1).to_bytes(4, "little")
    tiff += orientation.to_bytes(2, "little") + b"\x00\x00" + (0).to_bytes(4, "little")
    body = b"Exif\x00\x00" + tiff
    segment = b"\xff\xe1" + (len(body) + 2).to_bytes(2, "big") + body
    with open(path, "wb") as f:
        f.write(data[:2] + segment + data[2:])


def make_screenshot(path, seed):
    rng = random.Random(seed)
    args = ["-size", "1080x2400", "xc:#%02x%02x%02x" % (rng.randint(235, 255), rng.randint(235, 255), rng.randint(235, 255))]
    y = 120
    while y < 2300:
        h = rng.choice([28, 28, 36, 64])
        w = rng.randint(200, 980)
        args += ["-fill", "#%02x%02x%02x" % (rng.randint(20, 90), rng.randint(20, 90), rng.randint(20, 110)),
                 "-draw", "rectangle 48,%d %d,%d" % (y, 48 + w, y + h)]
        y += h + rng.choice([20, 28, 60])
    magick(args + ["-depth", "8", path])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--photos", type=int, default=60)
    ap.add_argument("--today", default="2026-10-14")
    ap.add_argument("--seed", type=int, default=7)
    ap.add_argument("--width", type=int, default=4000)
    ap.add_argument("--height", type=int, default=3000)
    a = ap.parse_args()
    if not shutil.which("magick"):
        sys.exit("make-picture-vault: ImageMagick's `magick` not found (nix shell nixpkgs#imagemagick -c …)")
    if a.photos < 8:
        sys.exit("make-picture-vault: at least 8 photos")
    root = os.path.abspath(a.dir)
    if os.path.exists(root) and os.listdir(root):
        sys.exit("make-picture-vault: %s exists and is not empty" % root)
    rng = random.Random(a.seed)
    today = dt.date.fromisoformat(a.today)
    os.makedirs(os.path.join(root, "assets"), exist_ok=True)

    photos, picture_bytes = [], 0
    for i in range(a.photos):
        name = "zdjęcie-%03d.jpg" % (i + 1)
        upright = i % 5 == 4
        # (Orientation 6: the sensor's landscape pixels, shown upright — a phone held upright.)
        orientation = 6 if i % 7 == 6 else 0
        w, h = (a.height, a.width) if upright else (a.width, a.height)
        path = os.path.join(root, "assets", name)
        make_photo(path, w, h, a.seed * 1000 + i * 2, orientation)
        picture_bytes += os.path.getsize(path)
        photos.append("assets/" + name)
    shots = []
    for i in range(10):
        name = "zrzut-%02d.png" % (i + 1)
        path = os.path.join(root, "assets", name)
        make_screenshot(path, a.seed * 100 + i)
        picture_bytes += os.path.getsize(path)
        shots.append("assets/" + name)

    note_bytes = 0
    # The album: a document, a photo and its caption per paragraph.
    album = ["---", "brulion: document", "---", "", "# Album", "", sentence(rng), ""]
    for i in range(min(40, len(photos))):
        album += ["![[%s]]" % os.path.basename(photos[i]), "", sentence(rng, 4, 10), ""]
    note_bytes += write(os.path.join(root, "Album.md"), album)

    pages = []
    for n in range(30):
        title = "Strona %02d" % (n + 1)
        lines = ["- " + sentence(rng)]
        for k in range(4):
            photo = photos[(n * 4 + k) % len(photos)]
            lines += ["- " + sentence(rng), "- ![[%s]]" % os.path.basename(photo)]
        if n % 3 == 0:
            lines += ["- ![[%s]]" % os.path.basename(shots[n % len(shots)])]
        lines += ["- " + sentence(rng) + " [[Album]]"]
        note_bytes += write(os.path.join(root, "Zdjęcia", title + ".md"), lines)
        pages.append("Zdjęcia/" + title + ".md")

    texts = []
    for n in range(30):
        title = "Notatka %02d" % (n + 1)
        lines = ["- " + sentence(rng) for _ in range(rng.randint(6, 30))]
        lines.append("- [[Strona %02d]] #zdjęcia" % (n + 1))
        note_bytes += write(os.path.join(root, "Tekst", title + ".md"), lines)
        texts.append("Tekst/" + title + ".md")

    days = 0
    for back in range(30):
        day = today - dt.timedelta(days=back)
        lines = ["- " + sentence(rng) for _ in range(rng.randint(2, 6))]
        if back % 3 != 2:
            for k in range(2):
                lines.append("- ![[%s]]" % os.path.basename(photos[(back * 2 + k) % len(photos)]))
            lines.append("- " + sentence(rng))
        note_bytes += write(os.path.join(root, "journals", day.isoformat() + ".md"), lines)
        days += 1

    # What the scenario opens: every picture in turn, picture pages and text pages alternating.
    mixed = [p for pair in zip(pages, texts) for p in pair]
    for suffix, rows in ((".pictures.txt", photos + shots), (".pages.txt", mixed), (".album.txt", ["Album.md"])):
        with open(root + suffix, "w", encoding="utf-8") as f:
            f.write("\n".join(rows) + "\n")
    print(json.dumps({"photos": len(photos), "screenshots": len(shots), "pictureBytes": picture_bytes,
                      "pages": len(pages), "textPages": len(texts), "journals": days, "noteBytes": note_bytes},
                     ensure_ascii=False))


if __name__ == "__main__":
    main()
