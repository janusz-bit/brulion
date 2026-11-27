#!/usr/bin/env bash
# The compressed Codex rollouts of tst_agentusage, made from rollout.jsonl (synthetic, in the shapes
# Codex 0.170 writes) with zstd from the flake's nixpkgs (1.5.7). Run it again after changing
# rollout.jsonl; the files it writes are committed.
#
#   rollout.jsonl.zst            as Codex compresses a rollout (codex-rs/rollout/src/compression.rs:
#                                level 3, the text's size in the frame, no checksum)
#   rollout-checksum.jsonl.zst   zstd's own default: with a checksum of the text
#   rollout-frames.jsonl.zst     two frames: the first seven lines, then the rest
#   rollout-cut.jsonl.zst        rollout.jsonl.zst cut to half its bytes
#   rollout-garbage.jsonl.zst    the first frame of rollout-frames.jsonl.zst, then 64 bytes of no frame
#   rollout-window.jsonl.zst     a window of 2^26 bytes (--long=26, the text's size not in the frame):
#                                larger than the reader takes (LogStream::MaxWindowLog)
#   rollout-huge-line.jsonl.zst  the first three lines, a line of 5 MiB, then the lines from the
#                                sixth on (a line too long to read, in the middle of the text)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
zstd=$(nix build --no-link --print-out-paths --inputs-from ../../../.. nixpkgs#zstd.bin)/bin/zstd
"$zstd" --version
z() { "$zstd" -q -c "$@"; }

z -3 --no-check rollout.jsonl > rollout.jsonl.zst
z -3 --check rollout.jsonl > rollout-checksum.jsonl.zst
{ head -n 7 rollout.jsonl | z -3 --no-check; tail -n +8 rollout.jsonl | z -3 --no-check; } > rollout-frames.jsonl.zst
head -c "$(( $(stat -c %s rollout.jsonl.zst) / 2 ))" rollout.jsonl.zst > rollout-cut.jsonl.zst
{ head -n 7 rollout.jsonl | z -3 --no-check; printf '\xff%.0s' $(seq 64); } > rollout-garbage.jsonl.zst
z -3 --no-check --long=26 < rollout.jsonl > rollout-window.jsonl.zst
{
  head -n 3 rollout.jsonl
  printf '{"timestamp":"2026-11-24T08:00:02.000Z","type":"response_item","payload":{"type":"function_call_output","output":"'
  head -c $(( 5 * 1024 * 1024 )) /dev/zero | tr '\0' 'a'
  printf '"}}\n'
  tail -n +6 rollout.jsonl
} | z -3 --no-check > rollout-huge-line.jsonl.zst
ls -l ./*.zst
