#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/markdown-page.py, which renders docs/PRIVACY.md into the site's privacy.html
# (docs/GOOGLE_PLAY.md "Privacy policy"): the Markdown it knows (headings with ids made of Polish
# text, lists, tables, bold, code, links, the Polish section), escaping, and what it must refuse
# rather than publish half-rendered (unsafe link addresses, code blocks, nested lists, broken
# tables, a template without its placeholder); then the real policy with the real template: every
# link to a part of the page leads to a heading, nothing of the Markdown is left.
# Exits 77 (skipped) without python3.
#
# Usage: tests/scripts/tst_markdown_page.sh [path/to/markdown-page.py [repository root]]
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/markdown-page.py}")
root=$(realpath -- "${2:-$(dirname -- "$script")/..}")
[[ -f $script && -f $root/docs/PRIVACY.md ]] || {
  echo "no $script or $root/docs/PRIVACY.md" >&2
  exit 2
}
command -v python3 >/dev/null || {
  echo "SKIP: no python3"
  exit 77
}
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT

failed=0
pass() { printf 'PASS   : %s\n' "$1"; }
fail() {
  printf 'FAIL!  : %s\n' "$1"
  failed=$((failed + 1))
}
expect() { # <description> <command...>: the command succeeds
  local what=$1
  shift
  if "$@"; then pass "$what"; else fail "$what"; fi
}
# render <markdown file> [template]: sets $status and $out (stdout), $err (stderr).
render() {
  status=0
  out=$(python3 -I "$script" "${2:-$tmp/template.html}" "$1" 2>"$tmp/err") || status=$?
  err=$(<"$tmp/err")
}
has() { grep -qF -- "$1" <<<"$out"; }

printf '<title>@TITLE@</title>\n<main>\n@CONTENT@\n</main>\n' >"$tmp/template.html"

cat >"$tmp/all.md" <<'EOF'
<!--
  A comment: left out.
-->

# Zażółć — [gęślą](https://example.org) jaźń

Effective **now**, with `code <b>` and a <https://example.org/a?b=1&c=2> link; <script>alert(1)</script>
continues the paragraph. [Down](#polityka-prywatnosci), [mail](mailto:a@example.org), [rel](privacy.html).

* one item
  that goes on
- **two** with [a link](https://example.org/x)

1. first
2. second

| Uprawnienie | Do czego |
|---|---|
| Mikrofon | Notatki `głosowe` |

---

<!-- section lang="pl" -->

# Polityka prywatności

## Polityka prywatności
EOF
render "$tmp/all.md"
printf '%s\n' "$out" | sed 's/^/        | /'
expect "the Markdown it knows renders" test "$status" = 0
expect "... the title: the first heading's text without its link" has '<title>Zażółć — gęślą jaźń</title>'
expect "... a heading with an id of ASCII letters and its link" \
  has '<h1 id="zazolc-gesla-jazn">Zażółć — <a href="https://example.org">gęślą</a> jaźń</h1>'
expect "... a paragraph of two lines, bold and escaped code" \
  has '<p>Effective <strong>now</strong>, with <code>code &lt;b&gt;</code> and a <a href="https://example.org/a?b=1&amp;c=2">https://example.org/a?b=1&amp;c=2</a> link; &lt;script&gt;alert(1)&lt;/script&gt; continues the paragraph.'
expect "... a fragment, mailto and a relative link" \
  has '<a href="#polityka-prywatnosci">Down</a>, <a href="mailto:a@example.org">mail</a>, <a href="privacy.html">rel</a>.</p>'
expect "... a list whose item goes on, either marker" \
  has '<li>one item that goes on</li>'
expect "... ... bold and a link in an item" has '<li><strong>two</strong> with <a href="https://example.org/x">a link</a></li>'
expect "... an ordered list" bash -c "grep -qF '<ol>' <<<\"\$1\" && grep -qF '<li>second</li>' <<<\"\$1\"" _ "$out"
expect "... a table" has '<tr><td>Mikrofon</td><td>Notatki <code>głosowe</code></td></tr>'
expect "... a rule" has '<hr>'
expect "... the Polish section, to the end" bash -c "grep -qx '<section lang=\"pl\">' <<<\"\$1\" && grep -qx '</section>' <<<\"\$1\"" _ "$out"
expect "... a heading of Polish letters (ł too)" has '<h1 id="polityka-prywatnosci">Polityka prywatności</h1>'
expect "... a repeated heading gets its own id" has '<h2 id="polityka-prywatnosci-2">Polityka prywatności</h2>'
expect "... no comment left" bash -c "! grep -qF 'left out' <<<\"\$1\"" _ "$out"

refused() { # <description> <markdown text> <message part>
  printf '# Title\n\n%s\n' "$2" >"$tmp/bad.md"
  render "$tmp/bad.md"
  if [[ $status == 1 && -z $out && $err == *"$3"* ]]; then pass "$1"; else
    fail "$1 (status $status, stderr: $err)"
  fi
}
refused "a javascript: link is refused" 'Click [here](javascript:alert(1)).' "line 3: unsafe or unknown link address 'javascript:"
refused "a data: link is refused" '[x](data:text/html,hi)' "unsafe or unknown link address"
refused "a fenced code block is refused" $'```\ncode\n```' "line 3: not supported here"
refused "HTML is refused" '<div>raw</div>' "line 3: not supported here"
refused "a quote is refused" '> quoted' "not supported here"
refused "a nested list is refused" $'* a\n  * b' "line 4: nested lists are not supported"
refused "a table without its |---| line is refused" '| a | b |' "a table needs a |---| line"
refused "a row with another number of cells is refused" $'| a | b |\n|---|---|\n| 1 |' "line 5: 1 cells, the header has 2"
refused "a comment that never ends is refused" '<!-- open' "an HTML comment that never ends"
printf 'No heading here.\n' >"$tmp/notitle.md"
render "$tmp/notitle.md"
expect "a document without a heading is refused" test "$status/$err" = "1/markdown-page: $tmp/notitle.md: no heading: the page needs a title"
printf '<p>no placeholder</p>\n' >"$tmp/plain.html"
render "$tmp/all.md" "$tmp/plain.html"
expect "a template without @CONTENT@ is refused" test "$status" = 1

# The real policy with the real template.
render "$root/docs/PRIVACY.md" "$root/packaging/fdroid-repo/privacy.html"
expect "docs/PRIVACY.md renders" test "$status" = 0
expect "... its title" has '<title>Brulion — privacy policy</title>'
expect "... both languages, the Polish one in its section" \
  bash -c "grep -qF '<h1 id=\"brulion-privacy-policy\">' <<<\"\$1\" && grep -qx '<section lang=\"pl\">' <<<\"\$1\"" _ "$out"
missing=$(grep -oE 'href="#[^"]+"' <<<"$out" | sed -E 's/href="#(.*)"/\1/' | while read -r id; do
  grep -qF "id=\"$id\"" <<<"$out" || echo "$id"
done)
expect "... every link to a part of the page has its heading${missing:+ (not: $missing)}" test -z "$missing"
expect "... nothing of the Markdown or the template left" bash -c "! grep -qE '@[A-Z]+@|\\*\\*|\\]\\(|^[*-] |^#' <<<\"\$1\"" _ "$out"

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
