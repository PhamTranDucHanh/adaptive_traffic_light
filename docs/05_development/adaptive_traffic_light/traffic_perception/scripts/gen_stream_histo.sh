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

        print lane, wakeup >> wakeup_file;
        close(wakeup_file);
        print lane, runtime >> exec_file;
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
    local bucket_width="${BUCKET_WIDTH_US:-1}"

    if ! command -v gnuplot >/dev/null 2>&1; then
        echo "Error: gnuplot is not installed." >&2
        exit 1
    fi

    if [[ ! "$bucket_width" =~ ^[1-9][0-9]*$ ]]; then
        echo "Error: BUCKET_WIDTH_US must be a positive integer." >&2
        exit 1
    fi

    local total_samples=0
    local global_min=""
    local global_max=""
    local plot_cmd=""
    local cleanup_files=()

    for l in 0 1 2 3; do
        local current_infile="${infile}_lane${l}.txt"
        local color="${LANE_COLORS[$l]}"

        if [[ ! -s "$current_infile" ]]; then
            echo "Skip: $current_infile (empty)"
            continue
        fi

        local values_file histogram_file
        values_file="$(mktemp)"
        histogram_file="$(mktemp)"
        cleanup_files+=("$values_file" "$histogram_file")

        awk '{ print $2 }' "$current_infile" > "$values_file"

        if [[ ! -s "$values_file" ]]; then
            echo "Error: no samples found in $current_infile" >&2
            continue
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

        total_samples=$((total_samples + sample_count))
        if [[ -z "$global_min" ]] || awk -v a="$min_value" -v b="$global_min" 'BEGIN { exit (a < b ? 0 : 1) }'; then global_min=$min_value; fi
        if [[ -z "$global_max" ]] || awk -v a="$max_value" -v b="$global_max" 'BEGIN { exit (a > b ? 0 : 1) }'; then global_max=$max_value; fi

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

        if [[ -n "$plot_cmd" ]]; then
            plot_cmd+=", \\
"
        else
            plot_cmd="plot \\
"
        fi
        plot_cmd+="\"${histogram_file}\" using ((${min_value} + (\$1 * ${bucket_width})) / display_scale):2 with impulses linewidth 1 linecolor rgb \"${color}\" title \"Lane ${l}\""
    done

    if [[ -z "$plot_cmd" ]]; then
        echo "Error: no data to plot for $infile" >&2
        return 1
    fi

    local display_unit display_scale
    if awk -v value="$global_max" 'BEGIN {exit !(value < 1)}'; then
        display_unit="ns"
        display_scale="0.001"
    elif awk -v value="$global_max" 'BEGIN {exit !(value < 1000)}'; then
        display_unit="us"
        display_scale="1"
    elif awk -v value="$global_max" 'BEGIN {exit !(value < 1000000)}'; then
        display_unit="ms"
        display_scale="1000"
    else
        display_unit="s"
        display_scale="1000000"
    fi

    local display_min display_max display_bucket
    read -r display_min display_max display_bucket < <(
      awk -v min="$global_min" -v max="$global_max" \
          -v bucket="$bucket_width" -v scale="$display_scale" \
          'BEGIN {printf "%.6g %.6g %.6g\n", min / scale, max / scale, bucket / scale}'
    )

    gnuplot <<EOF
set terminal pngcairo size 1600,900 enhanced
set output "${outpng}"
display_scale = ${display_scale}

set title "${title}"
set xlabel "${xlabel} (${display_unit}), Samples = ${total_samples}, Min = ${display_min} ${display_unit}, Max = ${display_max} ${display_unit}, Bucket = ${display_bucket} ${display_unit}"
set ylabel "Number of Samples"

set autoscale x
set yrange [0.9:*]
set logscale y

set grid xtics ytics
set border linewidth 1
set key top right

${plot_cmd}
EOF

    echo "Base Input: $infile"
    echo "Field    : $field_name"
    echo "Samples  : $total_samples"
    echo "Range    : ${display_min}..${display_max} ${display_unit}"
    echo "Bucket   : ${display_bucket} ${display_unit}"
    echo "Generated: $outpng"
    echo

    rm -f "${cleanup_files[@]}"
}

make_hist \
    "$WAKEUP_BASE" \
    "all" \
    "wakeup_latency_us" \
    "Stream Worker Wake-up Latency Histogram" \
    "Wake-up Latency" \
    "All Lanes" \
    "$OUTDIR/plots/wakeup_latency.png"

make_hist \
    "$EXEC_BASE" \
    "all" \
    "execution_time_us" \
    "Stream Worker Execution Time Histogram" \
    "Execution Time" \
    "All Lanes" \
    "$OUTDIR/plots/execution_time.png"

echo "Done."
