#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Tests scripts/check-qml-text-format.sh on small QML files — the Text and Label objects without
# a textFormat it has to find, the code it has to leave alone — and then runs it over the app's
# QML, where every Text says how its text is read (Qt's default, AutoText, takes a note's
# `<img src=…>` for markup; docs/UI.md "Text formats").
#
# Usage: tests/scripts/tst_check_qml_text_format.sh [path/to/check-qml-text-format.sh [QML directory]]
#        (default: scripts/check-qml-text-format.sh, src/app/qml)
set -euo pipefail

script=$(realpath -- "${1:-$(dirname -- "${BASH_SOURCE[0]}")/../../scripts/check-qml-text-format.sh}")
qml=$(realpath -- "${2:-$(dirname -- "$script")/../src/app/qml}")
[[ -f $script && -d $qml ]] || {
  echo "no $script or $qml" >&2
  exit 2
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
# check <arguments...>: runs the script; sets $status and $out (stdout + stderr).
check() {
  status=0
  out=$(bash "$script" "$@" 2>&1) || status=$?
  printf '%s\n' "$out" | sed 's/^/        | /'
}
has() { grep -qF -- "$1" <<<"$out"; }
reported() { grep -c '\.qml:[0-9]*:' <<<"$out" || true; }

# Objects that leave the format to Qt: lines 6, 11, 15, 19, 23 and 30.
cat >"$tmp/Kalendarz.qml" <<'EOF'
import QtQuick
import QtQuick.Templates as T

Item {
    id: root
    Text {
        text: root.tytuł // a page title: "<img src='file://host/x.png'>" would be markup
        elide: Text.ElideRight
    }
    Row {
        Label { text: "zażółć" }
    }
    ListView {
        // The format of the Text inside the delegate's Rectangle says nothing about this one.
        delegate: Text {
            text: modelData
            Rectangle { Text { textFormat: Text.PlainText; text: "x" } }
        }
        header: T.Label {
            text: qsTr("Tasks")
            font { pixelSize: 12 }
        }
        footer: QtQuick.Text { text: "…" }
    }
    component Hint: Text {
        textFormat: Text.PlainText
        color: "gray"
    }
    component Podpowiedź: Text {
        Text {
            // A handler's label of the same name is no property of the object.
            onLinkActivated: { textFormat: Text.PlainText }
        }
        textFormat: Text.StyledText
    }
}
EOF
check "$tmp/Kalendarz.qml"
expect "Text and Label objects without a format are found" test "$status" = 1
for line in 6 11 15 19 23 30; do
  expect "... the one on line $line" has "Kalendarz.qml:$line:"
done
expect "... and nothing else" test "$(reported)" = 6
expect "... saying what to write" has "textFormat: Text.PlainText"

# Formats given, other types whose names end in Text, comments, strings and functions.
cat >"$tmp/Zażółć.qml" <<'EOF'
import QtQuick
import QtQuick.Templates as T

// Text { text: "in a comment" }
Item {
    id: root
    readonly property string wzór: "Text { text: 1 }" // Label { }
    readonly property string inny: 'Label { text: "x" }'
    /* Text {
        text: "in a block comment"
    } */
    Text {
        id: title
        // textFormat: Text.AutoText in a comment is no format; the line below is.
        textFormat: Text.PlainText
        text: root.wzór
    }
    Text { text: "one line"; textFormat: Text.PlainText }
    T.Label {
        textFormat:Text.StyledText
        text: "<b>" + root.inny + "</b>"
    }
    BText { text: "sets its format itself" }
    BlockText { text: "another type" }
    TextEdit { text: "plain by default" }
    TextInput { text: "plain" }
    PlainText { }
    function znajdź(item: Item, name: string): Text {
        return root.find(item, name) as Text
    }
    function etykieta(): T.Label { return null }
    function format(t: Text): int {
        switch (t.textFormat) {
        case Text.PlainText: { return 0 }
        default: return 1
        }
    }
    property Component delegat: Component {
        Text {
            textFormat: Text.RichText
            text: `wzorzec { ${root.wzór} }
                   w dwóch liniach }`
            onLinkActivated: link => { root.open(link) }
        }
    }
}
EOF
check "$tmp/Zażółć.qml"
expect "formats given, other types, comments, strings and return types pass" test "$status/$(reported)" = 0/0

check "$tmp"
expect "a directory: its QML files are checked" test "$status/$(reported)" = 1/6

check "$tmp/Brak.qml"
expect "a missing file is an error of its own" test "$status" = 2

check "$qml"
expect "every Text and Label of the app's QML says how its text is read" test "$status/$(reported)" = 0/0

echo
if ((failed)); then
  echo "Totals: $failed failed"
  exit 1
fi
echo "Totals: all passed"
