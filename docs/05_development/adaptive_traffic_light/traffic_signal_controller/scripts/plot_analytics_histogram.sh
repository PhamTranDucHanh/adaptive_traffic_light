#!/bin/bash

# Usage:
#   ./plot_analytics_histogram.sh [metric] [input_file] [output_image]
#
# metric:
#   wakeup
#   execution
#   end_to_end
#   perception_to_controller
#   emergency

set -o pipefail

METRIC=${1:-wakeup}

ANALYTICS_LOG_FILE="/tmp/linux_rt_application/logs/CTRL.dlt"
DEFAULT_INPUT_FILE="${ANALYTICS_LOG_FILE}.txt"

RUNTIME_DIRECTORY="/tmp/traffic_signal_controller"
RUNTIME_LOG_DIRECTORY="${RUNTIME_DIRECTORY}/logs"
RUNTIME_PLOT_DIRECTORY="${RUNTIME_LOG_DIRECTORY}/plots"
ANALYTICS_REPORT_FILE="${RUNTIME_LOG_DIRECTORY}/analytics_report.txt"

INPUT_FILE=${2:-$DEFAULT_INPUT_FILE}

case "$METRIC" in
    wakeup)
        EVENT_NAME="FSM_WAKEUP"
        VALUE_KEY="latency_ns"
        TITLE="FSM Wakeup Latency Histogram"
        X_AXIS_NAME="FSM Wakeup Latency"
        LEGEND_NAME="Wakeup Latency"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_PLOT_DIRECTORY}/ctrl_wakeup_histogram.png"
        ;;
    execution)
        EVENT_NAME="FSM_EXECUTION"
        VALUE_KEY="execution_time_ns"
        TITLE="FSM Execution Time Histogram"
        X_AXIS_NAME="FSM Execution Time"
        LEGEND_NAME="Execution Time"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_PLOT_DIRECTORY}/ctrl_execution_histogram.png"
        ;;
    end_to_end)
        EVENT_NAME="TIMING_DECISION_RECEIVE_TO_CONTROLLER_RECEIVE"
        VALUE_KEY="latency_ns"
        TITLE="Timing Decision Receive-to-Controller Receive Latency Histogram"
        X_AXIS_NAME="Decision-to-Controller Latency"
        LEGEND_NAME="Decision-to-Controller Latency"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_PLOT_DIRECTORY}/deci_to_ctrl_histogram.png"
        ;;
    perception_to_controller)
        EVENT_NAME="PERCEPTION_PUBLISH_TO_CONTROLLER_RECEIVE"
        VALUE_KEY="latency_ns"
        TITLE="Perception Publish-to-Controller Receive Latency Histogram"
        X_AXIS_NAME="Perception-to-Controller Latency"
        LEGEND_NAME="Perception-to-Controller Latency"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_PLOT_DIRECTORY}/perc_to_ctrl_histogram.png"
        ;;
    emergency)
        EVENT_NAME="EMERGENCY_RECEIVE_TO_APPLY"
        VALUE_KEY="latency_ns"
        TITLE="Emergency Receive-to-Apply Latency Histogram"
        X_AXIS_NAME="Emergency Receive-to-Apply Latency"
        LEGEND_NAME="Receive-to-Apply Latency"
        DEFAULT_OUTPUT_IMAGE="${RUNTIME_PLOT_DIRECTORY}/ctrl_emergency_histogram.png"
        ;;
    *)
        echo "Error: Unsupported metric: $METRIC"
        echo "Supported metrics: wakeup, execution, end_to_end, perception_to_controller, emergency"
        exit 1
        ;;
esac

# Match the Timing Decision histogram's 1 us resolution for short real-time
# metrics. The Perception-to-Controller path spans seconds, so a 1 ms default
# keeps the same presentation readable. BUCKET_WIDTH_NS can override either.
if [[ -z "${BUCKET_WIDTH_NS:-}" ]]; then
    if [[ "$METRIC" == "perception_to_controller" ]]; then
        BUCKET_WIDTH_NS=1000000
    else
        BUCKET_WIDTH_NS=1000
    fi
fi

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

