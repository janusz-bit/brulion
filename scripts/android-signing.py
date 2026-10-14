#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Release signing for the generated Tauri Android project (src-tauri/gen/android).

`tauri android init` regenerates src-tauri/gen/android (it is not committed), so the release
signing setup from the Tauri v2 docs (https://v2.tauri.app/distribute/sign/android/) is applied
by this script after every init. Subcommands:

  patch [--gradle FILE]   Add a "release" signingConfig that reads keystore.properties from the
                          Gradle root (src-tauri/gen/android/keystore.properties) to
                          app/build.gradle.kts and use it for the release build type.
                          Idempotent: an already patched file is left untouched.
                          This is the default when no subcommand is given.
  check [--gradle FILE]   Exit 0 if the file is patched (and still well-formed), 1 if it is not
                          patched, 2 if it looks broken.
  properties --store-file KEYSTORE --key-alias ALIAS [--out FILE]
                          Write keystore.properties (mode 600) with correct .properties escaping.
                          Passwords come from the environment, never from the command line:
                          ANDROID_KEYSTORE_PASSWORD (required) and ANDROID_KEY_PASSWORD
                          (defaults to the store password, as PKCS12 keystores require).

keystore.properties keys: storeFile, storePassword, keyAlias, keyPassword. A relative storeFile is
resolved against the directory of keystore.properties. Without keystore.properties the patched
build still works, it just produces an unsigned release APK (debug builds are unaffected).
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
ANDROID_DIR = REPO_ROOT / "src-tauri" / "gen" / "android"
DEFAULT_GRADLE = ANDROID_DIR / "app" / "build.gradle.kts"
DEFAULT_PROPERTIES = ANDROID_DIR / "keystore.properties"

MARK_BEGIN = "// >>> brulion-signing"
MARK_END = "// <<< brulion-signing"
MARK_RELEASE = "// brulion-signing: release build type"
REQUIRED_IMPORTS = ("java.util.Properties", "java.io.FileInputStream")

# Inserted into `android { ... }` right before `buildTypes {`. `{u}` = one indentation unit.
SIGNING_BLOCK = (
    MARK_BEGIN + ": release key from keystore.properties (scripts/android-signing.py)",
    "signingConfigs {",
    '{u}val keystorePropertiesFile = rootProject.file("keystore.properties")',
    "{u}if (keystorePropertiesFile.exists()) {",
    "{u}{u}val keystoreProperties = Properties()",
    "{u}{u}FileInputStream(keystorePropertiesFile).use { keystoreProperties.load(it) }",
    '{u}{u}create("release") {',
    '{u}{u}{u}keyAlias = keystoreProperties.getProperty("keyAlias")',
    '{u}{u}{u}keyPassword = keystoreProperties.getProperty("keyPassword")',
    '{u}{u}{u}storeFile = rootProject.file(keystoreProperties.getProperty("storeFile"))',
    '{u}{u}{u}storePassword = keystoreProperties.getProperty("storePassword")',
    "{u}{u}}",
    "{u}}",
    "}",
    MARK_END,
)
# Inserted as the first statement of `getByName("release") { ... }` inside `buildTypes`.
RELEASE_LINE = 'signingConfigs.findByName("release")?.let { signingConfig = it } ' + MARK_RELEASE


class PatchError(Exception):
    """The Gradle file does not look like the Tauri template this script understands."""


# --------------------------------------------------------------------------------------------
# A tiny Kotlin lexer: enough to tell code from comments/strings so that braces and keywords
# inside them are ignored.


def code_mask(src: str) -> list[bool]:
    """Per character: True if it is code, False inside comments and string/char literals."""
    n = len(src)
    code = [True] * n
    i = 0

    def blank(start: int, end: int) -> None:
        for k in range(start, min(end, n)):
            code[k] = False

    while i < n:
        if src.startswith("//", i):
            end = src.find("\n", i)
            end = n if end < 0 else end
            blank(i, end)
            i = end
        elif src.startswith("/*", i):  # Kotlin block comments nest
            depth, j = 1, i + 2
            while j < n and depth:
                if src.startswith("/*", j):
                    depth, j = depth + 1, j + 2
                elif src.startswith("*/", j):
                    depth, j = depth - 1, j + 2
                else:
                    j += 1
            blank(i, j)
            i = j
        elif src.startswith('"""', i):
            end = src.find('"""', i + 3)
            end = n if end < 0 else end + 3
            while end < n and src[end] == '"':  # """...""""" ends at the last quote
                end += 1
            blank(i, end)
            i = end
        elif src[i] == '"':
            j = i + 1
            while j < n and src[j] not in '"\n':
                j += 2 if src[j] == "\\" else 1
            blank(i, j + 1)
            i = j + 1
        elif src[i] == "'":
            m = re.match(r"'(?:\\u[0-9a-fA-F]{4}|\\.|[^'\\\n])'", src[i : i + 8])
            if m:
                blank(i, i + m.end())
                i += m.end()
            else:
                i += 1
        else:
            i += 1
    return code


def matching_brace(src: str, code: list[bool], open_idx: int) -> int:
    depth = 0
    for k in range(open_idx, len(src)):
        if not code[k]:
            continue
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return k
    raise PatchError(f"unbalanced braces: '{{' at offset {open_idx} is never closed")


def find_code(pattern: str, src: str, code: list[bool], start: int = 0, end: int | None = None,
              flags: int = re.M) -> re.Match[str] | None:
    """First regex match in src[start:end] that starts and ends in code (not in a comment or
    string literal)."""
    end = len(src) if end is None else end
    for m in re.compile(pattern, flags).finditer(src, start, end):
        text = m.group(0)
        first = m.start() + len(text) - len(text.lstrip())  # skip leading indentation
        if text.strip() and code[first] and code[m.end() - 1]:
            return m
    return None


def find_block(pattern: str, src: str, code: list[bool], start: int = 0,
               end: int | None = None) -> tuple[re.Match[str], int, int] | None:
    """Find `<pattern> {` (pattern must end with `\\{`) and return (match, open, close)."""
    m = find_code(pattern, src, code, start, end)
    if m is None:
        return None
    open_idx = m.end() - 1
    assert src[open_idx] == "{", pattern
    return m, open_idx, matching_brace(src, code, open_idx)


def line_start(src: str, idx: int) -> int:
    return src.rfind("\n", 0, idx) + 1


def indent_of(src: str, idx: int) -> str:
    start = line_start(src, idx)
    return re.match(r"[ \t]*", src[start:]).group(0)


# --------------------------------------------------------------------------------------------


def locate(src: str) -> dict[str, tuple[int, int, int]]:
    """Positions of the blocks we patch: android, buildTypes (in android), release (in it)."""
    code = code_mask(src)
    android = find_block(r"^[ \t]*android\s*\{", src, code)
    if android is None:
        raise PatchError("no top-level `android { ... }` block found")
    _, a_open, a_close = android
    build_types = find_block(r"^[ \t]*buildTypes\s*\{", src, code, a_open, a_close)
    if build_types is None:
        raise PatchError("no `buildTypes { ... }` block inside `android { ... }`")
    bt_match, bt_open, bt_close = build_types
    release = find_block(r'getByName\(\s*"release"\s*\)\s*\{', src, code, bt_open, bt_close)
    if release is None:
        raise PatchError('no `getByName("release") { ... }` inside `buildTypes { ... }`')
    r_match, r_open, r_close = release
    return {
        "android": (android[0].start(), a_open, a_close),
        "buildTypes": (bt_match.start(), bt_open, bt_close),
        "release": (r_match.start(), r_open, r_close),
    }


def indent_unit(src: str, outer_idx: int, inner_idx: int) -> str:
    outer, inner = indent_of(src, outer_idx), indent_of(src, inner_idx)
    if inner.startswith(outer) and len(inner) > len(outer):
        return inner[len(outer):]
    return "    "


def add_imports(src: str) -> tuple[str, list[str]]:
    code = code_mask(src)
    missing = [
        imp for imp in REQUIRED_IMPORTS
        if find_code(rf"^[ \t]*import\s+{re.escape(imp)}[ \t]*;?[ \t]*$", src, code) is None
    ]
    if not missing:
        return src, []
    lines = "".join(f"import {imp}\n" for imp in missing)
    last_import = None
    pos = 0
    while True:
        m = find_code(r"^[ \t]*import\s+[\w.]+(?:\s+as\s+\w+)?[ \t]*;?[ \t]*$", src, code, pos)
        if m is None:
            break
        last_import, pos = m, m.end()
    if last_import is not None:
        pos = last_import.end()
        if src.startswith("\n", pos):
            return src[: pos + 1] + lines + src[pos + 1 :], missing
        return src[:pos] + "\n" + lines.rstrip("\n") + src[pos:], missing  # import at EOF
    # No imports yet: they go first, after `@file:` annotations if there are any.
    pos = 0
    for m in re.finditer(r"^@file:.*$\n?", src, re.M):
        if m.start() != pos:
            break
        pos = m.end()
    return src[:pos] + lines + ("\n" if pos == 0 else "") + src[pos:], missing


def patch_text(src: str) -> tuple[str, list[str]]:
    """Return the patched text and a list of the changes made (empty if already patched)."""
    newline = "\r\n" if "\r\n" in src else "\n"
    text = src.replace("\r\n", "\n")
    changes: list[str] = []

    has_block = MARK_BEGIN in text
    has_release = MARK_RELEASE in text
    if has_block != (MARK_END in text):
        raise PatchError(f"half of the brulion-signing block is missing ({MARK_BEGIN} / {MARK_END})")

    blocks = locate(text)
    code = code_mask(text)
    if not has_block:
        foreign = find_code(r'create\(\s*"release"\s*\)', text, code)
        if foreign is not None:
            raise PatchError('a `create("release")` signing config already exists; '
                             "remove it or sign manually")
    if not has_release:
        _, r_open, r_close = blocks["release"]
        if find_code(r"\bsigningConfig\s*=", text, code, r_open, r_close) is not None:
            raise PatchError("the release build type already sets `signingConfig`")

    # 1. First statement of the release build type.
    if not has_release:
        r_start, r_open, _ = blocks["release"]
        bt_start = blocks["buildTypes"][0]
        unit = indent_unit(text, bt_start, r_start)
        body_indent = indent_of(text, r_start) + unit
        text = text[: r_open + 1] + "\n" + body_indent + RELEASE_LINE + text[r_open + 1 :]
        changes.append('release build type: signingConfig = signingConfigs["release"]')

    # 2. signingConfigs { create("release") { ... } } right before `buildTypes {`.
    if not has_block:
        blocks = locate(text)
        a_start = blocks["android"][0]
        bt_start = blocks["buildTypes"][0]
        unit = indent_unit(text, a_start, bt_start)
        base = indent_of(text, bt_start)
        block = "".join(
            (base + line.replace("{u}", unit)).rstrip() + "\n" for line in SIGNING_BLOCK
        )
        pos = line_start(text, bt_start)
        text = text[:pos] + block + text[pos:]
        changes.append('android.signingConfigs: create("release") from keystore.properties')

    # 3. Imports used by the block (java.util.Properties is already in the Tauri template).
    text, added = add_imports(text)
    changes.extend(f"import {imp}" for imp in added)

    problems = verify_text(text)
    if problems:
        raise PatchError("patched file failed validation: " + "; ".join(problems))
    return text.replace("\n", newline), changes


def verify_text(src: str) -> list[str]:
    """Structural sanity checks of a patched build.gradle.kts. Returns a list of problems."""
    text = src.replace("\r\n", "\n")
    problems: list[str] = []
    code = code_mask(text)
    stack: list[tuple[str, int]] = []
    pairs = {")": "(", "]": "[", "}": "{"}
    for k, ch in enumerate(text):
        if not code[k]:
            continue
        if ch in "([{":
            stack.append((ch, k))
        elif ch in pairs:
            if not stack or stack[-1][0] != pairs[ch]:
                problems.append(f"unbalanced '{ch}' on line {text.count(chr(10), 0, k) + 1}")
                break
            stack.pop()
    if stack and not problems:
        ch, k = stack[-1]
        problems.append(f"unclosed '{ch}' from line {text.count(chr(10), 0, k) + 1}")
    if problems:
        return problems

    for marker in (MARK_BEGIN, MARK_END, MARK_RELEASE):
        count = text.count(marker)
        if count != 1:
            problems.append(f"marker {marker!r} found {count} times (expected 1)")
    for imp in REQUIRED_IMPORTS:
        if find_code(rf"^[ \t]*import\s+{re.escape(imp)}[ \t]*;?[ \t]*$", text, code) is None:
            problems.append(f"missing import {imp}")
    if problems:
        return problems

    # Imports must precede every other statement in a Kotlin script.
    first_stmt = find_code(r"^[ \t]*(?!import\b|@file:|package\b)\S", text, code)
    last_import = None
    pos = 0
    while True:
        m = find_code(r"^[ \t]*import\b", text, code, pos)
        if m is None:
            break
        last_import, pos = m, m.end()
    if first_stmt is not None and last_import is not None and last_import.start() > first_stmt.start():
        line = text.count("\n", 0, last_import.start()) + 1
        problems.append(f"import after other code on line {line}")

    try:
        blocks = locate(text)
    except PatchError as err:
        return [str(err)]
    _, a_open, a_close = blocks["android"]
    bt_start = blocks["buildTypes"][0]
    _, r_open, r_close = blocks["release"]
    begin, end = text.find(MARK_BEGIN), text.find(MARK_END)
    if not (a_open < begin < end < bt_start):
        problems.append("signingConfigs block is not inside android { } before buildTypes { }")
    sc = find_block(r"^[ \t]*signingConfigs\s*\{", text, code, begin, end)
    if sc is None or not (begin < sc[1] < sc[2] < end):
        problems.append("signingConfigs { } block is malformed")
    release_line = text.find(MARK_RELEASE)
    if not (r_open < release_line < r_close):
        problems.append('signingConfig assignment is not inside getByName("release") { }')
    if not a_close > r_close:
        problems.append("buildTypes is not inside android { }")
    return problems


def is_patched(src: str) -> bool:
    return MARK_BEGIN in src and MARK_END in src and MARK_RELEASE in src


# --------------------------------------------------------------------------------------------


def atomic_write(path: Path, data: str, mode: int | None = None) -> None:
    fd, tmp = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as fh:
            fh.write(data)
        if mode is None and path.exists():
            mode = path.stat().st_mode & 0o7777
        os.chmod(tmp, 0o644 if mode is None else mode)
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise


def escape_property(value: str, is_key: bool = False) -> str:
    """Escape like java.util.Properties.store (ISO-8859-1 file, \\uXXXX for the rest)."""
    out = []
    for i, ch in enumerate(value):
        o = ord(ch)
        if ch == "\\":
            out.append("\\\\")
        elif ch == " " and (i == 0 or is_key):
            out.append("\\ ")
        elif ch in "\t\n\r\f":
            out.append({"\t": "\\t", "\n": "\\n", "\r": "\\r", "\f": "\\f"}[ch])
        elif ch in "=:#!":
            out.append("\\" + ch)
        elif o < 0x20 or o > 0x7E:
            data = ch.encode("utf-16-be")
            for k in range(0, len(data), 2):
                out.append("\\u%04X" % int.from_bytes(data[k : k + 2], "big"))
        else:
            out.append(ch)
    return "".join(out)


def cmd_patch(gradle: Path) -> int:
    if not gradle.is_file():
        print(f"error: {gradle} not found (run `npx tauri android init` first)", file=sys.stderr)
        return 1
    src = gradle.read_text(encoding="utf-8")
    try:
        new, changes = patch_text(src)
    except PatchError as err:
        print(f"error: cannot patch {gradle}: {err}", file=sys.stderr)
        print("The Tauri Android template probably changed; update scripts/android-signing.py.",
              file=sys.stderr)
        return 2
    if not changes:
        print(f"{gradle}: release signing already configured")
        return 0
    atomic_write(gradle, new)
    print(f"{gradle}: patched")
    for change in changes:
        print(f"  + {change}")
    return 0


def cmd_check(gradle: Path) -> int:
    if not gradle.is_file():
        print(f"{gradle}: not found", file=sys.stderr)
        return 1
    src = gradle.read_text(encoding="utf-8")
    if not is_patched(src):
        print(f"{gradle}: not patched")
        return 1
    problems = verify_text(src)
    if problems:
        print(f"{gradle}: patched but broken: " + "; ".join(problems), file=sys.stderr)
        return 2
    print(f"{gradle}: release signing configured")
    return 0


def cmd_properties(out: Path, store_file: str, key_alias: str) -> int:
    store_password = os.environ.get("ANDROID_KEYSTORE_PASSWORD", "")
    key_password = os.environ.get("ANDROID_KEY_PASSWORD") or store_password
    if not store_password:
        print("error: ANDROID_KEYSTORE_PASSWORD is not set", file=sys.stderr)
        return 1
    if not key_alias:
        print("error: --key-alias must not be empty", file=sys.stderr)
        return 1
    store = Path(store_file).expanduser()
    if not store.is_absolute():
        store = (Path.cwd() / store).resolve()
    if not store.is_file():
        print(f"error: keystore {store} not found", file=sys.stderr)
        return 1
    out.parent.mkdir(parents=True, exist_ok=True)
    entries = {
        "storeFile": str(store),
        "storePassword": store_password,
        "keyAlias": key_alias,
        "keyPassword": key_password,
    }
    body = "# Written by scripts/android-signing.py - contains secrets, never commit it.\n"
    body += "".join(f"{k}={escape_property(v)}\n" for k, v in entries.items())
    atomic_write(out, body, mode=0o600)
    print(f"{out}: written (keystore {store}, alias {key_alias})")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__.split("\n\n", 1)[0],
        epilog="Default subcommand: patch.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command")
    for name in ("patch", "check"):
        p = sub.add_parser(name)
        p.add_argument("--gradle", type=Path, default=DEFAULT_GRADLE,
                       help=f"app/build.gradle.kts (default: {DEFAULT_GRADLE})")
    p = sub.add_parser("properties", help="write keystore.properties")
    p.add_argument("--store-file", required=True, help="keystore (.jks / .p12) path")
    p.add_argument("--key-alias", required=True)
    p.add_argument("--out", type=Path, default=DEFAULT_PROPERTIES,
                   help=f"default: {DEFAULT_PROPERTIES}")
    args = parser.parse_args(argv)

    if args.command in (None, "patch"):
        return cmd_patch(getattr(args, "gradle", DEFAULT_GRADLE))
    if args.command == "check":
        return cmd_check(args.gradle)
    return cmd_properties(args.out, args.store_file, args.key_alias)


if __name__ == "__main__":
    sys.exit(main())
