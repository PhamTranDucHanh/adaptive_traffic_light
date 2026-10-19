#!/usr/bin/env bash
set -euo pipefail

if [[ $# -gt 1 || ( $# -eq 1 && ! "$1" =~ ^[1-9][0-9]*s$ ) ]]; then
  echo "Usage: $0 [duration], example: $0 or $0 10s" >&2
  exit 1
fi

DURATION="${1:-}"
PROCESS_REGEX='^/tmp/linux_rt_application/bin/traffic_signal_controller$'

PID="$(pgrep -n -f "$PROCESS_REGEX" || true)"
[[ -n "$PID" ]] || {
  echo "traffic_signal_controller is not running" >&2
  exit 1
}

TARGET_COMM="$(cat "/proc/$PID/comm")"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$(dirname "$SCRIPT_DIR")/output/ftrace"

DAT_FILE="$OUTPUT_DIR/signal_controller.dat"
TXT_FILE="$OUTPUT_DIR/signal_controller_full.txt"
FILTERED_FILE="$OUTPUT_DIR/signal_controller_process.txt"

mkdir -p "$OUTPUT_DIR"

sudo trace-cmd reset >/dev/null 2>&1 || true

TRACE_CMD=(
  sudo trace-cmd record
  -p nop
  -C mono
  -b 65536
  -e sched:sched_switch
  -e sched:sched_waking
  -e sched:sched_wakeup
  -e sched:sched_migrate_task
  -e timer:hrtimer_start
  -e timer:hrtimer_cancel
  -e timer:hrtimer_expire_entry
  -e timer:hrtimer_expire_exit
  -e irq:irq_handler_entry
  -e irq:irq_handler_exit
  -e irq:softirq_raise
  -e irq:softirq_entry
  -e irq:softirq_exit
  -e power:cpu_idle
  -o "$DAT_FILE"
)

if [[ -n "$DURATION" ]]; then
  "${TRACE_CMD[@]}" sleep "${DURATION%s}"
else
  "${TRACE_CMD[@]}"
fi

# Full system trace.
sudo trace-cmd report -t \
  -i "$DAT_FILE" \
  > "$TXT_FILE"

# All Signal Controller threads, including a restarted process.
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
