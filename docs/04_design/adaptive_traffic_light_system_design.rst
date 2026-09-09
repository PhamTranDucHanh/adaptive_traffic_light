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
cross-cutting rationale, patterns, and trade-offs behind the
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

The reviewed implementation contains one
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

``Perception → Timing Decision → Signal Controller → Simulated signal output``.

Signal Controller also sends the applied light-signal state, ``SignalState``,
back to the Perception viewer so it can display the current lamps and remaining
time.  This return path is for observability only; it never feeds data back into
traffic detection or timing decisions.

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
     - Perception → Timing Decision
     - Latest timestamped vehicle count, queue length, occupancy and emergency
       indication for each approach
     - ``/traffic_snapshot_v1``; depth 4; non-blocking latest-value access
   * - ``TimingPlanMessageV1``
     - Timing Decision → Signal Controller
     - Proposed green and clearance durations
     - ``/traffic_timing_plan_v1``; depth 8; non-blocking publish; receiver
       waits at most 0.2 s; 72-byte versioned ``TPL1`` envelope
   * - ``SignalStateMessageV1``
     - Signal Controller → Perception viewer
     - Applied phase, lamp states and remaining times for display only
     - ``/traffic_signal_state_v1``; depth 4; non-blocking latest-value
       publication; 40-byte versioned envelope

Message fields and queue names are defined in:

* ``ipc/include/traffic_ipc/messages.h``
* ``ipc/include/traffic_ipc/timing_plan_message_v1.h``
* ``ipc/include/traffic_ipc/signal_state_message_v1.h``

Shared latest-value queue behaviour is defined in
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
     - Lifecycle Manager → managed processes
     - Requests Startup, Running and Stop transitions with bounded readiness
       and shutdown handling
     - A transition failure invokes configured recovery; it is not traffic data
   * - Health supervision
     - Managed processes → Health Monitor and Lifecycle Manager
     - Reports heartbeat, deadline and Alive observations
     - A failed health contract is diagnostic evidence, not a domain message
   * - Logging and diagnostics
     - Managed processes → S-CORE logging backends and operator
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
   uses a bounded wait of 0.2 s and then drains available entries.  Their
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

.. list-table:: Real-time process and worker configuration
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
     - Streams CPU4; pipeline CPU2; main/viewer CPU5
     - Fixed frame pool; latest-frame replacement.
   * - Perception Health Monitor
     - ``SCHED_RR``
     - 70
     - CPU5
     - Evaluation 0.1 s; supervisor API 2 s.
   * - Timing Decision periodic thread
     - ``SCHED_FIFO``
     - 80
     - CPU1
     - Mandatory memory lock; 64-KiB stack prefault.
   * - Timing Decision Health Monitor
     - ``SCHED_FIFO``
     - 50
     - CPU5
     - Evaluation 0.5 s; supervisor API 1 s.
   * - Controller FSM / receiver
     - ``SCHED_FIFO``
     - 80 / 70
     - CPU3
     - Controller memory lock is attempted; stacks are prefaulted.
   * - Controller Health Monitor
     - ``SCHED_FIFO``
     - 60
     - CPU5
     - Evaluation 0.5 s; supervisor API 1 s.

.. list-table:: Non-real-time worker configuration
   :header-rows: 1
   :widths: 25 19 16 16 24

   * - Worker
     - Policy
     - Priority
     - CPU
     - Behaviour
   * - Controller output simulator
     - ``SCHED_OTHER``
     - 0
     - CPU5
     - Renders the latest simulated output outside the signal FSM's real-time
       scheduling path.

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
design patterns, the forces behind the decisions, their consequences.  Values
and runtime contracts remain authoritative in those design descriptions; this
part must not redefine them.

Applied design patterns
~~~~~~~~~~~~~~~~~~~~~~~

.. list-table:: Patterns and architectural idioms
   :header-rows: 1
   :widths: 24 38 38

   * - Pattern / idiom
     - Realization
     - Design purpose
   * - Pipes and Filters
     - Snapshot → plan → signal-state pipeline
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

Design rationale and trade-offs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Component boundaries and control direction
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Separate processes isolate failures and allow supervision per service.  One-way
flow ensures that Controller validates every timing plan before application.

Time model and real-time mechanisms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Monotonic absolute releases reduce schedule drift; fixed priorities and bounded
resources improve predictability.  Each component's S-CORE Health Monitor
checks whether a processing cycle finishes within its configured deadline.
S-CORE Alive supervision checks over longer reporting cycles whether the
managed process continues to report progress.

Freshness, backpressure and IPC contracts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Bounded non-blocking queues favor recent data and may drop older values.
``TimingPlanMessageV1`` is versioned; ``TrafficSnapshot`` is transferred as a
raw C++ structure, so its producer and consumer must use a compatible data
layout.

Safety-oriented plan application
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Controller validates plans and applies normal updates only at ``ALL_RED``;
emergency updates affect only a matching green.  If a plan is missing or
invalid, Controller continues using the last valid plan.  The current design
does not limit how long that plan may remain active or command a physical
fallback state.

Lifecycle, health and recovery separation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

External S-CORE services manage startup, health, recovery and shutdown
independently of traffic processing.  The fallback preserves diagnostic access
but does not command a physical fail-safe signal state.

Configuration and observability
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Lifecycle and Perception JSON files own runtime settings; headers and constants
own interfaces and algorithms.  DLT and timing records are diagnostics only.
