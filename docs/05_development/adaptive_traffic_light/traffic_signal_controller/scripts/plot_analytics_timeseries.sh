#!/bin/bash

# Usage:
#   ./plot_analytics_timeseries.sh [metric] [input_file] [output_image]
#
# metric:
#   both
#   wakeup
#   execution
#   end_to_end
#   perception_to_controller
#   emergency
#
# Benchmark input data is archived by the analytics code after each run under:
#   /tmp/traffic_signal_controller/logs/output/*_analytics_input.txt

set -o pipefail

METRIC=${1:-both}

ANALYTICS_LOG_FILE="/tmp/linux_rt_application/logs/CTRL.dlt"
DEFAULT_INPUT_FILE="${ANALYTICS_LOG_FILE}.txt"

RUNTIME_DIRECTORY="/tmp/traffic_signal_controller"
RUNTIME_LOG_DIRECTORY="${RUNTIME_DIRECTORY}/logs"
OUTPUT_DIRECTORY="${RUNTIME_LOG_DIRECTORY}/output"
PLOT_WORK_DIRECTORY="${RUNTIME_LOG_DIRECTORY}/plot_work"

INPUT_FILE=${2:-$DEFAULT_INPUT_FILE}

case "$METRIC" in
    both)
        TITLE="FSM Execution Time and Wakeup Latency Over Time"
        ;;
    wakeup)
        TITLE="FSM Wakeup Latency Over Time"
        ;;
    execution)
        TITLE="FSM Execution Time Over Time"
        ;;
    end_to_end)
        TITLE="Timing Decision Receive-to-Controller Receive Latency Over Time"
        ;;
    perception_to_controller)
        TITLE="Perception Publish-to-Controller Receive Latency Over Time"
        ;;
    emergency)
        TITLE="Emergency Receive-to-Apply Latency Over Time"
        ;;
    *)
        echo "Error: Unsupported metric: $METRIC"
        echo "Supported metrics: both, wakeup, execution, end_to_end, perception_to_controller, emergency"
        exit 1
        ;;
esac

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

mkdir -p "$OUTPUT_DIRECTORY" "$PLOT_WORK_DIRECTORY"

