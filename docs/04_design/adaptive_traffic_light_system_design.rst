Adaptive Traffic Light - System Design Overview
===============================================

Design Description
------------------

This page contains the system-level Design Description followed by its Design
Explanation.  The description presents the architecture, component
responsibilities, cross-component interactions and externally visible runtime
behaviour.  Detailed internal designs are documented separately for
:doc:`Traffic Perception <traffic_perception_component_design>`,
:doc:`Traffic Timing Decision <traffic_timing_decision_component_design>`,
:doc:`Traffic Signal Controller <traffic_signal_controller_component_design>`.
Lifecycle and health supervision are external S-CORE platform services and are
summarized only at their system boundary.  The explanation records the
cross-cutting rationale, patterns, trade-offs and alternatives behind the
system design.

System view
~~~~~~~~~~~

Purpose and scope
^^^^^^^^^^^^^^^^^

The Adaptive Traffic Light system observes four approaches to an intersection,
estimates current traffic demand, calculates green-light durations, and applies
those durations through a controlled traffic-signal state machine.

The current implementation is a software prototype.  It accepts configured
video files, runs traffic perception and timing logic, and produces
a simulated signal output with a visual overlay.  It does not yet connect to a
physical traffic-light controller and does not claim production readiness or
functional-safety certification.

The reviewed implementation baseline is ``origin/dev``.  It contains one
Bazel workspace, one managed Lifecycle deployment, and the complete
three-process traffic-data path.  The source-code and configuration file
paths in this document are relative to
``docs/05_development/adaptive_traffic_light/``.

System architecture
^^^^^^^^^^^^^^^^^^^

The system component diagram shows the four principal components and the
direction of communication between them.

.. figure:: Component\ Diagrams/System\ Component.drawio.svg
   :alt: Adaptive Traffic Light system components and principal data flow
   :align: center
   :width: 100%

   Traffic data moves from Perception to Timing Decision and then to Signal
   Controller.  Lifecycle commands and health reports use a separate path.

At system level, operation is straightforward:

#. Traffic Perception and Acquisition converts four video inputs into one
   current ``TrafficSnapshot``.
#. Traffic Timing Decision converts the latest usable snapshot into a
   ``TimingPlan``.
#. Traffic Signal Controller validates the plan and applies it through the
   legal signal sequence.
#. Signal Controller returns the applied ``SignalState`` to the Perception
   viewer for display only.
#. Lifecycle Manager starts, stops and supervises all three application
   processes.

The domain-control path is one-way:

``Perception -> Timing Decision -> Signal Controller -> simulated signal output``.

The return path from Signal Controller to the Perception viewer is
observability only.  It never feeds signal state back into traffic detection or
timing decisions.

Real-time classification
^^^^^^^^^^^^^^^^^^^^^^^^

The system prefers fresh data, bounded memory use and predictable periodic
release times.  The implementation records wake-up-latency jitter and
execution-time traces, but these measurements do not yet establish bounded
WCET, blocking time or schedulability on a selected production target.  The
baseline is therefore a **monitored soft real-time system with safety-oriented
degraded behaviour**, not a proven hard real-time system.

Component interactions
~~~~~~~~~~~~~~~~~~~~~~

End-to-end sequence
^^^^^^^^^^^^^^^^^^^

The system sequence diagram summarizes one complete operating cycle.  It shows
only externally visible exchanges; internal worker and state-machine details
are documented on the component pages linked above.

.. figure:: Sequence\ Diagrams/system-sequence.drawio.svg
   :alt: End-to-end sequence between Adaptive Traffic Light components
   :align: center
   :width: 100%

   Perception publishes observations, Timing Decision publishes a proposed
   plan, Controller applies legal signal states, and managed processes report
   health independently.

Information exchanged
^^^^^^^^^^^^^^^^^^^^^

The application processes exchange three asynchronous messages through bounded
POSIX queues:

