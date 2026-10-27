#!/usr/bin/env bash
# Scrolling without jumps (ctest tst_scroll_jumps_qml): the real app, headless, scrolls the journal
# stream — the sample notebook plus generated days of very different lengths — the guide page, Tasks
# and the calendar agenda down to the end and back up with synthetic input: touchpad pixel deltas at
# the desktop size, touch drags at the phone size. The scroll probe (src/app/ui/scrollprobe.h, --probe-check) fails a run
# when the content moves on screen more than the input explains or shifts inside the list — a day or
# a block growing after the list had placed it, a block changing its height under the mouse pointer.
#
# Deterministic: most cases run with the list's cache off (`:0`, --probe-cache 0), so every row is
# created at the edge of the view and a row whose height is not final when it is created moves the
# content on every run. With the cache on (the app as it is), that depends on timing — whether the
# row was made ahead in the cache, where ListView grows the first row upwards, or only when it came
# into view (a loaded machine, a slow phone): a query block whose results came a moment after it
# failed one run in several. Geometry only, no timing thresholds: a slow run scrolls less far in its
# time, it does not jump more — also valid in the sanitizer build (BRULION_TEST_SLOWDOWN > 1: two
# cache-off cases, more time per direction), where it is mostly a memory check of creating and
# destroying delegates while scrolling. Every app run is bounded (timeout) as well as the test.
#
#   tests/app/perf/scroll-jumps.sh <brulion binary> <scratch dir>
#
# Environment: CASES ("<view>:<size>:<script>[:<cache px>]"; views journals, page, tasks, agenda;
# sizes desktop, phone; scripts touchpad, drag, …), DAYS (generated days, default 40), TOLERANCE
# (px, default 3), JOBS (cases run at the same time, default 2: each run is bounded in time, not in
# work, so two at a time halve the test's time; each in its own copy of the prepared home).
set -uo pipefail
bin="$1"
work="$2"
slowdown="${BRULION_TEST_SLOWDOWN:-1}"
days="${DAYS:-40}"
tolerance="${TOLERANCE:-3}"
now="2026-10-14 10:00"
if (( slowdown > 1 )); then
  default_cases="page:desktop:touchpad:0 journals:phone:drag:0"
  max_ms=30000
  wait_ms=5000
else
  default_cases="journals:desktop:touchpad journals:desktop:touchpad:0 journals:phone:drag:0
    page:desktop:touchpad:0 page:phone:drag:0 tasks:phone:drag:0 agenda:phone:drag:0"
  max_ms=20000
  wait_ms=1500
fi
# One run: both directions, the start and the settle time, with room for a slow start.
run_timeout=$(( 2 * max_ms / 1000 + wait_ms / 1000 + 90 ))
read -r -d '' -a cases <<< "${CASES:-$default_cases}"
rm -rf "$work"
mkdir -p "$work"

# One day of the journal: an outline of 1 to 30 blocks — short items, paragraphs that wrap,
# nested lists, tasks, a heading, a quote, a code block, a table, a query, a calculation, an
# embedded section, a picture, an embedded block — deterministic for its number.
day_text() { # day_text <n>
  local n="$1" size i r
  local sizes=(1 2 3 4 6 8 10 14 20 30)
  size="${sizes[$(( (n * 7 + 3) % 10 ))]}"
  (( n % 3 == 0 )) && printf '## Plan dnia\n'
  for (( i = 0; i < size; i++ )); do
    r=$(( (n * 31 + i * 17) % 20 ))
    case "$r" in
      0|1|2) printf -- '- [ ] Zadzwonić do hydraulika w sprawie łazienki %d 📅 2026-10-2%d\n' "$i" $(( i % 9 )) ;;
      3) printf -- '- [x] Zamówić farbę i pędzle do pokoju %d\n' "$i" ;;
      4|5) printf -- '- Rano spotkanie z zespołem, omówiliśmy plan na kolejny tydzień. Zażółć gęślą jaźń: trzeba jeszcze zadzwonić do hydraulika i zamówić farbę do łazienki, po południu spacer nad rzeką, wieczorem czytanie książki o historii Krakowa. Śnieg przyszedł wcześniej niż zwykle, a w ogrodzie zostały jeszcze jabłka do zebrania (%d).\n' "$i" ;;
      6|7) printf -- '- Pomysł na artykuł o [[Synchronizacja]] #pomysł %d\n  - notatki z telefonu i laptopa\n  - porównać Syncthing i Nextcloud\n    - koszty, wygoda\n' "$i" ;;
      8) printf -- '- > Cytat dnia: łąka, gwiazdy i pociąg do Gdańska %d\n' "$i" ;;
      9) printf -- '- ```python\n  def suma(a, b):\n      return a + b  # %d\n  ```\n' "$i" ;;
      10) printf -- '- | Rzecz | Ilość |\n  |---|---:|\n  | farba | %d |\n  | pędzle | 2 |\n' "$i" ;;
      # Blocks whose content comes from elsewhere (they used to grow after they were created).
      11) printf -- '- ```query\n  type:tasks status:open sort:due\n  ```\n' ;;
      12) printf -- '- ```calc\n  cena = 12,50\n  cena * %d\n  20%% z 150\n  ```\n' "$(( i + 2 ))" ;;
      13) printf -- '- Przepis na weekend: ![[Przepisy#Szarlotka babci]]\n' ;;
      14) printf -- '- Plan: ![[plan-mieszkania.svg]]\n' ;;
      15) printf -- '- Budżet: !((budzet1))\n' ;;
      *) printf -- '- Krótka notatka numer %d: kawa, rower, żurek i pierogi.\n' "$i" ;;
    esac
  done
}

