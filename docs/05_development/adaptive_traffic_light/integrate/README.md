# Adaptive Traffic Light — End-to-End Integrated Execution

> The full command to "copy and run" is at the end of this README

The deployment runs three actual processes under Eclipse S-CORE Lifecycle:

```text
traffic_perception
    -> /traffic_snapshot_v1
traffic_timing_decision
    -> /traffic_timing_plan_v1
traffic_signal_controller
```

## 1. Prepare Dependencies and Data

From the `integrate` directory, run the setup script first:

```bash
cd /adaptive_traffic_light/docs/05_development/adaptive_traffic_light/integrate
bash traffic_perception/scripts/setup_deps.sh
```

The script performs the following two tasks and can be safely executed multiple times:

- Downloads ONNX Runtime `1.27.1` for either `x86_64` or `aarch64` into
  `traffic_perception/lib/onnxruntime` if it is not already available.
- Downloads any missing files from [Google Drive](https://drive.google.com/drive/folders/1nXzpFTzbHLYzKeyYwoBCRkDmDqH5TnIM)
  into the `data` directory: `traffic.mp4` through `traffic4.mp4`,
  `yolov8m-oiv7.onnx`, `yolov8m.onnx`, and `yolov8n.onnx`.

The system must have `python3`, `python3-pip`, and either `curl` or `wget`
installed. If `gdown` is not available, the script temporarily installs
`gdown 5.2.0` without installing Python packages system-wide.

The Google Drive folder must allow files to be downloaded by anyone with the
link.

Verify the setup with:

```bash
test -e traffic_perception/lib/onnxruntime/lib/libonnxruntime.so.1
ls -lh data/*.mp4 data/*.onnx
```

## 2. Configure Real-Time Resource Limits for the Current Terminal

```bash
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
```

If the Bazel server was started before these limits were configured, stop it
so that the new server inherits the real-time limits from the current terminal:

```bash
bazel shutdown
```

## 3. Build and Run the End-to-End System

```bash
bazel run --config=x86_64-linux //deployment:traffic_light_system
```

The deployment automatically performs the following steps:

1. Builds the three applications and the Launch Manager.
2. Stages the binaries, configuration files, ONNX Runtime libraries, model,
   and video files in `/tmp/linux_rt_application`.
3. Removes stale IPC resources while the entire system is stopped.
4. Starts the processes in the following order:
   Perception -> Timing Decision -> Signal Controller.

There is no need to manually export `LD_LIBRARY_PATH`. The deployment sets
`/tmp/linux_rt_application/lib` as the library path for the child processes.

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

## Complete Command Sequence

```bash
cd docs/05_development/adaptive_traffic_light/integrate
bash traffic_perception/scripts/setup_deps.sh
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
bazel shutdown
bazel run --config=x86_64-linux //deployment:traffic_light_system
```