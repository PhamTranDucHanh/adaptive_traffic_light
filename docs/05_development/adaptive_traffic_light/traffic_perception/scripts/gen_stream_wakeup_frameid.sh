#!/usr/bin/env bash
set -euo pipefail

LOG=${1:-output/stream.log}
OUTDIR=${2:-output}

mkdir -p "$OUTDIR"

DATA="$OUTDIR/wakeup_latency.dat"

##############################################################################
# Parse
##############################################################################

awk '
{
    if (match($0,/LaneId= *([0-9]+)/,L) &&
        match($0,/FrameId= *([0-9]+)/,F) &&
        match($0,/ExpectedWakeup= *([0-9]+)/,E) &&
        match($0,/Begin= *([0-9]+)/,B) &&
        match($0,/End= *([0-9]+)/,EE))
    {
        latency = (B[1]-E[1])/1000000.0
        exec = (EE[1]-B[1])/1000000.0

        print L[1],F[1],latency,exec
    }
}
' "$LOG" > "$DATA"

##############################################################################
# Plot
##############################################################################

gnuplot <<EOF

set terminal pngcairo size 1800,900 enhanced font "Arial,15"
set output "$OUTDIR/wakeup_latency_vs_frame.png"

set title "Stream Worker Wake-up Latency"

set xlabel "Frame ID"
set ylabel "Wake-up Latency (ms)"

set grid
set border lw 1.5

set key outside right top

set pointsize 0.6

plot \
"< awk '\$1==0' $DATA" using 2:3 \
    with linespoints lw 2 pt 7 lc rgb "#d62728" title "Lane0", \
"< awk '\$1==1' $DATA" using 2:3 \
    with linespoints lw 2 pt 7 lc rgb "#1f77b4" title "Lane1", \
"< awk '\$1==2' $DATA" using 2:3 \
    with linespoints lw 2 pt 7 lc rgb "#2ca02c" title "Lane2", \
"< awk '\$1==3' $DATA" using 2:3 \
    with linespoints lw 2 pt 7 lc rgb "#9467bd" title "Lane3"

EOF

##############################################################################
# Execution Time
##############################################################################

gnuplot <<EOF

set terminal pngcairo size 1800,900 enhanced font "Arial,15"
set output "$OUTDIR/execution_time_vs_frame.png"

set title "Stream Worker Execution Time"

set xlabel "Frame ID"
set ylabel "Execution Time (ms)"

set grid
set border lw 1.5

set key outside right top

set pointsize 0.6

plot \
"< awk '\$1==0' $DATA" using 2:4 \
    with linespoints lw 2 pt 7 lc rgb "#d62728" title "Lane0", \
"< awk '\$1==1' $DATA" using 2:4 \
    with linespoints lw 2 pt 7 lc rgb "#1f77b4" title "Lane1", \
"< awk '\$1==2' $DATA" using 2:4 \
    with linespoints lw 2 pt 7 lc rgb "#2ca02c" title "Lane2", \
"< awk '\$1==3' $DATA" using 2:4 \
    with linespoints lw 2 pt 7 lc rgb "#9467bd" title "Lane3"

EOF

echo
echo "Generated:"
echo "  $OUTDIR/wakeup_latency_vs_frame.png"
echo "  $OUTDIR/execution_time_vs_frame.png"
