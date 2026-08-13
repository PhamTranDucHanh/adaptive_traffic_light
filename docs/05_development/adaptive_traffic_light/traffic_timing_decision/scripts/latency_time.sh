#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <execution_time.txt|wakeup_latency.txt>" >&2
  exit 1
fi

readonly INPUT_FILE="$1"
readonly NANOSECONDS_PER_SECOND="1000000000"
# Plot every raw sample by default so the time-series extrema and sample count
# exactly match the statistical report and histogram. Set BUCKET_SECONDS to a
# positive value only when an averaged overview is explicitly wanted.
readonly BUCKET_SECONDS="${BUCKET_SECONDS:-0}"

if [[ ! -f "$INPUT_FILE" || ! -r "$INPUT_FILE" ]]; then
  echo "Error: input file does not exist or is not readable: $INPUT_FILE" >&2
  exit 1
fi

if ! command -v gnuplot >/dev/null 2>&1; then
  echo "Error: gnuplot is not installed." >&2
  echo "Install it with: sudo apt install gnuplot" >&2
  exit 1
fi

if ! awk -v value="$BUCKET_SECONDS" \
  'BEGIN {exit !(value ~ /^[0-9]+([.][0-9]+)?$/ && value >= 0)}'; then
  echo "Error: BUCKET_SECONDS must be zero or a positive number." >&2
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
# Keep the full run, including startup and shutdown samples.
cp "$RAW_DATA_FILE" "$DATA_FILE"

readonly MAX_RAW_US="$(awk 'NR == 1 || $2 > max {max = $2} END {print max}' "$DATA_FILE")"
if awk -v value="$MAX_RAW_US" 'BEGIN {exit !(value < 1)}'; then
  readonly DISPLAY_UNIT="ns" DISPLAY_SCALE="0.001"
elif awk -v value="$MAX_RAW_US" 'BEGIN {exit !(value < 1000)}'; then
  readonly DISPLAY_UNIT="us" DISPLAY_SCALE="1"
elif awk -v value="$MAX_RAW_US" 'BEGIN {exit !(value < 1000000)}'; then
  readonly DISPLAY_UNIT="ms" DISPLAY_SCALE="1000"
else
  readonly DISPLAY_UNIT="s" DISPLAY_SCALE="1000000"
fi

# Use the common end-to-end CLOCK_MONOTONIC window when the runner supplies it.
# This keeps the X positions and range identical across all module plots.
readonly DATA_FIRST_NS="$(awk 'NR == 1 {print $1; exit}' "$DATA_FILE")"
readonly DATA_LAST_NS="$(awk 'END {print $1}' "$DATA_FILE")"
readonly SESSION_START_NS="${ANALYTICS_TIME_ORIGIN_NS:-$DATA_FIRST_NS}"
readonly SESSION_END_NS="${ANALYTICS_TIME_END_NS:-$DATA_LAST_NS}"

if [[ ! "$SESSION_START_NS" =~ ^[0-9]+$ ||
      ! "$SESSION_END_NS" =~ ^[0-9]+$ ]] ||
   awk -v start="$SESSION_START_NS" -v end="$SESSION_END_NS" \
     'BEGIN {exit !(end < start)}'; then
  echo "Error: invalid shared analytics time window: ${SESSION_START_NS}..${SESSION_END_NS}" >&2
  exit 1
fi

# Group samples into aligned buckets and convert microseconds to the selected
# display unit. Each point is the average of its bucket.
awk -v ns_per_second="$NANOSECONDS_PER_SECOND" \
    -v bucket_seconds="$BUCKET_SECONDS" \
    -v display_scale="$DISPLAY_SCALE" \
    -v session_start_ns="$SESSION_START_NS" '
  function flush_bucket() {
    if (bucket_sample_count == 0) return
    printf "%.9f %.6f\n", elapsed_seconds_sum / bucket_sample_count, \
           latency_display_sum / bucket_sample_count
  }

  NR == 1 && bucket_seconds > 0 {
    current_bucket = int(($1 - session_start_ns) / (bucket_seconds * ns_per_second))
  }
  {
    elapsed_nanoseconds = $1 - session_start_ns
    latency_display = $2 / display_scale

    if (bucket_seconds <= 0) {
      printf "%.9f %.6f\n", elapsed_nanoseconds / ns_per_second, latency_display
      next
    }

    bucket = int(elapsed_nanoseconds / (bucket_seconds * ns_per_second))

    if (bucket != current_bucket) {
      flush_bucket()
      current_bucket = bucket
      elapsed_seconds_sum = 0
      latency_display_sum = 0
      bucket_sample_count = 0
    }

    elapsed_seconds_sum += elapsed_nanoseconds / ns_per_second
    latency_display_sum += latency_display
    ++bucket_sample_count
  }

  END { flush_bucket() }
