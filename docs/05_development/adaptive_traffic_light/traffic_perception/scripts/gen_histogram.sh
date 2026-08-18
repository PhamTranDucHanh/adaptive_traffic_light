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
    if (match($0, /(^|[^[:alnum:]_])ExpectedWakeup= *([0-9]+)/, a) &&
        match($0, /(^|[^[:alnum:]_])Begin= *([0-9]+)/, b) &&
        match($0, /(^|[^[:alnum:]_])End= *([0-9]+)/, c))
    {
        if (b[2] == 0 || c[2] == 0 || c[2] < b[2]) next;
        if (a[2] != 0)
            print "wakeup_latency_us=" (b[2] - a[2]) / 1000;
        print "execution_time_us=" (c[2] - b[2]) / 1000;
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

    local max_abs display_min display_max display_bucket
    max_abs="$(awk -v minimum="$min_value" -v maximum="$max_value" 'BEGIN { minimum = minimum < 0 ? -minimum : minimum; maximum = maximum < 0 ? -maximum : maximum; print (minimum > maximum ? minimum : maximum) }')"
    select_time_unit "$max_abs"
    display_min="$(format_scaled_time "$min_value" "$TIME_SCALE")"
    display_max="$(format_scaled_time "$max_value" "$TIME_SCALE")"
    display_bucket="$(format_scaled_time "$bucket_width" "$TIME_SCALE")"

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
set xlabel "${xlabel} (${TIME_UNIT}), Samples = ${sample_count}, Min = ${display_min} ${TIME_UNIT}, Max = ${display_max} ${TIME_UNIT}, Bucket = ${display_bucket} ${TIME_UNIT}"
set ylabel "Number of Samples"

set autoscale x
set yrange [0.9:*]
set logscale y

set grid xtics ytics
set border linewidth 1
set key top right

plot "${histogram_file}" using \
((${min_value} + (\$1 * ${bucket_width})) / ${TIME_SCALE}):2 \
with impulses linewidth 1 linecolor rgb "#b000ff" \
title "${legend}"
EOF

    echo "Input    : $infile"
    echo "Field    : $field_name"
    echo "Samples  : $sample_count"
    echo "Range    : ${display_min}..${display_max} ${TIME_UNIT}"
    echo "Bucket   : ${display_bucket} ${TIME_UNIT}"
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
    "$OUTDIR/plots/perc_pipe_wakeup_histogram.png"

make_hist \
    "$EXEC_DATA" \
    "execution_time_us" \
    "Pipeline Execution Time Histogram" \
    "Execution Time" \
    "Execution Time" \
    "$OUTDIR/plots/perc_pipe_execution_histogram.png"

echo "Done."
