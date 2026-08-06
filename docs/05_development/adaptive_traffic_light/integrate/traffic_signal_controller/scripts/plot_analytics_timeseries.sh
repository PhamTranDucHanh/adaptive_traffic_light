#!/bin/bash

# Usage:
#   ./plot_analytics_timeseries.sh [metric] [input_file] [output_image]
#
# metric:
#   both
#   wakeup
#   execution
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
    *)
        echo "Error: Unsupported metric: $METRIC"
        echo "Supported metrics: both, wakeup, execution"
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
        NEXT_RUN_NUMBER=$((existing_number + 1))
    fi
done

RUN_ID=$(printf "%03d" "$NEXT_RUN_NUMBER")
RUN_BASENAME="${RUN_ID}_${METRIC}_timeseries"

DATA_CSV="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}.csv"
WAKEUP_PLOT_DATA="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_wakeup.dat"
EXECUTION_PLOT_DATA="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_execution.dat"
VALUE_DATA_NS="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}_values_ns.dat"
GNUPLOT_FILE="${PLOT_WORK_DIRECTORY}/${RUN_BASENAME}.gnu"
DEFAULT_OUTPUT_IMAGE="${OUTPUT_DIRECTORY}/${RUN_BASENAME}.png"
OUTPUT_IMAGE=${3:-$DEFAULT_OUTPUT_IMAGE}

mkdir -p "$(dirname "$OUTPUT_IMAGE")"

#-------------------------------------------------------
# Extract time-series samples
#
# Preferred x-axis source:
#   1. DLT text timestamp when the input line has one.
#   2. Wakeup monotonic timestamp from actual_wakeup_ns.
#   3. Execution samples aligned to wakeup sample order.
#   4. Sample index as a final fallback.
#-------------------------------------------------------

awk -v metric="$METRIC" -v input_file="$INPUT_FILE" -v run_id="$RUN_ID" '
function extract_uint(line, key,    pattern, value) {
    pattern = key "=[[:space:]]*[0-9]+"

    if (match(line, pattern)) {
        value = substr(line, RSTART, RLENGTH)
        sub(key "=[[:space:]]*", "", value)
        return value
    }

    return ""
}

function dlt_timestamp_ns(line,    fields, field_count) {
    field_count = split(line, fields, /[[:space:]]+/)

    if (field_count >= 3 && fields[3] ~ /^[0-9]+([.][0-9]+)?$/) {
        return sprintf("%.0f", fields[3] * 1000000000.0)
    }

    return ""
}

function remember_sample(kind, metric_index, value_ns, timestamp_ns, fallback_ns) {
    sample_kind[++sample_count] = kind
    sample_index[sample_count] = metric_index
    sample_value_ns[sample_count] = value_ns
    sample_dlt_ns[sample_count] = timestamp_ns
    sample_fallback_ns[sample_count] = fallback_ns

    if (timestamp_ns != "") {
        ++dlt_timestamp_count
    }
}

index($0, "event=FSM_WAKEUP") {
    if (metric != "both" && metric != "wakeup") {
        next
    }

    value_ns = extract_uint($0, "latency_ns")

    if (value_ns == "") {
        next
    }

    ++wakeup_count
    wakeup_actual_ns[wakeup_count] = extract_uint($0, "actual_wakeup_ns")
    remember_sample("wakeup", wakeup_count, value_ns, dlt_timestamp_ns($0), wakeup_actual_ns[wakeup_count])
}

index($0, "event=FSM_EXECUTION") {
    if (metric != "both" && metric != "execution") {
        next
    }

    value_ns = extract_uint($0, "execution_time_ns")

    if (value_ns == "") {
        next
    }

    ++execution_count
    remember_sample("execution", execution_count, value_ns, dlt_timestamp_ns($0), "")
}