.. list-table:: Internal project message contracts
   :header-rows: 1
   :widths: 18 20 27 35

   * - Message
     - Flow
     - Purpose
     - Transport contract
   * - ``TrafficSnapshot``
     - Perception -> Timing Decision
     - Latest timestamped vehicle count, queue length, occupancy and emergency
       indication for each approach
     - ``/traffic_snapshot_v1``; depth 4; non-blocking latest-value access
   * - ``TimingPlanMessageV1``
     - Timing Decision -> Signal Controller
     - Proposed green and clearance durations
     - ``/traffic_timing_plan_v1``; depth 8; non-blocking publish; receiver
       waits at most 200 ms; 72-byte versioned ``TPL1`` envelope
   * - ``SignalStateMessageV1``
     - Signal Controller -> Perception viewer
     - Applied phase, lamp states and remaining times for display only
     - ``/traffic_signal_state_v1``; depth 4; non-blocking latest-value
       publication; 40-byte versioned envelope

Message fields and queue names are defined in
``ipc/include/traffic_ipc/messages.h``,
``ipc/include/traffic_ipc/timing_plan_message_v1.h`` and
``ipc/include/traffic_ipc/signal_state_message_v1.h``.  Shared latest-value
queue behaviour is defined in
``ipc/include/traffic_ipc/latest_value_queue.h``.

These contracts are compile-time interfaces, not runtime JSON settings.
Developers changing an incompatible layout must define a new message version
and queue name, then update and rebuild both producer and consumer.  Runtime
validation rejects mismatched contracts but does not create versions
automatically.

External platform interfaces
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Eclipse S-CORE provides lifecycle, health and logging services outside the
project-owned traffic-data path:

.. list-table:: External S-CORE interfaces
   :header-rows: 1
   :widths: 22 24 30 24

   * - Interface
     - Provider and user
     - Role
     - Failure meaning
   * - Lifecycle control
     - Lifecycle Manager -> managed processes
     - Requests Startup, Running and Stop transitions with bounded readiness
       and shutdown handling
     - A transition failure invokes configured recovery; it is not traffic data
   * - Health supervision
     - Managed processes -> Health Monitor and Lifecycle Manager
     - Reports heartbeat, deadline and Alive observations
     - A failed health contract is diagnostic evidence, not a domain message
   * - Logging and diagnostics
     - Managed processes -> S-CORE logging backends and operator
     - Emits application, lifecycle, health and timing records; selected timing
       contexts are recorded as DLT data for offline analysis
     - Logging loss reduces diagnostics and timing evidence but does not become
       traffic data or directly change control behaviour

An Alive indication reports that supervised execution is making progress; it
does not prove that traffic data is correct.  Deployment-specific lifecycle,
health and recovery values are defined in
``config/traffic_light_lifecycle.json``.

Traceable system design decisions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. sys_des:: Exchange domain data through bounded latest-value channels
   :id: sys_des__latest_value_data_flow
   :status: valid
   :satisfies: sys_req__publish_traffic_snapshot
   :req_covered: yes

   Perception publishes ``TrafficSnapshot`` to ``/traffic_snapshot_v1`` and
   Timing Decision publishes ``TimingPlanMessageV1`` to
   ``/traffic_timing_plan_v1``.  Both publishers are non-blocking and both
   queues are bounded.  Snapshot reads are non-blocking; TimingPlan reception
   uses a bounded 200-ms wait and then drains available entries.  Their
   producer/consumer logic favors the newest usable value so a slow consumer
   does not create an unbounded backlog of obsolete traffic decisions.

.. sys_des:: Compute and publish an adaptive timing plan periodically
   :id: sys_des__periodic_adaptive_timing_decision
   :status: valid
   :satisfies: sys_req__produce_adaptive_timing_plan
   :req_covered: yes

   Timing Decision runs once per 2.5-second decision cycle.  It consumes the
   newest valid ``TrafficSnapshot``, calculates demand for the North/South and
   East/West directions, constrains the resulting green and clearance
   durations, and publishes them in ``TimingPlanMessageV1`` without blocking
   the periodic decision thread.  When no new usable snapshot is available,
   it preserves the most recently published plan, reports ``NO_NEW_DATA`` and
   retries any pending publication.  After 24 consecutive missed cycles, it
   reports an input timeout and fails the service cycle.

