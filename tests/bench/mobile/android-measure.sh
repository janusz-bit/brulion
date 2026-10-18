#!/usr/bin/env bash
# android-measure.sh <command> … — Brulion on the benchmark emulator (emu.sh), run on the laptop.
# Every timing command must run while nothing else loads the host (the caller holds the
# EXCLUSIVE laptop lock); results go to $W/out/<dir>/ (W = $BENCH_MOBILE_DIR, default
# ~/work/brulion-perf-mobile).
#
#   install <tag>                 adb install -r -g $W/apk/<tag>.apk (skipped when already installed)
#   push-vault <local dir> <name> the folder into the app's files/<name> (root adb; owner and SELinux
#                                 label of files/), e.g. the 5 000-note vault of make-bench-vault.py
#   open <args…>                  starts the app once with applicationArguments "<args>" (e.g.
#                                 "--vault /data/user/0/…/files/big5k"; "--demo" seeds the sample
#                                 notebook); waits, stops it. The vault becomes the current one, so
#                                 plain launcher starts open it afterwards.
#   use-vault <path>              makes <path> the current vault in the app's settings file (release
#                                 APKs ignore applicationArguments)
#   startup <dir> <label> <kind> <runs> [settle s]
#                                 launcher starts of the installed APK (no intent extras), one
#                                 discarded warm-up first; kind:
#                                   cold  force-stop + drop the page cache (as after a reboot)
#                                   warm  force-stop, page cache warm (Android's "cold start")
#                                   hot   HOME, then start again (process alive: "hot start")
#                                 per run: am start -W (LaunchState, TotalTime, WaitTime), logcat
#                                 "Displayed", after <settle> s dumpsys meminfo (PSS, RSS, heaps…) and
#                                 the process's CPU time → <dir>/startup-<label>-<kind>.csv
#   probe <dir> <label> <name> <runs> [scenario] [extra env K=V…]
#                                 debuggable measurement APK (android-probe/): runs of the bench
#                                 probe's scenario (startup, open-page, views, idle, …), JSONL pulled
#                                 into <dir>/<name>-<label>-NN.jsonl + <name>-<label>.runs.json
#                                 (tests/bench/app/analyze.py reads them). COLD=1: drop caches first.
#   meminfo <dir> <tag>           dumpsys meminfo of the running app → <dir>/meminfo-<tag>.txt + summary
#   idle <dir> <label> <seconds> [fg|bg]
#                                 the running app left alone: CPU time and context switches per
#                                 thread over <seconds> (/proc), wakeups/s, frames (gfxinfo), its
#                                 alarms (dumpsys alarm) and jobs → <dir>/idle-<label>-<fg|bg>.txt
#   nav <route-json>              opens a route in the running debuggable app (notification-tap extra
#                                 for pages: {"path": "…"}), used before meminfo
set -euo pipefail
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
W=${BENCH_MOBILE_DIR:-$HOME/work/brulion-perf-mobile}
PKG=io.github.januszbit.brulion
ACT=$PKG/$PKG.android.BrulionActivity
FILES=/data/user/0/$PKG/files
emu() { "$here/emu.sh" "$@"; }
adb() { emu adb "$@"; }
sh_() { adb shell "$@" | tr -d '\r'; }
pid_of() { sh_ pidof "$PKG" || true; }
now_ms() { date +%s%3N; }

wait_gone() { # <seconds>
  for _ in $(seq 1 "$1"); do [[ -z $(pid_of) ]] && return 0; sleep 1; done
  return 1
}

parse_meminfo() { # <file> -> "pss_kb rss_kb java_kb native_kb code_kb stack_kb graphics_kb other_kb system_kb"
  awk '
    /TOTAL PSS:/ { for (i = 1; i <= NF; i++) { if ($i == "PSS:") pss = $(i + 1); if ($i == "RSS:") rss = $(i + 1) } }
    /^ *TOTAL +[0-9]/ && !pss { pss = $2 }
    /Java Heap:/ { java = $3 } /Native Heap:/ { native = $3 } /^ *Code:/ { code = $2 }
    /^ *Stack:/ { stack = $2 } /^ *Graphics:/ { gfx = $2 } /Private Other:/ { other = $3 } /^ *System:/ { sys = $2 }
    END { printf "%s %s %s %s %s %s %s %s %s\n", pss, rss, java, native, code, stack, gfx, other, sys }' "$1"
}

