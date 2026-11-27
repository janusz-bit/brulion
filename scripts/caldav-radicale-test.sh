#!/usr/bin/env bash
# The caldav module against a real CalDAV server: Radicale from nixpkgs on a free port of
# 127.0.0.1, with its own storage and an htpasswd user, a task calendar and an event calendar made
# with MKCALENDAR (one task, a daily stand-up); then tests/core/caldav/tst_caldav_radicale
# (discovery, the first sync, edits both ways, events left in the inbox the way brulion-cli leaves
# them reaching the server and another device) with BRULION_RADICALE_URL / _USER / _PASSWORD. Not part of ctest (it needs nixpkgs'
# Radicale); run it inside `nix develop` after building the test:
#
#   scripts/caldav-radicale-test.sh [build-dir]        # default: build
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
build="${1:-build}"
test_bin="$build/tests/core/caldav/tst_caldav_radicale"
[[ -x $test_bin ]] || { echo "build $test_bin first" >&2; exit 2; }

work=$(mktemp -d)
pid=""
cleanup() {
  [[ -n $pid ]] && kill "$pid" 2>/dev/null || true
  rm -rf "$work"
}
trap cleanup EXIT

port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1])' 2>/dev/null || echo 52320)
user=jan
password='hasło-żółw 1'
mkdir -p "$work/collections"
printf '%s:%s\n' "$user" "$password" > "$work/users"
cat > "$work/config" <<CONF
[server]
hosts = 127.0.0.1:$port
[auth]
type = htpasswd
htpasswd_filename = $work/users
htpasswd_encryption = plain
[storage]
filesystem_folder = $work/collections
[logging]
level = warning
CONF
nix shell nixpkgs#radicale -c radicale --config "$work/config" > "$work/log" 2>&1 &
pid=$!
url="http://127.0.0.1:$port/"
for _ in $(seq 1 100); do
  curl -s -o /dev/null -u "$user:$password" -X PROPFIND -H 'Depth: 0' "$url" && break
  sleep 0.2
done

curl -sf -u "$user:$password" -X MKCALENDAR "${url}${user}/zadania/" -H 'Content-Type: application/xml' \
  --data '<?xml version="1.0"?><C:mkcalendar xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav"><D:set><D:prop><D:displayname>Zadania</D:displayname><C:supported-calendar-component-set><C:comp name="VTODO"/></C:supported-calendar-component-set></D:prop></D:set></C:mkcalendar>' >/dev/null
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nBEGIN:VTODO\r\nUID:ser\r\nDTSTAMP:20261101T000000Z\r\nSUMMARY:Kupić żółty ser\r\nDUE;VALUE=DATE:20261121\r\nEND:VTODO\r\nEND:VCALENDAR\r\n' \
  | curl -sf -u "$user:$password" -X PUT "${url}${user}/zadania/ser.ics" -H 'Content-Type: text/calendar' --data-binary @- >/dev/null

curl -sf -u "$user:$password" -X MKCALENDAR "${url}${user}/spotkania/" -H 'Content-Type: application/xml' \
  --data '<?xml version="1.0"?><C:mkcalendar xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:caldav"><D:set><D:prop><D:displayname>Spotkania</D:displayname><C:supported-calendar-component-set><C:comp name="VEVENT"/></C:supported-calendar-component-set></D:prop></D:set></C:mkcalendar>' >/dev/null
printf 'BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nBEGIN:VEVENT\r\nUID:stand\r\nDTSTAMP:20261101T000000Z\r\nDTSTART;TZID=Europe/Warsaw:20261201T093000\r\nDTEND;TZID=Europe/Warsaw:20261201T094500\r\nRRULE:FREQ=DAILY;COUNT=10\r\nSUMMARY:Stand-up\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n' \
  | curl -sf -u "$user:$password" -X PUT "${url}${user}/spotkania/stand.ics" -H 'Content-Type: text/calendar' --data-binary @- >/dev/null

BRULION_RADICALE_URL="$url" BRULION_RADICALE_USER="$user" BRULION_RADICALE_PASSWORD="$password" \
  QT_QPA_PLATFORM=offscreen "$test_bin" "${@:2}"