' "$DATA_FILE" >"$PLOT_DATA_FILE"

read -r SAMPLE_COUNT MIN_VALUE MAX_VALUE < <(
  awk -v display_scale="$DISPLAY_SCALE" '
    NR == 1 {
      minimum = $2 / display_scale
      maximum = $2 / display_scale
    }
    {
      display_value = $2 / display_scale
      if (display_value < minimum) minimum = display_value
      if (display_value > maximum) maximum = display_value
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
if awk -v value="$BUCKET_SECONDS" 'BEGIN {exit !(value == 0)}'; then
  readonly SERIES_DESCRIPTION="raw samples"
else
  readonly SERIES_DESCRIPTION="${BUCKET_SECONDS}s average, ${BUCKET_COUNT} buckets"
fi
readonly TOTAL_SECONDS_EXACT="$(awk -v ns_per_second="$NANOSECONDS_PER_SECOND" \
  -v start="$SESSION_START_NS" -v end="$SESSION_END_NS" \
  'BEGIN {printf "%.9f", (end - start) / ns_per_second}')"

if awk -v seconds="$TOTAL_SECONDS_EXACT" 'BEGIN {exit !(seconds < 7200)}'; then
  readonly X_UNIT="minutes" X_SHORT_UNIT="min" X_SCALE="60"
else
  readonly X_UNIT="hours" X_SHORT_UNIT="h" X_SCALE="3600"
fi

readonly TOTAL_X_EXACT="$(awk -v seconds="$TOTAL_SECONDS_EXACT" -v scale="$X_SCALE" \
  'BEGIN {printf "%.9f", seconds / scale}')"
readonly TOTAL_X="$(printf '%.3f' "$TOTAL_X_EXACT")"
readonly X_MAX="$(awk -v value="$TOTAL_X_EXACT" \
  'BEGIN {printf "%.9f", (value > 0 ? value : 1)}')"

gnuplot <<EOF
set terminal pngcairo size 1800,900 enhanced font "Arial,12"
set output "${OUTPUT_PNG}"

set title "${PLOT_TITLE}"
set xlabel "Elapsed time (${X_UNIT})"
set ylabel "${Y_AXIS_NAME} (${DISPLAY_UNIT})"

set xrange [0:${X_MAX}]
set yrange [0:*]

set grid xtics ytics
set border linewidth 1
set key top right
set format x "%.3g"
set format y "%.4g"

plot "${PLOT_DATA_FILE}" using (\$1/${X_SCALE}):2 \
with linespoints linewidth 1.2 pointtype 7 pointsize 0.5 \
linecolor rgb "${POINT_COLOR}" \
title "${LEGEND_NAME} (${SERIES_DESCRIPTION}; full run; min=${MIN_VALUE} ${DISPLAY_UNIT}, max=${MAX_VALUE} ${DISPLAY_UNIT})"
EOF

echo "Input     : $INPUT_FILE"
echo "Field     : $FIELD_NAME"
echo "Samples   : $SAMPLE_COUNT"
echo "Window    : full run ($RAW_SAMPLE_COUNT raw samples, none excluded)"
echo "Session   : CLOCK_MONOTONIC ${SESSION_START_NS}..${SESSION_END_NS} ns"
if awk -v value="$BUCKET_SECONDS" 'BEGIN {exit !(value == 0)}'; then
  echo "Series    : $BUCKET_COUNT raw samples (no averaging)"
else
  echo "Buckets   : $BUCKET_COUNT (${BUCKET_SECONDS} seconds each)"
fi
echo "X axis    : elapsed $X_UNIT (0..$TOTAL_X $X_SHORT_UNIT)"
echo "Range     : $MIN_VALUE..$MAX_VALUE $DISPLAY_UNIT"
echo "Generated : $OUTPUT_PNG"