cpu_ticks() { # <pid> -> utime+stime (clock ticks) of the whole process
  sh_ cat "/proc/$1/stat" | awk '{ i = index($0, ")"); split(substr($0, i + 2), f, " "); print f[12] + f[13] }'
}

cmd=${1:-}
shift || true
case $cmd in
  install)
    tag=$1
    apk=$W/apk/$tag.apk
    want=$(sha256sum <"$apk" | cut -d' ' -f1)
    have=$(sh_ cat /data/local/tmp/bench-installed 2>/dev/null || true)
    if [[ $have == "$want" ]]; then
      echo "installed: $tag (unchanged)"
      exit 0
    fi
    sh_ am force-stop "$PKG" || true
    t0=$(now_ms)
    adb install -r -g "$apk" >/dev/null
    echo "$want" | adb shell "cat > /data/local/tmp/bench-installed"
    echo "installed: $tag in $(($(now_ms) - t0)) ms ($(stat -c %s "$apk") B)"
    ;;
  push-vault)
    local_dir=$1 name=$2
    sh_ am force-stop "$PKG"
    sh_ rm -rf "$FILES/$name"
    # Owner and SELinux label (with the app's MLS categories) of the app's data directory.
    u=$(sh_ stat -c %u "/data/user/0/$PKG")
    ctx=$(sh_ ls -dZ "/data/user/0/$PKG" | awk '{ print $1 }')
    sh_ mkdir -p "$FILES"
    sh_ chown "$u:$u" "$FILES"
    sh_ chcon "$ctx" "$FILES"
    t0=$(now_ms)
    # Check that nothing arrives empty (a vault pushed without a sync before the emulator was
    # killed came back as 5 000 empty files after the next boot).
    adb push -Z "$local_dir" "$FILES/$name" >/dev/null
    sh_ chown -R "$u:$u" "$FILES/$name"
    sh_ chcon -R "$ctx" "$FILES/$name"
    sh_ sync # on disk before the emulator can be stopped (see emu.sh stop)
    empty_local=$(find "$local_dir" -type f -empty | wc -l)
    empty_device=$(sh_ find "$FILES/$name" -type f -empty | wc -l)
    echo "push-vault: $name: $(sh_ find "$FILES/$name" -name '*.md' | wc -l) notes, $(sh_ du -sk "$FILES/$name" | cut -f1) KB, owner $u, $ctx, $(($(now_ms) - t0)) ms"
    if [[ $empty_device != "$empty_local" ]]; then
      echo "push-vault: $empty_device empty files on the device, $empty_local in $local_dir: the copy failed" >&2
      exit 1
    fi
    ;;
  use-vault)
    # Release APKs ignore applicationArguments: point the saved settings at the vault instead
    # (in place, so the file keeps its owner and SELinux label).
    ini=$FILES/settings/Brulion/Brulion.ini
    sh_ am force-stop "$PKG"
    sh_ "sed 's#^current=.*#current=$1#' '$ini' > /data/local/tmp/bench.ini && cat /data/local/tmp/bench.ini > '$ini'"
    sh_ grep '^current=' "$ini"
    ;;
  open)
    args="$*"
    sh_ am force-stop "$PKG"
    adb logcat -c
    sh_ am start -W -n "$ACT" --es applicationArguments "'$args'" | grep -E "Status|TotalTime" || true
    sleep "${OPEN_WAIT:-25}"
    sh_ am force-stop "$PKG"
    adb logcat -d | grep -E "brulion|Brulion|qml|Qt" | grep -i -E "error|warn|fatal|vault|open" | head -20 || true
    ;;
  startup)
    dir=$W/out/$1 label=$2 kind=$3 runs=$4 settle=${5:-15}
    mkdir -p "$dir"
    csv=$dir/startup-$label-$kind.csv
    echo "run,launch_state,total_ms,wait_ms,displayed_ms,pss_kb,rss_kb,java_kb,native_kb,code_kb,stack_kb,graphics_kb,other_kb,system_kb,cpu_ms,threads,host_load" >"$csv"
    hz=100
    for i in $(seq 0 "$runs"); do
      case $kind in
        cold)
          sh_ am force-stop "$PKG"
          sh_ sync
          sh_ 'echo 3 > /proc/sys/vm/drop_caches'
          sleep 3
          ;;
        warm)
          sh_ am force-stop "$PKG"
          sleep 3
          ;;
        hot)
          if [[ -z $(pid_of) ]]; then
            sh_ am start -W -n "$ACT" >/dev/null
            sleep "$settle"
          fi
          sh_ input keyevent 3
          sleep 3
          ;;
      esac
      adb logcat -c
      out=$(sh_ am start -W -n "$ACT")
      state=$(sed -n 's/^LaunchState: //p' <<<"$out")
      total=$(sed -n 's/^TotalTime: //p' <<<"$out")
      waitt=$(sed -n 's/^WaitTime: //p' <<<"$out")
      sleep "$settle"
      # "Displayed io.github.januszbit.brulion/.android.BrulionActivity for user 0: +1s234ms"
      # (hot starts log no "Displayed")
      disp=$({ adb logcat -d | grep -m1 -E "Displayed $PKG" || true; } | sed -nE 's/.*: \+(([0-9]+)s)?([0-9]+)ms.*/\2 \3/p' |
        awk '{ if (NF == 2) print $1 * 1000 + $2; else print $1 }')
      p=$(pid_of)
      mem=(- - - - - - - - -)
      cpu=- threads=-
      if [[ -n $p ]]; then
        sh_ dumpsys meminfo "$PKG" >"$dir/meminfo-$label-$kind-$i.txt"
        read -r -a mem < <(parse_meminfo "$dir/meminfo-$label-$kind-$i.txt")
        cpu=$(($(cpu_ticks "$p") * 1000 / hz))
        threads=$(sh_ ls "/proc/$p/task" | wc -l)
      fi
      load=$(cut -d' ' -f1 /proc/loadavg)
      tag=$i
      ((i == 0)) && tag=warmup
      echo "$tag,${state:-},${total:-},${waitt:-},${disp:-},$(IFS=,; echo "${mem[*]}"),$cpu,$threads,$load" | tee -a "$csv"
    done
    ;;
  probe)
    dir=$W/out/$1 label=$2 name=$3 runs=$4 scenario=${5:-startup}
    shift 5 || shift $#
    mkdir -p "$dir"
    meta=$dir/$name-$label.runs.json
    entries=()
    for i in $(seq 0 "$runs"); do
      tag=$(printf '%02d' "$i")
      ((i == 0)) && tag=warmup
      remote=$FILES/bench-$name-$label-$tag.jsonl
      sh_ am force-stop "$PKG"
      sh_ rm -f "$remote"
      if [[ ${COLD:-0} == 1 ]]; then
        sh_ sync
        sh_ 'echo 3 > /proc/sys/vm/drop_caches'
      fi
      sleep 3
      extra=(--es extraenvvars_BENCH_OUT "$remote" --es extraenvvars_BENCH_SCENARIO "$scenario")
      for kv in "$@"; do extra+=(--es "extraenvvars_${kv%%=*}" "'${kv#*=}'"); done
      [[ -n ${APP_ARGS:-} ]] && extra+=(--es applicationArguments "'$APP_ARGS'")
      t0=$(now_ms)
      out=$(sh_ am start -W -n "$ACT" "${extra[@]}")
      total=$(sed -n 's/^TotalTime: //p' <<<"$out")
      code=
      for _ in $(seq 1 "${PROBE_TIMEOUT:-300}"); do
        # (adb shell joins its arguments into one command line: no quotes in the pattern)
        if sh_ grep -q 'ev.:.end' "$remote" 2>/dev/null; then
          code=$(sh_ grep 'ev.:.end' "$remote" | sed -nE 's/.*"code":([0-9-]+).*/\1/p')
          break
        fi
        sleep 1
      done
      wall=$(($(now_ms) - t0))
      if [[ ${KEEP_MEMINFO:-0} == 1 && -n $(pid_of) ]]; then
        sh_ dumpsys meminfo "$PKG" >"$dir/meminfo-$name-$label-$tag.txt"
      fi
      adb pull "$remote" "$dir/$name-$label-$tag.jsonl" >/dev/null 2>&1 || echo "probe: no output for run $tag"
      echo "probe: $name $label $tag exit=${code:-timeout} total=${total:-?} ms wall=$wall ms"
      warm=false
      ((i == 0)) && warm=true
      entries+=("{\"run\": \"$tag\", \"file\": \"$name-$label-$tag.jsonl\", \"exit\": ${code:-99}, \"wall_ms\": $wall, \"am_total_ms\": ${total:-null}, \"warmup\": $warm}")
      sh_ am force-stop "$PKG"
    done
    env_json=$(printf '"%s",' "$@")
    {
      echo "{\"name\": \"$name\", \"label\": \"$label\", \"scenario\": \"$scenario\", \"start\": \"$([[ ${COLD:-0} == 1 ]] && echo cold || echo warm)\","
      echo " \"platform\": \"android-emulator\", \"size\": \"1080x2400\", \"route\": null, \"env\": [${env_json%,}],"
      echo " \"bin\": \"$PKG\", \"host\": \"$(hostname)\", \"machine\": \"x86_64 emulator (API $(sh_ getprop ro.build.version.sdk))\","
      echo " \"runs\": ["
      (IFS=$'\n'; echo "${entries[*]}" | sed '$!s/$/,/')
      echo "]}"
    } >"$meta"
    ;;
  meminfo)
    dir=$W/out/$1 tag=$2
    mkdir -p "$dir"
    sh_ dumpsys meminfo "$PKG" >"$dir/meminfo-$tag.txt"
    echo "meminfo $tag: pss rss java native code stack graphics other system (KB): $(parse_meminfo "$dir/meminfo-$tag.txt")"
    ;;
  idle)
    dir=$W/out/$1 label=$2 secs=$3 where=${4:-fg}
    mkdir -p "$dir"
    f=$dir/idle-$label-$where.txt
    p=$(pid_of)
    [[ -n $p ]] || { echo "idle: app not running"; exit 1; }
    if [[ $where == bg ]]; then sh_ input keyevent 3; sleep 5; fi
    snap() { # per thread: tid comm utime stime voluntary nonvoluntary
      adb shell "for t in /proc/$p/task/*; do echo \"\${t##*/} \$(cat \$t/comm | tr ' ' _) \$(cut -d')' -f2 \$t/stat | awk '{print \$12, \$13}') \$(grep -E '^(voluntary|nonvoluntary)_ctxt_switches' \$t/status | awk '{print \$2}' | tr '\n' ' ')\"; done" | tr -d '\r'
    }
    sh_ dumpsys gfxinfo "$PKG" reset >/dev/null
    a=$(snap)
    sleep "$secs"
    b=$(snap)
    frames=$(sh_ dumpsys gfxinfo "$PKG" | sed -n 's/^Total frames rendered: //p' | sed -n 1p)
    {
      echo "# idle $label $where: $secs s, pid $p, frames rendered (HWUI) ${frames:-?}"
      echo "# tid comm cpu_ms vol_switches/s invol_switches/s"
      join <(sort <<<"$a") <(sort <<<"$b") | awk -v s="$secs" '{
        cpu = ($8 + $9 - $3 - $4) * 10; v = ($10 - $5) / s; n = ($11 - $6) / s
        tc += cpu; tv += v; tn += n
        if (cpu > 0 || v > 0.05) printf "%s %s %d %.2f %.2f\n", $1, $2, cpu, v, n
      } END { printf "TOTAL cpu_ms=%d (%.2f %% of one core) wakeups/s=%.2f involuntary/s=%.2f\n", tc, tc / (s * 10), tv, tn }'
      echo "# dumpsys alarm (this package)"
      { sh_ dumpsys alarm | grep -E -A3 "$PKG" || true; } | sed -n "1,60p"
      echo "# dumpsys jobscheduler (this package)"
      { sh_ dumpsys jobscheduler "$PKG" | grep -E -i "job|pending" || true; } | sed -n "1,20p"
    } >"$f"
    grep -E "^# idle|TOTAL" "$f" || true
    if [[ $where == bg ]]; then sh_ am start -n "$ACT" >/dev/null; fi
    ;;
  nav)
    sh_ am start -n "$ACT" --es "$PKG.android.extra.PATH" "'$1'" >/dev/null
    ;;
  *)
    awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"
    exit 2
    ;;
esac
