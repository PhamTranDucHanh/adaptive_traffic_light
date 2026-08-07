#!/usr/bin/env bash

set -euo pipefail

readonly GUARD_CYCLES=10
readonly WINDOW_PADDING_SECONDS="0.020"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly SCRIPT_DIR
readonly MODULE_DIR="$(dirname "$SCRIPT_DIR")"
readonly MODULE_OUTPUT="$MODULE_DIR/output"
readonly WORKSPACE_ROOT="$(git -C "$SCRIPT_DIR" rev-parse --show-toplevel)"
readonly WAKE_LOG="$MODULE_OUTPUT/wakeup_latency.txt"
readonly EXEC_LOG="$MODULE_OUTPUT/execution_time.txt"

usage() {
  echo "Usage: $0 [trace.dat]" >&2
}

if [[ $# -gt 1 ]]; then
  usage
  exit 2
fi

for command in trace-cmd awk sort grep; do
  command -v "$command" >/dev/null 2>&1 || {
    echo "Error: required command not found: $command" >&2
    exit 1
  }
done

for input in "$WAKE_LOG" "$EXEC_LOG"; do
  [[ -r "$input" ]] || {
    echo "Error: input file is not readable: $input" >&2
    exit 1
  }
done

TRACE_FILE="${1:-}"
if [[ -z "$TRACE_FILE" ]]; then
  SEARCH_DIRS=()
  for directory in \
    "$MODULE_OUTPUT/ftrace" \
    "$WORKSPACE_ROOT/traffic_timing_decision/output/ftrace"; do
    [[ -d "$directory" ]] && SEARCH_DIRS+=("$directory")
  done

  ((${#SEARCH_DIRS[@]} != 0)) || {
    echo "Error: no ftrace output directory was found." >&2
    exit 1
  }

  TRACE_FILE="$({
    find "${SEARCH_DIRS[@]}" -maxdepth 1 -type f -name 'timing_*.dat' \
      -printf '%T@\t%p\n'
  } | sort -t$'\t' -k1,1nr | sed -n '1{s/^[^\t]*\t//;p}')"
fi

[[ -n "$TRACE_FILE" && -r "$TRACE_FILE" ]] || {
  echo "Error: trace file is not readable: ${TRACE_FILE:-<not found>}" >&2
  exit 1
}
readonly TRACE_FILE

readonly TRACE_NAME="$(basename "${TRACE_FILE%.dat}")"
readonly EVIDENCE_DIR="$MODULE_OUTPUT/jitter_evidence/$TRACE_NAME"
readonly TOP10_FILE="$EVIDENCE_DIR/top10_jitter.tsv"
readonly TRACE_INFO_FILE="$EVIDENCE_DIR/trace_info.txt"
readonly REPORT_FILE="$EVIDENCE_DIR/top10_jitter_report.txt"

mkdir -p "$EVIDENCE_DIR"

RAW_SAMPLE_COUNT="$(awk '/wakeup_latency_us[[:space:]]*=/ {++count} END {print count + 0}' "$WAKE_LOG")"
readonly RAW_SAMPLE_COUNT
if ((RAW_SAMPLE_COUNT <= GUARD_CYCLES * 2)); then
  echo "Error: not enough samples after excluding boundary cycles." >&2
  exit 1
fi

{
  printf 'cycle_id\tlatency_us\tscheduled_s\tactual_s\tdlt_line\n'
  awk -v guard="$GUARD_CYCLES" -v total="$RAW_SAMPLE_COUNT" '
    /wakeup_latency_us[[:space:]]*=/ {
      ++record
      if (record <= guard || record > total - guard) next

      if (match($0, /cycle_id= *([0-9]+)/, cycle) &&
          match($0, /scheduled_release_ns= *([0-9]+)/, scheduled) &&
          match($0, /actual_wakeup_ns= *([0-9]+)/, actual) &&
          match($0, /wakeup_latency_us= *([0-9]+)/, latency)) {
        printf "%s\t%s\t%.9f\t%.9f\t%d\n", cycle[1], latency[1],
               scheduled[1] / 1000000000, actual[1] / 1000000000, NR
      }
    }
  ' "$WAKE_LOG" | sort -t$'\t' -k2,2nr | sed -n '1,10p'
} > "$TOP10_FILE"

while IFS=$'\t' read -r cycle latency scheduled actual dlt_line; do
  [[ "$cycle" == "cycle_id" ]] && continue
  {
    echo "cycle_id=$cycle"
    echo "wakeup_latency_us=$latency"
    echo "scheduled_release_s=$scheduled"
    echo "actual_wakeup_s=$actual"
    echo
    echo "WAKE-UP RECORD"
    sed -n "${dlt_line}p" "$WAKE_LOG"
    echo
    echo "EXECUTION RECORD"
    awk -v wanted="$cycle" '
      match($0, /cycle_id= *([0-9]+)/, value) && value[1] == wanted {
        print
        exit
      }
    ' "$EXEC_LOG"
  } > "$EVIDENCE_DIR/cycle_${cycle}_dlt.txt"
done < "$TOP10_FILE"

{
  echo "Trace: $TRACE_FILE"
  echo
  trace-cmd report --first-event -i "$TRACE_FILE"
  trace-cmd report --last-event -i "$TRACE_FILE"
  echo
  trace-cmd report --stat -i "$TRACE_FILE"
} > "$TRACE_INFO_FILE"

extract_windows() {
  local cpu="$1"
  local mode="$2"

  trace-cmd report -t -i "$TRACE_FILE" --cpu "$cpu" |
    awk -v top_file="$TOP10_FILE" \
        -v output_dir="$EVIDENCE_DIR" \
        -v cpu="$cpu" \
        -v mode="$mode" \
        -v padding="$WINDOW_PADDING_SECONDS" '
      function event_time(line, captured) {
        if (match(line,
                  /\[[0-9][0-9][0-9]\][[:space:]]+([0-9]+\.[0-9]+):/,
                  captured)) {
          return captured[1] + 0
        }
        return -1
      }

      BEGIN {
        getline header < top_file
        while ((getline row < top_file) > 0) {
          split(row, field, "\t")
          ++count
          cycle[count] = field[1]
          latency[count] = field[2]
          lower[count] = field[3] - padding
          upper[count] = field[4] + padding
          file[count] = output_dir "/cycle_" cycle[count] "_cpu" cpu ".txt"
          printf "cycle_id=%s latency_us=%s window=%.9f..%.9f\n\n",
                 cycle[count], latency[count], lower[count], upper[count] \
              > file[count]
          close(file[count])
        }
        close(top_file)
      }

      {
        timestamp = event_time($0, captured)
        if (timestamp < 0) next

        if (mode == "cpu0" &&
            $0 !~ /traffic_timing_/ &&
            $0 !~ /sched_rt_period_timer/ &&
            $0 !~ /sched_switch:/ &&
            $0 !~ /hrtimer_expire_entry/ &&
            $0 !~ /hrtimer_expire_exit/ &&
            $0 !~ /irq_handler_/ &&
            $0 !~ /softirq_/) next

        for (item = 1; item <= count; ++item) {
          if (timestamp >= lower[item] && timestamp <= upper[item]) {
            print $0 >> file[item]
            close(file[item])
          }
        }
      }
    '
}

echo "Extracting CPU 1 windows..."
extract_windows 1 cpu1
echo "Extracting CPU 0 windows..."
extract_windows 0 cpu0

{
  echo "TOP 10 TIMING DECISION WAKE-UP JITTERS"
  echo "Trace: $TRACE_FILE"
  echo "Guard band: first/last $GUARD_CYCLES cycles excluded"
  echo
  printf '%-8s %-12s %-13s %-13s %-30s %-12s\n' \
    "CYCLE" "LATENCY_US" "SCHEDULED_S" "ACTUAL_S" "CLASSIFICATION" "EXEC_US"

  while IFS=$'\t' read -r cycle latency scheduled actual dlt_line; do
    [[ "$cycle" == "cycle_id" ]] && continue
    cpu0_file="$EVIDENCE_DIR/cycle_${cycle}_cpu0.txt"
    cpu1_file="$EVIDENCE_DIR/cycle_${cycle}_cpu1.txt"
    dlt_file="$EVIDENCE_DIR/cycle_${cycle}_dlt.txt"

    execution_us="$(awk '
      match($0, /execution_time_us= *([0-9]+)/, value) {
        print value[1]
        exit
      }
    ' "$dlt_file")"

    classification="UNCLASSIFIED"
    if grep -Eq 'sched_switch:.*swapper/1:0.*==> traffic_timing_' "$cpu1_file"; then
      if grep -q 'sched_rt_period_timer' "$cpu0_file"; then
        classification="RT_THROTTLING_CONFIRMED"
      else
        classification="RT_THROTTLING_PATTERN"
      fi
    elif grep -Eq 'sched_switch:.*traffic_percept.*==> traffic_timing_' "$cpu1_file"; then
      classification="DIRECT_PREEMPT_DELAY"
    fi

    printf '%-8s %-12s %-13s %-13s %-30s %-12s\n' \
      "$cycle" "$latency" "$scheduled" "$actual" \
      "$classification" "${execution_us:-n/a}"

    echo "  CPU0 timer/wakeup evidence:"
    grep -E 'sched_(waking|wakeup):.*traffic_timing_|sched_rt_period_timer' \
      "$cpu0_file" | sed 's/^/    /' || true
    echo "  CPU1 scheduling evidence:"
    grep 'sched_switch:' "$cpu1_file" |
      grep -E 'traffic_timing_|traffic_percept.*==> swapper/1:0' |
      sed 's/^/    /' || true
    echo
  done < "$TOP10_FILE"

  echo "CLASSIFICATION"
  echo "  RT_THROTTLING_CONFIRMED: FIFO80 was released only after sched_rt_period_timer."
  echo "  RT_THROTTLING_PATTERN  : CPU1 idled, then selected FIFO80 at the same replenishment phase."
  echo "  DIRECT_PREEMPT_DELAY   : CPU1 switched directly from Perception RR70 to Timing FIFO80."
} > "$REPORT_FILE"

cat "$REPORT_FILE"
echo
echo "Evidence saved in: $EVIDENCE_DIR"
