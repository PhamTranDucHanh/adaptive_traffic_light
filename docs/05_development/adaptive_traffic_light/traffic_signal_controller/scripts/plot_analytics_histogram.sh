#!/bin/bash

# Usage:
#   ./plot_analytics_histogram.sh [metric] [input_file] [output_image]
#
# metric:
#   wakeup
#   execution
#   end_to_end
#   emergency

set -o pipefail

METRIC=${1:-wakeup}

ANALYTICS_LOG_FILE="/tmp/linux_rt_application/logs/CTRL.dlt"
DEFAULT_INPUT_FILE="${ANALYTICS_LOG_FILE}.txt"

RUNTIME_DIRECTORY="/tmp/traffic_signal_controller"
RUNTIME_LOG_DIRECTORY="${RUNTIME_DIRECTORY}/logs"
ANALYTICS_REPORT_FILE="${RUNTIME_LOG_DIRECTORY}/analytics_report.txt"

INPUT_FILE=${2:-$DEFAULT_INPUT_FILE}

case "$METRIC" in
    wakeup)
        EVENT_NAME="FSM_WAKEUP"
        VALUE_KEY="latency_ns"
        TITLE="FSM Wakeup Latency Histogram"
        X_AXIS_NAME="FSM Wakeup Latency"
        LEGEND_NAME="Wakeup Latency"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_LOG_DIRECTORY}/fsm_wakeup_latency_histogram.png"
        ;;
    execution)
        EVENT_NAME="FSM_EXECUTION"
        VALUE_KEY="execution_time_ns"
        TITLE="FSM Execution Time Histogram"
        X_AXIS_NAME="FSM Execution Time"
        LEGEND_NAME="Execution Time"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_LOG_DIRECTORY}/fsm_execution_time_histogram.png"
        ;;
    end_to_end)
        EVENT_NAME="PLAN_PUBLISH_TO_RECEIVE"
        VALUE_KEY="latency_ns"
        TITLE="Plan Publish-to-Receive Latency Histogram"
        X_AXIS_NAME="Plan Publish-to-Receive Latency"
        LEGEND_NAME="Publish-to-Receive Latency"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_LOG_DIRECTORY}/plan_publish_to_receive_latency_histogram.png"
        ;;
    emergency)
        EVENT_NAME="EMERGENCY_RECEIVE_TO_APPLY"
        VALUE_KEY="latency_ns"
        TITLE="Emergency Receive-to-Apply Latency Histogram"
        X_AXIS_NAME="Emergency Receive-to-Apply Latency"
        LEGEND_NAME="Receive-to-Apply Latency"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_LOG_DIRECTORY}/emergency_receive_to_apply_latency_histogram.png"
        ;;
    *)
        echo "Error: Unsupported metric: $METRIC"
        echo "Supported metrics: wakeup, execution, end_to_end, emergency"
        exit 1
        ;;
esac

OUTPUT_IMAGE=${3:-$DEFAULT_OUTPUT_IMAGE}

if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: File not found: $INPUT_FILE"
    exit 1
fi

if ! command -v gnuplot >/dev/null 2>&1; then
    echo "Error: gnuplot is not installed."
    echo "Install it with:"
    echo "  sudo apt update && sudo apt install gnuplot"
    exit 1
fi

mkdir -p "$RUNTIME_LOG_DIRECTORY"
mkdir -p "$(dirname "$OUTPUT_IMAGE")"

RAW_DATA_NS="${RUNTIME_LOG_DIRECTORY}/${METRIC}_samples_ns.dat"
SCALED_DATA="${RUNTIME_LOG_DIRECTORY}/${METRIC}_samples_scaled.dat"
HIST_DATA="${RUNTIME_LOG_DIRECTORY}/${METRIC}_histogram.dat"
GNUPLOT_FILE="${RUNTIME_LOG_DIRECTORY}/plot_${METRIC}_histogram.gnu"

#-------------------------------------------------------
# Extract raw samples in nanoseconds
#-------------------------------------------------------