END {
    print "run_id,metric,sample_index,x_kind,x_raw_ns,elapsed_s,value_ns,value_us,value_ms,input_file"

    if (sample_count == 0) {
        exit
    }

    use_dlt_time = dlt_timestamp_count == sample_count

    for (i = 1; i <= sample_count; ++i) {
        x_kind = "sample_index"
        x_raw_ns = sample_index[i]

        if (use_dlt_time) {
            x_kind = "dlt_timestamp_ns"
            x_raw_ns = sample_dlt_ns[i]
        } else if (sample_kind[i] == "wakeup" && sample_fallback_ns[i] != "") {
            x_kind = "monotonic_actual_wakeup_ns"
            x_raw_ns = sample_fallback_ns[i]
        } else if (sample_kind[i] == "execution" &&
                   wakeup_actual_ns[sample_index[i]] != "") {
            x_kind = "monotonic_wakeup_aligned_ns"
            x_raw_ns = wakeup_actual_ns[sample_index[i]]
        }

        resolved_x_raw_ns[i] = x_raw_ns
        resolved_x_kind[i] = x_kind

        if (i == 1 || x_raw_ns < first_x_raw_ns) {
            first_x_raw_ns = x_raw_ns
        }
    }

    for (i = 1; i <= sample_count; ++i) {
        if (resolved_x_kind[i] == "sample_index") {
            elapsed_s = resolved_x_raw_ns[i] - first_x_raw_ns
        } else {
            elapsed_s = (resolved_x_raw_ns[i] - first_x_raw_ns) / 1000000000.0
        }

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
    ' "$VALUE_DATA_NS"
}

P95_NS=$(percentile_from_sorted_file 0.95)

if awk -v value="$P95_NS" 'BEGIN {exit !(value < 10000)}'; then
    UNIT="ns"
    SCALE=1
elif awk -v value="$P95_NS" 'BEGIN {exit !(value < 10000000)}'; then
    UNIT="us"
    SCALE=1000
else
    UNIT="ms"
    SCALE=1000000
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

WAKEUP_COUNT=$(wc -l < "$WAKEUP_PLOT_DATA")
EXECUTION_COUNT=$(wc -l < "$EXECUTION_PLOT_DATA")

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

if awk -F, 'NR > 1 && $4 == "sample_index" {found = 1} END {exit !found}' "$DATA_CSV"; then
    X_LABEL="Sample index"
else
    X_LABEL="Elapsed time (s)"
fi

#-------------------------------------------------------
# Generate Gnuplot script
#-------------------------------------------------------

cat > "$GNUPLOT_FILE" << EOF
set terminal pngcairo size 1280,720 enhanced font "Arial,10"
set output "$OUTPUT_IMAGE"

set title "$TITLE"
set xlabel "$X_LABEL"
set ylabel "Time ($UNIT)"

set grid
set key outside
set key top right
set border lw 1
set tics out

set label 1 sprintf("Samples: %d", ${SAMPLE_COUNT}) at graph 0.02,0.95
set label 2 sprintf("Wakeup: %d", ${WAKEUP_COUNT}) at graph 0.02,0.90
set label 3 sprintf("Execution: %d", ${EXECUTION_COUNT}) at graph 0.02,0.85
set label 4 sprintf("Unit: %s", "$UNIT") at graph 0.02,0.80
set label 5 sprintf("X source: %s", "$X_KIND_SUMMARY") at graph 0.02,0.75

EOF

case "$METRIC" in
    both)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$WAKEUP_PLOT_DATA" using 1:2 with linespoints lw 1 pt 7 ps 0.8 title "Wakeup latency", \\
     "$EXECUTION_PLOT_DATA" using 1:2 with linespoints lw 1 pt 5 ps 0.8 title "Execution time"
EOF
        ;;
    wakeup)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$WAKEUP_PLOT_DATA" using 1:2 with linespoints lw 1 pt 7 ps 0.8 title "Wakeup latency"
EOF
        ;;
    execution)
        cat >> "$GNUPLOT_FILE" << EOF
plot "$EXECUTION_PLOT_DATA" using 1:2 with linespoints lw 1 pt 5 ps 0.8 title "Execution time"
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
echo "  Display unit    : $UNIT"
echo "  X-axis          : $X_LABEL"
echo "  X source        : $X_KIND_SUMMARY"
