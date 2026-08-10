#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <input_log> [output_dir]" >&2
    exit 1
fi

LOG="$1"
OUTDIR=${2:-output}

mkdir -p "$OUTDIR/plots"

WAKEUP_DATA="$OUTDIR/wakeup_latency.txt"
EXEC_DATA="$OUTDIR/execution_time.txt"

rm -f "$WAKEUP_DATA" "$EXEC_DATA"

# Extract wake-up latency (ns -> us) and execution time (ns -> us)
# Output format matches the example input contract:
#   wakeup_latency_us=<value>
#   execution_time_us=<value>
awk '
{
    if (match($0, /ExpectedWakeup= *([0-9]+)/, a) &&
        match($0, /Begin= *([0-9]+)/, b) &&
        match($0, /End= *([0-9]+)/, c))
    {
        wakeup = (b[1] - a[1]) / 1000;
        runtime = (c[1] - b[1]) / 1000;

        print "wakeup_latency_us=" wakeup;
        print "execution_time_us=" runtime;
    }
}
' "$LOG" > "$OUTDIR/pipeline_metrics.txt"

grep "wakeup_latency_us=" "$OUTDIR/pipeline_metrics.txt" | sed 's/wakeup_latency_us=//' > "$WAKEUP_DATA"
grep "execution_time_us=" "$OUTDIR/pipeline_metrics.txt" | sed 's/execution_time_us=//' > "$EXEC_DATA"

make_hist() {
    local infile="$1"
    local field_name="$2"
    local title="$3"
    local xlabel="$4"
    local legend="$5"
    local outpng="$6"

    if [[ ! -s "$infile" ]]; then
        echo "Skip: $infile (empty)"
        return
    fi

    local bucket_width="${BUCKET_WIDTH_US:-1}"

    if ! command -v gnuplot >/dev/null 2>&1; then
        echo "Error: gnuplot is not installed." >&2
        exit 1
    fi

    if [[ ! "$bucket_width" =~ ^[1-9][0-9]*$ ]]; then
        echo "Error: BUCKET_WIDTH_US must be a positive integer." >&2
        exit 1
    fi

    local values_file histogram_file
    values_file="$(mktemp)"
    histogram_file="$(mktemp)"

    # Input file already contains one raw integer value per line.
    cp "$infile" "$values_file"

    if [[ ! -s "$values_file" ]]; then
        echo "Error: no samples found in $infile" >&2
        rm -f "$values_file" "$histogram_file"
        return 1
    fi

    local sample_count min_value max_value
    read -r sample_count min_value max_value < <(
      awk '
        NR == 1 {
          minimum = $1
          maximum = $1
        }
        {
          if ($1 < minimum) minimum = $1
          if ($1 > maximum) maximum = $1
        }
        END {
          print NR, minimum, maximum
        }
      ' "$values_file"
    )

    local display_unit display_scale
    if awk -v value="$max_value" 'BEGIN {exit !(value < 1)}'; then
        display_unit="ns"
        display_scale="0.001"
    elif awk -v value="$max_value" 'BEGIN {exit !(value < 1000)}'; then
        display_unit="us"
        display_scale="1"
    elif awk -v value="$max_value" 'BEGIN {exit !(value < 1000000)}'; then
        display_unit="ms"
        display_scale="1000"
    else
        display_unit="s"
        display_scale="1000000"
    fi

    local display_min display_max display_bucket
    read -r display_min display_max display_bucket < <(
      awk -v min="$min_value" -v max="$max_value" \
          -v bucket="$bucket_width" -v scale="$display_scale" \
          'BEGIN {printf "%.6g %.6g %.6g\n", min / scale, max / scale, bucket / scale}'
    )

    # Bucket zero starts at the measured minimum.
    awk -v minimum="$min_value" -v width="$bucket_width" '
      {
        bucket = int(($1 - minimum) / width)
        count[bucket]++
      }
      END {
        for (bucket in count) {
          print bucket, count[bucket]
        }
      }
    ' "$values_file" | sort -n -k1,1 > "$histogram_file"

    gnuplot <<EOF
set terminal pngcairo size 1600,900 enhanced
set output "${outpng}"

set title "${title}"
set xlabel "${xlabel} (${display_unit}), Samples = ${sample_count}, Min = ${display_min} ${display_unit}, Max = ${display_max} ${display_unit}, Bucket = ${display_bucket} ${display_unit}"
set ylabel "Number of Samples"

set autoscale x
set yrange [0.9:*]
set logscale y

set grid xtics ytics
set border linewidth 1
set key top right

plot "${histogram_file}" using \
((${min_value} + (\$1 * ${bucket_width})) / ${display_scale}):2 \
with impulses linewidth 1 linecolor rgb "#b000ff" \
title "${legend}"
EOF

    echo "Input    : $infile"
    echo "Field    : $field_name"
    echo "Samples  : $sample_count"
    echo "Range    : ${min_value}..${max_value} us"
    echo "Bucket   : ${display_bucket} ${display_unit}"
    echo "Generated: $outpng"
    echo

    rm -f "$values_file" "$histogram_file"
}

make_hist \
    "$WAKEUP_DATA" \
    "wakeup_latency_us" \
    "Pipeline Wake-up Latency Histogram" \
    "Wake-up Latency" \
    "Wake-up Latency" \
    "$OUTDIR/plots/wakeup_latency_histogram.png"

make_hist \
    "$EXEC_DATA" \
    "execution_time_us" \
    "Pipeline Execution Time Histogram" \
    "Execution Time" \
    "Execution Time" \
    "$OUTDIR/plots/execution_time_histogram.png"

echo "Done."
