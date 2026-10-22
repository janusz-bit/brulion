#!/usr/bin/env python3
"""Regenerate Brulion's fallback fonts in assets/fonts/ (docs/UI.md "Emoji and symbol fonts").

Qt for WebAssembly draws text with the two fonts it bundles, DejaVu Sans and DejaVu Sans Mono,
and nothing else: every emoji (⏰ 🔔 📅 ✅ …) came out as an empty box ("tofu"). Brulion ships
two small subsets as fallbacks (src/app/ui/fallbackfonts.h installs them):

  BrulionEmoji.ttf  "Brulion Emoji": Noto Emoji (monochrome, wght 400) — every character of
                    src/ that DejaVu Sans lacks and Noto Emoji has, plus COMMON_EMOJI below.
  BrulionMath.otf   "Brulion Math": Noto Sans Math — the other characters of src/ that DejaVu
                    Sans lacks (⧉ of block embeds, rare LaTeX symbols of math.cpp).

Both fonts are under the SIL Open Font License 1.1 (assets/fonts/OFL.txt, no Reserved Font
Names); the subsets are renamed so they never pass for the complete fonts, and get DejaVu Sans's
vertical metrics so a fallback glyph never makes a line taller. The script also
writes assets/fonts/README.md (sources, SHA-256, the characters of each subset).

    scripts/fallback-fonts.py                 # fonts from nixpkgs pinned by flake.lock
    scripts/fallback-fonts.py --noto-emoji 'NotoEmoji[wght].ttf' --noto-math NotoSansMath-Regular.otf \\
        --dejavu DejaVuSans.ttf --ofl OFL.txt  # or explicit files

Needs Python 3 with fontTools; without it (and with nix) it re-runs itself in a nix shell with
the nixpkgs of flake.lock. Deterministic: the same inputs give byte-identical fonts.
"""

import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "assets" / "fonts"

# Emoji people commonly write in notes, journals and task lists (single code points only: no
# skin tones, flags or ZWJ sequences, which would add hundreds of glyphs). Brulion's own
# characters (⏰ 🔔 📅 ⏳ 🛫 ✅ ❌ 🔁 🔺 ⏫ …) come from src/ by themselves. About 100 KB in the
# browser download; keep additions moderate (docs/WEB.md, "Rozmiar").
COMMON_EMOJI = {
    "faces": "😀😃😄😁😆😅😂🤣🥲😊😇🙂🙃😉😌😍🥰😘😋😛😜🤪🤓😎🤩🥳😏😒😞😔😟😕🙁😣"
             "😖😫😩🥺😢😭😤😠😡🤯😳🥵🥶😱😨😰😓🤗🤔🤭🤫😶😐😑😬🙄😮😲🥱😴🤤😵🤐"
             "🥴🤢🤮🤧😷🤒🤕🤑🤠😈💩👻💀🤖🙈🙊",
    "hearts and marks": "❤🧡💛💚💙💜🖤🤍🤎💔💕💖💗💘💯💤💬💭💥✨",
    "hands and people": "👋✋👌✌🤞🤟🤙👈👉👆👇☝👍👎✊👊👏🙌🙏🤝✍💪👀🧠🤷🤦🙋👶👪",
    "notes and work": "⭐🌟🔥💡📌📍📎📝✏📄📋📁📂🗂🗓📆📈📉📊📓📚📖🔖🏷💼🗑🔍🔑🔒🔓🔗✂🧾📦✉"
                      "📧📞☎📱💻🖥⌨📷💰💳🛒🎁🎉🎊🎈🎂🏆🎯🚩🏁",
    "time and status": "⏰⏱⌚⌛⏳🔔🔕📣📢▶⏸⏹⏩⏪🔁🔂🔀🔄✅☑✔❌❎✖➕➖❗❓‼⁉⚠🚫⛔🆕🆗🆘"
                       "🆔🔴🟠🟡🟢🔵🟣⚫⚪🟥🟩🟦🟨⬛⬜🔺🔻🔸🔹🔶🔷⏫⏬🔼🔽⬆⬇⬅➡♻⚙🛠🔧🔨🧹",
    "places and travel": "🏠🏢🏫🏥🌍🚗🚌🚲🚆✈🛫🛬🚀🧳",
    "weather and nature": "☀⛅☁🌧⛈❄⛄☔🌈🌙⚡🌱🍀🌳🌸🌹🐶🐱",
    "food and drink": "☕🍵🍺🍷🍎🍓🍕🍔🍝🥗🍰🍽",
    "activities and health": "⚽🏃🚴🏊🧘🏋🎵🎶🎨🎬🎮🎓💊💉🩺🛏",
}