.. sys_des:: Return applied signal state as visualization-only telemetry
   :id: sys_des__signal_state_telemetry
   :status: valid
   :satisfies: sys_req__publish_signal_state
   :req_covered: yes

   Signal Controller publishes ``SignalStateMessageV1`` to
   ``/traffic_signal_state_v1`` after applying signal output.  The Perception
   viewer consumes it only to render phase, lamp and remaining-time state; it
   is prohibited from influencing perception, timing or control decisions.

.. sys_des:: Isolate lifecycle supervision from domain decisions
   :id: sys_des__lifecycle_health_control_plane
   :status: valid
   :satisfies: sys_req__supervise_managed_processes
   :req_covered: yes

   Each managed process exposes lifecycle state and health independently of
   the snapshot/plan data path.  Heartbeat and deadline observations are sent
   to Lifecycle Manager and diagnostic sinks; they do not modify domain
   messages.

End-to-end timing
^^^^^^^^^^^^^^^^^

End-to-end latency is phase-dependent rather than one fixed value.  Perception
currently publishes a new snapshot every 10 seconds.  That snapshot can wait
up to one 2.5-second Timing Decision release.  After Controller accepts a
normal plan, the plan can wait until the next safe ``ALL_RED`` application
boundary.

The worst case must therefore be measured across the complete path; it cannot
be inferred by simply adding the component periods.


System-wide runtime constraints
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Scheduling and resources
^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table:: Process and worker real-time configuration
   :header-rows: 1
   :widths: 25 19 16 16 24

   * - Process / worker
     - Policy
     - Priority
     - CPU
     - Memory or timing behaviour
   * - Perception process and stream/pipeline workers
     - ``SCHED_RR``
     - 70
     - Streams 4; pipeline 2; main/viewer 5
     - Fixed frame pool; latest-frame replacement.
   * - Perception Health Monitor
     - ``SCHED_RR``
     - 70
     - 5
     - Evaluation 100 ms; supervisor API 2,000 ms.
   * - Timing Decision periodic thread
     - ``SCHED_FIFO``
     - 80
     - 1
     - Mandatory memory lock; 64-KiB stack prefault.
   * - Timing Decision Health Monitor
     - ``SCHED_FIFO``
     - 50
     - 5
     - Evaluation 500 ms; supervisor API 1,000 ms.
   * - Controller FSM / receiver / output
     - ``SCHED_FIFO``
     - 80 / 70 / 60
     - 3
     - Controller memory lock is attempted; stacks are prefaulted.
   * - Controller Health Monitor
     - ``SCHED_FIFO``
     - 60
     - 5
     - Evaluation 500 ms; supervisor API 1,000 ms.

Workers use fixed-priority Linux real-time scheduling, logical CPU affinity
and bounded memory; a higher priority wins within the same policy.  Deployment
must provide the configured CPUs and required scheduling/memory-lock privileges.
Missing required resources fail Timing Decision or Controller initialization,
while Perception contains overload through frame reuse and replacement.  Exact
worker settings are documented on the three internal component pages.

The main configuration sources are:

* ``config/traffic_light_lifecycle.json`` for process order, lifecycle
  timeouts, health reporting, recovery and managed-process environment values.
* ``config/traffic_perception_config.json`` for video inputs, regions of
  interest (ROIs), model selection, periods, phases, CPU placement and
  priorities.
* ``ipc/include/traffic_ipc/`` for compile-time queue and message contracts.
* ``traffic_timing_decision/include/decision_constants.h``.
  ``traffic_timing_decision/src/decision_engine.cpp`` for timing-decision
  parameters.
