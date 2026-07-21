# Traffic Timing Decision Module

Managed Eclipse S-CORE Lifecycle process for the 2.5-second traffic timing
decision cycle.

The executable follows the upstream `examples/cpp_lifecycle_app` boundary:

- `TimingDecisionApplication` derives from
  `score::mw::lifecycle::Application`;
- `Initialize()` verifies the applied `SCHED_FIFO` policy and initializes the
  domain service;
- `Run(stop_token)` executes the periodic decision pipeline and owns all
  scheduling/stop handling;
- `main()` only calls `run_application<T>()`, which reports Running to Launch
  Manager after successful initialization.

`PeriodicService` contains domain initialization and one decision cycle only;
it does not own the periodic timer. Each process keeps its small timing helper
in its own `src/common.cpp`. The helper uses a `CLOCK_MONOTONIC` condition
variable with an absolute release deadline. A callback registered with the
S-CORE stop token signals the condition variable, allowing SIGTERM to wake a
sleeping process immediately without changing the periodic schedule phase.

Only `traffic_timing_decision` is health-supervised. `perception_demo` and
`signal_control_demo` are reporting-only Lifecycle processes used to exercise
the two POSIX message-queue channels.

Targets:

- `//traffic_timing_decision:traffic_timing_decision_core`: reusable decision
  logic and services
- `//traffic_timing_decision:traffic_timing_decision_application`: Lifecycle
  `Application` adapter
- `//traffic_timing_decision:traffic_timing_decision`: managed process binary
- `//traffic_timing_decision:decision_demo`: standalone legacy demo

## Build configurations

The repository exposes the same Linux architecture configurations as Eclipse
S-CORE Lifecycle v0.3.0:

| Config | Target | Compiler | Intended use |
| --- | --- | --- | --- |
| `host` | Native Linux host | System C++ + Ferrocene x86_64 | Fast local development |
| `x86_64-linux` | x86_64 Linux | S-CORE GCC 12.2 + Ferrocene x86_64 | Reproducible x86 build and local run |
| `arm64-linux` | AArch64 Linux | S-CORE cross-GCC 12.2 + Ferrocene AArch64 | Cross-build for an ARM target |

Build through the repository root:

```bash
cd traffic_timing_decision/
bazel build --config=x86_64-linux //traffic_timing_decision:traffic_timing_decision
bazel build --config=arm64-linux //traffic_timing_decision:traffic_timing_decision
```

Build the complete deployment for a selected architecture:

```bash
cd traffic_timing_decision/
bazel build --config=x86_64-linux //deployment:traffic_light_system
bazel build --config=arm64-linux //deployment:traffic_light_system
```

Run it through the managed deployment rather than directly. This command is
valid when the target binaries match the machine executing them:

```bash
cd traffic_timing_decision/
sudo prlimit --pid $$ --rtprio=99:99
bazel run --config=x86_64-linux //deployment:traffic_light_system
```

`arm64-linux` is a cross-build configuration on the x86_64 development PC.
`bazel run --config=arm64-linux` would build AArch64 executables and then try to
execute them on x86_64, resulting in `Exec format error`. Deploy those outputs
to an AArch64 Linux target, or configure an emulator/remote runner separately.

After the x86 deployment is running, lifecycle commands should use the same
configuration (another terminal):

```bash
cd traffic_timing_decision/
sudo prlimit --pid $$ --rtprio=99:99
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Startup
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Running
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Stop
```



Merged runtime output is also written to
`/tmp/linux_rt_application/logs/test.log`.
