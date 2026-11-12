#!/usr/bin/env python3
"""Regenerate Brulion's bundled text fonts in assets/fonts/ui/ (docs/UI.md "Look & feel", "Fonts").

The "Nocny" look sets its text in three open fonts, bundled so that every platform — the
browser, whose Qt has only DejaVu, and Android included — shows the same letters:

  Brulion Inter           the interface and note text, the default "sans" editor font (Inter 4,
                          opsz 14; Regular, Medium, SemiBold, Bold, Italic, Bold Italic)
  Brulion Literata        note text in the "serif" editor font (Literata 3, Regular, Italic, Bold,
                          Bold Italic)
  Brulion JetBrains Mono  code, dates and other meta text (JetBrains Mono 2, Regular, Bold)

Each file is a static instance subset to the Latin alphabets of Europe (Polish, of course) and
the Greek letters, with their punctuation, arrows and common symbols — Cyrillic, Vietnamese and
emoji come from the system's fonts (or "Brulion Emoji") —, without hinting, stylistic sets,
character variants and default ligatures / contextual alternates (they would take text out of
PDFs Qt writes with these fonts). They are renamed ("Inter" → "Brulion
Inter"), so a complete font of the system with the same name never gets mixed up with a subset
(the font pickers leave the "Brulion …" families out, DeviceInfo / Theme use them as the defaults).
All three are under the SIL Open Font License 1.1 without Reserved Font Names
(assets/fonts/ui/OFL-*.txt); the script also writes assets/fonts/ui/README.md.

    scripts/ui-fonts.py          # fonts from the nixpkgs pinned by flake.lock

Needs Python 3 with fontTools; without it (and with nix) it re-runs itself in a nix shell with
the nixpkgs of flake.lock. Deterministic: the same inputs give byte-identical fonts.
"""

import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "fonts" / "ui"

RANGES = [
    (0x0020, 0x007E), (0x00A0, 0x024F),  # Basic Latin, Latin-1, Latin Extended-A/B (ąćęłńóśźż)
    (0x0259, 0x0259), (0x02B0, 0x02FF),  # schwa, spacing modifiers
    (0x0300, 0x036F),                    # combining diacritics
    (0x0391, 0x03C9),                    # the Greek letters (formulas, units: α β µ Ω)
    (0x2000, 0x206F), (0x2070, 0x209F),  # punctuation, super/subscripts
    (0x20A0, 0x20CF), (0x2100, 0x218F),  # currencies, letterlike, number forms (½ ⅓ Ⅻ)
    (0x2190, 0x21FF), (0x2200, 0x22FF),  # arrows, mathematical operators
    (0x2300, 0x23FF), (0x2460, 0x24FF),  # technical (⌘ ⌥ ⏎), enclosed numbers
    (0x2500, 0x25FF),                    # box drawing, blocks, geometric shapes (code, tables)
    (0x2713, 0x2718), (0x2794, 0x27BF),  # ✓ ✗, heavy arrows
    (0x27E8, 0x27E9), (0x2E00, 0x2E7F),  # ⟨ ⟩, supplemental punctuation
    (0xFB00, 0xFB06), (0xFEFF, 0xFEFF), (0xFFFD, 0xFFFD),
]

# (output, family, source key, axes / static file, style name, weight class, italic)
FONTS = [
    ("BrulionInter-Regular.ttf", "Brulion Inter", "inter", {"wght": 400, "opsz": 14}, "Regular", 400, False),
    ("BrulionInter-Medium.ttf", "Brulion Inter", "inter", {"wght": 500, "opsz": 14}, "Medium", 500, False),
    ("BrulionInter-SemiBold.ttf", "Brulion Inter", "inter", {"wght": 600, "opsz": 14}, "SemiBold", 600, False),
    ("BrulionInter-Bold.ttf", "Brulion Inter", "inter", {"wght": 700, "opsz": 14}, "Bold", 700, False),
    ("BrulionInter-Italic.ttf", "Brulion Inter", "inter_italic", {"wght": 400, "opsz": 14}, "Italic", 400, True),
    ("BrulionInter-BoldItalic.ttf", "Brulion Inter", "inter_italic", {"wght": 700, "opsz": 14}, "Bold Italic", 700, True),
    ("BrulionLiterata-Regular.ttf", "Brulion Literata", "literata", "Literata-Regular.ttf", "Regular", 400, False),
    ("BrulionLiterata-Italic.ttf", "Brulion Literata", "literata", "Literata-Italic.ttf", "Italic", 400, True),
    ("BrulionLiterata-Bold.ttf", "Brulion Literata", "literata", "Literata-Bold.ttf", "Bold", 700, False),
    ("BrulionLiterata-BoldItalic.ttf", "Brulion Literata", "literata", "Literata-BoldItalic.ttf", "Bold Italic", 700, True),
    ("BrulionJetBrainsMono-Regular.ttf", "Brulion JetBrains Mono", "jetbrains", "JetBrainsMono-Regular.ttf", "Regular", 400, False),
    ("BrulionJetBrainsMono-Bold.ttf", "Brulion JetBrains Mono", "jetbrains", "JetBrainsMono-Bold.ttf", "Bold", 700, False),
]

