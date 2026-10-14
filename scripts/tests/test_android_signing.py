# SPDX-License-Identifier: AGPL-3.0-or-later
"""Tests for scripts/android-signing.py. Run: python3 -m unittest discover -s scripts/tests -v

fixtures/app.build.gradle.kts is app/build.gradle.kts exactly as `tauri android init` (CLI 2.12.2)
generates it for Brulion.
"""

from __future__ import annotations

import importlib.util
import os
import re
import stat
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
SCRIPT = HERE.parent / "android-signing.py"
FIXTURE = (HERE / "fixtures" / "app.build.gradle.kts").read_text(encoding="utf-8")

_spec = importlib.util.spec_from_file_location("android_signing", SCRIPT)
assert _spec and _spec.loader
signing = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(signing)


def unescape_properties_value(value: str) -> str:
    """Minimal java.util.Properties value decoding, to round-trip escape_property()."""
    out, i = [], 0
    while i < len(value):
        ch = value[i]
        if ch == "\\" and i + 1 < len(value):
            nxt = value[i + 1]
            if nxt == "u":
                out.append(chr(int(value[i + 2 : i + 6], 16)))
                i += 6
                continue
            out.append({"t": "\t", "n": "\n", "r": "\r", "f": "\f"}.get(nxt, nxt))
            i += 2
            continue
        out.append(ch)
        i += 1
    # Java joins surrogate pairs; do the same.
    return "".join(out).encode("utf-16", "surrogatepass").decode("utf-16")


def quiet(fn, *args):
    with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
        return fn(*args)


class PatchTest(unittest.TestCase):
    def test_fixture_is_not_patched(self):
        self.assertFalse(signing.is_patched(FIXTURE))

    def test_patch_adds_signing_config_imports_and_release_assignment(self):
        out, changes = signing.patch_text(FIXTURE)
        self.assertEqual(len(changes), 3, changes)
        self.assertTrue(signing.is_patched(out))
        self.assertEqual(signing.verify_text(out), [])
        self.assertIn("import java.io.FileInputStream\n", out)
        self.assertEqual(out.count("import java.util.Properties"), 1)
        self.assertIn('        create("release") {', out)
        self.assertIn('rootProject.file("keystore.properties")', out)
        for key in ("keyAlias", "keyPassword", "storeFile", "storePassword"):
            self.assertIn(f'getProperty("{key}")', out)
        # The block sits in android { } right before buildTypes { }, indented like it.
        self.assertRegex(out, r"\n    // <<< brulion-signing\n    buildTypes \{\n")
        # The release build type uses it as its first statement.
        self.assertRegex(
            out,
            r'getByName\("release"\) \{\n            signingConfigs\.findByName\("release"\)'
            r"\?\.let \{ signingConfig = it \} // brulion-signing",
        )
        # Nothing else changed: removing the inserted lines gives back the template.
        stripped = re.sub(r"    // >>> brulion-signing.*?// <<< brulion-signing\n", "", out, flags=re.S)
        stripped = re.sub(r"\n *signingConfigs\.findByName.*", "", stripped)
        stripped = stripped.replace("import java.io.FileInputStream\n", "")
        self.assertEqual(stripped, FIXTURE)

    def test_patch_is_idempotent(self):
        once, _ = signing.patch_text(FIXTURE)
        twice, changes = signing.patch_text(once)
        self.assertEqual(changes, [])
        self.assertEqual(twice, once)

    def test_completes_a_partial_patch(self):
        full, _ = signing.patch_text(FIXTURE)
        partial = re.sub(r"\n *signingConfigs\.findByName.*", "", full)
        out, changes = signing.patch_text(partial)
        self.assertEqual(len(changes), 1, changes)
        self.assertEqual(out, full)

    def test_crlf_line_endings_are_preserved(self):
        crlf = FIXTURE.replace("\n", "\r\n")
        out, _ = signing.patch_text(crlf)
        self.assertNotIn("\n", out.replace("\r\n", ""))
        self.assertEqual(out.replace("\r\n", "\n"), signing.patch_text(FIXTURE)[0])

    def test_ignores_braces_and_keywords_in_comments_and_strings(self):
        tricky = FIXTURE.replace(
            "android {\n",
            'android {\n    // buildTypes { getByName("release") {\n'
            '    /* nested /* buildTypes { */ } */\n'
            '    val note = "buildTypes { {{ "\n'
            "    val brace = '{'\n",
            1,
        )
        out, _ = signing.patch_text(tricky)
        self.assertEqual(signing.verify_text(out), [])
        self.assertLess(out.index("// <<< brulion-signing"), out.index("    buildTypes {\n"))
        self.assertGreater(out.index("// >>> brulion-signing"), out.index("val brace"))

    def test_adds_both_imports_when_there_are_none(self):
        src = FIXTURE.replace("import java.util.Properties\n", "").replace(
            "import org.jetbrains.kotlin.gradle.dsl.JvmTarget\n", ""
        )
        src = src.replace("JvmTarget.JVM_1_8", '"1.8"')
        out, _ = signing.patch_text(src)
        self.assertTrue(out.startswith("import java.util.Properties\nimport java.io.FileInputStream\n"))
        self.assertEqual(signing.verify_text(out), [])

    def test_refuses_foreign_release_signing_config(self):
        src = FIXTURE.replace(
            "    buildTypes {",
            '    signingConfigs {\n        create("release") {\n        }\n    }\n    buildTypes {',
        )
        with self.assertRaisesRegex(signing.PatchError, 'create\\("release"\\)'):
            signing.patch_text(src)

    def test_refuses_existing_signing_config_in_release_type(self):
        src = FIXTURE.replace(
            'getByName("release") {',
            'getByName("release") {\n            signingConfig = signingConfigs.getByName("debug")',
        )
        with self.assertRaisesRegex(signing.PatchError, "already sets"):
            signing.patch_text(src)

    def test_unknown_template_is_an_error(self):
        with self.assertRaisesRegex(signing.PatchError, "buildTypes"):
            signing.patch_text(FIXTURE.replace("buildTypes {", "flavors {"))
        with self.assertRaisesRegex(signing.PatchError, "android"):
            signing.patch_text("plugins {\n}\n")
        with self.assertRaisesRegex(signing.PatchError, "half"):
            signing.patch_text(FIXTURE + "\n// >>> brulion-signing\n")

    def test_verify_reports_broken_braces(self):
        out, _ = signing.patch_text(FIXTURE)
        broken = out.replace("            create(\"release\") {\n", "            create(\"release\")\n")
        self.assertTrue(signing.verify_text(broken))

    def test_cli_patch_and_check(self):
        with tempfile.TemporaryDirectory() as tmp:
            gradle = Path(tmp) / "build.gradle.kts"
            gradle.write_text(FIXTURE, encoding="utf-8")
            os.chmod(gradle, 0o640)
            self.assertEqual(quiet(signing.main, ["check", "--gradle", str(gradle)]), 1)
            self.assertEqual(quiet(signing.main, ["patch", "--gradle", str(gradle)]), 0)
            first = gradle.read_text(encoding="utf-8")
            self.assertEqual(quiet(signing.main, ["patch", "--gradle", str(gradle)]), 0)
            self.assertEqual(gradle.read_text(encoding="utf-8"), first)
            self.assertEqual(quiet(signing.main, ["check", "--gradle", str(gradle)]), 0)
            self.assertEqual(stat.S_IMODE(gradle.stat().st_mode), 0o640)
            self.assertEqual(sorted(p.name for p in Path(tmp).iterdir()), ["build.gradle.kts"])
            self.assertEqual(quiet(signing.main, ["patch", "--gradle", str(Path(tmp) / "nope")]), 1)


