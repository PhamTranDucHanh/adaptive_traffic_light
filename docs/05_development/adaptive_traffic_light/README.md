# Adaptive Traffic Light — End-to-End Integrated Execution

The deployment runs three actual processes under Eclipse S-CORE Lifecycle:

```text
traffic_perception
    -> /traffic_snapshot_v1
traffic_timing_decision
    -> /traffic_timing_plan_v1
traffic_signal_controller
```

## 1. Prerequisites and Runtime Data

From the repository root, enter the Bazel workspace:

```bash
cd docs/05_development/adaptive_traffic_light
```

The recommended end-to-end runner calls the dependency setup script
automatically. To prepare dependencies without starting the system, run:

```bash
cd /adaptive_traffic_light/docs/05_development/adaptive_traffic_light/integrate
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

Verify the setup with:

```bash
test -e traffic_perception/lib/onnxruntime/lib/libonnxruntime.so.1
ls -lh data/*.mp4 data/*.onnx
```

## 2. Run the System and Generate Analytics

Recommended command:

```bash
./deployment/run_end_to_end_analytics.sh [duration] [--vendor-dir <path>]
```

Duration is optional. No value means run until `Ctrl-C`; a bare number or `s`
means seconds, `m` means minutes, and `h` means hours.

Common examples:

```bash
./deployment/run_end_to_end_analytics.sh       # until Ctrl-C
./deployment/run_end_to_end_analytics.sh 120   # 120 seconds
./deployment/run_end_to_end_analytics.sh 5m    # five minutes
```

The runner prepares dependencies, configures RT limits, restarts Bazel, runs
the Lifecycle deployment, and generates all analytics. It sets the system-wide
`kernel.sched_rt_runtime_us=-1` value and does not restore its previous value.

### Bazel vendor mode

Default execution uses normal Bazel dependency resolution. To use the existing
vendor directory:

```bash
./deployment/run_end_to_end_analytics.sh 5m --vendor-dir vendor
```

`--vendor-dir=<path>` and `--vendor_dir <path>` are also accepted. Relative
paths use the workspace root; the directory must contain `VENDOR.bazel`.
Vendor mode affects Bazel dependencies only, not model/video setup.

### Generated output

Each run writes reports, logs, and plots below:

```text
output/end_to_end/<YYYYMMDD_HHMMSS>/
```

Override the root when needed:

```bash
E2E_ANALYTICS_OUTPUT_DIR=/tmp/traffic-e2e \
  ./deployment/run_end_to_end_analytics.sh 120
```

Traffic Perception keeps its source logs, `perc_*_statistic.txt` reports, and
final PNGs; temporary plotting datasets are removed. Check `postprocess.log`
for optional analytics warnings.

## 3. Runtime Behavior and Logs

The deployment stages binaries, configuration, ONNX Runtime, models, and video
files below `/tmp/linux_rt_application`. There is no need to export
`LD_LIBRARY_PATH`; the deployment configures
`/tmp/linux_rt_application/lib` for its child processes.

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

## 4. Run the Deployment Without Analytics

For runtime-only execution:

```bash
cd docs/05_development/adaptive_traffic_light
bash traffic_perception/scripts/setup_deps.sh
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
sudo sysctl -w kernel.sched_rt_runtime_us=-1 
bazel shutdown
bazel run --config=x86_64-linux //deployment:traffic_light_system
```

Direct runtime execution does not extract DLT logs or generate the timestamped
analytics output described above.
