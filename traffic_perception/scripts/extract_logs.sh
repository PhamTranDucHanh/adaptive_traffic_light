#!/usr/bin/env bash
#
# extract_logs.sh
#
# Convert a DLT file to text and split benchmark logs by DLT context.
#
# Usage:
#   ./extract_logs.sh <input.dlt> <output_dir>
#
# Example:
#   ./extract_logs.sh /tmp/TPER.dlt ./output
#

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <input.dlt> <output_dir>"
    exit 1
fi

INPUT_DLT="$1"
OUT_DIR="$2"

if [[ ! -f "$INPUT_DLT" ]]; then
    echo "Error: '$INPUT_DLT' does not exist."
    exit 1
fi

if ! command -v dlt-convert >/dev/null 2>&1; then
    echo "Error: dlt-convert not found in PATH."
    exit 1
fi

mkdir -p "$OUT_DIR"

BASE_NAME="$(basename "$INPUT_DLT" .dlt)"
TXT_FILE="$OUT_DIR/${BASE_NAME}.txt"

echo "========================================"
echo "Converting DLT log..."
echo "Input : $INPUT_DLT"
echo "Output: $TXT_FILE"
echo "========================================"

# Convert DLT -> ASCII text
dlt-convert -a "$INPUT_DLT" > "$TXT_FILE"

echo
echo "Extracting benchmark logs..."

grep -F "STRM" "$TXT_FILE" > "$OUT_DIR/stream.log"   || true
grep -F "VIEW" "$TXT_FILE" > "$OUT_DIR/viewer.log"   || true
grep -F "PIPE" "$TXT_FILE" > "$OUT_DIR/pipeline.log" || true

echo
echo "========================================"
echo "Extraction completed."
echo "========================================"

printf "%-15s %6d lines\n" "stream.log"   "$(wc -l < "$OUT_DIR/stream.log")"
printf "%-15s %6d lines\n" "viewer.log"   "$(wc -l < "$OUT_DIR/viewer.log")"
printf "%-15s %6d lines\n" "pipeline.log" "$(wc -l < "$OUT_DIR/pipeline.log")"

echo
echo "Generated files:"
echo "  $TXT_FILE"
echo "  $OUT_DIR/stream.log"
echo "  $OUT_DIR/viewer.log"
echo "  $OUT_DIR/pipeline.log"
