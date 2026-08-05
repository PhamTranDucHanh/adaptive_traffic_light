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
DATA_FILE="$OUTDIR/pipeline_line_metrics.txt"

# Columns: CycleId, date, time, wake-up latency (us), execution time (us).
awk '
{
    if (match($0, /CycleId= *([0-9]+)/, i) &&
        match($0, /ExpectedWakeup= *([0-9]+)/, a) &&
        match($0, /Begin= *([0-9]+)/, b) &&
        match($0, /End= *([0-9]+)/, c) &&
        match($0, /[0-9]{4}\/[0-9]{2}\/[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]+/, t))
    {
        wakeup = (b[1] - a[1]) / 1000;
        runtime = (c[1] - b[1]) / 1000;
        timestamp = substr($0, RSTART, RLENGTH);
        print i[1], timestamp, wakeup, runtime;
    }
}
' "$LOG" > "$DATA_FILE"

if [[ ! -s "$DATA_FILE" ]]; then
    echo "Error: no pipeline samples found in $LOG" >&2
    exit 1
fi

if [[ "$X_AXIS" == "timestamp" ]]; then
    X_SETUP='set xdata time
set format x "%H:%M:%S"
set xlabel "Timestamp"
set xtics rotate by -45'
    X_VALUE='(timecolumn(2, "%Y/%m/%d %H:%M:%S"))'
else
    X_SETUP='set xlabel "CycleId"'
    X_VALUE=1
fi

plot_metric() {
    local column="$1"
    local title="$2"
    local ylabel="$3"
    local output="$4"

    gnuplot <<EOF
set terminal pngcairo size 1600,900 enhanced
set output "${output}"
set title "${title}"
${X_SETUP}
set ylabel "${ylabel} (us)"
set grid
set key top right
plot "${DATA_FILE}" using ${X_VALUE}:${column} with linespoints linewidth 2 pointtype 7 pointsize 0.5 title "${ylabel}"
EOF
}

plot_metric 4 "Pipeline Wake-up Latency" "Wake-up Latency" "$OUTDIR/plots/pipeline_wakeup_latency_line.png"
plot_metric 5 "Pipeline Execution Time" "Execution Time" "$OUTDIR/plots/pipeline_execution_time_line.png"

echo "X axis   : $X_AXIS"
echo "Generated: $OUTDIR/plots/pipeline_wakeup_latency_line.png"
echo "Generated: $OUTDIR/plots/pipeline_execution_time_line.png"