awk -v event="$EVENT_NAME" -v key="$VALUE_KEY" '
index($0, "event=" event) {
    pattern = key "=[[:space:]]*[0-9]+"

    if (match($0, pattern)) {
        value = substr($0, RSTART, RLENGTH)
        sub(key "=[[:space:]]*", "", value)
        print value
    }
}
' "$INPUT_FILE" | sort -n > "$RAW_DATA_NS"

if [ ! -s "$RAW_DATA_NS" ]; then
    echo "Error: No analytics samples found."
    echo "  Event: $EVENT_NAME"
    echo "  Key  : $VALUE_KEY"
    echo "  Input: $INPUT_FILE"
    exit 1
fi

SAMPLE_COUNT=$(wc -l < "$RAW_DATA_NS")

MIN_NS=$(awk 'NR == 1 {print $1}' "$RAW_DATA_NS")
MAX_NS=$(awk 'END {print $1}' "$RAW_DATA_NS")

MEAN_NS=$(awk '
{
    sum += $1
}
END {
    if (NR > 0) {
        printf "%.9f", sum / NR
    }
}
' "$RAW_DATA_NS")

percentile_from_sorted_file() {
    local percentile="$1"

    awk -v p="$percentile" '
    {
        values[NR] = $1
    }
    END {
        if (NR == 0) {
            exit
        }

        percentile_index = int(p * (NR - 1) + 0.5) + 1

        if (percentile_index < 1) {
            percentile_index = 1
        }

        if (percentile_index > NR) {
            percentile_index = NR
        }

        print values[percentile_index]
    }
    ' "$RAW_DATA_NS"
}

P50_NS=$(percentile_from_sorted_file 0.50)
P90_NS=$(percentile_from_sorted_file 0.90)
P95_NS=$(percentile_from_sorted_file 0.95)
P99_NS=$(percentile_from_sorted_file 0.99)

#-------------------------------------------------------
# Select display unit
#
# Use P95 for unit selection because the execution samples
# can contain rare scheduler/preemption outliers.
#-------------------------------------------------------

if awk -v value="$P95_NS" 'BEGIN {exit !(value < 1000)}'; then
    UNIT="ns"
    SCALE=1
elif awk -v value="$P95_NS" 'BEGIN {exit !(value < 1000000)}'; then
    UNIT="us"
    SCALE=1000
elif awk -v value="$P95_NS" 'BEGIN {exit !(value < 1000000000)}'; then
    UNIT="ms"
    SCALE=1000000
else
    UNIT="s"
    SCALE=1000000000
fi

awk -v scale="$SCALE" '
{
    printf "%.9f\n", $1 / scale
}
' "$RAW_DATA_NS" > "$SCALED_DATA"

convert_value() {
    awk -v value="$1" -v scale="$SCALE" '
    BEGIN {
        printf "%.6f", value / scale
    }
    '
}

MIN_VALUE=$(convert_value "$MIN_NS")
MAX_VALUE=$(convert_value "$MAX_NS")
MEAN_VALUE=$(convert_value "$MEAN_NS")
P50_VALUE=$(convert_value "$P50_NS")
P90_VALUE=$(convert_value "$P90_NS")
P95_VALUE=$(convert_value "$P95_NS")
P99_VALUE=$(convert_value "$P99_NS")

#-------------------------------------------------------
# Choose the main-distribution percentile
#
# If P99 is much larger than P95, treat the upper tail as
# outliers and base the visible histogram on P95.
# Otherwise use P99.
#-------------------------------------------------------

OUTLIER_RATIO=$(awk -v p95="$P95_VALUE" -v p99="$P99_VALUE" '
BEGIN {
    if (p95 <= 0.0) {
        print 1.0
    } else {
        printf "%.6f", p99 / p95
    }
}
')

if awk -v ratio="$OUTLIER_RATIO" 'BEGIN {exit !(ratio > 5.0)}'; then
    RANGE_PERCENTILE="P95"
    RANGE_ANCHOR="$P95_VALUE"
else
    RANGE_PERCENTILE="P99"
    RANGE_ANCHOR="$P99_VALUE"
fi

#-------------------------------------------------------
# Choose a readable bin width
#
# Aim for about 40 bins across the main distribution and
# round the result to a 1, 2, 5, 10 ... step.
#-------------------------------------------------------

RAW_BIN_WIDTH=$(awk -v anchor="$RANGE_ANCHOR" '
BEGIN {
    width = anchor / 40.0

    if (width <= 0.0) {
        width = 1.0
    }

    printf "%.12f", width
}
')

BIN_WIDTH=$(awk -v value="$RAW_BIN_WIDTH" '
function power10(exponent, result, i) {
    result = 1.0

    if (exponent >= 0) {
        for (i = 0; i < exponent; ++i) {
            result *= 10.0
        }
    } else {
        for (i = 0; i > exponent; --i) {
            result /= 10.0
        }
    }

    return result
}

BEGIN {
    if (value <= 0.0) {
        print 1.0
        exit
    }

    exponent = int(log(value) / log(10.0))

    if (value < 1.0 && value < power10(exponent)) {
        exponent--
    }

    magnitude = power10(exponent)
    normalized = value / magnitude

    if (normalized <= 1.0) {
        nice = 1.0
    } else if (normalized <= 2.0) {
        nice = 2.0
    } else if (normalized <= 5.0) {
        nice = 5.0
    } else {
        nice = 10.0
    }

    printf "%.9f", nice * magnitude
}
')

#-------------------------------------------------------
# Build histogram
#-------------------------------------------------------

awk -v bw="$BIN_WIDTH" '
{
    bin_index = int($1 / bw)
    hist[bin_index]++
}
END {
    for (bin_index in hist) {
        printf "%.9f %d\n", bin_index * bw, hist[bin_index]
    }
}
' "$SCALED_DATA" | sort -n > "$HIST_DATA"

#-------------------------------------------------------
# Generate Gnuplot script
#-------------------------------------------------------

cat > "$GNUPLOT_FILE" << EOF
set terminal pngcairo size 1600,900 enhanced
set output "$OUTPUT_IMAGE"

set title "$TITLE"
set xlabel "$X_AXIS_NAME ($UNIT), Samples = $SAMPLE_COUNT, Min = $MIN_VALUE $UNIT, Max = $MAX_VALUE $UNIT, Bucket = $BIN_WIDTH $UNIT"
set ylabel "Number of Samples"

set xrange [0:*]
set yrange [0.9:*]
set logscale y

set grid xtics ytics
set border linewidth 1
set key top right

plot "$HIST_DATA" using 1:2 \
with impulses linewidth 1 linecolor rgb "#b000ff" \
title "$LEGEND_NAME"

EOF

gnuplot "$GNUPLOT_FILE"

if [ $? -ne 0 ]; then
    echo "Error: gnuplot failed."
    exit 1
fi

echo ""
echo "Histogram successfully generated:"
echo "  Metric          : $METRIC"
echo "  Event           : $EVENT_NAME"
echo "  Input           : $INPUT_FILE"
echo "  Output          : $OUTPUT_IMAGE"
echo "  Samples         : $SAMPLE_COUNT"
echo "  Display unit    : $UNIT"
echo "  Bin width       : $BIN_WIDTH $UNIT"
echo "  Minimum         : $MIN_VALUE $UNIT"
echo "  Mean            : $MEAN_VALUE $UNIT"
echo "  P50             : $P50_VALUE $UNIT"
echo "  P90             : $P90_VALUE $UNIT"
echo "  P95             : $P95_VALUE $UNIT"
echo "  P99             : $P99_VALUE $UNIT"
echo "  Maximum         : $MAX_VALUE $UNIT"
echo "  Tail ratio      : P99/P95 = $OUTLIER_RATIO"
echo "  Bin based on    : $RANGE_PERCENTILE"
