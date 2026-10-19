#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || ! "$1" =~ ^[1-9][0-9]*s$ ]]; then
  echo "Usage: $0 <duration>, example: $0 10s" >&2
  exit 1
fi

DURATION="$1"
PROCESS_REGEX='^/tmp/linux_rt_application/bin/traffic_timing_decision$'

PID="$(pgrep -n -f "$PROCESS_REGEX" || true)"
[[ -n "$PID" ]] || {
  echo "traffic_timing_decision is not running" >&2
  exit 1
}

TARGET_COMM="$(cat "/proc/$PID/comm")"

mapfile -t TIDS < <(
  find "/proc/$PID/task" \
    -mindepth 1 -maxdepth 1 -printf '%f\n' |
    sort -n
)

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$(dirname "$SCRIPT_DIR")/output/ftrace"

DAT_FILE="$OUTPUT_DIR/timing_decision.dat"
TXT_FILE="$OUTPUT_DIR/timing_decision_full.txt"
FILTERED_FILE="$OUTPUT_DIR/timing_decision_process.txt"

mkdir -p "$OUTPUT_DIR"


sudo trace-cmd reset >/dev/null 2>&1 || true

sudo trace-cmd record \
  -p nop \
  -C mono \
  -e sched:sched_waking \
  -e sched:sched_wakeup \
  -e sched:sched_switch \
  -e sched:sched_migrate_task \
  -o "$DAT_FILE" \
  sleep "${DURATION%s}"

# Full system trace
sudo trace-cmd report -t \
  -i "$DAT_FILE" \
  > "$TXT_FILE"

# All Timing Decision threads, including a restarted process
sudo trace-cmd report -t \
  -i "$DAT_FILE" \
  -F "sched/sched_waking:comm == \"$TARGET_COMM\"" \
  -F "sched/sched_wakeup:comm == \"$TARGET_COMM\"" \
  -F "sched/sched_switch:prev_comm == \"$TARGET_COMM\" || next_comm == \"$TARGET_COMM\"" \
  > "$FILTERED_FILE"

echo
echo "Saved: $DAT_FILE"
echo "Saved: $TXT_FILE"
echo "Saved: $FILTERED_FILE"
