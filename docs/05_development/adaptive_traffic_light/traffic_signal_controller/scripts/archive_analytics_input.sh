#!/bin/bash

# Usage:
#   ./archive_analytics_input.sh [input_file] [output_directory]
#
# This archives the source data that Analytics reads. It does not archive any
# histogram/time-series intermediate data.

set -o pipefail

ANALYTICS_LOG_FILE="/tmp/CTRL.dlt"
DEFAULT_INPUT_FILE="${ANALYTICS_LOG_FILE}.txt"
RUNTIME_LOG_DIRECTORY="/tmp/traffic_signal_controller/logs"
DEFAULT_OUTPUT_DIRECTORY="${RUNTIME_LOG_DIRECTORY}/output"

INPUT_FILE=${1:-$DEFAULT_INPUT_FILE}
OUTPUT_DIRECTORY=${2:-$DEFAULT_OUTPUT_DIRECTORY}

if [ ! -s "$INPUT_FILE" ]; then
    if [ "$INPUT_FILE" = "$DEFAULT_INPUT_FILE" ] && [ -s "$ANALYTICS_LOG_FILE" ]; then
        INPUT_FILE="$ANALYTICS_LOG_FILE"
    else
        echo "Error: analytics input file not found or empty: $INPUT_FILE"
        echo "Tried default text input: $DEFAULT_INPUT_FILE"
        echo "Tried default DLT input : $ANALYTICS_LOG_FILE"
        exit 1
    fi
fi

mkdir -p "$OUTPUT_DIRECTORY"

NEXT_RUN_NUMBER=1
for existing_file in "$OUTPUT_DIRECTORY"/[0-9][0-9][0-9]_analytics_input.*; do
    [ -e "$existing_file" ] || continue

    existing_name=$(basename "$existing_file")
    existing_number=${existing_name%%_*}

    case "$existing_number" in
        ''|*[!0-9]*)
            continue
            ;;
    esac

    if [ "$existing_number" -ge "$NEXT_RUN_NUMBER" ]; then
        NEXT_RUN_NUMBER=$((existing_number + 1))
    fi
done

RUN_ID=$(printf "%03d" "$NEXT_RUN_NUMBER")
case "$INPUT_FILE" in
    *.dlt)
        ARCHIVE_EXTENSION="dlt"
        ;;
    *)
        ARCHIVE_EXTENSION="txt"
        ;;
esac

ARCHIVED_INPUT="${OUTPUT_DIRECTORY}/${RUN_ID}_analytics_input.${ARCHIVE_EXTENSION}"

cp "$INPUT_FILE" "$ARCHIVED_INPUT"

echo "Analytics input archived:"
echo "  Input : $INPUT_FILE"
echo "  Output: $ARCHIVED_INPUT"
