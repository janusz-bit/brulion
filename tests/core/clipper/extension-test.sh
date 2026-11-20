#!/usr/bin/env bash
# extension-test.sh <clipper-dir> — ctest tst_clipper_extension: the Web Clipper extension's scripts
# parse (node --check) and its unit tests pass (clipper/test/*.test.mjs with node --test: the clip
# request, settings, templates, the manifest's ids against the host's, the translations, the page
# reader's helpers). Without node the test is skipped (exit 77).
set -euo pipefail
dir="${1:?usage: extension-test.sh <clipper-dir>}"
command -v node >/dev/null 2>&1 || { echo "extension-test: no node, skipped"; exit 77; }
for file in "$dir"/*.js "$dir"/lib/*.js "$dir"/content/*.js; do
  node --check "$file" || { echo "extension-test: $file does not parse" >&2; exit 1; }
done
node --test "$dir"/test/*.test.mjs
