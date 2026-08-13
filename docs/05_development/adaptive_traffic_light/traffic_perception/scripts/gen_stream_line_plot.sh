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
DATA_BASE="$OUTDIR/stream_line_metrics"
rm -f "$DATA_BASE"_lane{0,1,2,3}.txt

# Per-lane columns: FrameId, Begin timestamp (CLOCK_MONOTONIC ns),
# wake-up latency (us), execution time (us).
awk '
{
    if (match($0, /LaneId= *([0-9]+)/, l) &&
        match($0, /FrameId= *([0-9]+)/, i) &&
        match($0, /ExpectedWakeup= *([0-9]+)/, a) &&
        match($0, /Begin= *([0-9]+)/, b) &&
        match($0, /End= *([0-9]+)/, c))
    {
        if (b[1] == 0 || c[1] == 0 || c[1] < b[1]) next;
        wakeup = a[1] != 0 ? (b[1] - a[1]) / 1000 : "NaN";
        runtime = (c[1] - b[1]) / 1000;
        output = "'"$DATA_BASE"'_lane" (l[1] + 0) ".txt";
        print i[1], b[1], wakeup, runtime >> output;
        close(output);
    }
}
' "$LOG"

DATA_FILES=()
for lane in 0 1 2 3; do
    data_file="${DATA_BASE}_lane${lane}.txt"
    [[ -s "$data_file" ]] && DATA_FILES+=("$data_file")
done

if (( ${#DATA_FILES[@]} == 0 )); then
    echo "Error: no stream samples found in $LOG" >&2
    exit 1
fi

read -r FIRST_NS LAST_NS < <(
    awk 'NR == 1 || $2 < first {first=$2} NR == 1 || $2 > last {last=$2} END {print first, last}' \
        "${DATA_FILES[@]}"
)
SESSION_START_NS="${ANALYTICS_TIME_ORIGIN_NS:-$FIRST_NS}"
SESSION_END_NS="${ANALYTICS_TIME_END_NS:-$LAST_NS}"
if [[ ! "$SESSION_START_NS" =~ ^[0-9]+$ ||
      ! "$SESSION_END_NS" =~ ^[0-9]+$ ]] ||
   awk -v start="$SESSION_START_NS" -v end="$SESSION_END_NS" \
     'BEGIN {exit !(end < start)}'; then
    echo "Error: invalid shared analytics time window: ${SESSION_START_NS}..${SESSION_END_NS}" >&2
    exit 1
fi
TOTAL_SECONDS="$(awk -v first="$SESSION_START_NS" -v last="$SESSION_END_NS" \
    'BEGIN {printf "%.9f", (last - first) / 1000000000.0}')"
select_elapsed_axis "$TOTAL_SECONDS"
X_MAX="$(awk -v total="$TOTAL_SECONDS" -v scale="$ELAPSED_SCALE" \
    'BEGIN {value=total/scale; printf "%.9f", (value > 0 ? value : 1)}')"

LANE_COLORS=("#d62728" "#1f77b4" "#2ca02c" "#9467bd")

plot_metric() {
    local column="$1"
    local title="$2"
    local ylabel="$3"
    local output="$4"
    local plot_cmd=""
    local max_us
    max_us="$(awk -v column="$column" 'BEGIN { max = 0 } { value = $column < 0 ? -$column : $column; if (value > max) max = value } END { print max }' "${DATA_FILES[@]}")"
    select_time_unit "$max_us"

    for lane in 0 1 2 3; do
        local data_file="${DATA_BASE}_lane${lane}.txt"
        [[ -s "$data_file" ]] || continue
        [[ -z "$plot_cmd" ]] || plot_cmd+=", "
        plot_cmd+="\"${data_file}\" using (((\$2-${SESSION_START_NS})/1000000000.0)/${ELAPSED_SCALE}):(\$${column}/${TIME_SCALE}) with linespoints linewidth 1.2 pointtype 7 pointsize 0.35 linecolor rgb \"${LANE_COLORS[$lane]}\" title \"Lane ${lane}\""
    done

    if [[ -z "$plot_cmd" ]]; then
        echo "Error: no stream samples found in $LOG" >&2
        exit 1
    fi

    gnuplot <<EOF
set terminal pngcairo size 1800,900 enhanced font "Arial,12"
set output "${output}"
set title "${title}"
set xlabel "Elapsed time (${ELAPSED_UNIT})"
set ylabel "${ylabel} (${TIME_UNIT})"
set xrange [0:${X_MAX}]
set yrange [0:*]
set format x "%.3g"
set format y "%.4g"
set grid xtics ytics
set border linewidth 1
set key top right
plot ${plot_cmd}
EOF
    echo "Unit     : ${ylabel} = ${TIME_UNIT}"
}

plot_metric 3 "Stream Worker Wake-up Latency over Time" "Wake-up Latency" "$OUTDIR/plots/stream_wakeup_latency_line.png"
plot_metric 4 "Stream Worker Execution Time over Time" "Execution Time" "$OUTDIR/plots/stream_execution_time_line.png"

echo "X axis   : elapsed ${ELAPSED_UNIT} (0..${ELAPSED_TOTAL} ${ELAPSED_SHORT_UNIT})"
echo "Session  : CLOCK_MONOTONIC ${SESSION_START_NS}..${SESSION_END_NS} ns"
echo "Generated: $OUTDIR/plots/stream_wakeup_latency_line.png"
echo "Generated: $OUTDIR/plots/stream_execution_time_line.png"
