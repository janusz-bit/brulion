#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Finds Text and Label objects in QML that do not say how their text is to be read. Qt Quick's
# default, Text.AutoText, takes any text with a tag before its first line break as styled text:
# a page title, a task or a line of a note such as `<img src="file://host/share/x.png">` is then
# markup — Qt fetches the picture (on Windows from another machine, with the user's credentials),
# and `<font size=7>` redraws the interface. Text that came from a note is shown as written only
# with `textFormat: Text.PlainText`; markup made by Brulion's renderers (escaped) says
# Text.StyledText or Text.RichText. So every Text states its format (docs/UI.md "Text formats");
# AutoText also costs a scan of every string (Qt: "Text.PlainText … when possible").
#
# Usage: scripts/check-qml-text-format.sh [file or directory...]   (default: src/app/qml)
# Prints file:line: of each such object's opening line; exit status 1 when there is one, 2 on bad
# usage. An object is found by the `{` after its type on one line (`Text {`, `delegate: Label {`,
# `component Hint: Text {`, `T.Label {`); a function's return type (`function f(): Text {`) is none.
set -euo pipefail

if [[ $# -eq 0 ]]; then
  set -- "$(dirname -- "${BASH_SOURCE[0]}")/../src/app/qml"
fi
files=()
for arg in "$@"; do
  if [[ -d $arg ]]; then
    while IFS= read -r -d '' f; do files+=("$f"); done < <(find "$arg" -name '*.qml' -type f -print0 | sort -z)
  elif [[ -f $arg ]]; then
    files+=("$arg")
  else
    echo "check-qml-text-format: no such file or directory: $arg" >&2
    exit 2
  fi
done
[[ ${#files[@]} -gt 0 ]] || exit 0

# One pass over each file, a character at a time, outside comments and strings: every `{` opens
# an object or a block (remembered with whether it is a Text / Label object), every `}` closes
# the last one; `textFormat:` counts for the object it stands in directly, not for one around it.
awk '
function ident(c) { return c ~ /[A-Za-z0-9_$]/ }
# Does the `{` after `before` (the code of its line so far) open a Text or Label object?
function opensText(before,    rest) {
    sub(/[ \t]+$/, "", before)
    if (before !~ /(^|[^A-Za-z0-9_$])(Text|Label)$/)
        return 0
    rest = before
    sub(/(Text|Label)$/, "", rest)
    sub(/([A-Za-z_][A-Za-z0-9_]*\.)+$/, "", rest) # a module name: T.Label, QtQuick.Text
    sub(/[ \t]+$/, "", rest)
    return rest !~ /\)[ \t]*:$/ # `function f(): Text {` returns one, it is none
}
FNR == 1 { depth = 0; inComment = 0; inTemplate = 0 }
{
    line = $0
    n = length(line)
    code = ""
    i = 1
    while (i <= n) {
        c = substr(line, i, 1)
        two = substr(line, i, 2)
        if (inComment) {
            if (two == "*/") { inComment = 0; i += 2 } else i++
            continue
        }
        if (inTemplate) {
            if (c == "\\") i += 2
            else { if (c == "`") inTemplate = 0; i++ }
            continue
        }
        if (two == "//")
            break
        if (two == "/*") { inComment = 1; i += 2; continue }
        if (c == "\"" || c == "\047") {
            # A string ends on its line; its text is no code.
            i++
            while (i <= n) {
                d = substr(line, i, 1)
                if (d == "\\") { i += 2; continue }
                i++
                if (d == c)
                    break
            }
            code = code c c
            continue
        }
        if (c == "`") { inTemplate = 1; i++; continue }
        if (c == "{") {
            depth++
            isText[depth] = opensText(code)
            startLine[depth] = FNR
            hasFormat[depth] = 0
        } else if (c == "}") {
            if (depth > 0) {
                if (isText[depth] && !hasFormat[depth]) {
                    printf "%s:%d: %s\n", FILENAME, startLine[depth], "a Text or Label without textFormat"
                    found = 1
                }
                depth--
            }
        } else if (c == "t" && depth > 0 && isText[depth] && substr(line, i, 10) == "textFormat" \
                   && (i == 1 || (!ident(substr(line, i - 1, 1)) && substr(line, i - 1, 1) != ".")) \
                   && substr(line, i + 10) ~ /^[ \t]*:/) {
            hasFormat[depth] = 1
        }
        code = code c
        i++
    }
}
END {
    if (found) {
        print "check-qml-text-format: say how the text is read — textFormat: Text.PlainText for text" \
              " as written, Text.StyledText for escaped markup (docs/UI.md)" > "/dev/stderr"
        exit 1
    }
}
' "${files[@]}"
