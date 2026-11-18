#!/usr/bin/env bash
# The generated tasks (finetune/generate.py, docs/FINETUNE.md "Generated tasks") are tasks the
# assistant can do: every one done by its own solution, in both splits and both languages, with
# no tool call failing; and the same seed makes the same notebooks and tasks.
#
#   tst_generated.sh <brulion-assistantbench> <python3> <finetune/generate.py>
set -euo pipefail
bench=$1
python=$2
generate=$3
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$python" -I "$generate" --out "$work/train" --split train --count 4 --seed 1
"$python" -I "$generate" --out "$work/test" --split test --count 4 --seed 1001
for split in train test; do
    if ! "$bench" --solution --jobs 4 --tasks "$work/$split/tasks" --notebooks "$work/$split/notebooks" \
        > "$work/$split.txt"; then
        grep -v '^PASS' "$work/$split.txt"
        exit 1
    fi
    if ! grep -q 'failed calls 0 ' "$work/$split.txt"; then
        echo "$split: a solution's tool call failed"
        tail -3 "$work/$split.txt"
        exit 1
    fi
done

"$python" -I "$generate" --out "$work/again" --split train --count 4 --seed 1
diff -r "$work/train" "$work/again"
echo "generated tasks: all done by their solutions"
