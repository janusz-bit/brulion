#!/usr/bin/env bash
# lint.sh <brulion> <brulion-uimetrics> <work-dir> — ctest tst_uibench_lint_qml, the UI
# benchmark's regression guard (docs/UI.md "UI benchmark (Brulion vs Obsidian)"): 8 shots of the
# real app (daily, doc, settings, palette × desktop light / phone dark; offscreen + software, a
# fresh home each, the bench's notebook, the system's fonts) with --dump-layout, then
# `brulion-uimetrics lint` with lint-allow.json: fails on an enabled, visible text below its WCAG
# contrast or a target below its size (phone 44 px, desktop WCAG 2.5.8) that the allow list does
# not excuse. Needs node (screens.json). BRULION_TEST_SLOWDOWN stretches the delays.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=lib.sh
source "$here/lib.sh"

bin="${1:?usage: lint.sh <brulion> <brulion-uimetrics> <work-dir>}"
uimetrics="${2:?}"
work="${3:?}"
slowdown="${BRULION_TEST_SLOWDOWN:-1}"
(( slowdown > 3 )) && slowdown=3
now="2026-10-14 10:00"
rm -rf "$work"
mkdir -p "$work/shots"
work="$(cd "$work" && pwd)"

BRULION_BIN="$bin" NOW="$now" bash "$here/prepare-vault.sh" "$work" --no-obsidian

failed=0
checked=0
for pair in "desktop light" "phone dark"; do
  read -r want_size want_theme <<< "$pair"
  shots="$(uibench_shots "$here/screens.json" daily,doc,settings,palette "$want_size" "$want_theme")"
  while IFS=$'\t' read -r id size theme w h scale touch delay argstr; do
    [[ -n "$id" ]] || continue
    IFS=$'\x1f' read -r -a args <<< "$argstr"
    base="$work/shots/$id-$size-$theme"
    ok=0
    for attempt in 1 2; do
      if uibench_brulion_shot "$bin" "$work/vault-brulion/Przykładowy brulion" "$work/home" "$base.png" \
          "$base.layout.json" "$w" "$h" "$scale" "$touch" "$theme" "$((delay * slowdown))" offscreen "$now" \
          "${args[@]}" >"$base.log" 2>&1 && [[ -s "$base.layout.json" ]]; then
        ok=1
        break
      fi
      echo "lint: $id-$size-$theme: the app failed (attempt $attempt)" >&2
      cat "$base.log" >&2
    done
    if [[ $ok == 0 ]]; then
      failed=1
      continue
    fi
    grep -h "Binding loop detected" "$base.log" >&2 || true
    device=desktop
    [[ "$touch" == true ]] && device=phone
    if "$uimetrics" lint --png "$base.png" --dump "$base.layout.json" --device "$device" --screen "$id" \
        --allow "$here/lint-allow.json"; then
      echo "lint: $id-$size-$theme ok"
    else
      echo "lint: $id-$size-$theme: failures above (excuse a deliberate one in tests/app/uibench/lint-allow.json)" >&2
      failed=1
    fi
    checked=$((checked + 1))
  done <<< "$shots"
done
[[ $checked == 8 ]] || { echo "lint: $checked shots checked, expected 8" >&2; failed=1; }
exit $failed
