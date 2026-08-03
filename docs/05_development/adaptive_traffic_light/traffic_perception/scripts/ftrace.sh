#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[1-9][0-9]*s$ ]]; then
  echo "Usage: $0 <duration>, example: $0 10s" >&2
  exit 1
fi

DURATION="$1"
TID="$(pgrep -f '^/bazel-bin/pipeline_manager_test$' || true)"
[[ -n "$TID" ]] || { echo "traffic_timing_decision is not running" >&2; exit 1; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$(dirname "$SCRIPT_DIR")/output/ftrace"
DAT_FILE="$OUTPUT_DIR/perception.dat"
TXT_FILE="$OUTPUT_DIR/perception.txt"
mkdir -p "$OUTPUT_DIR"a
echo "Tracing TID=$TID for $DURATION"
sudo trace-cmd record -p nop -C mono \
  -e sched:sched_waking \
  -e sched:sched_wakeup \
  -e sched:sched_switch \
  -o "$DAT_FILE" sleep "${DURATION%s}"
sudo trace-cmd report -i "$DAT_FILE" > "$TXT_FILE"
echo "Saved: $DAT_FILE"
echo "Saved: $TXT_FILE"
 
