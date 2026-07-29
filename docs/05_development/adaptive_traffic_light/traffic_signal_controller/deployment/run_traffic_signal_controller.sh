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

  echo "[SIGNAL_CONTROL_DAEMON][ERROR] did not consume request before timeout" >&2
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

if [[ $# -ne 9 ]]; then
  echo "ERROR: deployment target received an invalid runfiles layout" >&2
  exit 1
fi

launch_manager="$(rlocation "$1")"
control_daemon="$(rlocation "$2")"
lmcontrol="$(rlocation "$3")"
signal_controller="$(rlocation "$4")"
generated_config="$(rlocation "$5")"
ecu_logging_config="$(rlocation "$6")"
hm_logging_config="$(rlocation "$7")"
lm_logging_config="$(rlocation "$8")"
signal_control_logging_config="$(rlocation "${9}")"

runtime_root="${TRAFFIC_SIGNAL_CONTROLLER_RUNTIME_DIR:-/tmp/traffic_signal_controller}"
runtime_bin="$runtime_root/bin"
runtime_etc="$runtime_root/etc"
runtime_logs="$runtime_root/logs"
test_log="$runtime_logs/test.log"

mkdir -p "$runtime_bin" "$runtime_etc" "$runtime_logs"
: > "$test_log"

# Keep the log mirror alive while Launch Manager performs signal-driven
# shutdown, so its final process-stop messages are not lost to a closed pipe.
exec > >(setsid --fork tee -a "$test_log") 2>&1

echo "[DEPLOYMENT][LOG] console output is mirrored to $test_log"
echo "[DEPLOYMENT][STAGE] preparing runtime=$runtime_root"
install -m 0755 "$launch_manager" "$runtime_bin/launch_manager"
install -m 0755 "$control_daemon" "$runtime_bin/control_daemon"
install -m 0755 "$lmcontrol" "$runtime_bin/lmcontrol"
install -m 0755 "$signal_controller" "$runtime_bin/traffic_signal_controller"

# Bazel outputs are read-only. Replace old generated files before copying so
# repeated deployments can stage the runtime configuration deterministically.
cp -R --remove-destination "$generated_config"/. "$runtime_etc"/
install -m 0644 "$ecu_logging_config" "$runtime_etc/ecu_logging_config.json"
install -m 0644 "$hm_logging_config" "$runtime_etc/hm_logging.json"
install -m 0644 "$lm_logging_config" "$runtime_etc/logging.json"
install -m 0644 "$signal_control_logging_config" "$runtime_etc/signal_control_logging.json"

echo "[DEPLOYMENT][STAGE] runtime ready"
echo "[LAUNCH_MANAGER][START] binary=$runtime_bin/launch_manager"
launch_manager_pid=$$
echo "[LAUNCH_MANAGER][START] pid=$launch_manager_pid"

# Startup launches the control daemon. Once its IPC endpoint exists, request
# Running so Launch Manager starts exactly one product process: this module.
# Detach the helper so Launch Manager only waitpid()s its own managed children.
setsid --fork "$0" --activate-running "$runtime_bin/lmcontrol" \
  "$launch_manager_pid"

# Make Launch Manager the process owned by `bazel run`; Ctrl-C/SIGTERM then use
# its official shutdown handler and stop all managed process groups in order.
cd "$runtime_root"
exec "$runtime_bin/launch_manager"