* ``traffic_signal_controller/include/common/config.h`` for Controller timing
  and priority defaults.

JSON and environment values are deployment configuration.  Header and source
constants are reviewed design parameters and require a rebuild when changed.

Runtime and failure behaviour
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

During normal operation, the three application processes run at independent
rates and communicate only through bounded latest-useful-value interfaces.
Each process also reports heartbeat and deadline observations through the
separate Lifecycle/Health control plane.

Failure handling preserves the last usable domain state and contains optional
telemetry failures:

* Missing Perception input keeps the previous timing plan; 24 consecutive
  missed decision cycles fail the Timing Decision service cycle.
* Missing or invalid TimingPlan input leaves Controller's active plan
  unchanged.  A missing queue is retried; an incompatible contract fails the
  receiver worker.
* SignalState telemetry loss is logged, but signal control continues.
* Lifecycle Manager applies the configured recovery action when a process or
  health contract fails, and coordinates bounded shutdown after a Stop request.

.. sys_des:: Preserve safe signal state when an input plan is unusable
   :id: sys_des__safe_plan_fallback
   :status: valid
   :satisfies: sys_req__apply_safe_signal_transitions
   :req_covered: yes

   Signal Controller validates a timing plan before use.  If the plan is
   absent or rejected, Controller preserves its current plan and advances only
   through the signal state machine.  The baseline implements neither a
   maximum plan-hold time nor a forced fallback phase.

Current system limitations
~~~~~~~~~~~~~~~~~~~~~~~~~~

* Controller has no maximum active-plan hold time or TimingPlan age gate.
* Runtime fallback retains Control Daemon but defines no physical lamp command,
  because the prototype has no physical signal-output adapter.
* No production hardware or resource budget has been selected, and no complete
  WCET, blocking-time or schedulability evidence exists for such a target.
* Raw POSIX queue payload compatibility still depends on controlled compiler
  and architecture settings despite versioned envelopes and layout assertions.
* Automated component and end-to-end verification remains limited.

Design Explanation
------------------

This part explains why the structures and mechanisms in the system overview
and three internal component descriptions were selected.  It records applied
design patterns, the forces behind the decisions, their consequences and the
alternatives considered.  Values and runtime contracts remain authoritative
in those design descriptions; this part must not redefine them.

Applied design patterns
~~~~~~~~~~~~~~~~~~~~~~~

.. list-table:: Patterns and architectural idioms
   :header-rows: 1
   :widths: 24 38 38

   * - Pattern / idiom
     - Realization
     - Design purpose
   * - Pipes and Filters
     - Snapshot -> plan -> signal-state pipeline
     - Separates sensing, policy and actuation into independently supervised
       timing domains.
   * - Latest-Value Mailbox
     - Fixed-capacity frame handoffs and bounded POSIX queues
     - Bounds memory and favors fresh control input over complete history.
   * - Strategy
     - Configurable inference backends behind ``IModelBackend``
     - Changes the detector without coupling it to capture or publication.
   * - Object Pool
     - Preallocated and recycled ``FramePool`` entries
     - Limits allocation and makes overload visible as dropped work.
   * - State
     - ``SignalFSMEngine`` owns legal green, yellow and all-red transitions
     - Prevents timing messages from directly commanding arbitrary lamps.
   * - Active Producer-Consumer
     - Dedicated capture, inference, receiver, FSM, output and health workers
     - Isolates blocking I/O and heavy computation across bounded handoffs.
   * - Supervisor
     - S-CORE Lifecycle Manager and per-process Health Monitors
     - Centralizes process transitions, failure detection and recovery.
   * - Adapter
     - Lifecycle wrappers, message converters and ``OutputSimulator``
     - Keeps framework, transport and output concerns outside domain logic.

The table names observable implementation structures; not every entry is a
textbook GoF pattern.