home="$work/home"
vault="$home/Przykładowy brulion"
export XDG_CONFIG_HOME="$home/config" XDG_DATA_HOME="$home/data" XDG_CACHE_HOME="$home/cache" XDG_STATE_HOME="$home/state"
# A first start seeds the sample notebook (and indexes it), like a user's first run.
if ! timeout -k 10 "$run_timeout" "$bin" --demo --vault "$vault" --size 800x600 --now "$now" \
  --no-single-instance --delay 1500 \
  --screenshot "$work/prep.png" > "$work/prep.log" 2>&1; then
  echo "FAIL: the app did not start"
  cat "$work/prep.log"
  exit 1
fi
mkdir -p "$vault/journals"
for (( n = 3; n < days + 3; n++ )); do
  file="$vault/journals/$(date -d "2026-10-14 - $n days" +%F).md"
  [[ -e "$file" ]] || day_text "$n" > "$file" # (the sample notebook's own days stay)
done

# One case: its result lines in <work>/<name>.result.
case_name() { # case_name <case>
  local view size script cache
  IFS=: read -r view size script cache <<< "$1"
  printf '%s-%s-%s%s' "$view" "$size" "$script" "${cache:+-cache$cache}"
}
run_case() { # run_case <case>
  local c="$1" view size script cache name geometry route cache_args own
  IFS=: read -r view size script cache <<< "$c"
  case "$size" in
    desktop) geometry=(--size 1280x800) ;;
    phone) geometry=(--size 412x915 --touch) ;;
  esac
  case "$view" in
    journals) route=journals ;;
    page) route='{"view":"page","path":"Witaj w Brulionie.md"}' ;;
    tasks) route=tasks ;;
    agenda) route='{"view":"calendar","mode":"agenda"}' ;;
  esac
  name="$(case_name "$c")"
  cache_args=()
  [[ -n "${cache:-}" ]] && cache_args=(--probe-cache "$cache")
  # Its own copy of the home (settings, caches, the notebook): runs at the same time share nothing.
  own="$work/home-$name"
  rm -rf "$own"
  cp -a "$home" "$own"
  # Down to the end and back up, each direction at most max_ms.
  if XDG_CONFIG_HOME="$own/config" XDG_DATA_HOME="$own/data" XDG_CACHE_HOME="$own/cache" XDG_STATE_HOME="$own/state" \
    timeout -k 10 "$run_timeout" "$bin" --vault "$own/Przykładowy brulion" --route "$route" "${geometry[@]}" --now "$now" \
    --locale pl --no-single-instance \
    --scroll-probe auto --scroll-script "$script" --probe-speed 3000 --probe-max "$max_ms" \
    --probe-wait "$wait_ms" --probe-check "$tolerance" ${cache_args[@]+"${cache_args[@]}"} --probe-out "$work/$name.csv" \
    > "$work/$name.log" 2>&1; then
    echo "ok: $name — $(grep -E '^scroll-probe: CHECK' "$work/$name.log" | cut -c15-)" > "$work/$name.result"
  else
    {
      echo "FAIL: $name"
      grep -E "^scroll-probe: (target|phase|CHECK|JUMPS|VIEW|THUMB|  jump)" "$work/$name.log" | head -30
      tail -5 "$work/$name.log"
    } > "$work/$name.result"
  fi
  rm -rf "$own"
}

jobs_max="${JOBS:-2}"
for c in "${cases[@]}"; do
  while (( $(jobs -rp | wc -l) >= jobs_max )); do wait -n || true; done
  run_case "$c" &
done
wait
failed=0
for c in "${cases[@]}"; do
  result="$work/$(case_name "$c").result"
  if [[ -f "$result" ]]; then cat "$result"; else echo "FAIL: $(case_name "$c") (no result)"; fi
  grep -q '^ok: ' "$result" 2>/dev/null || failed=$((failed + 1))
done
exit "$failed"
