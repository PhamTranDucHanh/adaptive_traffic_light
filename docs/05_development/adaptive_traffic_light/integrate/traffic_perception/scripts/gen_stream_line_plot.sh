#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <input_log> [output_dir]" >&2
    echo "Set X_AXIS=id or X_AXIS=timestamp (default: id)." >&2
    exit 1
fi

LOG="$1"
OUTDIR=${2:-output}
X_AXIS=${X_AXIS:-id}

if [[ "$X_AXIS" != "id" && "$X_AXIS" != "timestamp" ]]; then
    echo "Error: X_AXIS must be 'id' or 'timestamp'." >&2
    exit 1
fi

if ! command -v gnuplot >/dev/null 2>&1; then
    echo "Error: gnuplot is not installed." >&2
    exit 1
fi

mkdir -p "$OUTDIR/plots"
DATA_BASE="$OUTDIR/stream_line_metrics"
rm -f "$DATA_BASE"_lane{0,1,2,3}.txt

# Per-lane columns: FrameId, date, time, wake-up latency (us), execution time (us).
awk '
{
    if (match($0, /LaneId= *([0-9]+)/, l) &&
        match($0, /FrameId= *([0-9]+)/, i) &&
        match($0, /ExpectedWakeup= *([0-9]+)/, a) &&
        match($0, /Begin= *([0-9]+)/, b) &&
        match($0, /End= *([0-9]+)/, c) &&
        match($0, /[0-9]{4}\/[0-9]{2}\/[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+/, t))
    {
        wakeup = (b[1] - a[1]) / 1000;
        runtime = (c[1] - b[1]) / 1000;
        timestamp = substr($0, RSTART, RLENGTH);
        output = "'"$DATA_BASE"'_lane" (l[1] + 0) ".txt";
        print i[1], timestamp, wakeup, runtime >> output;
        close(output);
    }
}
' "$LOG"

if [[ "$X_AXIS" == "timestamp" ]]; then
    X_SETUP='set xdata time
set format x "%H:%M:%S"
set xlabel "Timestamp"
set xtics rotate by -45'
    X_VALUE='(timecolumn(2, "%Y/%m/%d %H:%M:%S"))'
else
    X_SETUP='set xlabel "FrameId"'
    X_VALUE=1
fi

LANE_COLORS=("#d62728" "#1f77b4" "#2ca02c" "#9467bd")

plot_metric() {
    local column="$1"
    local title="$2"
    local ylabel="$3"
    local output="$4"
    local plot_cmd=""

    for lane in 0 1 2 3; do
        local data_file="${DATA_BASE}_lane${lane}.txt"
        [[ -s "$data_file" ]] || continue
        [[ -z "$plot_cmd" ]] || plot_cmd+=", "
        plot_cmd+="\"${data_file}\" using ${X_VALUE}:${column} with linespoints linewidth 2 pointtype 7 pointsize 0.4 linecolor rgb \"${LANE_COLORS[$lane]}\" title \"Lane ${lane}\""
    done

    if [[ -z "$plot_cmd" ]]; then
        echo "Error: no stream samples found in $LOG" >&2
        exit 1
    fi

    gnuplot <<EOF
set terminal pngcairo size 1600,900 enhanced
set output "${output}"
set title "${title}"
${X_SETUP}
set ylabel "${ylabel} (us)"
set grid
set key top right
plot ${plot_cmd}
EOF
}

plot_metric 4 "Stream Worker Wake-up Latency" "Wake-up Latency" "$OUTDIR/plots/stream_wakeup_latency_line.png"
plot_metric 5 "Stream Worker Execution Time" "Execution Time" "$OUTDIR/plots/stream_execution_time_line.png"

echo "X axis   : $X_AXIS"
echo "Generated: $OUTDIR/plots/stream_wakeup_latency_line.png"
echo "Generated: $OUTDIR/plots/stream_execution_time_line.png"
