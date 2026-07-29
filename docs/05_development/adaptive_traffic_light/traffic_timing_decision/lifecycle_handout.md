# S-CORE Lifecycle Integration Handout

This handout defines the minimum coding and deployment contract for running the
Adaptive Traffic Light applications with Eclipse S-CORE Lifecycle v0.3.0.

## 1. System model

S-CORE Launch Manager is an external orchestrator. It starts, supervises, and
stops the managed traffic processes:

- Traffic Perception & Acquisition
- Traffic Timing Decision
- Traffic Signal Controller
- Analytics Service

`control_daemon` is the State Manager for the current PoC. `lmcontrol` sends
Run Target requests to it; `control_daemon` then calls the official
`ControlClient::ActivateRunTarget()` API.

```text
lmcontrol
  -> control_daemon
     -> S-CORE ControlClient
        -> Launch Manager
           -> managed application
              -> module + Health Monitor
```

## 2. Required structure for each application module

New modules should use a flat layout. The nested
`traffic_timing_decision/traffic_timing_decision` layout is transitional and
should not be copied.

```text
<application>/
├── .bazelversion
├── .bazelrc
├── MODULE.bazel
├── BUILD or BUILD.bazel
├── include/<application>/
│   ├── <application>_module.h
│   ├── <application>_periodic_service.h
│   ├── lifecycle_health_reporter.h
│   └── <application>_application.h
└── src/
    ├── <application>_module.cpp
    ├── <application>_periodic_service.cpp
    ├── lifecycle_health_reporter.cpp
    ├── <application>_application.cpp
    ├── <application>_demo.cpp
    └── main.cpp
```

Each module produces two binaries:

| Binary | Purpose | Run directly? |
|---|---|---|
| `*_demo` | Finite domain test | Yes |
| managed application | Lifecycle process | No; Launch Manager only |

Both binaries must call the same `*Module` facade so that demo testing and
managed execution exercise the same domain pipeline.

## 3. Layer responsibilities

| Layer | Responsibility |
|---|---|
| `main.cpp` | Call `run_application<Application>()` only |
| `*Application` | Implement S-CORE Lifecycle entry points and periodic scheduling |
| `*PeriodicService` | Wrap exactly one domain cycle with health monitoring |
| `*Module` | Own domain initialization, one bounded cycle, and shutdown |
| Domain classes | Perception, validation, decision, FSM, transport, or output logic |
| `LifecycleHealthReporter` | Thin wrapper around the official S-CORE HMON API |
| `*_demo_.cpp` | Run a finite number of `*Module` cycles without Lifecycle/HMON |

Do not put domain logic, a custom loop, or signal handlers in `main.cpp`.
Do not call Launch Manager, report Running, or send Alive from domain code.

## 4. Managed application contract

The managed `main.cpp` must stay minimal:

```cpp
#include <score/mw/lifecycle/runapplication.h>

#include "<application>/<application>_application.h"

int main(int argc, char** argv) {
  return score::mw::lifecycle::run_application<Application>(argc, argv);
}
```

The application class must derive from
`score::mw::lifecycle::Application` and override:

- `Initialize(context)`: open and validate resources, establish safe state,
  initialize HMON, then return success.
- `Run(stop_token)`: wait on an absolute monotonic schedule, execute one cycle
  per release, observe the stop token, shut down deterministically, and return
  an exit code.

Use `score::concurrency::wait_until(stop_token, next_release)`. Do not install
custom `SIGTERM` or `SIGINT` handlers; S-CORE converts the termination request
into the supplied stop token.

One `runCycle()` must be bounded and non-blocking:

- no sleep or infinite loop inside domain code;
- use receive-latest or a bounded timeout for input;
- return the real publish/apply result;
- keep resource ownership in `initialize()`/`shutdown()`;
- never request a Run Target transition.

## 5. Health monitoring contract

The official S-CORE `HealthMonitor` is local to each managed process:

- **Deadline Monitor** measures useful work from cycle start to cycle finish.
  The wait between releases is outside the deadline.
- **Heartbeat Monitor** records a meaningful successful event, such as a
  published snapshot, a completed decision, or a confirmed controller output.
- **Logic Monitor** validates applied state transitions. It is required for the
  Signal Controller FSM.

