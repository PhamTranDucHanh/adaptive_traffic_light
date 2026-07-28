#!/usr/bin/env bash
set -euo pipefail

DURATION="${1:-10}"
[[ "$DURATION" =~ ^[1-9][0-9]*$ ]] || {
  echo "Usage: $0 [duration_seconds], example: $0 10" >&2
  exit 1
}

PERF_BIN="$(find /usr/lib -maxdepth 2 -path '/usr/lib/linux-tools-*/perf' \
  -type f | sort -V | tail -n 1)"
[[ -x "$PERF_BIN" ]] || { echo "perf binary not found" >&2; exit 1; }
PID="$(pgrep -f '^/tmp/linux_rt_application/bin/traffic_timing_decision$' || true)"
[[ -n "$PID" ]] || { echo "traffic_timing_decision is not running" >&2; exit 1; }
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="$(dirname "$SCRIPT_DIR")/output/perf"
mkdir -p "$OUTPUT_DIR"

echo "Measuring PID=$PID for ${DURATION}s with $PERF_BIN"
sudo "$PERF_BIN" stat \
  -e task-clock,context-switches,cpu-migrations,page-faults \
  -p "$PID" -- sleep "$DURATION" 2>&1 | tee "$OUTPUT_DIR/perf_stat.txt"

echo "Recording scheduler events for ${DURATION}s..."
sudo "$PERF_BIN" sched record -a -o "$OUTPUT_DIR/perf_sched.data" \
  -- sleep "$DURATION"
sudo "$PERF_BIN" sched timehist -i "$OUTPUT_DIR/perf_sched.data" \
  --pid "$PID" --wakeups --migrations --with-summary \
  | tee "$OUTPUT_DIR/perf_sched.txt"
echo "Results saved in: $OUTPUT_DIR"
