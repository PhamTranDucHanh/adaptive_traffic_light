#!/usr/bin/env bash
set -euo pipefail

LOG=${1:-output/stream.log}
OUTDIR=${2:-output}

mkdir -p "$OUTDIR/plots"

WAKEUP_BASE="$OUTDIR/wakeup_latency"
EXEC_BASE="$OUTDIR/execution_time"

rm -f "$WAKEUP_BASE"_lane{0,1,2,3}.txt "$EXEC_BASE"_lane{0,1,2,3}.txt

# Extract per-lane wake-up latency (ns -> us) and execution time (ns -> us).
# Output format: lane_id value_us
awk '
{
    if (match($0, /LaneId= *([0-9]+)/, l) &&
        match($0, /ExpectedWakeup= *([0-9]+)/, a) &&
        match($0, /Begin= *([0-9]+)/, b) &&
        match($0, /End= *([0-9]+)/, c))
    {
        lane = l[1] + 0;
        wakeup = (b[1] - a[1]) / 1000;
        runtime = (c[1] - b[1]) / 1000;

        wakeup_file = "'"$WAKEUP_BASE"'_lane" lane ".txt";
        exec_file = "'"$EXEC_BASE"'_lane" lane ".txt";

        print lane, wakeup > wakeup_file;
        close(wakeup_file);
        print lane, runtime > exec_file;
        close(exec_file);
    }
}
' "$LOG"

LANE_COLORS=("#d62728" "#1f77b4" "#2ca02c" "#9467bd")

make_hist() {
    local infile="$1"
    local lane="$2"
    local field_name="$3"
    local title="$4"
    local xlabel="$5"
    local legend="$6"
    local outpng="$7"
    local color="${LANE_COLORS[$lane]}"

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

    awk '{ print $2 }' "$infile" > "$values_file"

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
set xlabel "${xlabel} (us), Samples = ${sample_count}, Min = ${min_value} us, Max = ${max_value} us, Bucket = ${bucket_width} us"
set ylabel "Number of Samples"

set xrange [0:*]
set yrange [0.9:*]
set logscale y

set grid xtics ytics
set border linewidth 1
set key top right

plot "${histogram_file}" using \
(${min_value} + (\$1 * ${bucket_width})):2 \
with impulses linewidth 1 linecolor rgb "${color}" \
title "${legend}"
EOF

    echo "Lane     : $lane"
    echo "Input    : $infile"
    echo "Field    : $field_name"
    echo "Samples  : $sample_count"
    echo "Range    : ${min_value}..${max_value} us"
    echo "Bucket   : ${bucket_width} us"
    echo "Generated: $outpng"
    echo

    rm -f "$values_file" "$histogram_file"
}

for lane in 0 1 2 3; do
    make_hist \
        "$WAKEUP_BASE"_lane${lane}.txt \
        "$lane" \
        "wakeup_latency_us" \
        "Stream Worker Wake-up Latency Histogram (Lane ${lane})" \
        "Wake-up Latency" \
        "Lane ${lane}" \
        "$OUTDIR/plots/wakeup_latency_lane${lane}.png"

    make_hist \
        "$EXEC_BASE"_lane${lane}.txt \
        "$lane" \
        "execution_time_us" \
        "Stream Worker Execution Time Histogram (Lane ${lane})" \
        "Execution Time" \
        "Lane ${lane}" \
        "$OUTDIR/plots/execution_time_lane${lane}.png"
done

echo "Done."