Design rationale and trade-offs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Component boundaries and control direction
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Perception, Timing Decision and Signal Controller have different rates,
resource profiles and failure consequences.  Separate managed processes keep
inference load outside the Controller failure domain and let Lifecycle identify
the failed service.  One-way domain flow also ensures that every plan crosses
Controller validation and that viewer telemetry cannot influence control.

Time model and real-time mechanisms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Periodic activities use ``CLOCK_MONOTONIC``/``steady_clock`` and absolute
release times so wall-clock changes and execution duration do not accumulate
as schedule drift.  Fixed priorities, CPU affinity, memory locking, prefaulted
stacks, bounded pools and bounded queues improve predictability.  Wake-up
latency and execution-time traces support analysis, but do not replace WCET,
blocking-time and schedulability evidence for a production target.

Functional deadlines determine whether one result was timely; wider health
windows determine whether a process still makes progress.  Keeping them
separate avoids treating one late cycle as immediate process failure.

Freshness, backpressure and IPC contracts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Bounded latest-value communication is chosen because traffic observations and
plans lose value with age.  Replacing or draining old entries bounds memory,
while non-blocking publication prevents a slow consumer from suspending an
upstream periodic worker.  The trade-off is intentional loss of intermediate
values and a requirement to distinguish missing, stale and invalid input.

``TimingPlanMessageV1`` uses a fixed-size versioned envelope to reject an
incompatible layout or transport sequence before domain validation.  The raw
``TrafficSnapshot`` contract is less strongly versioned and therefore retains
a compiler/layout compatibility risk.

Safety-oriented plan application
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Controller uses two validation layers.  Transport validation establishes
that bytes belong to the expected wire contract; domain validation checks that
the decoded plan is meaningful.  Valid normal plans wait for an ``ALL_RED``
boundary, so yellow and all-red clearance cannot be replaced mid-phase.  The
constrained emergency path may interrupt only a matching green phase, and the
FSM remains the sole owner of applied signal state.

Keeping the previous valid plan on missing input avoids an arbitrary phase
change.  Without a plan-hold timeout or physical fallback output, this is
failure containment rather than a complete production safety concept.

Lifecycle, health and recovery separation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Lifecycle commands and health indications use a control plane separate from
domain queues.  A queued value therefore cannot prove process health, and an
empty queue alone cannot prove process failure.  Ordered startup, bounded
readiness/shutdown and configured recovery supervise processes without changing
domain decisions.  The control-only fallback preserves diagnostic access but
is not a physical traffic-signal fail-safe.

Configuration and observability
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Configuration is layered according to ownership.  Lifecycle JSON controls
deployment policy and recovery; Perception JSON controls inputs and workers;
shared headers control wire compatibility; module constants control algorithms
and safety bounds; environment variables provide explicit deployment
overrides.  DLT records, wake-up samples, execution timing and timestamps stay
outside domain values so diagnostics cannot alter control decisions.

Alternatives considered
~~~~~~~~~~~~~~~~~~~~~~~

.. list-table:: Alternatives and reasons for rejection
   :header-rows: 1
   :widths: 34 66

   * - Alternative
     - Reason not selected
   * - One process for the complete pipeline
     - Perception load and failure would share the Controller failure domain,
       and supervision could not attribute failure by component.
   * - Deliver every value through FIFO queues
     - A slow consumer would process obsolete traffic state and turn queue
       capacity into additional control latency.
   * - Synchronous request/response IPC
     - Blocking and timeout propagation would couple otherwise independent
       component schedules.
   * - Relative periodic sleeps
     - Execution time and jitter would accumulate as schedule drift.
   * - Shared-memory domain transport
     - Current messages are small, while shared memory adds ownership,
       synchronization and crash-recovery complexity.
   * - Apply each plan immediately
     - It could replace a clearance phase or cause an unsafe phase jump;
       normal plans therefore wait for ``ALL_RED``.
   * - Unbounded allocation in periodic paths
     - Allocator latency, fragmentation and failure would reduce timing
       predictability.
