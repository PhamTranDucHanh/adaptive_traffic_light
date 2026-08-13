#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <input_log> [output_dir]" >&2
    exit 1
fi

LOG="$1"
OUTDIR=${2:-output}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/plot_utils.sh"

if ! command -v gnuplot >/dev/null 2>&1; then
    echo "Error: gnuplot is not installed." >&2
    exit 1
fi

mkdir -p "$OUTDIR/plots"
DATA_FILE="$OUTDIR/pipeline_line_metrics.txt"

# Columns: CycleId, Begin timestamp (CLOCK_MONOTONIC ns), wake-up latency (us),
# execution time (us).
awk '
{
    has_cycle_id = match($0, /CycleId= *([0-9]+)/, i);

    if (has_cycle_id &&
        match($0, /ExpectedWakeup= *([0-9]+)/, a) &&
        match($0, /Begin= *([0-9]+)/, b) &&
        match($0, /End= *([0-9]+)/, c))
    {
        wakeup = (b[1] - a[1]) / 1000;
        runtime = (c[1] - b[1]) / 1000;
        print i[1], b[1], wakeup, runtime;
    }
}
' "$LOG" > "$DATA_FILE"

if [[ ! -s "$DATA_FILE" ]]; then
    echo "Error: no pipeline samples found in $LOG" >&2
    exit 1
fi

read -r FIRST_NS LAST_NS < <(
    awk 'NR == 1 {first=$2} {last=$2} END {print first, last}' "$DATA_FILE"
)
TOTAL_SECONDS="$(awk -v first="$FIRST_NS" -v last="$LAST_NS" \
    'BEGIN {printf "%.9f", (last - first) / 1000000000.0}')"
select_elapsed_axis "$TOTAL_SECONDS"
X_MAX="$(awk -v total="$TOTAL_SECONDS" -v scale="$ELAPSED_SCALE" \
    'BEGIN {value=total/scale; printf "%.9f", (value > 0 ? value : 1)}')"

plot_metric() {
    local column="$1"
    local title="$2"
    local ylabel="$3"
    local output="$4"
    local max_us
    max_us="$(awk -v column="$column" 'BEGIN { max = 0 } { value = $column < 0 ? -$column : $column; if (value > max) max = value } END { print max }' "$DATA_FILE")"
    select_time_unit "$max_us"

    gnuplot <<EOF
set terminal pngcairo size 1800,900 enhanced font "Arial,12"
set output "${output}"
set title "${title}"
set xlabel "Elapsed time (${ELAPSED_UNIT}), Total = ${ELAPSED_TOTAL} ${ELAPSED_SHORT_UNIT}"
set ylabel "${ylabel} (${TIME_UNIT})"
set xrange [0:${X_MAX}]
set yrange [0:*]
set format x "%.3g"
set format y "%.4g"
set grid xtics ytics
set border linewidth 1
set key top right
plot "${DATA_FILE}" using (((\$2-${FIRST_NS})/1000000000.0)/${ELAPSED_SCALE}):(\$${column}/${TIME_SCALE}) with linespoints linewidth 1.2 pointtype 7 pointsize 0.4 title "${ylabel}"
EOF

    echo "Unit     : ${ylabel} = ${TIME_UNIT}"
}

plot_metric 3 "Pipeline Wake-up Latency over Time" "Wake-up Latency" "$OUTDIR/plots/pipeline_wakeup_latency_line.png"
plot_metric 4 "Pipeline Execution Time over Time" "Execution Time" "$OUTDIR/plots/pipeline_execution_time_line.png"

echo "X axis   : elapsed ${ELAPSED_UNIT} (0..${ELAPSED_TOTAL} ${ELAPSED_SHORT_UNIT})"
echo "Generated: $OUTDIR/plots/pipeline_wakeup_latency_line.png"
echo "Generated: $OUTDIR/plots/pipeline_execution_time_line.png"