# Source files whose text Brulion writes into notes or shows (UI strings, the user guide).
SOURCE_SUFFIXES = {".cpp", ".h", ".qml", ".js", ".md"}

FONTS = [
    # (output file, family, PostScript name, source key, source family, source PostScript name)
    ("BrulionEmoji.ttf", "Brulion Emoji", "BrulionEmoji", "noto_emoji", "Noto Emoji", "NotoEmoji"),
    ("BrulionMath.otf", "Brulion Math", "BrulionMath", "noto_math", "Noto Sans Math", "NotoSansMath"),
]


def reexec_with_fonttools():
    """Runs this script again in a nix shell whose Python has fontTools (flake.lock's nixpkgs)."""
    if os.environ.get("BRULION_FALLBACK_FONTS_REEXEC"):
        sys.exit("fallback-fonts: fontTools is missing (pip install fonttools, or run in a nix shell)")
    if not shutil.which("nix"):
        sys.exit("fallback-fonts: needs Python with fontTools (pip install fonttools)")
    rev = nixpkgs_rev()
    expr = (f'(builtins.getFlake "github:NixOS/nixpkgs/{rev}").legacyPackages.${{builtins.currentSystem}}'
            '.python3.withPackages (p: [ p.fonttools ])')
    env = dict(os.environ, BRULION_FALLBACK_FONTS_REEXEC="1")
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


def default_inputs():
    emoji_src = nix_path("noto-fonts-monochrome-emoji.src") / "ofl" / "notoemoji"
    return {
        "noto_emoji": emoji_src / "NotoEmoji[wght].ttf",
        "ofl": emoji_src / "OFL.txt",
        "noto_math": nix_path("noto-fonts") / "share" / "fonts" / "noto" / "NotoSansMath-Regular.otf",
        "dejavu": (nix_path("dejavu_fonts") / "share" / "fonts" / "truetype" / "DejaVuSans.ttf").resolve(),
    }


def source_code_points():
    """Every code point >= U+00A0 in Brulion's sources, with the first file that has it."""
    found = {}
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix not in SOURCE_SUFFIXES or not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        for ch in text:
            cp = ord(ch)
            if cp >= 0xA0 and cp not in found:
                found[cp] = path.relative_to(ROOT).as_posix()
    return found


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def char_name(cp):
    try:
        return unicodedata.name(chr(cp))
    except ValueError:
        return "?"


def rename(font, family, ps_name, old_family, old_ps_name):
    """Gives the subset its own family ("Noto Emoji" -> "Brulion Emoji"); keeps copyright,
    trademark, licence and version records."""
    for record in font["name"].names:
        if record.nameID not in (1, 3, 4, 6, 16, 17, 21, 22, 25):
            continue
        text = record.toUnicode()
        text = text.replace(old_family, family).replace(old_ps_name, ps_name)
        record.string = text
    if "CFF " in font:
        cff = font["CFF "].cff
        cff.fontNames = [name.replace(old_ps_name, ps_name) for name in cff.fontNames]
        for top in cff.topDictIndex:
            for key in ("FullName", "FamilyName"):
                if hasattr(top, key):
                    setattr(top, key, getattr(top, key).replace(old_family, family))
    # A subset is a different font: add a description that says so.
    font["name"].setName(f"{family}: a subset of {old_family} for Brulion's fallback fonts "
                         "(https://github.com/janusz-bit/brulion).", 10, 3, 1, 0x409)