if [[ ! "$BUCKET_WIDTH_NS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Error: BUCKET_WIDTH_NS must be a positive integer."
    exit 1
fi

mkdir -p "$RUNTIME_LOG_DIRECTORY"
mkdir -p "$(dirname "$OUTPUT_IMAGE")"

RAW_DATA_NS="${RUNTIME_LOG_DIRECTORY}/${METRIC}_samples_ns.dat"
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
# Use Max for unit selection so plot labels and ticks do not show values
# above 1000 in a smaller unit such as microseconds.
#-------------------------------------------------------

if awk -v value="$MAX_NS" 'BEGIN {exit !(value < 1000)}'; then
    UNIT="ns"
    SCALE=1
elif awk -v value="$MAX_NS" 'BEGIN {exit !(value < 1000000)}'; then
    UNIT="us"
    SCALE=1000
elif awk -v value="$MAX_NS" 'BEGIN {exit !(value < 1000000000)}'; then
    UNIT="ms"
    SCALE=1000000
else
    UNIT="s"
    SCALE=1000000000
fi

convert_value() {
    awk -v value="$1" -v scale="$SCALE" '
    BEGIN {
        printf "%.6f", value / scale
    }
    '
}

format_value() {
    awk -v value="$1" '
    function trim_number(text) {
        sub(/0+$/, "", text)
        sub(/[.]$/, "", text)
        if (text == "" || text == "-0") {
            text = "0"
        }
        return text
    }
    BEGIN {
        absolute_value = value < 0 ? -value : value

        if (absolute_value == 0.0) {
            print "0"
        } else if (absolute_value < 0.001) {
            printf "%s", trim_number(sprintf("%.6f", value))
        } else {
            printf "%s", trim_number(sprintf("%.3f", value))
        }
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

MIN_LABEL=$(format_value "$MIN_VALUE")
MAX_LABEL=$(format_value "$MAX_VALUE")
MEAN_LABEL=$(format_value "$MEAN_VALUE")
P50_LABEL=$(format_value "$P50_VALUE")
P90_LABEL=$(format_value "$P90_VALUE")
P95_LABEL=$(format_value "$P95_VALUE")
P99_LABEL=$(format_value "$P99_VALUE")

#-------------------------------------------------------
# Use the same histogram layout as Timing Decision:
#   - fixed 1 us bucket by default
#   - first bucket anchored at the measured minimum
#   - only non-empty buckets emitted
#
# BUCKET_WIDTH_NS remains configurable without changing the
# extracted analytics samples or their statistics.
#-------------------------------------------------------

BIN_WIDTH=$(convert_value "$BUCKET_WIDTH_NS")
BIN_WIDTH_LABEL=$(format_value "$BIN_WIDTH")

#-------------------------------------------------------
# Build histogram
#-------------------------------------------------------

awk -v minimum="$MIN_NS" -v width="$BUCKET_WIDTH_NS" '
{
    bin_index = int(($1 - minimum) / width)
    hist[bin_index]++
}
END {
    for (bin_index in hist) {
        print bin_index, hist[bin_index]
    }
}
' "$RAW_DATA_NS" | sort -n -k1,1 > "$HIST_DATA"

#-------------------------------------------------------
# Generate Gnuplot script
#-------------------------------------------------------

cat > "$GNUPLOT_FILE" << EOF
set terminal pngcairo size 1600,900 enhanced
set output "$OUTPUT_IMAGE"

set title "$TITLE"
set xlabel "$X_AXIS_NAME ($UNIT), Samples = $SAMPLE_COUNT, Min = $MIN_LABEL $UNIT, Max = $MAX_LABEL $UNIT, Bucket = $BIN_WIDTH_LABEL $UNIT"
set ylabel "Number of Samples"
set format x "%.4g"
set format y "%.4g"

set xrange [0:*]
set yrange [0.9:*]
set logscale y

set grid xtics ytics
set border linewidth 1
set key top right

plot "$HIST_DATA" using \
(($MIN_NS + (\$1 * $BUCKET_WIDTH_NS)) / $SCALE):2 \
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
echo "  Bin width       : $BIN_WIDTH_LABEL $UNIT"
echo "  Minimum         : $MIN_LABEL $UNIT"
echo "  Mean            : $MEAN_LABEL $UNIT"
echo "  P50             : $P50_LABEL $UNIT"
echo "  P90             : $P90_LABEL $UNIT"
echo "  P95             : $P95_LABEL $UNIT"
echo "  P99             : $P99_LABEL $UNIT"
echo "  Maximum         : $MAX_LABEL $UNIT"