SOURCES = {
    "inter": ("Inter", "inter", "share/fonts/truetype"),
    "literata": ("Literata", "literata", "share/fonts/truetype"),
    "jetbrains": ("JetBrains Mono", "jetbrains-mono", "share/fonts/truetype"),
}


def reexec_with_fonttools():
    if os.environ.get("BRULION_UI_FONTS_REEXEC"):
        sys.exit("ui-fonts: fontTools is missing (pip install fonttools, or run in a nix shell)")
    if not shutil.which("nix"):
        sys.exit("ui-fonts: needs Python with fontTools (pip install fonttools)")
    expr = (f'(builtins.getFlake "github:NixOS/nixpkgs/{nixpkgs_rev()}").legacyPackages.${{builtins.currentSystem}}'
            '.python3.withPackages (p: [ p.fonttools ])')
    env = dict(os.environ, BRULION_UI_FONTS_REEXEC="1")
    cmd = ["nix", "shell", "--impure", "--expr", expr, "--command", "python3", __file__, *sys.argv[1:]]
    sys.exit(subprocess.call(cmd, env=env))


def nixpkgs_rev():
    lock = json.loads((ROOT / "flake.lock").read_text(encoding="utf-8"))
    return lock["nodes"]["nixpkgs"]["locked"]["rev"]


def nix_path(attr):
    out = subprocess.check_output(
        ["nix", "build", "--no-link", "--print-out-paths", f"github:NixOS/nixpkgs/{nixpkgs_rev()}#{attr}"],
        text=True)
    return Path(out.strip().splitlines()[-1])


def source_dirs():
    return {key: nix_path(attr) / sub for key, (_, attr, sub) in SOURCES.items()}


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def code_points():
    out = set()
    for lo, hi in RANGES:
        out.update(range(lo, hi + 1))
    return out


def rename(font, family, style, weight, italic, old_family):
    """The subset's own family and style names; copyright, licence and version stay."""
    from fontTools.ttLib.tables._n_a_m_e import NameRecord  # noqa: F401
    name = font["name"]
    full = f"{family} {style}" if style != "Regular" else family
    ps = (family + "-" + style).replace(" ", "")
    # Drop the typographic family/subfamily and variation records of the source: one family with
    # the four classic styles (Regular, Italic, Bold, Bold Italic) or a weight of its own.
    name.names = [r for r in name.names if r.nameID not in (1, 2, 3, 4, 6, 16, 17, 21, 22, 25)
                  and r.nameID < 256]
    # One family for every weight (Medium and SemiBold too): Qt then picks the file by the weight
    # asked for (OS/2 usWeightClass below), and lists one family per font, not "Brulion Inter
    # Medium" beside "Brulion Inter".
    for plat, enc, lang in ((3, 1, 0x409), (1, 0, 0)):
        name.setName(family, 1, plat, enc, lang)
        name.setName(style, 2, plat, enc, lang)
        name.setName(f"{ps};Brulion", 3, plat, enc, lang)
        name.setName(full, 4, plat, enc, lang)
        name.setName(ps, 6, plat, enc, lang)
    name.setName(f"{family}: a subset of {old_family} for Brulion (https://github.com/janusz-bit/brulion).",
                 10, 3, 1, 0x409)
    os2 = font["OS/2"]
    os2.usWeightClass = weight
    # fsSelection: italic (bit 0), bold (bit 5), regular (bit 6); macStyle the same.
    sel = os2.fsSelection & ~((1 << 0) | (1 << 5) | (1 << 6))
    if italic:
        sel |= 1 << 0
    if weight >= 700:
        sel |= 1 << 5
    if not italic and weight < 700:
        sel |= 1 << 6
    os2.fsSelection = sel
    font["head"].macStyle = (1 if weight >= 700 else 0) | (2 if italic else 0)
    for table in ("STAT", "fvar", "gvar", "avar", "HVAR", "MVAR", "cvar"):
        if table in font:
            del font[table]


