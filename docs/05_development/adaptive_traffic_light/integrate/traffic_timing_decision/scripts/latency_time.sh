#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <execution_time.txt|wakeup_latency.txt>" >&2
  exit 1
fi

readonly INPUT_FILE="$1"
readonly NANOSECONDS_PER_HOUR="3600000000000"
readonly NANOSECONDS_PER_SECOND="1000000000"
readonly MICROSECONDS_PER_MILLISECOND="1000"
readonly BUCKET_SECONDS="30"
readonly BOUNDARY_CYCLES="15"

if [[ ! -f "$INPUT_FILE" || ! -r "$INPUT_FILE" ]]; then
  echo "Error: input file does not exist or is not readable: $INPUT_FILE" >&2
  exit 1
fi

if ! command -v gnuplot >/dev/null 2>&1; then
  echo "Error: gnuplot is not installed." >&2
  echo "Install it with: sudo apt install gnuplot" >&2
  exit 1
fi

if grep -q "wakeup_latency_us[[:space:]]*=" "$INPUT_FILE"; then
  readonly FIELD_NAME="wakeup_latency_us"
  readonly TIME_FIELD_NAME="actual_wakeup_ns"
  readonly PLOT_TITLE="Wake-up Latency over Time"
  readonly Y_AXIS_NAME="Wake-up Latency"
  readonly LEGEND_NAME="Wake-up Latency"
  readonly POINT_COLOR="#0066cc"
elif grep -q "execution_time_us[[:space:]]*=" "$INPUT_FILE"; then
  readonly FIELD_NAME="execution_time_us"
  readonly TIME_FIELD_NAME="execution_start_ns"
  readonly PLOT_TITLE="Decision Execution Time over Time"
  readonly Y_AXIS_NAME="Execution Time"
  readonly LEGEND_NAME="Execution Time"
  readonly POINT_COLOR="#b000ff"
else
  echo "Error: no wakeup_latency_us or execution_time_us records found in:" >&2
  echo "       $INPUT_FILE" >&2
  exit 1
fi

readonly INPUT_DIRECTORY="$(dirname "$INPUT_FILE")"
readonly INPUT_BASENAME="$(basename "$INPUT_FILE")"
readonly OUTPUT_BASENAME="${INPUT_BASENAME%.*}"
readonly PLOT_DIRECTORY="${INPUT_DIRECTORY}/plots"
readonly OUTPUT_PNG="${PLOT_DIRECTORY}/${OUTPUT_BASENAME}_by_time.png"

mkdir -p "$PLOT_DIRECTORY"

RAW_DATA_FILE="$(mktemp)"
DATA_FILE="$(mktemp)"
PLOT_DATA_FILE="$(mktemp)"
readonly RAW_DATA_FILE
readonly DATA_FILE
readonly PLOT_DATA_FILE
trap 'rm -f "$RAW_DATA_FILE" "$DATA_FILE" "$PLOT_DATA_FILE"' EXIT

# Extract the monotonic event timestamp and selected metric from every record.
# CLOCK_MONOTONIC is used so elapsed time is unaffected by wall-clock changes.
awk -v time_field="$TIME_FIELD_NAME" -v value_field="$FIELD_NAME" '
  {
    time_pattern = time_field "[[:space:]]*=[[:space:]]*[0-9]+"
    value_pattern = value_field "[[:space:]]*=[[:space:]]*[0-9]+"

    if (match($0, time_pattern)) {
      timestamp = substr($0, RSTART, RLENGTH)
      sub(".*=[[:space:]]*", "", timestamp)
    } else {
      next
    }

    if (match($0, value_pattern)) {
      value = substr($0, RSTART, RLENGTH)
      sub(".*=[[:space:]]*", "", value)
      print timestamp, value
    }
  }
' "$INPUT_FILE" >"$RAW_DATA_FILE"

if [[ ! -s "$RAW_DATA_FILE" ]]; then
  echo "Error: failed to extract timestamp and $FIELD_NAME from $INPUT_FILE" >&2
  exit 1
fi

readonly RAW_SAMPLE_COUNT="$(wc -l < "$RAW_DATA_FILE")"
if (( RAW_SAMPLE_COUNT <= BOUNDARY_CYCLES * 2 )); then
  echo "Error: need more than $((BOUNDARY_CYCLES * 2)) samples to exclude startup and shutdown guard bands." >&2
  exit 1
fi

