#!/usr/bin/env bash

set -uo pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly WORKSPACE_ROOT="$(dirname "$SCRIPT_DIR")"
readonly RUNTIME_ROOT="${LINUX_RT_RUNTIME_DIR:-/tmp/linux_rt_application}"
readonly RUNTIME_LOGS="$RUNTIME_ROOT/logs"
readonly RUN_ID="$(date +%Y%m%d_%H%M%S)"
readonly OUTPUT_ROOT="${E2E_ANALYTICS_OUTPUT_DIR:-$WORKSPACE_ROOT/output/end_to_end}"
readonly RUN_OUTPUT="$OUTPUT_ROOT/$RUN_ID"
readonly DURATION="${1:-}"

APP_PID=""
TIMER_PID=""
STOP_REQUESTED=0
POSTPROCESS_FAILURES=0
readonly POSTPROCESS_LOG="$RUN_OUTPUT/postprocess.log"

usage() {
  cat <<EOF
Usage: $0 [duration]

Examples:
  $0          Run until Ctrl+C
  $0 60       Run for 60 seconds
  $0 5m       Run for 5 minutes
  $0 1h       Run for 1 hour
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
    warn "Lifecycle Stop request failed; forwarding SIGINT to Bazel."
    [[ -n "$APP_PID" ]] && kill -INT "$APP_PID" 2>/dev/null || true
  elif [[ -n "$APP_PID" ]] && kill -0 "$APP_PID" 2>/dev/null; then
    kill -INT "$APP_PID" 2>/dev/null || true
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

if [[ $# -gt 1 || "$DURATION" == "-h" || "$DURATION" == "--help" ]]; then
  usage
  [[ $# -le 1 && ("$DURATION" == "-h" || "$DURATION" == "--help") ]] && exit 0
  exit 2
fi

if [[ -n "$DURATION" && ! "$DURATION" =~ ^[1-9][0-9]*([smh])?$ ]]; then
  printf '[E2E][ERROR] invalid duration: %s (use 60, 60s, 5m, or 1h)\n' \
    "$DURATION" >&2
  exit 2
fi

for command in bazel dlt-convert gnuplot sudo; do
  require_command "$command"
done

mkdir -p "$RUN_OUTPUT"/{perception,timing_decision,signal_controller}
: >"$POSTPROCESS_LOG"

cd "$WORKSPACE_ROOT"

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

bazel run --config=x86_64-linux //deployment:traffic_light_system &
APP_PID=$!

if [[ -n "$DURATION" ]]; then
  parent_pid=$$
  (
    sleep "$DURATION"
    kill -INT "$parent_pid" 2>/dev/null || true
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
TIMING_MODULE_OUTPUT="$WORKSPACE_ROOT/traffic_timing_decision/output"

if [[ -s "$RUNTIME_LOGS/traffic_perception.dlt" ]]; then
  run_optional "Extracting Traffic Perception logs" \
    "$WORKSPACE_ROOT/traffic_perception/scripts/extract_logs.sh" \
    "$RUNTIME_LOGS/traffic_perception.dlt" "$PERCEPTION_DIR"
  run_with_report "Analyzing Traffic Perception pipeline logs" \
    "$PERCEPTION_DIR/pipeline_analytics_report.txt" \
    bazel run --config=x86_64-linux //traffic_perception:analyze_log \
    "$PERCEPTION_DIR/pipeline.log"
  run_with_report "Analyzing Traffic Perception stream logs" \
    "$PERCEPTION_DIR/stream_analytics_report.txt" \
    bazel run --config=x86_64-linux //traffic_perception:analyze_log \
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
  run_optional "Building Traffic Timing Decision analytics" \
    bazel build --config=x86_64-linux //traffic_timing_decision:analytics
  run_with_report "Printing Traffic Timing Decision analytics" \
    "$TIMING_DIR/analytics_report.txt" \
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

if [[ ! -s "$RUNTIME_LOGS/CTRL.dlt.txt" && -s "$RUNTIME_LOGS/CTRL.dlt" ]]; then
  log "Converting Signal Controller DLT log to text..."
  dlt-convert -a "$RUNTIME_LOGS/CTRL.dlt" >"$RUNTIME_LOGS/CTRL.dlt.txt"
fi

if [[ -s "$RUNTIME_LOGS/CTRL.dlt.txt" ]]; then
  cp "$RUNTIME_LOGS/CTRL.dlt.txt" "$CONTROLLER_DIR/CTRL.dlt.txt"
  if [[ -s "$RUNTIME_LOGS/signal_control_analytics_report.txt" ]]; then
    cp "$RUNTIME_LOGS/signal_control_analytics_report.txt" \
      "$CONTROLLER_DIR/analytics_report.txt"
  else
    warn "Signal Controller analytics report is missing or empty."
    POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
  fi

  for metric in wakeup execution; do
    run_optional "Plotting Signal Controller $metric time series" \
      "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_timeseries.sh" \
      "$metric" "$CONTROLLER_DIR/CTRL.dlt.txt" \
      "$CONTROLLER_DIR/${metric}_timeseries.png"
    run_optional "Plotting Signal Controller $metric histogram" \
      "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_histogram.sh" \
      "$metric" "$CONTROLLER_DIR/CTRL.dlt.txt" \
      "$CONTROLLER_DIR/${metric}_histogram.png"
  done

  run_optional "Plotting Signal Controller end-to-end time series" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_timeseries.sh" \
    end_to_end "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$CONTROLLER_DIR/decision_to_controller_latency_timeseries.png"
  run_optional "Plotting Signal Controller end-to-end histogram" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_histogram.sh" \
    end_to_end "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$CONTROLLER_DIR/decision_to_controller_latency_histogram.png"
  run_optional "Plotting Signal Controller emergency time series" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_timeseries.sh" \
    emergency "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$CONTROLLER_DIR/emergency_receive_to_apply_latency_timeseries.png"
  run_optional "Plotting Signal Controller emergency histogram" \
    "$WORKSPACE_ROOT/traffic_signal_controller/scripts/plot_analytics_histogram.sh" \
    emergency "$CONTROLLER_DIR/CTRL.dlt.txt" \
    "$CONTROLLER_DIR/emergency_receive_to_apply_latency_histogram.png"
else
  warn "Signal Controller converted log is missing or empty."
  POSTPROCESS_FAILURES=$((POSTPROCESS_FAILURES + 1))
fi

log "Generated analytics reports and plots: $RUN_OUTPUT"

if ((POSTPROCESS_FAILURES != 0)); then
  warn "$POSTPROCESS_FAILURES optional analytics/plot step(s) had no data or failed."
fi

if ((APP_STATUS != 0 && APP_STATUS != 130 && STOP_REQUESTED == 0)); then
  exit "$APP_STATUS"
fi
exit 0
