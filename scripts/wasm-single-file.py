#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Turns the built browser version (dist-wasm/, scripts/wasm-build.sh) into one HTML file.

The page opened from disk (file://), without a web server: the release's
brulion-web-<version>.html (docs/WEB.md, "Jeden plik HTML").

  scripts/wasm-single-file.py [DIST_DIR] [OUTPUT]
      DIST_DIR  the assembled site [dist-wasm]
      OUTPUT    the file to write [dist-wasm-single/brulion.html]

What changes against the site's index.html (wasm/index.html knows the single-file version):
  * brulion.wasm goes into <script id="brulion-wasm" type="application/octet-stream">,
    gzip-compressed (level 9, no time stamp: the same input gives the same file) in base64; the
    page decompresses it as a stream (DecompressionStream);
  * qtloader.js and brulion.js become scripts of the page, before the page's own (in place of
    the "brulion-single-file" comment); "</script" in them is written "<\\/script";
  * the pictures under icons/ become data: URIs, the web app manifest's <link> goes (file:// has
    no installable app, and no service worker either: the page registers none).
Any other reference to a file of the site fails the script, so nothing is left to fetch.
Needs only Python 3 (standard library).
"""
import base64
import gzip
import re
import sys
from pathlib import Path

MARKER = "<!-- brulion-single-file: the app's scripts (scripts/wasm-single-file.py) -->"
MIME = {".svg": "image/svg+xml", ".png": "image/png"}


def fail(message):
    print(f"wasm-single-file: error: {message}", file=sys.stderr)
    sys.exit(1)


def inline_script(text, name):
    """The script's text safe inside <script>…</script>."""
    # "<!--" inside a script element changes how HTML finds its end ("script data escaped").
    if "<!--" in text:
        fail(f"{name} contains '<!--', which cannot be inlined safely")
    # "</script" ends the element wherever it is; in JavaScript "<\/" means "</" in strings,
    # template literals, regular expressions and comments (the only places it can occur).
    return re.sub(r"</(script)", r"<\\/\1", text, flags=re.IGNORECASE)


def data_uri(path):
    mime = MIME.get(path.suffix.lower())
    if not mime:
        fail(f"no MIME type for {path.name}")
    return f"data:{mime};base64," + base64.b64encode(path.read_bytes()).decode("ascii")


def main(argv):
    if any(a in ("-h", "--help") for a in argv):
        print(__doc__.strip())
        return 0
    if len(argv) > 2:
        fail("usage: wasm-single-file.py [DIST_DIR] [OUTPUT]")
    dist = Path(argv[0] if argv else "dist-wasm")
    out = Path(argv[1] if len(argv) > 1 else "dist-wasm-single/brulion.html")

    for name in ("index.html", "brulion.wasm", "brulion.js", "qtloader.js"):
        if not (dist / name).is_file():
            fail(f"{dist / name} missing (scripts/wasm-build.sh first)")
    page = (dist / "index.html").read_text(encoding="utf-8")
    if re.search(r"@BRULION_[A-Z_]+@", page):
        fail(f"{dist / 'index.html'} has unreplaced placeholders (use the assembled site, not wasm/)")
    if page.count(MARKER) != 1:
        fail(f"{dist / 'index.html'} must contain the marker exactly once: {MARKER}")

    # The web app manifest: no installable app from file://.
    page, manifests = re.subn(r'[ \t]*<link rel="manifest"[^>]*>\n?', "", page)
    if manifests != 1:
        fail(f"expected one manifest <link>, found {manifests}")

    # Pictures of the page (icons/…) as data: URIs.
    def picture(match):
        path = dist / match.group(2)
        if not path.is_file():
            fail(f"{path} missing (referenced by index.html)")
        return f'{match.group(1)}="{data_uri(path)}"'

    page = re.sub(r'\b(href|src)="(icons/[^"?#]+)"', picture, page)

    # Nothing else of the site may be referenced from markup (the shell's own fetches are skipped
    # in the single-file version: wasm/index.html).
    for attr, ref in re.findall(r'\b(href|src)="([^"]*)"', page):
        if not re.match(r"(data:|https://|#)", ref):
            fail(f'index.html still references {attr}="{ref}"')

    wasm = (dist / "brulion.wasm").read_bytes()
    packed = base64.b64encode(gzip.compress(wasm, compresslevel=9, mtime=0)).decode("ascii")
    qtloader = inline_script((dist / "qtloader.js").read_text(encoding="utf-8"), "qtloader.js")
    app = inline_script((dist / "brulion.js").read_text(encoding="utf-8"), "brulion.js")
    scripts = (
        '<script id="brulion-wasm" type="application/octet-stream">' + packed + "</script>\n"
        "    <script>\n" + qtloader + "\n    </script>\n"
        "    <script>\n" + app + "\n    </script>"
    )
    page = page.replace(MARKER, scripts)

    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(page, encoding="utf-8")
    size = out.stat().st_size
    print(f"{out}  {size / 1048576:.2f} MiB (brulion.wasm {len(wasm) / 1048576:.2f} MiB, "
          f"packed {len(packed) / 1048576:.2f} MiB in base64)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