# The periodic application is not yet in steady state immediately after
# activation, and coordinated shutdown can deschedule its CPU before the stop
# token reaches this process. Exclude only these fixed boundary windows; all
# samples occurring during the steady-state run, including outliers, remain.
awk -v boundary="$BOUNDARY_CYCLES" -v total="$RAW_SAMPLE_COUNT" '
  NR > boundary && NR <= total - boundary
' "$RAW_DATA_FILE" >"$DATA_FILE"

# Normalize the first sample to 0 hours, group samples into 30-second time
# buckets, and convert microseconds to milliseconds. Each plotted point is the
# average of all retained samples in its bucket.
awk -v ns_per_hour="$NANOSECONDS_PER_HOUR" \
    -v ns_per_second="$NANOSECONDS_PER_SECOND" \
    -v bucket_seconds="$BUCKET_SECONDS" \
    -v us_per_ms="$MICROSECONDS_PER_MILLISECOND" '
  function flush_bucket() {
    if (bucket_sample_count == 0) return
    printf "%.9f %.6f\n", elapsed_hours_sum / bucket_sample_count, \
           latency_ms_sum / bucket_sample_count
  }

  NR == 1 {
    first_timestamp = $1
    current_bucket = 0
  }
  {
    elapsed_nanoseconds = $1 - first_timestamp
    bucket = int(elapsed_nanoseconds / (bucket_seconds * ns_per_second))
    latency_ms = $2 / us_per_ms

    if (bucket != current_bucket) {
      flush_bucket()
      current_bucket = bucket
      elapsed_hours_sum = 0
      latency_ms_sum = 0
      bucket_sample_count = 0
    }

    elapsed_hours_sum += elapsed_nanoseconds / ns_per_hour
    latency_ms_sum += latency_ms
    ++bucket_sample_count
  }

  END { flush_bucket() }
' "$DATA_FILE" >"$PLOT_DATA_FILE"

read -r SAMPLE_COUNT MIN_VALUE MAX_VALUE < <(
  awk -v us_per_ms="$MICROSECONDS_PER_MILLISECOND" '
    NR == 1 {
      minimum = $2 / us_per_ms
      maximum = $2 / us_per_ms
    }
    {
      value_ms = $2 / us_per_ms
      if (value_ms < minimum) minimum = value_ms
      if (value_ms > maximum) maximum = value_ms
    }
    END {
      printf "%d %.6f %.6f\n", NR, minimum, maximum
    }
  ' "$DATA_FILE"
)

readonly SAMPLE_COUNT
readonly MIN_VALUE
readonly MAX_VALUE
readonly BUCKET_COUNT="$(wc -l < "$PLOT_DATA_FILE")"
readonly TOTAL_HOURS_EXACT="$(awk -v ns_per_hour="$NANOSECONDS_PER_HOUR" '
  NR == 1 { first_timestamp = $1 }
  END { printf "%.9f", ($1 - first_timestamp) / ns_per_hour }
' "$DATA_FILE")"
readonly TOTAL_HOURS="$(printf '%.3f' "$TOTAL_HOURS_EXACT")"

gnuplot <<EOF
set terminal pngcairo size 1800,900 enhanced
set output "${OUTPUT_PNG}"

set title "${PLOT_TITLE}"
set xlabel "Elapsed Time (hours), Total = ${TOTAL_HOURS} h"
set ylabel "${Y_AXIS_NAME} (ms)"

set xrange [0:${TOTAL_HOURS_EXACT}]
set yrange [0:*]

set grid xtics ytics
set border linewidth 1
set key top right

plot "${PLOT_DATA_FILE}" using 1:2 \
with linespoints linewidth 1.2 pointtype 7 pointsize 0.5 \
linecolor rgb "${POINT_COLOR}" \
title "${LEGEND_NAME} (${BUCKET_SECONDS}s average, ${BUCKET_COUNT} buckets; first/last ${BOUNDARY_CYCLES} cycles excluded; raw min=${MIN_VALUE} ms, raw max=${MAX_VALUE} ms)"
EOF

echo "Input     : $INPUT_FILE"
echo "Field     : $FIELD_NAME"
echo "Samples   : $SAMPLE_COUNT"
echo "Excluded  : first $BOUNDARY_CYCLES + last $BOUNDARY_CYCLES cycles ($RAW_SAMPLE_COUNT raw samples)"
echo "Buckets   : $BUCKET_COUNT (${BUCKET_SECONDS} seconds each)"
echo "Duration  : 0..$TOTAL_HOURS hours"
echo "Range     : $MIN_VALUE..$MAX_VALUE ms"
echo "Generated : $OUTPUT_PNG"