NEXT_RUN_NUMBER=1
for existing_file in "$OUTPUT_DIRECTORY"/[0-9][0-9][0-9]_*; do
    [ -e "$existing_file" ] || continue

    existing_name=$(basename "$existing_file")
    existing_number=${existing_name%%_*}

    case "$existing_number" in
        ''|*[!0-9]*)
            continue
            ;;
    esac

    if [ "$existing_number" -ge "$NEXT_RUN_NUMBER" ]; then
        NEXT_RUN_NUMBER=$((10#$existing_number + 1))
    fi
done

RUN_ID=$(printf "%03d" "$NEXT_RUN_NUMBER")
RUN_BASENAME="${RUN_ID}_${METRIC}_timeseries"

DATA_CSV="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}.csv"
WAKEUP_PLOT_DATA="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_wakeup.dat"
EXECUTION_PLOT_DATA="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_execution.dat"
END_TO_END_PLOT_DATA="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_end_to_end.dat"
PERCEPTION_PLOT_DATA="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_perception_to_controller.dat"
EMERGENCY_PLOT_DATA="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_emergency.dat"
VALUE_DATA_NS="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_values_ns.dat"
GNUPLOT_FILE="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}.gnu"
DEFAULT_OUTPUT_IMAGE="${OUTPUT_DIRECTORY}/${RUN_BASENAME}.png"
OUTPUT_IMAGE=${3:-$DEFAULT_OUTPUT_IMAGE}

mkdir -p "$(dirname "$OUTPUT_IMAGE")"

#-------------------------------------------------------
# Extract time-series samples
#
# The x axis always uses a real CLOCK_MONOTONIC event timestamp:
#   - FSM wakeup: actual_wakeup_ns
#   - FSM execution: execution_start_ns (legacy logs fall back to the
#     immediately preceding FSM wakeup timestamp)
#   - plan receive: controller_receive_timestamp_ns
#   - emergency apply: apply_timestamp_ns
# Samples without a valid event timestamp are omitted from the time-series plot
# instead of being mixed with an artificial sample-index axis.
#-------------------------------------------------------

awk -v metric="$METRIC" -v input_file="$INPUT_FILE" -v run_id="$RUN_ID" \
    -v requested_origin_ns="${ANALYTICS_TIME_ORIGIN_NS:-}" '
function extract_uint(line, key,    pattern, value) {
    pattern = key "=[[:space:]]*[0-9]+"

    if (match(line, pattern)) {
        value = substr(line, RSTART, RLENGTH)
        sub(key "=[[:space:]]*", "", value)
        return value
    }

    return ""
}

function remember_sample(kind, metric_index, value_ns, event_timestamp_ns) {
    if (event_timestamp_ns == "") {
        return
    }

    sample_kind[++sample_count] = kind
    sample_index[sample_count] = metric_index
    sample_value_ns[sample_count] = value_ns
    sample_timestamp_ns[sample_count] = event_timestamp_ns
}

index($0, "event=FSM_WAKEUP") {
    value_ns = extract_uint($0, "latency_ns")
    actual_wakeup_ns = extract_uint($0, "actual_wakeup_ns")

    if (actual_wakeup_ns != "") {
        last_wakeup_ns = actual_wakeup_ns
    }

    if (metric != "both" && metric != "wakeup") {
        next
    }

    if (value_ns == "") {
        next
    }

    ++wakeup_count
    remember_sample("wakeup", wakeup_count, value_ns, actual_wakeup_ns)
    next
}

index($0, "event=FSM_EXECUTION") {
    if (metric != "both" && metric != "execution") {
        next
    }

    value_ns = extract_uint($0, "execution_time_ns")
    execution_start_ns = extract_uint($0, "execution_start_ns")

    if (value_ns == "") {
        next
    }

    if (execution_start_ns == "") {
        execution_start_ns = last_wakeup_ns
    }

    ++execution_count
    remember_sample("execution", execution_count, value_ns,
                    execution_start_ns)
    next
}

index($0, "event=TIMING_DECISION_RECEIVE_TO_CONTROLLER_RECEIVE") {
    if (metric != "end_to_end") {
        next
    }

    value_ns = extract_uint($0, "latency_ns")
    if (value_ns != "") {
        ++end_to_end_count
        remember_sample("end_to_end", end_to_end_count, value_ns,
                        extract_uint($0, "controller_receive_timestamp_ns"))
    }
    next
}

index($0, "event=PERCEPTION_PUBLISH_TO_CONTROLLER_RECEIVE") {
    if (metric != "perception_to_controller") {
        next
    }

    value_ns = extract_uint($0, "latency_ns")
    if (value_ns != "") {
        ++perception_count
        remember_sample("perception_to_controller", perception_count,
                        value_ns,
                        extract_uint($0, "controller_receive_timestamp_ns"))
    }
    next
}

index($0, "event=EMERGENCY_RECEIVE_TO_APPLY") {
    if (metric != "emergency") {
        next
    }

    value_ns = extract_uint($0, "latency_ns")
    if (value_ns != "") {
        ++emergency_count
        remember_sample("emergency", emergency_count, value_ns,
                        extract_uint($0, "apply_timestamp_ns"))
    }
    next
}

END {
    print "run_id,metric,sample_index,x_kind,x_raw_ns,elapsed_s,value_ns,value_us,value_ms,input_file"

    if (sample_count == 0) {
        exit
    }

    for (i = 1; i <= sample_count; ++i) {
        x_raw_ns = sample_timestamp_ns[i]

        if (sample_kind[i] == "wakeup") {
            x_kind = "monotonic_actual_wakeup_ns"
        } else if (sample_kind[i] == "execution") {
            x_kind = "monotonic_execution_start_ns"
        } else if (sample_kind[i] == "emergency") {
            x_kind = "monotonic_apply_timestamp_ns"
        } else {
            x_kind = "monotonic_controller_receive_timestamp_ns"
        }

        resolved_x_raw_ns[i] = x_raw_ns
        resolved_x_kind[i] = x_kind

        if (i == 1 || x_raw_ns < first_x_raw_ns) {
            first_x_raw_ns = x_raw_ns
        }
    }

    if (requested_origin_ns != "") {
        first_x_raw_ns = requested_origin_ns
    }

    for (i = 1; i <= sample_count; ++i) {
        elapsed_s = (resolved_x_raw_ns[i] - first_x_raw_ns) / 1000000000.0

        printf "%s,%s,%d,%s,%s,%.9f,%s,%.9f,%.9f,%s\n",
            run_id,
            sample_kind[i],
            sample_index[i],
            resolved_x_kind[i],
            resolved_x_raw_ns[i],
            elapsed_s,
            sample_value_ns[i],
            sample_value_ns[i] / 1000.0,
            sample_value_ns[i] / 1000000.0,
            input_file
    }
}
' "$INPUT_FILE" > "$DATA_CSV"

SAMPLE_COUNT=$(awk -F, 'NR > 1 {count++} END {print count + 0}' "$DATA_CSV")

if [ "$SAMPLE_COUNT" -eq 0 ]; then
    echo "Error: No analytics samples found."
    echo "  Metric: $METRIC"
    echo "  Input : $INPUT_FILE"
    rm -f "$DATA_CSV"
    exit 1
fi

awk -F, 'NR > 1 {print $7}' "$DATA_CSV" | sort -n > "$VALUE_DATA_NS"

MAX_NS=$(awk 'END {print $1}' "$VALUE_DATA_NS")

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

awk -F, -v scale="$SCALE" '
NR > 1 && $2 == "wakeup" {
    printf "%.9f %.9f\n", $6, $7 / scale
}
' "$DATA_CSV" > "$WAKEUP_PLOT_DATA"

awk -F, -v scale="$SCALE" '
NR > 1 && $2 == "execution" {
    printf "%.9f %.9f\n", $6, $7 / scale
}
' "$DATA_CSV" > "$EXECUTION_PLOT_DATA"

awk -F, -v scale="$SCALE" '
NR > 1 && $2 == "end_to_end" {printf "%.9f %.9f\n", $6, $7 / scale}
' "$DATA_CSV" > "$END_TO_END_PLOT_DATA"

awk -F, -v scale="$SCALE" '
NR > 1 && $2 == "perception_to_controller" {
    printf "%.9f %.9f\n", $6, $7 / scale
}
' "$DATA_CSV" > "$PERCEPTION_PLOT_DATA"

awk -F, -v scale="$SCALE" '
NR > 1 && $2 == "emergency" {printf "%.9f %.9f\n", $6, $7 / scale}
' "$DATA_CSV" > "$EMERGENCY_PLOT_DATA"

WAKEUP_COUNT=$(wc -l < "$WAKEUP_PLOT_DATA")
EXECUTION_COUNT=$(wc -l < "$EXECUTION_PLOT_DATA")
END_TO_END_COUNT=$(wc -l < "$END_TO_END_PLOT_DATA")
PERCEPTION_COUNT=$(wc -l < "$PERCEPTION_PLOT_DATA")
EMERGENCY_COUNT=$(wc -l < "$EMERGENCY_PLOT_DATA")

X_KIND_SUMMARY=$(awk -F, '
NR > 1 {
    kinds[$4] = 1
}
END {
    first = 1
    for (kind in kinds) {
        if (!first) {
            printf ", "
        }
        printf "%s", kind
        first = 0
    }
}
' "$DATA_CSV")

if [ -z "$X_KIND_SUMMARY" ]; then
    X_KIND_SUMMARY="unknown"
fi

DATA_TOTAL_SECONDS=$(awk -F, '
NR > 1 && $6 > maximum {maximum = $6}
END {printf "%.9f", maximum + 0}
' "$DATA_CSV")

if [[ -n "${ANALYTICS_TIME_ORIGIN_NS:-}" &&
      -n "${ANALYTICS_TIME_END_NS:-}" ]]; then
    if [[ ! "$ANALYTICS_TIME_ORIGIN_NS" =~ ^[0-9]+$ ||
          ! "$ANALYTICS_TIME_END_NS" =~ ^[0-9]+$ ]] ||
       awk -v start="$ANALYTICS_TIME_ORIGIN_NS" \
           -v end="$ANALYTICS_TIME_END_NS" \
           'BEGIN {exit !(end < start)}'; then
        echo "Error: invalid shared analytics time window: ${ANALYTICS_TIME_ORIGIN_NS}..${ANALYTICS_TIME_END_NS}" >&2
        exit 1
    fi
    TOTAL_SECONDS=$(awk -v start="$ANALYTICS_TIME_ORIGIN_NS" \
        -v end="$ANALYTICS_TIME_END_NS" \
        'BEGIN {printf "%.9f", (end - start) / 1000000000.0}')
else
    TOTAL_SECONDS="$DATA_TOTAL_SECONDS"
fi

if awk -v seconds="$TOTAL_SECONDS" 'BEGIN {exit !(seconds < 7200)}'; then
    X_UNIT="minutes"
    X_SHORT_UNIT="min"
    X_SCALE=60
else
    X_UNIT="hours"
    X_SHORT_UNIT="h"
    X_SCALE=3600
fi

TOTAL_X=$(awk -v seconds="$TOTAL_SECONDS" -v scale="$X_SCALE" \
    'BEGIN {printf "%.3f", seconds / scale}')
X_MAX=$(awk -v seconds="$TOTAL_SECONDS" -v scale="$X_SCALE" \
    'BEGIN {value=seconds/scale; printf "%.9f", (value > 0 ? value : 1)}')
X_LABEL="Elapsed time ($X_UNIT)"

#-------------------------------------------------------
# Generate Gnuplot script
#-------------------------------------------------------

cat > "$GNUPLOT_FILE" << EOF
set terminal pngcairo size 1800,900 enhanced font "Arial,12"
set output "$OUTPUT_IMAGE"

set title "$TITLE"
set xlabel "$X_LABEL"
set ylabel "Time ($UNIT)"
set xrange [0:$X_MAX]
set yrange [0:*]
set format x "%.3g"
set format y "%.4g"

set grid xtics ytics
set key top right
set border lw 1
set tics out

EOF

case "$METRIC" in
    both)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$WAKEUP_PLOT_DATA" using (\$1/$X_SCALE):2 with linespoints lw 1.2 pt 7 ps 0.5 title "Wakeup latency", \\
     "$EXECUTION_PLOT_DATA" using (\$1/$X_SCALE):2 with linespoints lw 1.2 pt 5 ps 0.5 title "Execution time"
EOF
        ;;
    wakeup)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$WAKEUP_PLOT_DATA" using (\$1/$X_SCALE):2 with linespoints lw 1.2 pt 7 ps 0.5 title "Wakeup latency"
EOF
        ;;
    execution)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$EXECUTION_PLOT_DATA" using (\$1/$X_SCALE):2 with linespoints lw 1.2 pt 5 ps 0.5 title "Execution time"
EOF
        ;;
    end_to_end)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$END_TO_END_PLOT_DATA" using (\$1/$X_SCALE):2 with linespoints lw 1.2 pt 7 ps 0.5 title "Decision-to-controller latency"
