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
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
bazel run --config=x86_64-linux //deployment:traffic_light_system
```

Timing Decision intentionally fails initialization if
`mlockall(MCL_CURRENT | MCL_FUTURE)` cannot lock the process mappings. The
shell-level `RLIMIT_MEMLOCK` above is inherited by Launch Manager and then by
the Timing Decision process. `CAP_IPC_LOCK` is an alternative, but raising the
inherited limit is the reproducible development setup.

`arm64-linux` is a cross-build configuration on the x86_64 development PC.
`bazel run --config=arm64-linux` would build AArch64 executables and then try to
execute them on x86_64, resulting in `Exec format error`. Deploy those outputs
to an AArch64 Linux target, or configure an emulator/remote runner separately.

After the x86 deployment is running, lifecycle commands should use the same
configuration (another terminal):

```bash
cd traffic_timing_decision/
sudo prlimit --pid $$ --rtprio=99:99 --memlock=unlimited:unlimited
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Startup
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Running
bazel run --config=x86_64-linux //control_daemon:lmcontrol -- Stop
```
The Timing Decision process uses Eclipse S-CORE's DLT file backend. Every
deployment truncates the previous capture and writes the new trace to:

```text
/tmp/linux_rt_application/logs/timing_decision.dlt
```

DLT application identifiers are limited to four bytes. S-CORE therefore opens
the canonical `DECI.dlt` name for application ID `DECI`; deployment creates
`DECI.dlt` and `timing_decision.dlt` as hard links to the same inode. Open the
descriptive `timing_decision.dlt` path in tooling. The Timing Decision logging
configuration uses `kFile`, so `DECI` logger records are written only to the
DLT file and are not duplicated on the terminal. Perception and Signal Control
keep their own configured recorders. Output is no longer mirrored to
`test.log`.

## Timing report

Every successful periodic release produces records in two additional,
independent S-CORE DLT files:

```text
/tmp/linux_rt_application/logs/wakeup_latency.dlt
/tmp/linux_rt_application/logs/execution_time.dlt
```

`wakeup_latency.dlt` uses APID `WKUP` and context `LATN`. Each record contains
`cycle_id`, the scheduled and actual `CLOCK_MONOTONIC` timestamps in
nanoseconds, wake-up latency in microseconds, and the period in milliseconds:

```text
wakeup_latency_us = (actual_wakeup_ns - scheduled_release_ns) / 1,000
period_ms = period_ns / 1,000,000
```

`execution_time.dlt` uses APID `EXEC` and context `TIME`.
`execution_start_ns` and `execution_end_ns` bracket only
`service_.runDecisionCycle()`, while `execution_time_us` stores that duration
in microseconds. `deadline_ms` stores the 2.5-second deadline as `2500`.
`response_time_ns` runs from the scheduled release to completion. The file
records two explicit checks:

- `execution_deadline_miss`: execution itself exceeded 2.5 seconds, matching
  the decision-pipeline window monitored by Health Monitor.
- `cycle_deadline_miss`: completion occurred more than 2.5 seconds after the
  scheduled release. This is the periodic task's end-to-end deadline result
  and includes wake-up latency.

Both overrun values are zero when the corresponding deadline is met. Timing
records are written only after the completion timestamp has been captured, so
the newly added timing-report write is not included in the measured execution
or response time. Existing logs emitted inside `runDecisionCycle()` remain part
of its real execution cost.

S-CORE's `CreateLogger()` contexts in one process share the recorder owned by
the logging runtime. Timing Decision configures that process-wide recorder as
a console/file composite for its normal `DECI` records. The timing report owns
two additional S-CORE file recorders directly so that `WKUP.dlt` and
`EXEC.dlt` are genuinely separate files. Deployment exposes all three streams
through the descriptive hard-link names shown above.

## Open the trace with DLT Viewer

On Ubuntu 24.04, install the packaged COVESA DLT Viewer:

```bash
sudo apt update
sudo apt install dlt-viewer
```

Open the capture in the GUI:

```bash
dlt-viewer /tmp/linux_rt_application/logs/timing_decision.dlt
```

Open either timing report in the same way:

```bash
dlt-viewer /tmp/linux_rt_application/logs/wakeup_latency.dlt
```
```bash
dlt-viewer /tmp/linux_rt_application/logs/execution_time.dlt
```
Then we can copy those .dlt files into our repo at traffic traffic_timing_decision/traffic_timing_decision/output/ (this folder will be ignore by git):

```bash
cd traffic_timing_decision/
mkdir traffic_timing_decision/output/
cp /tmp/linux_rt_application/logs/{timing_decision,wakeup_latency,execution_time}.dlt   output/
```

For easier analytic, use linux tool to convert .dlt file to .txt file:

```bash
sudo apt install dlt-tools -y

dlt-convert -a traffic_timing_decision/output/wakeup_latency.dlt > traffic_timing_decision/output/wakeup_latency.txt

dlt-convert -a traffic_timing_decision/output/execution_time.dlt > traffic_timing_decision/output/execution_time.txt
```

After that, the analytic module is ready, run to see statistics from ```wakeup_latency.dlt``` and ```execution_time.dlt``` :

```bash
bazel build --config=x86_64-linux //traffic_timing_decision:analytics
bazel-bin/traffic_timing_decision/analytics
```
