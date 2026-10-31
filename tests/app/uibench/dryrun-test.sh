#!/usr/bin/env bash
# dryrun-test.sh <work-dir> — ctest tst_uibench_bench_dryrun: bench.sh --dry-run lists, for
# screens.json, one Brulion command per screen × size × theme (48) with the right --size, --scale
# and --touch, the Obsidian driver's invocation and the metrics; an unknown screen in --only fails;
# the Node scripts parse (node --check); judge.mjs scores three synthetic rounds right
# (judge-test.mjs). Needs node (bench.sh reads screens.json with it): without it the test is
# skipped (exit 77).
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
work="${1:?usage: dryrun-test.sh <work-dir>}"
command -v node >/dev/null 2>&1 || { echo "dryrun-test: no node, skipped"; exit 77; }
mkdir -p "$work"

fail() { echo "dryrun-test: $*" >&2; exit 1; }

listing="$(bash "$here/bench.sh" --dry-run --out "$work/round")" || fail "bench.sh --dry-run failed"
printf '%s\n' "$listing" > "$work/listing.txt"

shots="$(grep -c -- '--dump-layout' <<< "$listing" || true)"
[[ "$shots" == 48 ]] || fail "$shots Brulion commands, expected 48"
desktop="$(grep -- '--dump-layout' <<< "$listing" | grep -c -- '--size 1280x800 --scale 1 --theme' || true)"
[[ "$desktop" == 26 ]] || fail "$desktop desktop commands (1280x800, scale 1, no --touch), expected 26"
phone="$(grep -- '--dump-layout' <<< "$listing" | grep -c -- '--size 412x915 --scale 2 --touch --theme' || true)"
[[ "$phone" == 22 ]] || fail "$phone phone commands (412x915, scale 2, --touch), expected 22"
for theme in light dark; do
  n="$(grep -- '--dump-layout' <<< "$listing" | grep -c -- "--theme $theme " || true)"
  [[ "$n" == 24 ]] || fail "$n $theme commands, expected 24"
done
grep -q -- '--now 2026-10-14\\ 10:00' <<< "$listing" || fail "no --now from screens.json"
grep -q -- "--editor-script edit:Pierwszy@60" <<< "$listing" || fail "doc-edit's editor script missing"
obsidian="$(grep -c 'obsidian-shots.mjs' <<< "$listing" || true)"
[[ "$obsidian" == 1 ]] || fail "$obsidian Obsidian invocations, expected 1"
grep 'obsidian-shots.mjs' <<< "$listing" | grep -q -- '--version 1.14.4' || fail "Obsidian not pinned to 1.14.4"
grep -q -- 'brulion-uimetrics summarize' <<< "$listing" || fail "no metrics summary"

# --app brulion: no Obsidian; --only and --sizes narrow the list.
narrow="$(bash "$here/bench.sh" --dry-run --out "$work/round" --app brulion --only doc,palette --sizes phone)"
[[ "$(grep -c -- '--dump-layout' <<< "$narrow" || true)" == 4 ]] || fail "--only doc,palette --sizes phone: not 4 commands"
grep -q 'obsidian-shots.mjs' <<< "$narrow" && fail "--app brulion still runs Obsidian"
# --app brulion --reuse-obsidian (the loop's rounds): Brulion shot, Obsidian's shots copied, not shot.
reuse="$(bash "$here/bench.sh" --dry-run --out "$work/round" --app brulion --reuse-obsidian "$work/round-00/shots/obsidian")"
[[ "$(grep -c -- '--dump-layout' <<< "$reuse" || true)" == 48 ]] || fail "--reuse-obsidian: not 48 Brulion commands"
grep -q "^cp -a $work/round-00/shots/obsidian/\. $work/round/shots/obsidian/" <<< "$reuse" \
  || fail "--app brulion --reuse-obsidian does not copy the Obsidian shots"
grep -q 'obsidian-shots.mjs' <<< "$reuse" && fail "--reuse-obsidian still shoots Obsidian"
# The realism check: 4 shots under Xvfb with xcb.
gl="$(bash "$here/bench.sh" --dry-run --out "$work/round" --render xcb-gl)"
[[ "$(grep -c -- 'QT_QPA_PLATFORM=xcb' <<< "$gl" || true)" == 4 ]] || fail "--render xcb-gl: not 4 xcb shots"

if bash "$here/bench.sh" --dry-run --out "$work/round" --only no-such-screen >/dev/null 2>&1; then
  fail "an unknown screen in --only was accepted"
fi
if bash "$here/bench.sh" --dry-run --out "$work/round" --sizes tablet >/dev/null 2>&1; then
  fail "an unknown size was accepted"
fi

node --check "$here/obsidian-shots.mjs" || fail "obsidian-shots.mjs does not parse"
# The Node scripts inside bench.sh (the manifest) and lib.sh (screens.json) parse too: a quote in
# them would end the shell's string.
rm -f "$work"/inline-*.js
for f in bench.sh lib.sh; do
  awk -v out="$work/inline-${f%.sh}" '
    /node -e \x27$/ { on = 1; n++; next }
    on && /^\x27/ { on = 0; next }
    on { print > (out "-" n ".js") }' "$here/$f"
done
inline=("$work"/inline-*.js)
[[ ${#inline[@]} -ge 2 && -e "${inline[0]}" ]] || fail "the inline Node scripts were not found"
for js in "${inline[@]}"; do
  ! grep -n "'" "$js" || fail "a quote in an inline Node script ($js) ends the shell's string"
  node --check "$js" || fail "an inline Node script ($js) does not parse"
done
node --check "$here/judge.mjs" || fail "judge.mjs does not parse"
# judge.mjs over three synthetic rounds (votes carried through two rounds, vote files, refusals).
node "$here/judge-test.mjs" "$work/judge" || fail "judge-test.mjs failed"
echo "dryrun-test: ok ($shots Brulion shots)"