def build(src, axes, out, family, style, weight, italic, old_family):
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    font = TTFont(src, recalcTimestamp=False)
    if isinstance(axes, dict) and "fvar" in font:
        font = instancer.instantiateVariableFont(font, axes, updateFontNames=False)
        buf = io.BytesIO()
        font.save(buf)
        buf.seek(0)
        font = TTFont(buf, recalcTimestamp=False)
    options = subset.Options()
    # Kerning, marks, figures, fractions, case forms — no stylistic sets and character variants
    # (Inter has dozens: hundreds of glyphs nobody asks for), and none of the substitutions text
    # gets by default (liga, clig, calt): a glyph they put in has no character of its own, and
    # Qt's PDF writer maps glyphs back to text through the cmap only — Inter's contextual em dash
    # came out of a PDF as nothing (copying, searching, the PDF viewer's selection).
    options.layout_features = ["kern", "mark", "mkmk", "ccmp", "locl", "rlig",
                               "tnum", "pnum", "lnum", "onum", "case", "frac", "numr", "dnom",
                               "sups", "subs", "zero"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.recalc_timestamp = False
    options.hinting = False
    options.desubroutinize = True
    options.drop_tables += ["DSIG", "STAT"]
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=sorted(code_points()))
    subsetter.subset(font)
    rename(font, family, style, weight, italic, old_family)
    font.save(out, reorderTables=True)
    return len(TTFont(out).getBestCmap())


def main():
    try:
        import fontTools  # noqa: F401
    except ImportError:
        reexec_with_fonttools()
    dirs = source_dirs()
    files = {
        "inter": dirs["inter"] / "InterVariable.ttf",
        "inter_italic": dirs["inter"] / "InterVariable-Italic.ttf",
    }
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    rows = []
    used = {}
    for out_name, family, key, axes, style, weight, italic in FONTS:
        src_key = "inter" if key.startswith("inter") else key
        src = files[key] if key in files else dirs[src_key] / axes
        old_family = SOURCES[src_key][0]
        out = OUT_DIR / out_name
        count = build(src, axes if isinstance(axes, dict) else None, out, family, style, weight, italic, old_family)
        size = out.stat().st_size
        print(f"{out_name}: {count} characters, {size} bytes")
        used[src] = sha256(src)
        rows.append(f"| {out_name} | {family} | {style} | {Path(src).name}"
                    f"{' ' + str(axes) if isinstance(axes, dict) else ''} | {count} | {size} |")

    # The licences: from the packages' sources (the font outputs carry none).
    for key, (fam, attr, _) in SOURCES.items():
        src_dir = nix_path(f"{attr}.src")
        lic = next((src_dir / n for n in ("OFL.txt", "LICENSE.txt") if (src_dir / n).is_file()), None)
        if not lic:
            sys.exit(f"ui-fonts: no licence file in {src_dir}")
        shutil.copyfile(lic, OUT_DIR / f"OFL-{fam.replace(' ', '')}.txt")

    sources = "\n".join(f"* `{Path(p).name}` — SHA-256 `{h}`" for p, h in sorted(used.items(), key=lambda x: str(x[0])))
    readme = f"""# Bundled text fonts

Generated by `scripts/ui-fonts.py` — do not edit; re-run the script instead (docs/UI.md, "Look &
feel"). The "Nocny" look's fonts, on every platform: **Brulion Inter** (the interface and, by
default, note text, from Inter), **Brulion Literata** (the serif editor font, from Literata), **Brulion JetBrains
Mono** (code, dates and meta text, from JetBrains Mono). Static instances, subset to the Latin
alphabets of Europe and the Greek letters with their punctuation, arrows and common symbols
(other scripts come from the system's fonts), without hinting or stylistic sets, renamed
so they are never mistaken for the complete fonts (src/app/ui/uifonts.h loads them).

Licence: SIL Open Font License 1.1, no Reserved Font Names — [Inter](OFL-Inter.txt),
[Literata](OFL-Literata.txt), [JetBrains Mono](OFL-JetBrainsMono.txt).

| File | Family | Style | Source | Characters | Bytes |
|---|---|---|---|---|---|
{chr(10).join(rows)}

Sources (nixpkgs of flake.lock, `{nixpkgs_rev()}`):

{sources}
"""
    (OUT_DIR / "README.md").write_text(readme, encoding="utf-8")


if __name__ == "__main__":
    main()