class PropertiesTest(unittest.TestCase):
    def test_escape_round_trip(self):
        for value in ["plain", "a=b:c#d!e", "back\\slash", " leading space", "tab\tnew\nline",
                      "zażółć gęślą jaźń", "emoji 🔑", "trailing space "]:
            with self.subTest(value=value):
                escaped = signing.escape_property(value)
                self.assertTrue(all(32 <= ord(c) <= 126 for c in escaped), escaped)
                self.assertEqual(unescape_properties_value(escaped), value)

    def test_writes_private_properties_file_from_env(self):
        with tempfile.TemporaryDirectory() as tmp:
            store = Path(tmp) / "release.jks"
            store.write_bytes(b"x")
            out = Path(tmp) / "android" / "keystore.properties"
            env = {"ANDROID_KEYSTORE_PASSWORD": "pä:ss=wörd\\", "ANDROID_KEY_PASSWORD": ""}
            with mock.patch.dict(os.environ, env):
                rc = quiet(signing.main, ["properties", "--out", str(out),
                                          "--store-file", str(store), "--key-alias", "brulion"])
            self.assertEqual(rc, 0)
            self.assertEqual(stat.S_IMODE(out.stat().st_mode), 0o600)
            props = {}
            for line in out.read_text(encoding="latin-1").splitlines():
                if line and not line.startswith("#"):
                    key, _, value = line.partition("=")
                    props[key] = unescape_properties_value(value)
            self.assertEqual(props, {
                "storeFile": str(store),
                "storePassword": "pä:ss=wörd\\",
                "keyAlias": "brulion",
                "keyPassword": "pä:ss=wörd\\",  # defaults to the store password
            })

    def test_requires_password_and_existing_keystore(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "keystore.properties"
            store = Path(tmp) / "release.jks"
            args = ["properties", "--out", str(out), "--store-file", str(store), "--key-alias", "a"]
            with mock.patch.dict(os.environ, {"ANDROID_KEYSTORE_PASSWORD": ""}):
                self.assertEqual(quiet(signing.main, args), 1)
            with mock.patch.dict(os.environ, {"ANDROID_KEYSTORE_PASSWORD": "secret12"}):
                self.assertEqual(quiet(signing.main, args), 1)  # keystore missing
            self.assertFalse(out.exists())


if __name__ == "__main__":
    unittest.main()