def match_metrics(font, reference):
    """Gives the subset the vertical metrics of `reference` (DejaVu Sans), scaled to its em: a
    fallback glyph then never makes a line taller (Noto Sans Math's are 1.5 times as tall), and
    no "fixed pitch" flag (Noto Emoji has one), which Qt would pass on to the fonts it tries
    after this one."""
    ref_em = reference["head"].unitsPerEm
    em = font["head"].unitsPerEm
    ascent = round(reference["hhea"].ascent * em / ref_em)
    descent = round(-reference["hhea"].descent * em / ref_em)
    font["hhea"].ascent, font["hhea"].descent, font["hhea"].lineGap = ascent, -descent, 0
    os2 = font["OS/2"]
    os2.sTypoAscender, os2.sTypoDescender, os2.sTypoLineGap = ascent, -descent, 0
    os2.usWinAscent, os2.usWinDescent = ascent, descent
    os2.fsSelection |= 1 << 7  # USE_TYPO_METRICS: the typo metrics above are the ones to use
    font["post"].isFixedPitch = 0
    os2.panose.bProportion = 0


def build_font(src, out, code_points, family, ps_name, old_family, old_ps_name, reference):
    from fontTools import subset
    from fontTools.ttLib import TTFont
    from fontTools.varLib import instancer

    font = TTFont(src, recalcTimestamp=False)
    if "fvar" in font:
        # Static Regular instance: smaller, and Qt needs no variation support for it.
        font = instancer.instantiateVariableFont(font, {"wght": 400}, updateFontNames=False)
        buf = io.BytesIO()
        font.save(buf)
        buf.seek(0)
        font = TTFont(buf, recalcTimestamp=False)
    options = subset.Options()
    options.layout_features = ["*"]
    options.name_IDs = ["*"]
    options.name_languages = ["*"]
    options.notdef_outline = True
    options.recalc_timestamp = False
    options.drop_tables += ["DSIG"]
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=sorted(code_points))
    subsetter.subset(font)
    rename(font, family, ps_name, old_family, old_ps_name)
    match_metrics(font, reference)
    font.save(out, reorderTables=True)
    return sorted(TTFont(out).getBestCmap())


