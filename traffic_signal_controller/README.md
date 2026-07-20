# Traffic Signal Controller

Standalone Bazel workspace for launching `traffic_signal_controller` as one
managed Eclipse S-CORE Lifecycle v0.3.0 process.

The existing controller and analytics logic is unchanged. The Lifecycle layer
is deliberately small:

- `SignalControlApplication` derives from
  `score::mw::lifecycle::Application`;
- `Initialize()` connects `HealthReporter` to Lifecycle supervision;
- `Run(stop_token)` currently runs a health-monitored cycle every second,
  prints `hello`, and increments a counter;
- `main()` only calls `run_application<T>()`;
- the `//deployment:traffic_signal_controller_system` target stages and starts
  Launch Manager, the module-local control daemon, and this process.

The temporary loop has a `TODO` marking where the real receive/compute/publish
controller pipeline should be connected later. `HealthReporter` follows the
timing-decision pattern and owns the official S-CORE `HealthMonitor`, one
heartbeat monitor, one deadline monitor, and the worker that reports Alive to
Launch Manager. Each loop calls `startControlCycle()` before the `TODO` work and
`finishControlCycle()` after it.

Targets:

- `//traffic_signal_controller:traffic_signal_control_lib`: existing domain
  logic plus the health adapter
- `//traffic_signal_controller:traffic_signal_controller_application`:
  Lifecycle `Application` adapter
- `//traffic_signal_controller:traffic_signal_controller`: managed binary
- `//traffic_signal_controller:traffic_signal_controller_demo`: legacy demo

## Build

Run all commands from this folder so Bazel uses this module's independent
`MODULE.bazel` and toolchain configuration:

```bash
cd traffic_signal_controller/
bazel build --config=x86_64-linux //deployment:traffic_signal_controller_system
bazel build --config=arm64-linux //deployment:traffic_signal_controller_system

bazel build --config=arm64-linux //traffic_signal_controller:traffic_signal_controller
```

`arm64-linux` is a cross-build on an x86_64 development machine. Do not run its
AArch64 output directly on x86_64; deploy it to an AArch64 target or configure
an emulator/remote runner.

## Run through Lifecycle

Run the native x86_64 deployment rather than starting the controller binary
directly:

```bash
cd traffic_signal_controller/
sudo prlimit --pid $$ --rtprio=99:99 
bazel run --config=x86_64-linux //deployment:traffic_signal_controller_system
```

The deployment starts at `Startup` and automatically activates `Running`.
While it is running, use another terminal for Lifecycle commands:

```bash
cd traffic_signal_controller/
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Startup
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Running
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Stop
```

`Startup` keeps only the control daemon active, `Running` launches the signal
controller, and `Stop` asks Launch Manager to perform its managed shutdown. The
module-local control daemon runs as uid/gid 0 so it can signal the root-owned
Launch Manager used from this `/root` development workspace.

If the first `bazel run` keeps the same Bazel output-base lock on your Bazel
version, use the already staged CLI from the second terminal instead; it sends
the identical commands without starting another build:

```bash
/tmp/traffic_signal_controller/bin/lmcontrol Startup
/tmp/traffic_signal_controller/bin/lmcontrol Running
/tmp/traffic_signal_controller/bin/lmcontrol Stop
```

Console output from Launch Manager, the control daemon, and the controller is
also mirrored to:

```text
/tmp/traffic_signal_controller/logs/test.log
```

Lifecycle references:
<https://github.com/eclipse-score/lifecycle/tree/v0.3.0> and
<https://eclipse-score.github.io/lifecycle/v0.3.0/index.html>.
