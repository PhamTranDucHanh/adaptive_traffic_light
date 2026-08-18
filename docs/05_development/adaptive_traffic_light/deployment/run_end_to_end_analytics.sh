#!/usr/bin/env bash

set -uo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly WORKSPACE_ROOT="$(dirname "$SCRIPT_DIR")"
readonly RUNTIME_ROOT="${LINUX_RT_RUNTIME_DIR:-/tmp/linux_rt_application}"
readonly RUNTIME_LOGS="$RUNTIME_ROOT/logs"
readonly RUN_ID="$(date +%Y%m%d_%H%M%S)"
readonly RUN_START_EPOCH="$(date +%s)"
readonly OUTPUT_ROOT="${E2E_ANALYTICS_OUTPUT_DIR:-$WORKSPACE_ROOT/output/end_to_end}"
readonly RUN_OUTPUT="$OUTPUT_ROOT/$RUN_ID"

DURATION=""
VENDOR_DIR=""
BAZEL_BUILD_ARGS=(--config=x86_64-linux)

APP_PID=""
TIMER_PID=""
STOP_REQUESTED=0
POSTPROCESS_FAILURES=0
readonly POSTPROCESS_LOG="$RUN_OUTPUT/postprocess.log"

usage() {
  cat <<EOF
Usage: $0 [duration] [--vendor-dir <path>]

Examples:
  $0                         Run normally until Ctrl+C
  $0 60                      Run normally for 60 seconds
  $0 5m --vendor-dir vendor  Run for 5 minutes using vendored dependencies
  $0 --vendor-dir=/data/vendor
                             Run until Ctrl+C using an absolute vendor path

Options:
  --vendor-dir <path>  Pass --vendor_dir=<path> to every Bazel build/run.
                       Relative paths are resolved from the workspace root.
  -h, --help           Show this help.
EOF
}

log() {
  printf '[E2E] %s\n' "$*"
}

warn() {
  printf '[E2E][WARN] %s\n' "$*" >&2
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || {
    printf '[E2E][ERROR] required command not found: %s\n' "$1" >&2
    exit 1
  }
}

