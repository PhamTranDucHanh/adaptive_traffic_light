#!/usr/bin/env bash

set -euo pipefail

if [[ "${1:-}" == "--activate-running" ]]; then
  if [[ $# -ne 3 ]]; then
    echo "[DEPLOYMENT][ERROR] invalid transition helper arguments" >&2
    exit 1
  fi

  lmcontrol_binary="$2"
  launch_manager_pid="$3"
  request_output=""

  for attempt in $(seq 1 50); do
    if ! kill -0 "$launch_manager_pid" 2>/dev/null; then
      echo "[LAUNCH_MANAGER][ERROR] exited during Startup" >&2
      exit 1
    fi

    if request_output=$("$lmcontrol_binary" Running 2>&1); then
      echo "$request_output"
      echo "[DEPLOYMENT][TRANSITION] Running activated on attempt=$attempt"
      echo "[DEPLOYMENT][RUN] requested run_target=Running"
      echo "[DEPLOYMENT][RUN] stop with Ctrl-C or: bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Stop"
      exit 0
    else
      request_status=$?
    fi
    if [[ $request_status -eq 2 ]]; then
      echo "$request_output" >&2
      echo "[DEPLOYMENT][ERROR] Running activation failed" >&2
      kill -TERM "$launch_manager_pid" 2>/dev/null || true
      exit 1
    fi
    sleep 0.1
  done

  echo "[CONTROL_DAEMON][ERROR] did not consume request before timeout" >&2
  kill -TERM "$launch_manager_pid" 2>/dev/null || true
  exit 1
fi

# --- begin runfiles.bash initialization v3 ---
set +e
f=bazel_tools/tools/bash/runfiles/runfiles.bash
source "${RUNFILES_DIR:-/dev/null}/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "${RUNFILES_MANIFEST_FILE:-/dev/null}" | cut -d' ' -f2-)" 2>/dev/null || \
  source "$0.runfiles/$f" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.runfiles_manifest" | cut -d' ' -f2-)" 2>/dev/null || \
  source "$(grep -sm1 "^$f " "$0.exe.runfiles_manifest" | cut -d' ' -f2-)" 2>/dev/null
set -e
if ! type rlocation >/dev/null 2>&1; then
  echo "ERROR: cannot find Bazel runfiles library" >&2
  exit 1
fi
# --- end runfiles.bash initialization v3 ---

if [[ $# -ne 14 ]]; then
  echo "ERROR: deployment target received an invalid runfiles layout" >&2
  exit 1
fi

launch_manager="$(rlocation "$1")"
control_daemon="$(rlocation "$2")"
lmcontrol="$(rlocation "$3")"
demo_perception="$(rlocation "$4")"
timing_decision="$(rlocation "$5")"
traffic_signal_controller="$(rlocation "$6")"
ipc_reset="$(rlocation "$7")"
generated_config="$(rlocation "$8")"
ecu_logging_config="$(rlocation "$9")"
hm_logging_config="$(rlocation "${10}")"
lm_logging_config="$(rlocation "${11}")"
perception_logging_config="$(rlocation "${12}")"
timing_decision_logging_config="$(rlocation "${13}")"
signal_control_logging_config="$(rlocation "${14}")"

runtime_root="${LINUX_RT_RUNTIME_DIR:-/tmp/linux_rt_application}"
runtime_bin="$runtime_root/bin"
runtime_etc="$runtime_root/etc"
runtime_logs="$runtime_root/logs"
timing_decision_dlt="$runtime_logs/timing_decision.dlt"
timing_decision_backend_dlt="$runtime_logs/DECI.dlt"
wakeup_latency_dlt="$runtime_logs/wakeup_latency.dlt"
wakeup_latency_backend_dlt="$runtime_logs/WKUP.dlt"
execution_time_dlt="$runtime_logs/execution_time.dlt"
execution_time_backend_dlt="$runtime_logs/EXEC.dlt"
signal_control_dlt="$runtime_logs/signal_control.dlt"
signal_control_backend_dlt="$runtime_logs/CTRL.dlt"
signal_control_converted_log="$runtime_logs/CTRL.dlt.txt"
signal_control_analytics_report="$runtime_logs/signal_control_analytics_report.txt"

# Must be at least the maximum SCHED_FIFO priority requested by any managed
# component in config/traffic_light_lifecycle.json.
required_rt_priority=80
rtprio_limit="$(ulimit -r)"
cap_eff_hex="$(sed -n 's/^CapEff:[[:space:]]*//p' /proc/self/status)"
has_cap_sys_nice=0
if [[ -n "$cap_eff_hex" ]] && \
    (( (16#$cap_eff_hex & (1 << 23)) != 0 )); then
  has_cap_sys_nice=1
fi

if [[ "$rtprio_limit" != "unlimited" ]] && \
    (( rtprio_limit < required_rt_priority )) && \
    (( has_cap_sys_nice == 0 )); then
  echo "[DEPLOYMENT][RT][ERROR] SCHED_FIFO priority=$required_rt_priority requires RLIMIT_RTPRIO >= $required_rt_priority or CAP_SYS_NICE; current_rtprio=$rtprio_limit" >&2
  echo '[DEPLOYMENT][RT][HINT] current shell: sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited' >&2
  echo '[DEPLOYMENT][RT][HINT] then run: bazel shutdown; rerun the deployment' >&2
  exit 1
fi

mkdir -p "$runtime_bin" "$runtime_etc" "$runtime_logs"
# S-CORE's file backend derives the canonical file name from the four-byte DLT
# application ID. Keep protocol-correct four-byte APIDs while exposing each
# recorder through a descriptive hard link to the same freshly truncated file.
rm -f "$runtime_logs/test.log" "$timing_decision_dlt" \
  "$timing_decision_backend_dlt" "$runtime_logs/PERC.dlt" \
  "$runtime_logs/SIGC.dlt" "$runtime_logs/adaptive_traffic.dlt" \
  "$runtime_logs/ADPT.dlt" \
  "$wakeup_latency_backend_dlt" "$execution_time_backend_dlt" \
  "$signal_control_dlt" "$signal_control_backend_dlt" \
  "$signal_control_converted_log" \
  "$signal_control_analytics_report"
: > "$timing_decision_dlt"
: > "$wakeup_latency_dlt"
: > "$execution_time_dlt"
: > "$signal_control_dlt"
: > "$signal_control_converted_log"
: > "$signal_control_analytics_report"
ln "$timing_decision_dlt" "$timing_decision_backend_dlt"
ln "$wakeup_latency_dlt" "$wakeup_latency_backend_dlt"
ln "$execution_time_dlt" "$execution_time_backend_dlt"
ln "$signal_control_dlt" "$signal_control_backend_dlt"
# Lifecycle sandboxes may run managed applications under a uid different from
# the deployment wrapper. All files are pre-created so recorders never need a
# world-writable directory; grant write access only to these runtime artifacts.
chmod 0666 "$timing_decision_dlt" "$wakeup_latency_dlt" \
  "$execution_time_dlt" "$signal_control_dlt" \
  "$signal_control_converted_log" \
  "$signal_control_analytics_report"

# Do not redirect stdout/stderr: Launch Manager and all three managed
# applications inherit the Bazel terminal directly. Timing Decision's S-CORE
# composite recorder independently fans each DECI log record out to the
# console and its DLT file.
echo "[DEPLOYMENT][LOG] timing decision DLT=$timing_decision_dlt"
echo "[DEPLOYMENT][LOG] wake-up latency DLT=$wakeup_latency_dlt"
echo "[DEPLOYMENT][LOG] execution/deadline DLT=$execution_time_dlt"
echo "[DEPLOYMENT][LOG] signal control DLT=$signal_control_dlt"
echo "[DEPLOYMENT][STAGE] preparing runtime=$runtime_root"
if ! "$ipc_reset"; then
  echo "[DEPLOYMENT][IPC][ERROR] could not reset POSIX MQ objects" >&2
  exit 1
fi
echo "[DEPLOYMENT][IPC] stale POSIX MQ objects removed"
install -m 0755 "$launch_manager" "$runtime_bin/launch_manager"
install -m 0755 "$control_daemon" "$runtime_bin/control_daemon"
install -m 0755 "$lmcontrol" "$runtime_bin/lmcontrol"
install -m 0755 "$demo_perception" "$runtime_bin/demo_perception"
install -m 0755 "$timing_decision" "$runtime_bin/traffic_timing_decision"
install -m 0755 "$traffic_signal_controller" \
  "$runtime_bin/traffic_signal_controller"
# Bazel outputs are read-only. A plain recursive copy preserves that mode, so
# the next run cannot truncate the existing generated configuration files.
# Unlink each old destination before copying to keep staging repeatable.
cp -R --remove-destination "$generated_config"/. "$runtime_etc"/
install -m 0644 "$ecu_logging_config" "$runtime_etc/ecu_logging_config.json"
install -m 0644 "$hm_logging_config" "$runtime_etc/hm_logging.json"
install -m 0644 "$lm_logging_config" "$runtime_etc/logging.json"
install -m 0644 "$perception_logging_config" \
  "$runtime_etc/perception_logging.json"
install -m 0644 "$timing_decision_logging_config" \
  "$runtime_etc/timing_decision_logging.json"
install -m 0644 "$signal_control_logging_config" \
  "$runtime_etc/signal_control_logging.json"

echo "[DEPLOYMENT][STAGE] runtime ready"
echo "[LAUNCH_MANAGER][START] binary=$runtime_bin/launch_manager"
launch_manager_pid=$$
echo "[LAUNCH_MANAGER][START] pid=$launch_manager_pid"
export TIMING_REPORT_LOG_DIR="$runtime_logs"

# Startup contains the control daemon. Once its IPC endpoint is ready, request
# Running so Launch Manager starts the ordered three-process pipeline.
# Detach the one-shot helper before exec. Launch Manager uses waitpid() for its
# own managed children, so the helper must not become one of those children.
setsid --fork "$0" --activate-running "$runtime_bin/lmcontrol" \
  "$launch_manager_pid"

# Make Launch Manager the process directly owned by `bazel run`. Its official
# SIGINT/SIGTERM handler can then stop all managed processes in dependency order.
cd "$runtime_root"
exec "$runtime_bin/launch_manager"