EOF
        ;;
    perception_to_controller)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$PERCEPTION_PLOT_DATA" using (\$1/$X_SCALE):2 with linespoints lw 1.2 pt 7 ps 0.5 title "Perception-to-controller latency"
EOF
        ;;
    emergency)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$EMERGENCY_PLOT_DATA" using (\$1/$X_SCALE):2 with linespoints lw 1.2 pt 7 ps 0.5 title "Receive-to-apply latency"
EOF
        ;;
esac

gnuplot "$GNUPLOT_FILE"

if [ $? -ne 0 ]; then
    echo "Error: gnuplot failed."
    exit 1
fi

echo ""
echo "Time-series plot successfully generated:"
echo "  Metric          : $METRIC"
echo "  Input           : $INPUT_FILE"
echo "  Output image    : $OUTPUT_IMAGE"
echo "  Samples         : $SAMPLE_COUNT"
echo "  Wakeup samples  : $WAKEUP_COUNT"
echo "  Execution samples: $EXECUTION_COUNT"
echo "  End-to-end samples: $END_TO_END_COUNT"
echo "  Perception-to-controller samples: $PERCEPTION_COUNT"
echo "  Emergency samples: $EMERGENCY_COUNT"
echo "  Display unit    : $UNIT"
echo "  X-axis          : elapsed $X_UNIT (0..$TOTAL_X $X_SHORT_UNIT)"
echo "  X source        : $X_KIND_SUMMARY"
if [[ -n "${ANALYTICS_TIME_ORIGIN_NS:-}" ]]; then
    echo "  Session window  : CLOCK_MONOTONIC ${ANALYTICS_TIME_ORIGIN_NS}..${ANALYTICS_TIME_END_NS} ns"
else
    echo "  Session window  : metric-local CLOCK_MONOTONIC range"
fi
