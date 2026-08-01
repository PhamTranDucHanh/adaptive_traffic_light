#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <execution_time.txt|wakeup_latency.txt>" >&2
  exit 1
fi

readonly INPUT_FILE="$1"
readonly NANOSECONDS_PER_HOUR="3600000000000"
readonly MICROSECONDS_PER_MILLISECOND="1000"

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

DATA_FILE="$(mktemp)"
PLOT_DATA_FILE="$(mktemp)"
readonly DATA_FILE
readonly PLOT_DATA_FILE
trap 'rm -f "$DATA_FILE" "$PLOT_DATA_FILE"' EXIT

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
' "$INPUT_FILE" >"$DATA_FILE"

if [[ ! -s "$DATA_FILE" ]]; then
  echo "Error: failed to extract timestamp and $FIELD_NAME from $INPUT_FILE" >&2
  exit 1
fi

# Normalize the first sample to 0 hours and convert microseconds to
# milliseconds. No sample is bucketed, filtered, or otherwise discarded.
awk -v ns_per_hour="$NANOSECONDS_PER_HOUR" \
    -v us_per_ms="$MICROSECONDS_PER_MILLISECOND" '
  NR == 1 {first_timestamp = $1}
  {
    elapsed_hours = ($1 - first_timestamp) / ns_per_hour
    latency_ms = $2 / us_per_ms
    printf "%.9f %.6f\n", elapsed_hours, latency_ms
  }
' "$DATA_FILE" >"$PLOT_DATA_FILE"

read -r SAMPLE_COUNT MIN_VALUE MAX_VALUE < <(
  awk '
    NR == 1 {
      minimum = $2
      maximum = $2
    }
    {
      if ($2 < minimum) minimum = $2
      if ($2 > maximum) maximum = $2
    }
    END {
      print NR, minimum, maximum
    }
  ' "$PLOT_DATA_FILE"
)

readonly SAMPLE_COUNT
readonly MIN_VALUE
readonly MAX_VALUE
readonly TOTAL_HOURS_EXACT="$(awk 'END {print $1}' "$PLOT_DATA_FILE")"
readonly TOTAL_HOURS="$(awk 'END {printf "%.3f", $1}' "$PLOT_DATA_FILE")"

gnuplot <<EOF
set terminal pngcairo size 1600,900 enhanced
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
with points pointtype 7 pointsize 0.45 linecolor rgb "${POINT_COLOR}" \
title "${LEGEND_NAME} (${SAMPLE_COUNT} samples, min=${MIN_VALUE} ms, max=${MAX_VALUE} ms)"
EOF

echo "Input     : $INPUT_FILE"
echo "Field     : $FIELD_NAME"
echo "Samples   : $SAMPLE_COUNT"
echo "Duration  : 0..$TOTAL_HOURS hours"
echo "Range     : $MIN_VALUE..$MAX_VALUE ms"
echo "Generated : $OUTPUT_PNG"