prepare_script_permissions() {
  local -a script_directories=(
    "$WORKSPACE_ROOT/deployment"
    "$WORKSPACE_ROOT/traffic_perception/scripts"
    "$WORKSPACE_ROOT/traffic_timing_decision/scripts"
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts"
  )
  local script_directory=""
  local script_path=""
  local script_count=0

  for script_directory in "${script_directories[@]}"; do
    if [[ ! -d "$script_directory" ]]; then
      printf '[E2E][ERROR] script directory not found: %s\n' \
        "$script_directory" >&2
      exit 1
    fi

    for script_path in "$script_directory"/*.sh; do
      [[ -f "$script_path" ]] || continue
      if ! chmod +x -- "$script_path"; then
        printf '[E2E][ERROR] could not make script executable: %s\n' \
          "$script_path" >&2
        exit 1
      fi
      script_count=$((script_count + 1))
    done
  done

  if ((script_count == 0)); then
    printf '[E2E][ERROR] no shell scripts found to prepare\n' >&2
    exit 1
  fi

  log "Executable permission prepared for $script_count shell scripts."
}

request_stop() {
  if ((STOP_REQUESTED != 0)); then
    return
  fi
  STOP_REQUESTED=1
  log "Stopping the traffic-light system..."

  if [[ -x "$RUNTIME_ROOT/bin/lmcontrol" ]]; then
    if "$RUNTIME_ROOT/bin/lmcontrol" Stop; then
      return
    fi
    warn "Lifecycle Stop request failed; forwarding SIGTERM to Bazel."
    [[ -n "$APP_PID" ]] && kill -TERM "$APP_PID" 2>/dev/null || true
  elif [[ -n "$APP_PID" ]] && kill -0 "$APP_PID" 2>/dev/null; then
    kill -TERM "$APP_PID" 2>/dev/null || true
  fi
}

run_optional() {
  local description="$1"
  shift

  {
    printf '\n===== %s =====\n' "$description"
    printf 'Command:'
    printf ' %q' "$@"
    printf '\n'
  } >>"$POSTPROCESS_LOG"

  local status=0
  if "$@" >>"$POSTPROCESS_LOG" 2>&1; then
    log "$description completed."
    return 0
  else
    status=$?
  fi

  warn "$description failed with status $status; see $POSTPROCESS_LOG"
  POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
  return 0
}

run_with_report() {
  local description="$1"
  local report_file="$2"
  shift 2

  if "$@" >"$report_file" 2>&1; then
    return 0
  fi

  warn "$description failed; see $report_file"
  POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
  return 0
}

extract_report_section() {
  local input_file="$1"
  local start_title="$2"
  local next_title="$3"
  local output_file="$4"

  awk -v start="$start_title" -v stop="$next_title" '
    $0 == start {capture=1}
    capture && $0 == stop {exit}
    capture {print}
  ' "$input_file" >"$output_file"

  if [[ ! -s "$output_file" ]]; then
    warn "Could not extract report section: $start_title"
    POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
  fi
}

cleanup_perception_intermediates() {
  local perception_dir="$1"
  local -a intermediate_files=(
    "$perception_dir/pipeline.log_statistics.txt"
    "$perception_dir/stream.log_statistics.txt"
    "$perception_dir/pipeline_metrics.txt"
    "$perception_dir/wakeup_latency.txt"
    "$perception_dir/execution_time.txt"
    "$perception_dir/pipeline_line_metrics.txt"
    "$perception_dir/stream_line_metrics_lane0.txt"
    "$perception_dir/stream_line_metrics_lane1.txt"
    "$perception_dir/stream_line_metrics_lane2.txt"
    "$perception_dir/stream_line_metrics_lane3.txt"
    "$perception_dir/wakeup_latency_lane0.txt"
    "$perception_dir/wakeup_latency_lane1.txt"
    "$perception_dir/wakeup_latency_lane2.txt"
    "$perception_dir/wakeup_latency_lane3.txt"
    "$perception_dir/execution_time_lane0.txt"
    "$perception_dir/execution_time_lane1.txt"
    "$perception_dir/execution_time_lane2.txt"
    "$perception_dir/execution_time_lane3.txt"
    "$perception_dir/plots/wakeup_latency.dat"
  )
  local removed_count=0
  local intermediate_file=""

  # These files are inputs generated solely for the reports and PNGs above.
  # Keep the converted/source logs because the ftrace diagnostic consumes them
  # after this script exits, and keep all final reports and plots.
  for intermediate_file in "${intermediate_files[@]}"; do
    if [[ -e "$intermediate_file" ]]; then
      if rm -f -- "$intermediate_file"; then
        removed_count=$((removed_count + 1))
      else
        warn "Could not remove Traffic Perception intermediate: $intermediate_file"
        POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
      fi
    fi
  done

  log "Removed $removed_count intermediate Traffic Perception file(s)."
}

while (($# > 0)); do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --vendor-dir|--vendor_dir)
      if [[ -n "$VENDOR_DIR" ]]; then
        printf '[E2E][ERROR] vendor directory was specified more than once\n' >&2
        exit 2
      fi
      if (($# < 2)) || [[ -z "$2" ]]; then
        printf '[E2E][ERROR] %s requires a directory path\n' "$1" >&2
        exit 2
      fi
      VENDOR_DIR="$2"
      shift 2
      ;;
    --vendor-dir=*|--vendor_dir=*)
      if [[ -n "$VENDOR_DIR" ]]; then
        printf '[E2E][ERROR] vendor directory was specified more than once\n' >&2
        exit 2
      fi
      VENDOR_DIR="${1#*=}"
      if [[ -z "$VENDOR_DIR" ]]; then
        printf '[E2E][ERROR] vendor directory path must not be empty\n' >&2
        exit 2
      fi
      shift
      ;;
    --*)
      printf '[E2E][ERROR] unknown option: %s\n' "$1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ -n "$DURATION" ]]; then
        printf '[E2E][ERROR] unexpected argument: %s\n' "$1" >&2
        usage >&2
        exit 2
      fi
      DURATION="$1"
      shift
      ;;
  esac
done

if [[ -n "$DURATION" && ! "$DURATION" =~ ^[1-9][0-9]*([smh])?$ ]]; then
  printf '[E2E][ERROR] invalid duration: %s (use 60, 60s, 5m, or 1h)\n' \
    "$DURATION" >&2
  exit 2
fi

if [[ -n "$VENDOR_DIR" ]]; then
  if [[ "$VENDOR_DIR" != /* ]]; then
    VENDOR_DIR="$WORKSPACE_ROOT/$VENDOR_DIR"
  fi
  if [[ ! -d "$VENDOR_DIR" ]]; then
    printf '[E2E][ERROR] Bazel vendor directory does not exist: %s\n' \
      "$VENDOR_DIR" >&2
    exit 2
  fi
  if [[ ! -f "$VENDOR_DIR/VENDOR.bazel" ]]; then
    printf '[E2E][ERROR] Bazel vendor metadata is missing: %s/VENDOR.bazel\n' \
      "$VENDOR_DIR" >&2
    exit 2
  fi
  VENDOR_DIR="$(cd "$VENDOR_DIR" && pwd -P)"
  BAZEL_BUILD_ARGS+=("--vendor_dir=$VENDOR_DIR")
fi

for command in bazel dlt-convert gnuplot sudo; do
  require_command "$command"
done

mkdir -p \
  "$RUN_OUTPUT"/{perception,timing_decision,signal_controller,end_to_end} \
  "$RUN_OUTPUT/signal_controller/plots" \
  "$RUN_OUTPUT/end_to_end/plots"
: >"$POSTPROCESS_LOG"

cd "$WORKSPACE_ROOT"

if [[ -n "$VENDOR_DIR" ]]; then
  log "Bazel vendor mode enabled: $VENDOR_DIR"
else
  log "Bazel vendor mode disabled; using normal dependency resolution."
fi

log "Preparing executable permissions for deployment and module scripts..."
prepare_script_permissions

log "Checking Traffic Perception runtime dependencies..."
bash "$WORKSPACE_ROOT/traffic_perception/scripts/setup_deps.sh"

log "Configuring realtime limits for this script and its child processes..."
sudo prlimit --pid "$$" --rtprio=99:99 --memlock=unlimited:unlimited
sudo sysctl -w kernel.sched_rt_runtime_us=-1

log "Stopping the Bazel server so it inherits the new realtime limits..."
bazel shutdown

trap request_stop INT TERM

log "Starting //deployment:traffic_light_system"
if [[ -n "$DURATION" ]]; then
  log "Automatic stop after $DURATION"
else
  log "Running until Ctrl+C"
fi

bazel run "${BAZEL_BUILD_ARGS[@]}" //deployment:traffic_light_system &
APP_PID=$!

if [[ -n "$DURATION" ]]; then
  parent_pid=$$
  (
    sleep "$DURATION"
    # This script can itself be launched as a background job. Bash starts
    # asynchronous jobs with SIGINT ignored, so an INT-based timer may never
    # reach request_stop. SIGTERM remains trappable and follows the same
    # graceful lifecycle shutdown path.
    kill -TERM "$parent_pid" 2>/dev/null || true
  ) &
  TIMER_PID=$!
fi

APP_STATUS=0
while true; do
  wait "$APP_PID"
  APP_STATUS=$?
  kill -0 "$APP_PID" 2>/dev/null || break
done
APP_PID=""

if [[ -n "$TIMER_PID" ]]; then
  kill "$TIMER_PID" 2>/dev/null || true
  wait "$TIMER_PID" 2>/dev/null || true
  TIMER_PID=""
fi

trap - INT TERM

# Ctrl+C and the duration timer are expected exits. A different deployment
# failure is retained, but analytics are still attempted for available logs.
if ((APP_STATUS != 0 && APP_STATUS != 130 && STOP_REQUESTED == 0)); then
  warn "deployment exited with status $APP_STATUS"
fi

PERCEPTION_DIR="$RUN_OUTPUT/perception"
TIMING_DIR="$RUN_OUTPUT/timing_decision"
CONTROLLER_DIR="$RUN_OUTPUT/signal_controller"
END_TO_END_DIR="$RUN_OUTPUT/end_to_end"
CONTROLLER_PLOTS_DIR="$CONTROLLER_DIR/plots"
END_TO_END_PLOTS_DIR="$END_TO_END_DIR/plots"
TIMING_MODULE_OUTPUT="$WORKSPACE_ROOT/traffic_timing_decision/output"

# Prepare every module's immutable input inside this run directory before any
# plot is generated. This prevents plots from accidentally reading a converted
# text file left by an earlier session.
if [[ -s "$RUNTIME_LOGS/traffic_perception.dlt" ]]; then
  run_optional "Extracting Traffic Perception logs" \
    "$WORKSPACE_ROOT/traffic_perception/scripts/extract_logs.sh" \
    "$RUNTIME_LOGS/traffic_perception.dlt" "$PERCEPTION_DIR"
fi

if [[ -s "$RUNTIME_LOGS/timing_decision.dlt" && \
      -s "$RUNTIME_LOGS/wakeup_latency.dlt" && \
      -s "$RUNTIME_LOGS/execution_time.dlt" ]]; then
  mkdir -p "$TIMING_MODULE_OUTPUT"
  cp "$RUNTIME_LOGS"/{timing_decision,wakeup_latency,execution_time}.dlt \
    "$TIMING_MODULE_OUTPUT/"
  dlt-convert -a "$TIMING_MODULE_OUTPUT/wakeup_latency.dlt" \
    > "$TIMING_MODULE_OUTPUT/wakeup_latency.txt"
  dlt-convert -a "$TIMING_MODULE_OUTPUT/execution_time.dlt" \
    > "$TIMING_MODULE_OUTPUT/execution_time.txt"
  cp "$TIMING_MODULE_OUTPUT"/{timing_decision,wakeup_latency,execution_time}.dlt \
    "$TIMING_DIR/"
  cp "$TIMING_MODULE_OUTPUT"/{wakeup_latency,execution_time}.txt "$TIMING_DIR/"
fi

if [[ -s "$RUNTIME_LOGS/CTRL.dlt" ]]; then
  cp "$RUNTIME_LOGS/CTRL.dlt" "$CONTROLLER_DIR/CTRL.dlt"
  dlt-convert -a "$CONTROLLER_DIR/CTRL.dlt" \
    > "$CONTROLLER_DIR/CTRL.dlt.txt"
elif [[ -s "$RUNTIME_LOGS/CTRL.dlt.txt" ]]; then
  warn "CTRL.dlt is unavailable; using the existing converted text log."
  cp "$RUNTIME_LOGS/CTRL.dlt.txt" "$CONTROLLER_DIR/CTRL.dlt.txt"
fi

# All producers use CLOCK_MONOTONIC. Build one shared full-run window from all
# available module timestamps, then export it to every time-series script.
TIME_WINDOW_INPUTS=()
for input_file in \
  "$PERCEPTION_DIR/pipeline.log" \
  "$PERCEPTION_DIR/stream.log" \
  "$PERCEPTION_DIR/viewer.log" \
  "$TIMING_DIR/wakeup_latency.txt" \
  "$TIMING_DIR/execution_time.txt" \
  "$CONTROLLER_DIR/CTRL.dlt.txt"; do
  [[ -s "$input_file" ]] && TIME_WINDOW_INPUTS+=("$input_file")
done

if (( ${#TIME_WINDOW_INPUTS[@]} != 0 )); then
  TIME_WINDOW_VALUES="$RUN_OUTPUT/time_window_samples_ns.txt"
  awk '
    {
      remaining = $0
      pattern = "(ExpectedWakeup|Begin|End|actual_wakeup_ns|execution_start_ns|controller_receive_timestamp_ns|apply_timestamp_ns)[[:space:]]*=[[:space:]]*[0-9]+"
      while (match(remaining, pattern)) {
        value = substr(remaining, RSTART, RLENGTH)
        sub(".*=[[:space:]]*", "", value)
        if (value != "0") {
          numeric_value = value + 0
          if (!found || numeric_value < minimum) {
            minimum = numeric_value
            minimum_text = value
          }
          if (!found || numeric_value > maximum) {
            maximum = numeric_value
            maximum_text = value
          }
          found = 1
        }
        remaining = substr(remaining, RSTART + RLENGTH)
      }
    }
    END {
      if (found) {
        print minimum_text
        print maximum_text
      }
    }
  ' "${TIME_WINDOW_INPUTS[@]}" > "$TIME_WINDOW_VALUES"

  if [[ -s "$TIME_WINDOW_VALUES" ]]; then
    ANALYTICS_TIME_ORIGIN_NS="$(awk 'NR == 1 {print; exit}' "$TIME_WINDOW_VALUES")"
    ANALYTICS_TIME_END_NS="$(awk 'END {print}' "$TIME_WINDOW_VALUES")"
    export ANALYTICS_TIME_ORIGIN_NS ANALYTICS_TIME_END_NS
    log "Shared CLOCK_MONOTONIC plot window: ${ANALYTICS_TIME_ORIGIN_NS}..${ANALYTICS_TIME_END_NS} ns"
  else
    warn "No CLOCK_MONOTONIC timestamps found; plots will use local ranges."
  fi
fi

if [[ -s "$PERCEPTION_DIR/pipeline.log" || -s "$PERCEPTION_DIR/stream.log" ]]; then
  run_with_report "Analyzing Traffic Perception pipeline logs" \
    "$PERCEPTION_DIR/perc_pipe_statistic.txt" \
    bazel run "${BAZEL_BUILD_ARGS[@]}" //traffic_perception:analyze_log \
    "$PERCEPTION_DIR/pipeline.log"
  run_with_report "Analyzing Traffic Perception stream logs" \
    "$PERCEPTION_DIR/perc_stream_statistic.txt" \
    bazel run "${BAZEL_BUILD_ARGS[@]}" //traffic_perception:analyze_log \
    "$PERCEPTION_DIR/stream.log"
  run_optional "Plotting Traffic Perception pipeline histograms" \
    "$WORKSPACE_ROOT/traffic_perception/scripts/gen_histogram.sh" \
    "$PERCEPTION_DIR/pipeline.log" "$PERCEPTION_DIR"
  run_optional "Plotting Traffic Perception stream histograms" \
    "$WORKSPACE_ROOT/traffic_perception/scripts/gen_stream_histo.sh" \
    "$PERCEPTION_DIR/stream.log" "$PERCEPTION_DIR"
  run_optional "Plotting Traffic Perception pipeline time series" \
    "$WORKSPACE_ROOT/traffic_perception/scripts/gen_line_plot.sh" \
    "$PERCEPTION_DIR/pipeline.log" "$PERCEPTION_DIR"
  run_optional "Plotting Traffic Perception stream time series" \
    "$WORKSPACE_ROOT/traffic_perception/scripts/gen_stream_line_plot.sh" \
    "$PERCEPTION_DIR/stream.log" "$PERCEPTION_DIR"
  run_optional "Plotting Traffic Perception per-frame stream timing" \
    "$WORKSPACE_ROOT/traffic_perception/scripts/gen_stream_wakeup_frameid.sh" \
    "$PERCEPTION_DIR/stream.log" "$PERCEPTION_DIR/plots"
else
  warn "Traffic Perception log is missing or empty: $RUNTIME_LOGS/traffic_perception.dlt"
  POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
fi

if [[ -s "$RUNTIME_LOGS/timing_decision.dlt" && \
      -s "$RUNTIME_LOGS/wakeup_latency.dlt" && \
      -s "$RUNTIME_LOGS/execution_time.dlt" ]]; then
  run_optional "Building Traffic Timing Decision analytics" \
    bazel build "${BAZEL_BUILD_ARGS[@]}" //traffic_timing_decision:analytics
  run_with_report "Printing Traffic Timing Decision analytics" \
    "$TIMING_DIR/deci_statistic.txt" \
    "$WORKSPACE_ROOT/bazel-bin/traffic_timing_decision/analytics" \
    "$TIMING_DIR/wakeup_latency.txt" "$TIMING_DIR/execution_time.txt"
  run_optional "Plotting Timing Decision wakeup histogram" \
    "$WORKSPACE_ROOT/traffic_timing_decision/scripts/histogram.sh" \
    "$TIMING_DIR/wakeup_latency.txt"
  run_optional "Plotting Timing Decision execution histogram" \
    "$WORKSPACE_ROOT/traffic_timing_decision/scripts/histogram.sh" \
    "$TIMING_DIR/execution_time.txt"
  run_optional "Plotting Timing Decision wakeup over time" \
    "$WORKSPACE_ROOT/traffic_timing_decision/scripts/latency_time.sh" \
    "$TIMING_DIR/wakeup_latency.txt"
  run_optional "Plotting Timing Decision execution over time" \
    "$WORKSPACE_ROOT/traffic_timing_decision/scripts/latency_time.sh" \
    "$TIMING_DIR/execution_time.txt"
else
  warn "Timing Decision timing/wakeup/execution logs are missing or empty."
  POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
fi

if [[ -s "$CONTROLLER_DIR/CTRL.dlt.txt" ]]; then
  CONTROLLER_REPORT="$RUNTIME_LOGS/signal_control_analytics_report.txt"
  CONTROLLER_REPORT_MTIME=0
  [[ -e "$CONTROLLER_REPORT" ]] && \
    CONTROLLER_REPORT_MTIME="$(stat -c %Y "$CONTROLLER_REPORT")"
  if [[ -s "$CONTROLLER_REPORT" && \
        "$CONTROLLER_REPORT_MTIME" -ge "$RUN_START_EPOCH" ]]; then
    extract_report_section \
      "$CONTROLLER_REPORT" \
      "PERCEPTION PUBLISH-TO-CONTROLLER RECEIVE LATENCY" \
      "TIMING DECISION RECEIVE-TO-CONTROLLER RECEIVE LATENCY" \
      "$END_TO_END_DIR/perc_to_ctrl_statistic.txt"
    extract_report_section \
      "$CONTROLLER_REPORT" \
      "TIMING DECISION RECEIVE-TO-CONTROLLER RECEIVE LATENCY" \
      "EMERGENCY RECEIVE-TO-APPLY LATENCY" \
      "$END_TO_END_DIR/deci_to_ctrl_statistic.txt"

    # The complete runtime report remains the source for E2E extraction.
    # Hide the two E2E sections only from the Signal Controller report copy so
    # the same statistics are not displayed in two output categories.
    awk '
      $0 == "PERCEPTION PUBLISH-TO-CONTROLLER RECEIVE LATENCY" {skip=1}
      $0 == "EMERGENCY RECEIVE-TO-APPLY LATENCY" {skip=0}
      !skip {print}
    ' "$CONTROLLER_REPORT" >"$CONTROLLER_DIR/ctrl_statistic.txt"
  else
    warn "Signal Controller analytics report is missing or stale for this run."
    POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
  fi

  for metric in wakeup execution; do
    run_optional "Plotting Signal Controller $metric time series" \
      "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_timeseries.sh" \
      "$metric" "$CONTROLLER_DIR/CTRL.dlt.txt" \
      "$CONTROLLER_PLOTS_DIR/ctrl_${metric}_timeseries.png"
    run_optional "Plotting Signal Controller $metric histogram" \
      "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_histogram.sh" \
      "$metric" "$CONTROLLER_DIR/CTRL.dlt.txt" \
      "$CONTROLLER_PLOTS_DIR/ctrl_${metric}_histogram.png"
  done

  run_optional "Plotting Signal Controller end-to-end time series" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_timeseries.sh" \
    end_to_end "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$END_TO_END_PLOTS_DIR/deci_to_ctrl_timeseries.png"
  run_optional "Plotting Signal Controller end-to-end histogram" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_histogram.sh" \
    end_to_end "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$END_TO_END_PLOTS_DIR/deci_to_ctrl_histogram.png"
  run_optional "Plotting Perception-to-Controller time series" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_timeseries.sh" \
    perception_to_controller "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$END_TO_END_PLOTS_DIR/perc_to_ctrl_timeseries.png"
  run_optional "Plotting Perception-to-Controller histogram" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_histogram.sh" \
    perception_to_controller "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$END_TO_END_PLOTS_DIR/perc_to_ctrl_histogram.png"
  run_optional "Plotting Signal Controller emergency time series" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_timeseries.sh" \
    emergency "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$CONTROLLER_PLOTS_DIR/ctrl_emergency_timeseries.png"
  run_optional "Plotting Signal Controller emergency histogram" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_histogram.sh" \
    emergency "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$CONTROLLER_PLOTS_DIR/ctrl_emergency_histogram.png"
else
  warn "Signal Controller converted log is missing or empty."
  POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
fi

cleanup_perception_intermediates "$PERCEPTION_DIR"

log "Generated analytics reports and plots: $RUN_OUTPUT"

if ((POSTPROCESS_FAILURES != 0)); then
  warn "$POSTPROCESS_FAILURES optional analytics/plot step(s) had no data or failed."
fi

if ((APP_STATUS != 0 && APP_STATUS != 130 && STOP_REQUESTED == 0)); then
  exit "$APP_STATUS"
fi
exit 0
