# Traffic Perception

This folder is an independent Bazel workspace that launches
`traffic_perception` as one managed Eclipse S-CORE Lifecycle v0.3.0 process.

The existing perception/OpenCV implementation remains in the separate
`traffic_perception_core` target and is not executed by the managed placeholder.
The Lifecycle process currently:

- derives `TrafficPerceptionApplication` from
  `score::mw::lifecycle::Application`;
- creates the official S-CORE `HealthMonitor` in
  `LifecycleHealthReporter`;
- runs once every three seconds, reports heartbeat/deadline health, prints
  `hello`, and increments a counter;
- contains a `TODO` marking where the real perception pipeline will be
  connected later.

## Managed deployment build

Run commands from this folder so Bazel uses this module's `MODULE.bazel` and
toolchain configuration:

```bash
cd traffic_perception/
bazel build --config=x86_64-linux //deployment:traffic_perception_system
bazel build --config=arm64-linux //deployment:traffic_perception_system
```

`arm64-linux` is a cross-build configuration on the x86_64 development PC. Its
AArch64 binaries must be deployed to an AArch64 target or run through a
separately configured emulator/remote runner. Running them directly on x86_64
results in `Exec format error`.

## Run through Lifecycle

Run the native deployment rather than starting the perception binary directly:

```bash
cd traffic_perception/
bazel run --config=x86_64-linux //deployment:traffic_perception_system
```

The deployment starts at `Startup` and automatically requests `Running`. From
another terminal, the module-local control client accepts:

```bash
cd traffic_perception/
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Startup
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Running
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Stop
```

If the active deployment holds the Bazel output-base lock, use the staged
client directly:

```bash
/tmp/traffic_perception/bin/lmcontrol Startup
/tmp/traffic_perception/bin/lmcontrol Running
/tmp/traffic_perception/bin/lmcontrol Stop
```

Console output from Launch Manager, control daemon, HealthMonitor, and the
perception process is mirrored to:

```text
/tmp/traffic_perception/logs/test.log
```

## Existing domain targets

The existing domain code remains available independently:

```bash
bazel build --config=host //:traffic_perception_core
bazel build --config=host //:perception_smoke
bazel build --config=host //:module_demo
```

These targets use the host OpenCV installation. They are intentionally not a
dependency of the temporary cross-architecture Lifecycle process. Their domain
completeness is outside this integration change; the managed deployment targets
above are the targets verified here.

Lifecycle references:
<https://github.com/eclipse-score/lifecycle/tree/v0.3.0> and
<https://eclipse-score.github.io/lifecycle/v0.3.0/index.html>.
