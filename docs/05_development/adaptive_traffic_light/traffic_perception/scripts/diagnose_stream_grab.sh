#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: sudo ./diagnose_stream_grab.sh <perception_pid> [duration_seconds] [output_dir]

Collects syscall timings for every thread in the perception process and, when
available, CPU call stacks with perf. Run for a short interval that contains a
slow cap.grab() event.

Examples:
  sudo traffic_perception/scripts/diagnose_stream_grab.sh 341000 60
  sudo traffic_perception/scripts/diagnose_stream_grab.sh "$(pidof traffic_perception)" 30 /tmp/grab-diag
EOF
}

if (( $# < 1 || $# > 3 )); then
  usage >&2
  exit 1
fi

TARGET_PID="$1"
DURATION_SECONDS="${2:-30}"
OUTPUT_DIR="${3:-traffic_perception/output/grab_diag_$(date +%Y%m%d_%H%M%S)}"

if [[ ! "$TARGET_PID" =~ ^[1-9][0-9]*$ ]] || [[ ! -d "/proc/$TARGET_PID/task" ]]; then
  echo "Error: PID $TARGET_PID is not a running process." >&2
  exit 1
fi
if [[ ! "$DURATION_SECONDS" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: duration must be a positive integer." >&2
  exit 1
fi
if ! command -v strace >/dev/null 2>&1; then
  echo "Error: strace is not installed." >&2
  exit 1
fi

mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath "$OUTPUT_DIR")"
TASKS_FILE="$OUTPUT_DIR/tasks.tsv"
EVENTS_FILE="$OUTPUT_DIR/syscall_events.tsv"
LONG_FILE="$OUTPUT_DIR/long_syscalls.tsv"
SUMMARY_FILE="$OUTPUT_DIR/syscall_summary.tsv"
STRACE_PREFIX="$OUTPUT_DIR/strace"
PERF_DATA="$OUTPUT_DIR/perf.data"
PERF_REPORT="$OUTPUT_DIR/perf_report.txt"

snapshot_tasks() {
  local task_dir tid comm
  for task_dir in "/proc/$TARGET_PID"/task/[0-9]*; do
    [[ -d "$task_dir" ]] || continue
    tid="${task_dir##*/}"
    comm="$(<"$task_dir/comm")"
    printf '%s\t%s\n' "$tid" "$comm"
  done
}

snapshot_tasks > "$TASKS_FILE"

echo "Target   : PID $TARGET_PID ($(tr -d '\n' < "/proc/$TARGET_PID/comm"))"
echo "Duration : ${DURATION_SECONDS}s"
echo "Output   : $OUTPUT_DIR"
echo "Note     : strace perturbs timing; use this run to classify the stall, not for WCET numbers."

# -ff writes one file per TID, making ownership of a long syscall unambiguous.
# The selected calls cover FFmpeg/OpenCV waits, file/device I/O and timer sleeps
# without recording every high-frequency syscall in the system.
timeout --signal=INT --kill-after=5 "${DURATION_SECONDS}s" \
  strace -ff -ttt -T -yy -s 128 \
    -e trace=read,pread64,readv,ioctl,futex,poll,ppoll,select,pselect6,epoll_wait,clock_nanosleep,nanosleep,sched_yield,mmap,mprotect,munmap,madvise \
    -p "$TARGET_PID" -o "$STRACE_PREFIX" &
STRACE_PID=$!

PERF_PID=""
if command -v perf >/dev/null 2>&1 && perf version >/dev/null 2>&1; then
  # Call stacks distinguish CPU-bound avcodec/OpenCV work from time blocked in
  # a syscall. Failure is non-fatal because matching kernel tools may be absent.
  perf record -q -F 199 -g -p "$TARGET_PID" -o "$PERF_DATA" -- \
    sleep "$DURATION_SECONDS" 2>"$OUTPUT_DIR/perf_error.txt" &
  PERF_PID=$!
fi

cleanup() {
  kill -INT "$STRACE_PID" 2>/dev/null || true
  if [[ -n "$PERF_PID" ]]; then
    kill -INT "$PERF_PID" 2>/dev/null || true
  fi
}
trap cleanup INT TERM

wait "$STRACE_PID" || true
if [[ -n "$PERF_PID" ]]; then
  wait "$PERF_PID" || true
fi
trap - INT TERM

# Include helpers that appeared while collection was active and retain the
# latest name for each TID.
if [[ -d "/proc/$TARGET_PID/task" ]]; then
  snapshot_tasks >> "$TASKS_FILE"
fi
awk -F '\t' '{ name[$1]=$2 } END { for (tid in name) print tid "\t" name[tid] }' \
  "$TASKS_FILE" | sort -n > "$TASKS_FILE.tmp"
mv "$TASKS_FILE.tmp" "$TASKS_FILE"

printf 'timestamp_s\ttid\tcomm\tsyscall\tduration_ms\n' > "$EVENTS_FILE"
shopt -s nullglob
STRACE_FILES=("$STRACE_PREFIX".*)
if (( ${#STRACE_FILES[@]} == 0 )); then
  echo "Error: strace produced no per-thread files. Check ptrace permissions." >&2
  exit 1
fi

for trace_file in "${STRACE_FILES[@]}"; do
  tid="${trace_file##*.}"
  comm="$(awk -F '\t' -v tid="$tid" '$1 == tid { print $2; exit }' "$TASKS_FILE")"
  [[ -n "$comm" ]] || comm="unknown"
  awk -v tid="$tid" -v comm="$comm" '
    match($0, /^([0-9]+\.[0-9]+)[[:space:]]+([[:alnum:]_]+)\(/, call) &&
    match($0, /<([0-9]+\.[0-9]+)>$/, elapsed) {
      printf "%s\t%s\t%s\t%s\t%.6f\n",
             call[1], tid, comm, call[2], elapsed[1] * 1000.0
    }
  ' "$trace_file" >> "$EVENTS_FILE"
done

{
  printf 'timestamp_s\ttid\tcomm\tsyscall\tduration_ms\n'
  awk -F '\t' 'NR > 1 && $5 >= 20.0' "$EVENTS_FILE" | sort -t $'\t' -k5,5nr
} > "$LONG_FILE"

{
  printf 'tid\tcomm\tsyscall\tcount\ttotal_ms\tmax_ms\n'
  awk -F '\t' '
    NR > 1 {
      key=$2 FS $3 FS $4
      count[key]++
      total[key]+=$5
      if ($5 > maximum[key]) maximum[key]=$5
    }
    END {
      for (key in count) {
        printf "%s\t%d\t%.3f\t%.3f\n",
               key, count[key], total[key], maximum[key]
      }
    }
  ' "$EVENTS_FILE" | sort -t $'\t' -k6,6nr
} > "$SUMMARY_FILE"

if [[ -s "$PERF_DATA" ]]; then
  perf report --stdio --no-children -i "$PERF_DATA" > "$PERF_REPORT" \
    2>>"$OUTPUT_DIR/perf_error.txt" || true
fi

echo
echo "Longest syscalls (>=20 ms):"
column -t -s $'\t' "$LONG_FILE" | head -21
echo
echo "Per-thread/syscall summary (sorted by maximum):"
column -t -s $'\t' "$SUMMARY_FILE" | head -31
echo
echo "Interpretation:"
echo "  futex/poll long  -> grab waited for an FFmpeg/OpenCV worker or buffer"
echo "  read/pread long  -> file/storage I/O wait"
echo "  ioctl long       -> camera/device/driver wait"
echo "  no long syscall + avcodec high in perf -> userspace decode is CPU-bound"
echo "  no long syscall + missing CPU samples  -> preemption/IRQ/platform stall"
echo
echo "Saved: $LONG_FILE"
echo "Saved: $SUMMARY_FILE"
if [[ -s "$PERF_REPORT" ]]; then
  echo "Saved: $PERF_REPORT"
else
  echo "Perf report unavailable; see $OUTPUT_DIR/perf_error.txt"
fi
