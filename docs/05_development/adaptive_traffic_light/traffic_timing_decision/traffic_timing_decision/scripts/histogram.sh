#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage:"
  echo "    $0 <execution_time.txt|wakeup_latency.txt>"
  exit 1
fi

readonly INPUT_FILE="$1"
readonly BUCKET_WIDTH_US="${BUCKET_WIDTH_US:-1}"

if [[ ! -f "$INPUT_FILE" ]]; then
  echo "Error: file not found: $INPUT_FILE" >&2
  exit 1
fi

if [[ ! -r "$INPUT_FILE" ]]; then
  echo "Error: file is not readable: $INPUT_FILE" >&2
  exit 1
fi

if ! command -v gnuplot >/dev/null 2>&1; then
  echo "Error: gnuplot is not installed." >&2
  echo "Install it with: sudo apt install gnuplot" >&2
  exit 1
fi

if [[ ! "$BUCKET_WIDTH_US" =~ ^[1-9][0-9]*$ ]]; then
  echo "Error: BUCKET_WIDTH_US must be a positive integer." >&2
  exit 1
fi

if grep -q "execution_time_us[[:space:]]*=" "$INPUT_FILE"; then
  readonly FIELD_NAME="execution_time_us"
  readonly PLOT_TITLE="Decision Execution Time Histogram"
  readonly X_AXIS_NAME="Execution Time"
  readonly LEGEND_NAME="Execution Time"
elif grep -q "wakeup_latency_us[[:space:]]*=" "$INPUT_FILE"; then
  readonly FIELD_NAME="wakeup_latency_us"
  readonly PLOT_TITLE="Wake-up Latency Histogram"
  readonly X_AXIS_NAME="Wake-up Latency"
  readonly LEGEND_NAME="Wake-up Latency"
else
  echo "Error: no execution_time_us or wakeup_latency_us records found in:" >&2
  echo "       $INPUT_FILE" >&2
  exit 1
fi

readonly INPUT_DIRECTORY="$(dirname "$INPUT_FILE")"
readonly PLOT_DIRECTORY="${INPUT_DIRECTORY}/plots"
readonly INPUT_BASENAME="$(basename "$INPUT_FILE")"
readonly OUTPUT_BASENAME="${INPUT_BASENAME%.*}"
readonly OUTPUT_PNG="${PLOT_DIRECTORY}/${OUTPUT_BASENAME}.png"

mkdir -p "$PLOT_DIRECTORY"

VALUES_FILE="$(mktemp)"
HISTOGRAM_FILE="$(mktemp)"
readonly VALUES_FILE
readonly HISTOGRAM_FILE
trap 'rm -f "$VALUES_FILE" "$HISTOGRAM_FILE"' EXIT

# Extract the integer value following the selected DLT field. This tolerates
# arbitrary spaces around '=' while ignoring all unrelated fields.
awk -v field="$FIELD_NAME" '
  {
    pattern = field "[[:space:]]*=[[:space:]]*[0-9]+"
    if (match($0, pattern)) {
      value = substr($0, RSTART, RLENGTH)
      sub(".*=[[:space:]]*", "", value)
      print value
    }
  }
' "$INPUT_FILE" >"$VALUES_FILE"

if [[ ! -s "$VALUES_FILE" ]]; then
  echo "Error: failed to extract $FIELD_NAME samples from $INPUT_FILE" >&2
  exit 1
fi

read -r SAMPLE_COUNT MIN_VALUE MAX_VALUE < <(
  awk '
    NR == 1 {
      minimum = $1
      maximum = $1
    }
    {
      if ($1 < minimum) {
        minimum = $1
      }
      if ($1 > maximum) {
        maximum = $1
      }
    }
    END {
      print NR, minimum, maximum
    }
  ' "$VALUES_FILE"
)

readonly SAMPLE_COUNT
readonly MIN_VALUE
readonly MAX_VALUE

# Bucket zero starts at the measured minimum. Only non-empty buckets are
# emitted, which is suitable for the logarithmic sample-count axis.
awk -v minimum="$MIN_VALUE" -v width="$BUCKET_WIDTH_US" '
  {
    bucket = int(($1 - minimum) / width)
    count[bucket]++
  }
  END {
    for (bucket in count) {
      print bucket, count[bucket]
    }
  }
' "$VALUES_FILE" | sort -n -k1,1 >"$HISTOGRAM_FILE"

gnuplot <<EOF
set terminal pngcairo size 1600,900 enhanced
set output "${OUTPUT_PNG}"

set title "${PLOT_TITLE}"
set xlabel "${X_AXIS_NAME} (us), Samples = ${SAMPLE_COUNT}, Min = ${MIN_VALUE} us, Max = ${MAX_VALUE} us, Bucket = ${BUCKET_WIDTH_US} us"
set ylabel "Number of Samples"

set xrange [0:*]
set yrange [0.9:*]
set logscale y

set grid xtics ytics
set border linewidth 1
set key top right

plot "${HISTOGRAM_FILE}" using \
(${MIN_VALUE} + (\$1 * ${BUCKET_WIDTH_US})):2 \
with impulses linewidth 1 linecolor rgb "#b000ff" \
title "${LEGEND_NAME}"
EOF

echo "Input    : $INPUT_FILE"
echo "Field    : $FIELD_NAME"
echo "Samples  : $SAMPLE_COUNT"
echo "Range    : $MIN_VALUE..$MAX_VALUE us"
echo "Bucket   : $BUCKET_WIDTH_US us"
echo "Generated: $OUTPUT_PNG"
