#!/usr/bin/env bash

# Shared helpers for Perception plots. Input values are microseconds.

select_time_unit() {
    local max_us="${1:-0}"

    if awk -v value="$max_us" 'BEGIN {exit !(value < 1)}'; then
        TIME_UNIT="ns"
        TIME_SCALE="0.001"
    elif awk -v value="$max_us" 'BEGIN {exit !(value < 1000)}'; then
        TIME_UNIT="us"
        TIME_SCALE="1"
    elif awk -v value="$max_us" 'BEGIN {exit !(value < 1000000)}'; then
        TIME_UNIT="ms"
        TIME_SCALE="1000"
    else
        TIME_UNIT="s"
        TIME_SCALE="1000000"
    fi
}

format_scaled_time() {
    local value="$1"
    local scale="$2"

    awk -v value="$value" -v scale="$scale" '
      BEGIN {
        printf "%.6g", value / scale
      }
    '
}

# Select a common elapsed-time axis for all time-series plots.
# Runs shorter than two hours are clearer in minutes; longer runs use hours.
select_elapsed_axis() {
    local total_seconds="${1:-0}"

    if awk -v seconds="$total_seconds" 'BEGIN {exit !(seconds < 7200)}'; then
        ELAPSED_UNIT="minutes"
        ELAPSED_SHORT_UNIT="min"
        ELAPSED_SCALE="60"
    else
        ELAPSED_UNIT="hours"
        ELAPSED_SHORT_UNIT="h"
        ELAPSED_SCALE="3600"
    fi

    ELAPSED_TOTAL="$(awk -v seconds="$total_seconds" \
        -v scale="$ELAPSED_SCALE" 'BEGIN {printf "%.3f", seconds / scale}')"
}