def main():
    try:
        import fontTools  # noqa: F401
    except ImportError:
        reexec_with_fonttools()
    from fontTools.ttLib import TTFont

    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--noto-emoji", help="NotoEmoji[wght].ttf (google/fonts ofl/notoemoji)")
    parser.add_argument("--noto-math", help="NotoSansMath-Regular.otf (notofonts/math)")
    parser.add_argument("--dejavu", help="DejaVuSans.ttf 2.37, the font of Qt for WebAssembly")
    parser.add_argument("--ofl", help="OFL.txt of Noto Emoji (the licence text)")
    args = parser.parse_args()
    given = {k: getattr(args, k) for k in ("noto_emoji", "noto_math", "dejavu", "ofl")}
    inputs = default_inputs() if not all(given.values()) else {}
    inputs.update({k: Path(v) for k, v in given.items() if v})

    reference = TTFont(inputs["dejavu"])
    dejavu = set(reference.getBestCmap())
    sources = source_code_points()
    missing = {cp for cp in sources if cp not in dejavu and not unicodedata.category(chr(cp)).startswith(("Z", "C", "M"))}
    common = {ord(ch) for group in COMMON_EMOJI.values() for ch in group}

    emoji_cmap = set(TTFont(inputs["noto_emoji"]).getBestCmap())
    math_cmap = set(TTFont(inputs["noto_math"]).getBestCmap())
    wanted = {
        "BrulionEmoji.ttf": (missing & emoji_cmap) | (common & emoji_cmap),
        "BrulionMath.otf": (missing & math_cmap) - emoji_cmap,
    }
    not_in_emoji = common - emoji_cmap
    if not_in_emoji:
        sys.exit("fallback-fonts: COMMON_EMOJI has characters Noto Emoji lacks: "
                 + " ".join(f"U+{cp:04X}" for cp in sorted(not_in_emoji)))
    uncovered = sorted(missing - wanted["BrulionEmoji.ttf"] - wanted["BrulionMath.otf"])

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    built = {}
    for out_name, family, ps_name, key, old_family, old_ps_name in FONTS:
        out = OUT_DIR / out_name
        cmap = build_font(inputs[key], out, wanted[out_name], family, ps_name, old_family, old_ps_name, reference)
        built[out_name] = cmap
        print(f"{out_name}: {len(cmap)} characters, {out.stat().st_size} bytes")

    # The licence: Noto Emoji's OFL.txt with Noto Sans Math's copyright line added.
    ofl = Path(inputs["ofl"]).read_text(encoding="utf-8")
    math_copyright = TTFont(inputs["noto_math"])["name"].getDebugName(0)
    first, rest = ofl.split("\n", 1)
    (OUT_DIR / "OFL.txt").write_text(f"{first} (Noto Emoji)\n{math_copyright} (Noto Sans Math)\n{rest}",
                                     encoding="utf-8")

    def font_version(path):
        return TTFont(path)["name"].getDebugName(5)

    def listing(cps):
        return "".join(chr(cp) for cp in cps)

    own = sorted(missing & set(built["BrulionEmoji.ttf"]))
    readme = f"""# Fallback fonts

Generated by `scripts/fallback-fonts.py` — do not edit; re-run the script instead (see
docs/UI.md, "Emoji and symbol fonts"). Qt for WebAssembly only has DejaVu Sans and DejaVu Sans
Mono, so the browser build drew every emoji as an empty box; these subsets fill the gaps there,
and on Linux systems whose fonts lack them (src/app/ui/fallbackfonts.h). Android never uses them.

Licence: SIL Open Font License 1.1, see [OFL.txt](OFL.txt). The subsets carry their own family
names, so they are never mistaken for the complete fonts, and the vertical metrics of DejaVu
Sans, so a fallback glyph never makes a line taller.

| File | Family | Source | Source version | Source SHA-256 |
|---|---|---|---|---|
| BrulionEmoji.ttf | Brulion Emoji | Noto Emoji (google/fonts `ofl/notoemoji/NotoEmoji[wght].ttf`), instance wght 400 | {font_version(inputs["noto_emoji"])} | `{sha256(inputs["noto_emoji"])}` |
| BrulionMath.otf | Brulion Math | Noto Sans Math (notofonts `NotoSansMath-Regular.otf`) | {font_version(inputs["noto_math"])} | `{sha256(inputs["noto_math"])}` |

Coverage was computed against DejaVu Sans {font_version(inputs["dejavu"])} (`{sha256(inputs["dejavu"])}`).

## Brulion Emoji ({len(built["BrulionEmoji.ttf"])} characters)

Brulion's own (from src/): {listing(own)}

All: {listing(built["BrulionEmoji.ttf"])}

## Brulion Math ({len(built["BrulionMath.otf"])} characters)

{listing(built["BrulionMath.otf"])}
"""
    if uncovered:
        readme += "\n## Not covered\n\nCharacters of src/ that neither DejaVu Sans nor these fonts have:\n\n"
        readme += "".join(f"* U+{cp:04X} {chr(cp)} {char_name(cp)} ({sources[cp]})\n" for cp in uncovered)
        print("not covered: " + ", ".join(f"U+{cp:04X} {char_name(cp)} ({sources[cp]})" for cp in uncovered))
    (OUT_DIR / "README.md").write_text(readme, encoding="utf-8")


if __name__ == "__main__":
    main()
