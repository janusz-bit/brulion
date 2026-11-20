#!/usr/bin/env bash
# brulion-cli mcp serve against the official MCP Python SDK (docs/CLI.md "Remote MCP server"): a
# temporary notebook and password, the server on a free port, then scripts/mcp-remote-interop.py
# — the SDK's OAuth client (discovery, dynamic client registration, PKCE, iss) and Streamable HTTP
# in both eras, with this script signing in as the owner. The SDK comes from the flake's nixpkgs
# (python3Packages.mcp), so the first run needs the network; that is why it is no ctest.
#
#   scripts/mcp-remote-interop.sh [path/to/brulion-cli]     # default: build-clang's, else PATH's
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cli=${1:-}
if [[ -z $cli ]]; then
  for candidate in "$root/build-clang/src/cli/brulion-cli" "$root/build/src/cli/brulion-cli"; do
    [[ -x $candidate ]] && cli=$candidate && break
  done
  cli=${cli:-$(command -v brulion-cli || true)}
fi
[[ -x $cli ]] || { echo "mcp-remote-interop: no brulion-cli (give its path)" >&2; exit 2; }

nixpkgs=$(nix eval --raw --inputs-from "$root" nixpkgs#path)
python=$(nix build --no-link --print-out-paths --impure \
  --expr "(import $nixpkgs { }).python3.withPackages (ps: [ ps.mcp ])")/bin/python3

work=$(mktemp -d)
server=
cleanup() {
  [[ -n $server ]] && kill "$server" 2>/dev/null && wait "$server" 2>/dev/null
  rm -rf "$work"
}
trap cleanup EXIT
export HOME=$work/home XDG_CONFIG_HOME=$work/home/.config XDG_DATA_HOME=$work/home/.local/share
mkdir -p "$HOME"
"$cli" init "$work/Notatki" >/dev/null
printf -- '- żółw błotny śpi w ogrodzie\n' >"$work/Notatki/Ogród.md"
password='Hasło interop 123'
printf '%s\n' "$password" | "$cli" --vault "$work/Notatki" --stdin mcp password >/dev/null
"$cli" --vault "$work/Notatki" mcp serve --port 0 >"$work/out" 2>"$work/log" &
server=$!
for _ in $(seq 100); do [[ -s $work/out ]] && break; sleep 0.1; done
endpoint=$("$python" -I -c 'import json,sys; print(json.load(open(sys.argv[1]))["result"]["endpoint"])' "$work/out")
echo "mcp-remote-interop: $endpoint ($("$python" -I -c 'import mcp, importlib.metadata as m; print("mcp", m.version("mcp"))'))"
if ! "$python" -I "$root/scripts/mcp-remote-interop.py" "$endpoint" "$password" "$work/Notatki"; then
  echo "--- server log" >&2
  cat "$work/log" >&2
  exit 1
fi
