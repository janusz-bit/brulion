#!/usr/bin/env bash
# Starts a throwaway Trilium server on 127.0.0.1 with an empty data directory, runs the given
# command with TRILIUM_URL pointing at it, then stops the server and deletes the data directory.
#
#   ./run.sh python3 generate.py out                      # current Trilium from nixpkgs
#   LEGACY=1 ./run.sh python3 generate_legacy.py out-063   # zadam/trilium 0.63 (nixos-24.11)
#   TRILIUM_SERVER=/nix/store/...-trilium-server-X ./run.sh ...  # any other build
set -euo pipefail
if [[ -n "${TRILIUM_SERVER:-}" ]]; then
  SERVER=$TRILIUM_SERVER
elif [[ -n "${LEGACY:-}" ]]; then
  SERVER=$(nix build --no-link --print-out-paths --impure --expr \
    '(import (fetchTarball "https://channels.nixos.org/nixos-24.11/nixexprs.tar.xz") {}).trilium-server')
else
  SERVER=$(nix build --no-link --print-out-paths nixpkgs#trilium-next-server)
fi
mkdir -p "$HOME/work/trilium-fixtures"
DATA=$(mktemp -d "$HOME/work/trilium-fixtures/data.XXXXXX")
PORT=${PORT:-37841}
LOG=${LOG:-$PWD/server.log}
export TRILIUM_DATA_DIR=$DATA TRILIUM_NETWORK_HOST=127.0.0.1 TRILIUM_NETWORK_PORT=$PORT \
  TRILIUM_HOST=127.0.0.1 TRILIUM_PORT=$PORT TRILIUM_GENERAL_NOBACKUP=true
"$SERVER/bin/trilium-server" >"$LOG" 2>&1 &
PID=$!
cleanup() { kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; rm -rf "$DATA"; }
trap cleanup EXIT
for _ in $(seq 120); do
  curl -s -o /dev/null "http://127.0.0.1:$PORT/api/setup/status" && break
  sleep 0.5
done
echo "server: $SERVER"
TRILIUM_URL="http://127.0.0.1:$PORT" "$@"