`heartbeat()` is not an Alive IPC call. The HMON worker evaluates all local
monitors and sends Alive asynchronously only while they remain healthy.
Launch Manager/PHM evaluates Alive indications and executes the configured
recovery action.

Initialize HMON in this order:

1. build `HealthMonitor`;
2. obtain every configured monitor and deadline handle;
3. call `healthMonitor.start()`;
4. only then allow `Initialize()` to return success.

Recommended cycle order:

```text
start deadline
  -> run one module cycle
  -> close deadline
  -> report applied logic transition, if used
  -> heartbeat only after the meaningful result succeeds
```

Current propose timings:

| Process | Period | Deadline | Heartbeat | HMON evaluation | Alive API |
|---|---:|---:|---:|---:|---:|
| Perception | 1000 ms | 0–750 ms | 200–2200 ms | 50 ms | 500 ms |
| Timing Decision | 2500 ms | 0–2500 ms | 2000–3000 ms | 100 ms | 500 ms |
| Signal Controller | 100 ms | 0–80 ms | 10–300 ms | 10 ms | 100 ms |

These values are integration-test defaults, not production guarantees. Measure
WCET, transport latency, scheduler jitter before fixing
for official thresholds.


## 6. Bazel contract

An independent application folder owns:

- Bazel 8.6.0 configuration;

Use the official Lifecycle v0.3.0 module and pin:

```text
module: score_lifecycle_health
repository name: @score_lifecycle
commit: 654ac348e1cb9327e5c8c4d84fd0028ad3ef2714
```

Typical dependencies:

```starlark
"@score_lifecycle//score/health_monitor:health_monitoring_cc"
"@score_lifecycle//score/launch_manager:lifecycle_cc"
"@score_baselibs//score/concurrency:interruptible_wait"
```

Build and test from the application Bazel root:

```bash
bazel run --config=host //:<application>_demo
bazel build --config=host //:<managed_application>
bazel build --config=x86_64-linux //:<managed_application>
bazel build --config=arm64-linux //:<managed_application>
```

 Never
run the managed binary directly; it requires Lifecycle descriptors, process
identity, Alive IPC, scheduling, and sandbox context from Launch Manager.

## 7. System deployment contract

Only the future system root should own:

```text
adaptive_traffic_light/
├── MODULE.bazel
├── .bazelrc
├── config/                 # generated Launch Manager/PHM configuration
├── control_daemon/         # State Manager and lmcontrol
├── deployment/             # staging and run targets
├── traffic_perception/
├── traffic_timing_decision/
├── traffic_signal_controller/
└── analytics_service/
```



## 8. Run Targets and dependencies

```text
Startup
  -> State Manager
  -> Signal Controller 

Running
  -> Perception
  -> Timing Decision
  -> Signal Controller
  -> State Manager/control_daemon

Fallback
  -> State Manager
  -> Signal Controller using a safe/default plan

Off
  -> all managed traffic processes stopped, thoroughly clean-up
```

Rules:

- Controller establishes safe output before normal operation.
- Decision starts after Perception is ready.
- Analytics is not part of the critical readiness chain.
- Shutdown follows reverse dependency order.
- Controller is the last traffic process to stop.
- Analytics failure must not move traffic control to Fallback.

## 9. Definition of done

- A demo binary runs a finite number of real domain cycles and exits `0`.
- Managed binary compiles but is launched only by Launch Manager.
- `main.cpp` contains only `run_application<T>()`.
- One domain cycle is bounded and completes within its deadline.
- Heartbeat represents a real successful outcome.
- HMON uses the official S-CORE API; no fake/no-op reporter is linked.
- Shutdown is deterministic and leaves the system in a safe state.
- Full deployment verifies Startup, Running, Fallback, recovery, and Off.

## References

- [S-CORE Lifecycle v0.3.0](https://eclipse-score.github.io/lifecycle/v0.3.0/index.html)
- [Lifecycle v0.3.0 source](https://github.com/eclipse-score/lifecycle/tree/v0.3.0)
- [C++ supervised application example](https://github.com/eclipse-score/lifecycle/blob/v0.3.0/examples/cpp_supervised_app/main.cpp)
- [C++ Health Monitor API](https://github.com/eclipse-score/lifecycle/blob/v0.3.0/score/health_monitor/src/cpp/health_monitor.h)
