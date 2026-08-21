# Adaptive Traffic Light - End-to-End Execution

The deployment runs three actual processes under Eclipse S-CORE Lifecycle:

```text
traffic_perception
    -> /traffic_snapshot_v1 (Message Queue)
traffic_timing_decision
    -> /traffic_timing_plan_v1 (Message Queue)
traffic_signal_controller
    -> /traffic_signal_state_v1 (Message Queue)
traffic_perception
```

## 1. Setup

From the repository root, enter the Bazel workspace:

```bash
cd docs/05_development/adaptive_traffic_light
```

The recommended end-to-end runner calls the dependency setup script
automatically. To prepare dependencies without starting the system, run:

```bash
bash traffic_perception/scripts/setup_deps.sh
```

The setup is idempotent and performs the following tasks:

- Downloads ONNX Runtime `1.27.1` for either `x86_64` or `aarch64` into
  `traffic_perception/lib/onnxruntime` if it is not already available.
- Downloads any missing files from [Google Drive](https://drive.google.com/drive/folders/1nXzpFTzbHLYzKeyYwoBCRkDmDqH5TnIM)
  into the `data` directory: `traffic.mp4` through `traffic4.mp4`,
  `yolov8m-oiv7.onnx`, `yolov8n-oiv7.onnx`, `yolov8m.onnx`,
  `yolov8n.onnx`, and `rt-detrv2-s.onnx`.

The host must provide `bazel`, `sudo`, `dlt-convert`, `gnuplot`, `python3`,
`python3-pip`, and either `curl` or `wget`. If `gdown` is unavailable, setup
temporarily installs `gdown 5.2.0` without modifying system Python packages.

The Google Drive folder must allow files to be downloaded by anyone with the
link.

(Optional) Verify the setup with:

```bash
test -e traffic_perception/lib/onnxruntime/lib/libonnxruntime.so.1
ls -lh data/*.mp4 data/*.onnx
```


## 2. Build

Build the complete Lifecycle deployment:

```bash
bazel build --config=x86_64-linux //deployment:traffic_light_system
```

The deployment target builds the wrapper and all runtime dependencies,
including Traffic Perception, Traffic Timing Decision, Traffic Signal
Controller, Control Daemon, `lmcontrol`, IPC reset, and Launch Manager.

The main build outputs are available through the Bazel output tree:

```text
bazel-bin/deployment/traffic_light_system
bazel-bin/traffic_perception/traffic_perception
bazel-bin/traffic_timing_decision/traffic_timing_decision
bazel-bin/traffic_signal_controller/traffic_signal_controller
bazel-bin/control_daemon/control_daemon
bazel-bin/control_daemon/lmcontrol
```

`bazel-bin` is a Bazel-managed symlink into the local output cache. It is not a
self-contained deployment package and should not be copied directly between
machines. When the deployment is started, its script collects the target
runfiles and stages the executable runtime under
`/tmp/linux_rt_application/bin`, with configuration and data in the sibling
runtime directories.

### Bazel vendor mode

Normal Bazel execution resolves external modules from the configured
registries. Vendor mode instead reads those external repositories from a
prepared local directory. It is useful when the PC used for the final build
has restricted or unavailable network access.

On a network-connected PC, such as the lab PC, create or refresh the vendor
directory:

```bash
traffic_perception/scripts/setup_deps.sh

bazel vendor --vendor_dir="$PWD/vendor" --config=x86_64-linux //deployment:traffic_light_system
```

Copy the repository at the same commit, its `MODULE.bazel.lock`, and the
generated `vendor` directory to the destination PC. Build there with:

```bash
bazel build --config=x86_64-linux \
  --vendor_dir="$PWD/vendor" \
  //deployment:traffic_light_system
```

The vendor directory contains external Bazel repositories, not compiled
application binaries. The destination PC still performs the build and must
provide compatible host libraries and tools. ONNX Runtime, models, and videos
are also handled separately by `setup_deps.sh`.

The existing end-to-end runner accepts
`--vendor_dir=<path>`, or `--vendor_dir <path>`. Relative paths are resolved
from the workspace root, and the `vendor/` directory must contain `VENDOR.bazel`.

## 3. Run

Environmental setup commands:
```bash
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
sudo sysctl -w kernel.sched_rt_runtime_us=-1
bazel shutdown
```

For deployment without analytics generation:

```bash
bazel run --config=x86_64-linux //deployment:traffic_light_system
```

Or, to use vendored Bazel dependencies, add the vendor option:

```bash
bazel run --config=x86_64-linux \
  --vendor_dir="$PWD/vendor" \
  //deployment:traffic_light_system
```

`bazel run` first builds any missing or outdated targets and then executes the
deployment wrapper. Direct runtime execution does not extract DLT logs or
generate the timestamped analytics output described above.


### Runtime behavior and logs

The deployment stages binaries, configuration, ONNX Runtime, models, and video
files below `/tmp/linux_rt_application`.

Valid output should include traffic light phases similar to:

```text
Phase: NS_GREEN, Remaining: 29 s
...
Phase: YELLOW, Remaining: 1 s
Phase: ALL_RED, Remaining: 1 s
Phase: EW_GREEN, Remaining: ...
```

The main log files are:

```text
/tmp/linux_rt_application/logs/traffic_perception.dlt
/tmp/linux_rt_application/logs/timing_decision.dlt
/tmp/linux_rt_application/logs/signal_control.dlt
/tmp/linux_rt_application/logs/CTRL.dlt.txt
```

Stop the system by pressing `Ctrl-C`. The Lifecycle Manager will stop the
processes according to their dependency order. The current `shutdown_timeout`
is 10 seconds.

## 4. Deploy

### End-to-end run and analytics

Recommended command:

```bash
./deployment/run_end_to_end_analytics.sh [duration] [--vendor-dir <path>]
```

`duration` argument and `vendor-dir` flag are optional. No value means run until `Ctrl-C`; a bare number or `s`
means seconds, `m` means minutes, and `h` means hours.

Common examples:

```bash
./deployment/run_end_to_end_analytics.sh       # until Ctrl-C
./deployment/run_end_to_end_analytics.sh 120s   # 120 seconds
./deployment/run_end_to_end_analytics.sh 5m    # five minutes
./deployment/run_end_to_end_analytics.sh 4h    # four hours
```

The scripts prepares dependencies, configures RT limits, restarts Bazel, builds
any target that is not already up to date, runs the Lifecycle deployment, and
generates all analytics. It sets the system-wide
`kernel.sched_rt_runtime_us=-1` value and does not restore its previous value.

To run with the vendor directory prepared in the build section:

```bash
./deployment/run_end_to_end_analytics.sh 5m --vendor-dir vendor
```

Vendor mode affects Bazel dependencies only, not model/video setup.

### Generated output

Each run writes reports, logs, and plots below:

```text
output/<YYYYMMDD_HHMMSS>/<MODULE_NAMES>
```