Adaptive Traffic Light - System Design
======================================

Design Description
------------------

This part describes what the system does, what each component owns, how the
components communicate, and how the deployed system behaves.  It moves from
the complete system to individual components and then to their interactions.
Design rationale, patterns and alternatives are kept in the
`Design Explanation`_ that follows.

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

The system is decomposed into Traffic Perception and Acquisition, Traffic
Timing Decision, Traffic Signal Controller, and Lifecycle Manager.  Domain
data flows in one direction through the first three components; lifecycle
commands and health reporting form a separate control plane.

Design boundaries
^^^^^^^^^^^^^^^^^

This description defines:

* Component responsibilities and their boundaries.
* The snapshot, timing-plan and signal-state interfaces.
* Important periods, deadlines, freshness rules and resource allocation.
* Startup, normal operation, failure handling and shutdown.
* The implementation limitations that affect design review.

It does not define perception-model accuracy, detailed C++ implementation,
production hardware, a physical signal-lamp I/O adapter, or safety
certification evidence.

Real-time classification
^^^^^^^^^^^^^^^^^^^^^^^^

The system prefers fresh data, bounded memory use and predictable periodic
release times.  The implementation records wake-up-latency jitter and
execution-time traces, but these measurements do not yet establish bounded
WCET, blocking time or schedulability on a selected production target.  The baseline is therefore a
**monitored soft real-time system with safety-oriented degraded behaviour**,
not a proven hard real-time system.

Component design
~~~~~~~~~~~~~~~~

Traffic Perception and Acquisition
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Purpose
"""""""

Traffic Perception and Acquisition is the system's observation component.  It
owns video capture, object detection, per-approach traffic analysis, snapshot
publication and the operator visualization.  It does not choose signal timing
and cannot directly change a signal phase.

Its primary inputs are four configured video file paths: one for each North,
South, East and West approach.  Its primary output is the latest
``TrafficSnapshot``,
containing a timestamp plus vehicle count, queue length, occupancy and an
emergency indication for every approach.

Structure
"""""""""

The component view shows the functional processing blocks and their data flow.

.. figure:: Component\ Diagrams/Traffic\ Perception\ &\ Acquisition\ (Component)-Page-2.drawio.svg
   :alt: Traffic Perception and Acquisition component structure
   :align: center
   :width: 100%

   Video capture feeds a bounded latest-frame path, followed by inference,
   traffic analysis, snapshot publication and visualization.

The component contains these functional blocks:

Capture workers
   Four periodic workers read the configured video sources.  Each lane keeps
   only its latest available frame, so slow downstream processing cannot build
   an unbounded frame backlog.

Inference and traffic analysis
   The pipeline takes the latest frame from each lane, runs the selected ONNX
   model, filters relevant detections, and calculates the traffic measurements
   needed by Timing Decision.

Snapshot publisher
   The pipeline publishes the newest ``TrafficSnapshot`` through a bounded
   POSIX message queue.  Publication never blocks the inference worker.

Viewer
   The viewer renders traffic results and the latest applied signal state.
   This block is outside the control loop: display delay or failure cannot
   change a timing decision or signal output.

Processing sequence
"""""""""""""""""""

The main sequence diagram shows one Perception processing cycle.

.. figure:: Sequence\ Diagrams/Traffic\ Perception\ &\ Acquisition\ (Sequence)-main.drawio.svg
   :alt: Traffic Perception and Acquisition main processing sequence
   :align: center
   :width: 100%

   Periodic capture updates the latest lane frames; the inference pipeline
   analyzes the newest available set and publishes a traffic snapshot.

The four capture workers share a 500-ms period but are staggered across the
period.  The inference pipeline runs independently every 10 seconds.  It
therefore works from the latest available observation rather than waiting for
a synchronized history of every captured frame.

Configuration and timing
""""""""""""""""""""""""

Change runtime Perception settings in
``config/traffic_perception_config.json``.  This file owns the video/model
paths, ROIs, image resolution, capture/pipeline/viewer periods and phases, and
stream/pipeline CPU priorities.  Process-level scheduling, environment and
Lifecycle supervision are configured in
``config/traffic_light_lifecycle.json``.

The remaining values in the table are compiled settings and require a source
change plus rebuild:

* SignalState retry/staleness:
  ``traffic_perception/src/viewer/opencv_lanes_viewer.cpp``;
* analytics thresholds:
  ``traffic_perception/src/io/timeline_analyzer.cpp``;
* Health Monitor timing:
  ``traffic_perception/src/lifecycle_health_reporter.cpp``; and
* frame-pool size:
  ``traffic_perception/src/perception_module.cpp``.

.. list-table:: Traffic Perception configuration in the reviewed baseline
   :header-rows: 1
   :widths: 24 36 40

   * - Area
     - Configured value
     - Observable behaviour
   * - Input
     - Four 1920x1080 video inputs; one polygonal ROI for each approach
     - Each capture worker owns one North/South/East/West input.
   * - Model
     - ``yolov8_oiv7`` with ``yolov8m-oiv7.onnx``; emergency class
       ``Van``
     - Backend selection is configuration-driven and resolved at startup.
   * - Capture
     - Period 500 ms; base phase 0 ms; lane offsets 0/125/250/375 ms
     - A new frame replaces the previous unconsumed frame.
   * - Inference and snapshot
     - Period 10,000 ms; phase 50 ms
     - The latest usable observation is published when processing completes.
   * - Viewer
     - Content period 10,000 ms; phase 260 ms; overlay refresh 250 ms
     - Missed display releases are skipped and cannot delay control.
   * - SignalState display
     - Queue-open retry 1,000 ms; data stale after 3,000 ms
     - Missing or stale telemetry is visible to the viewer only.
   * - Health
     - Deadline 0..30,000 ms; heartbeat 100..10,000 ms
     - Local evaluation runs every 100 ms; supervisor API cycle is 2,000 ms.
   * - Scheduling
     - ``SCHED_RR`` priority 70; stream workers CPU 4; pipeline CPU 2;
       main/viewer and Health Monitor CPU 5
     - If worker creation is denied with ``EPERM``, the component falls back
       to ``SCHED_OTHER`` for development and reports degraded operation.
   * - Memory
     - Fixed pool of 20 pre-created 1920x1080 image frames
     - Pool exhaustion drops work instead of allocating an unbounded backlog.

Failure behaviour
"""""""""""""""""

The Perception error sequence shows how unavailable frames or processing
failures remain contained inside the component.

.. figure:: Sequence\ Diagrams/Traffic\ Perception\ &\ Acquisition\ (Sequence)-error.drawio.svg
   :alt: Traffic Perception and Acquisition error handling sequence
   :align: center
   :width: 100%

   Perception reports failures and avoids publishing an observation that
   falsely appears successful.

If a frame-pool entry is unavailable, the capture cycle is skipped.  If the
snapshot queue cannot be opened or its attributes are incompatible, component
initialization fails.  Health and diagnostic failures are reported separately
from ``TrafficSnapshot`` data.

Traffic Timing Decision
^^^^^^^^^^^^^^^^^^^^^^^

Purpose
"""""""

Traffic Timing Decision converts current traffic measurements into proposed
signal durations.  It owns the demand calculation, green-time constraints,
periodic decision release and timing diagnostics.  It does not drive lamps or
select the active signal phase.

Its input is the latest valid ``TrafficSnapshot``.  Its output is a versioned
``TimingPlanMessageV1`` for Signal Controller.

Structure
"""""""""

.. figure:: Component\ Diagrams/Traffic\ Timing\ Decision-Page-2.drawio.svg
   :alt: Traffic Timing Decision component structure
   :align: center
   :width: 100%

   Snapshot reception, validation, demand calculation, plan generation,
   publication, health reporting and timing analytics are separate concerns.

Decision flow
"""""""""""""

.. figure:: Flowchart/Traffic\ Timing\ Decision\ Flowchart.drawio.svg
   :alt: Traffic Timing Decision processing flow
   :align: center
   :width: 100%

   A snapshot is validated before its measurements can influence a constrained
   timing plan.

One decision cycle performs these steps:

#. Read the newest snapshot available at the 2.5-second release.
#. Reject missing, stale, future-dated or structurally invalid input.
#. Combine queue length, normalized vehicle count and occupancy into a demand
   score for the North/South and East/West directions.
#. Select a target green duration from the configured demand thresholds.
#. Move the current green duration toward that target by a bounded step.
#. Add fixed yellow and all-red clearance durations.
#. Publish the resulting plan without blocking the periodic decision thread.

The module sequence diagram shows how that processing fits into the managed
application cycle.

.. figure:: Sequence\ Diagrams/Traffic\ Timing\ Decision.drawio.svg
   :alt: Traffic Timing Decision managed processing sequence
   :align: center
   :width: 100%

   Lifecycle execution, snapshot consumption, plan publication, health
   reporting and timing diagnostics remain coordinated but logically separate.

Demand scoring and green-time selection
"""""""""""""""""""""""""""""""""""""""

The module does not compare a threshold directly with the number of detected
vehicles.  Instead, it calculates a dimensionless demand score for each of the
two controlled direction pairs: North/South and East/West.  Treating the pairs
separately allows a busy direction to receive a longer target green time
without assigning the same duration to the other direction.

For each direction pair, the score combines three measurements from the latest
valid ``TrafficSnapshot``:

.. list-table:: Demand-score inputs
   :header-rows: 1
   :widths: 22 30 15 33

   * - Measurement
     - Conversion to a component score
     - Weight
     - Meaning
   * - Queue length
     - Average the two approach values and multiply by 100
     - 0.2
     - Represents how much of the configured approach region is occupied by a
       continuous queue.  Perception normally supplies a value from 0 to 1.
   * - Vehicle count
     - Average the two approach counts, multiply by 6 and cap at 100
     - 0.6
     - Gives the largest influence to the current number of detected vehicles
       while preventing this component from exceeding 100.
   * - Occupancy
     - Average the two approach values and multiply by 100
     - 0.2
     - Represents the fraction of the approach region covered by detected
       vehicles.  Perception supplies a value from 0 to 1.

The calculation is therefore equivalent to:

``direction demand score = 0.2 * queue percentage + 0.6 * vehicle score + 0.2 * occupancy percentage``

With normal Perception inputs, the resulting score ranges from 0 to 100.  The
thresholds divide that range into demand levels and select a target green time:

.. list-table:: Demand-score ranges and target green times
   :header-rows: 1
   :widths: 30 30 40

   * - Direction demand score
     - Demand level
     - Target green time
   * - Less than 30
     - Low
     - 20,000 ms
   * - At least 30 but less than 60
     - Moderate
     - 30,000 ms
   * - At least 60 but less than 90
     - High
     - 40,000 ms
   * - At least 90
     - Very high
     - 50,000 ms

For example, a queue percentage of 40, vehicle score of 60 and occupancy
percentage of 50 produce ``0.2 * 40 + 0.6 * 60 + 0.2 * 50 = 54``.  The score
therefore selects the moderate target of 30,000 ms.

The selected value is a target rather than an immediate output.  On each
2.5-second decision cycle, the module moves the previous green duration toward
the target by no more than 5,000 ms.  For example, a current value of 20,000 ms
and a high-demand target of 40,000 ms produce successive requested values of
25,000, 30,000, 35,000 and 40,000 ms if the demand remains high.  This limits
abrupt timing changes between consecutive plans.

After both direction targets are updated, the constraint manager enforces the
10,000..60,000-ms green bounds, adds the fixed yellow and all-red clearance
times, and proportionally reduces green durations if the complete cycle would
exceed 100,000 ms.  Emergency input bypasses demand-based target selection: the
module preserves the previous green durations and forwards the relevant
emergency flags to Signal Controller.

Decision configuration
""""""""""""""""""""""

Timing Decision currently has no module runtime JSON.  Its functional values
are compiled and require a source change plus rebuild:

* decision period and consecutive-miss limit:
  ``traffic_timing_decision/include/periodic_service.h``;
* green-time bounds, steps and demand thresholds:
  ``traffic_timing_decision/include/decision_constants.h``;
* demand-score calculation and weights:
  ``traffic_timing_decision/src/decision_engine.cpp``;
* snapshot freshness and content validation:
  ``traffic_timing_decision/src/traffic_data_receiver.cpp``; and
* Health Monitor timing:
  ``traffic_timing_decision/src/health_reporter.cpp``.

Process scheduling, environment variables, readiness and shutdown values are
configured in ``config/traffic_light_lifecycle.json``.  The TimingPlan queue
contract is configured in
``ipc/include/traffic_ipc/timing_plan_message_v1.h``.

.. list-table:: Traffic Timing Decision configuration
   :header-rows: 1
   :widths: 26 34 40

   * - Area
     - Configured value
     - Result
   * - Release
     - Period and functional deadline 2,500 ms
     - Wake-up latency, execution time and response time are measured against
       the same release.
   * - Snapshot freshness
     - Maximum age 6,000 ms; future tolerance 100 ms
     - A snapshot outside this window is rejected.
   * - Snapshot content
     - Increasing non-zero frame ID; vehicle count at most 10,000 per
       direction; queue length 0..1,000; occupancy 0..1
     - Invalid values cannot reach the demand calculation.
   * - Demand weights
     - Queue length 0.2; normalized vehicle count 0.6; occupancy 0.2
     - Average vehicle count is multiplied by 6 and capped at 100 before
       weighting.
   * - Demand thresholds
     - 30 / 60 / 90
     - Select low, moderate, high or very-high target timing.
   * - Green timing
     - Low 20,000 ms; initial/moderate 30,000 ms; high 40,000 ms; very-high
       50,000 ms
     - Each plan changes toward its target in 5,000-ms steps and remains within
       10,000..60,000 ms.
   * - Clearance timing
     - Yellow 3,000 ms; all-red 1,000 ms; maximum cycle 100,000 ms
     - Clearance values are added consistently for both directions.
   * - Health
     - Deadline 0..10,000 ms; heartbeat 500..15,000 ms
     - Local evaluation runs every 500 ms; supervisor API cycle is 1,000 ms.
   * - Scheduling
     - ``SCHED_FIFO`` priority 80 on CPU 1; Health Monitor priority 50 on
       CPU 5
     - ``mlockall(MCL_CURRENT|MCL_FUTURE)`` and a prefaulted 64-KiB
       periodic stack are required.

Missing-input behaviour
"""""""""""""""""""""""

When no usable snapshot is available, Timing Decision keeps the previously
published plan, reports ``NO_NEW_DATA`` and retries any pending publication.
After 24 consecutive missed 2.5-second cycles, equivalent to 60 seconds, it
reports an input timeout and fails the service cycle.

The TimingPlan publisher is non-blocking.  If its depth-eight queue is full,
the newest pending plan is retained locally and retried during a later decision
cycle.

Traffic Signal Controller
^^^^^^^^^^^^^^^^^^^^^^^^^

Purpose
"""""""

Traffic Signal Controller is the only component allowed to apply signal state.
It validates incoming plans, translates them into a six-phase representation,
owns the active plan, advances the signal state machine, produces simulated
lamp output and publishes the applied state for visualization.

It does not calculate traffic demand.  Receipt of a ``TimingPlan`` is a
proposal, not permission to write arbitrary lamp values.

Plan reception and application
""""""""""""""""""""""""""""""

.. figure:: Sequence\ Diagrams/Traffic\ Signal\ Controller\ Sequence\ Diagram.drawio.svg
   :alt: Traffic Signal Controller processing sequence
   :align: center
   :width: 100%

   Plan reception and validation are separated from the real-time FSM and
   output workers.

The receiver waits at most 200 ms for a TimingPlan message and retries opening
a missing queue every 100 ms.  During one receive pass, it drains available
entries and selects the newest transport-valid publisher instance and sequence.
The selected plan then passes through Controller domain validation.  If that
plan is rejected, the active FSM plan remains unchanged; the receiver does not
fall back to an older drained plan.

Signal state machine
""""""""""""""""""""

.. figure:: FSM/Traffic\ Signal\ Controller\ FSM.drawio.svg
   :alt: Traffic Signal Controller finite-state machine
   :align: center
   :width: 100%

   The Controller owns the legal order of green, yellow and all-red states.

The normal signal sequence is:

``NS_GREEN -> YELLOW -> ALL_RED -> EW_GREEN -> YELLOW -> ALL_RED``.

A normal plan is staged and becomes active only at an ``ALL_RED`` boundary.
It cannot interrupt a current green, yellow or all-red state.  Yellow and
all-red are always non-interruptible.

An emergency plan is handled separately.  It may adjust the matching current
green only when the remaining time is strictly between 3,000 and 15,000 ms.
When accepted, the remaining green duration becomes 20,000 ms.  Emergency
handling does not bypass yellow or all-red clearance.

Controller configuration
"""""""""""""""""""""""""

Controller duration validation, startup phases, emergency window, FSM tick,
worker priorities, CPU allocation and default environment values are compiled
in ``traffic_signal_controller/include/common/config.h``.  Controller Health
Monitor timing is compiled in
``traffic_signal_controller/src/common/health_reporter.cpp``.  Both require a
rebuild after modification.

Process-level scheduling and deployment environment overrides are configured
in ``config/traffic_light_lifecycle.json``.  TimingPlan and SignalState wire
contracts are configured in
``ipc/include/traffic_ipc/timing_plan_message_v1.h`` and
``ipc/include/traffic_ipc/signal_state_message_v1.h`` respectively.

.. list-table:: Traffic Signal Controller configuration
   :header-rows: 1
   :widths: 25 35 40

   * - Area
     - Configured value
     - Observable behaviour
   * - Plan identity
     - Non-zero plan ID; at most one emergency direction
     - Invalid identity or emergency encoding is rejected.
   * - Accepted durations
     - Green 1,000..500,000 ms; yellow 1,000..10,000 ms; all-red
       500..10,000 ms
     - A non-zero declared cycle length must equal the six-phase sum.
   * - Startup plan
     - 30-s NS green, 3-s yellow, 1-s all-red, 30-s EW green, 3-s yellow,
       1-s all-red
     - The FSM can operate before receiving its first external plan.
   * - FSM
     - Absolute ``CLOCK_MONOTONIC`` tick every 1,000 ms
     - Wake-up latency is recorded; late releases advance to a future tick
       instead of running an unbounded catch-up burst.
   * - Plan receiver
     - Receive timeout 200 ms; queue-open retry 100 ms
     - Missing input does not stop the FSM.  Invalid input is reported and
       rejected.
   * - SignalState telemetry
     - 40-byte versioned message; queue depth 4; non-blocking latest-value
       publication
     - Queue failure affects visualization and diagnostics, not signal control.
   * - Health
     - Deadline 0..5,000 ms; heartbeat every two control cycles, nominally
       2,000 ms; accepted interval 500..15,000 ms
     - Local evaluation runs every 500 ms; supervisor API cycle is 1,000 ms.
   * - Scheduling
     - ``SCHED_FIFO`` on CPU 3: FSM priority 80, receiver 70, output 60;
       Health Monitor priority 60 on CPU 5
     - FSM priority must remain greater than receiver priority.  Relevant
       stacks are prefaulted by 64 KiB.
   * - Memory locking
     - ``mlockall(MCL_CURRENT|MCL_FUTURE)`` is attempted
     - Failure is logged, but does not fail Controller initialization.

The Decision producer's output range is a subset of the Controller's accepted
duration range.  TimingPlan timestamps are propagated for latency analytics;
the Controller does not currently reject a plan because of its age.

Lifecycle Manager
^^^^^^^^^^^^^^^^^

Purpose
"""""""

Lifecycle Manager owns process orchestration rather than traffic decisions.  It
starts components in dependency order, waits for readiness, supervises health,
selects the configured recovery action and performs ordered shutdown.

.. figure:: Component\ Diagrams/Life\ Manager\ Component.drawio.svg
   :alt: Lifecycle Manager component structure
   :align: center
   :width: 100%

   Lifecycle control, process state and health supervision are separate from
   TrafficSnapshot, TimingPlan and SignalState communication.

Startup and supervision
"""""""""""""""""""""""

.. figure:: Sequence\ Diagrams/Life\ Manager\ Seq.drawio.svg
   :alt: Lifecycle Manager startup, state-transition and shutdown sequence
   :align: center
   :width: 100%

   The system moves from Startup to Running, supervises the managed processes,
   and shuts them down in dependency order.

Deployment first removes stale POSIX queue objects and starts Lifecycle Manager
with the Control Daemon in the ``Startup`` target.  After the Control Daemon
endpoint becomes available, a deployment helper requests ``Running``.
Lifecycle Manager then starts Perception, Timing Decision and Signal Controller
in dependency order.  A component becomes ready only after its required domain
resources and health monitor have initialized successfully.

Lifecycle configuration
"""""""""""""""""""""""

All values in this table are configured in
``config/traffic_light_lifecycle.json``.  Edit that file to change component
dependencies, run targets, readiness/shutdown timeouts, Alive reporting,
scheduling policy or recovery actions.

``Ready timeout``
   The maximum time Lifecycle Manager waits after starting a component for it
   to reach its configured ready condition.  Readiness means that the process
   is running and has completed the initialization required to participate in
   the system.  If the component is not ready before this time expires, the
   startup transition fails and Lifecycle Manager applies the configured
   readiness-recovery action.  This timeout applies to startup, not to normal
   traffic processing.

``Shutdown timeout``
   The maximum time Lifecycle Manager allows a component to stop cleanly after
   requesting shutdown.  If the process has not stopped when the timeout
   expires, Lifecycle Manager treats the shutdown as failed and handles it
   according to the platform's configured lifecycle recovery behaviour.  This
   timeout therefore prevents a run-target transition from waiting forever for
   one process.

``Alive reporting``
   The ongoing liveness contract between a managed component and S-CORE Health
   Monitor while the component is running.  The component periodically sends
   Alive indications to show that its supervised execution is still making
   progress.  For example, ``10-s cycle; 1..8 indications; tolerance 6 failed
   cycles`` means that Health Monitor evaluates ten-second windows, expects
   between one and eight indications in each window, and tolerates up to six
   failed windows before the configured recovery is initiated.  Alive reports
   indicate process progress; they do not prove that TrafficSnapshot,
   TimingPlan or SignalState data is correct.

.. list-table:: Integrated Lifecycle configuration
   :header-rows: 1
   :widths: 23 16 17 18 26

   * - Component
     - Ready timeout
     - Shutdown timeout
     - Alive reporting
     - Dependency in ``Running``
   * - Control Daemon
     - 30 s
     - 30 s
     - Not supervised; minimum indications 0
     - First component and owner of run-target commands.
   * - Traffic Perception
     - 120 s
     - 120 s
     - 10-s cycle; 1..8 indications; tolerance 6 failed cycles
     - Included after Control Daemon in the run target.
   * - Timing Decision
     - 60 s
     - 60 s
     - 5-s cycle; 1..8 indications; tolerance 6 failed cycles
     - Depends on Traffic Perception.
   * - Traffic Signal Controller
     - 60 s
     - 60 s
     - 5-s cycle; 1..8 indications; tolerance 6 failed cycles
     - Depends on Timing Decision.

The ``Running`` transition timeout is 180 seconds.  Readiness recovery allows
one restart after 100 ms.  A runtime recovery switches to
``fallback_run_target``, which has a 120-second transition timeout and keeps
only Control Daemon active.  These values bound orchestration; they are not
traffic-processing deadlines.

Component interactions
~~~~~~~~~~~~~~~~~~~~~~

End-to-end sequence
^^^^^^^^^^^^^^^^^^^

The system sequence diagram connects the component-specific behaviour above
into one operating cycle.

.. figure:: Sequence\ Diagrams/system-sequence.drawio.svg
   :alt: End-to-end sequence between Adaptive Traffic Light components
   :align: center
   :width: 100%

   Perception publishes observations, Timing Decision publishes a proposed
   plan, Controller applies legal signal states, and managed processes report
   health independently.

Information exchanged
^^^^^^^^^^^^^^^^^^^^^

Three project-owned application messages cross component boundaries:

``TrafficSnapshot``
   The latest traffic observation from Perception to Timing Decision.

``TimingPlanMessageV1``
   A proposed timing plan from Timing Decision to Signal Controller.

``SignalStateMessageV1``
   Applied signal state from Signal Controller to the Perception viewer.  This
   message is telemetry and not a control input.

.. list-table:: Internal project message contracts
   :header-rows: 1
   :widths: 18 17 17 22 26

   * - Interface
     - Producer
     - Consumer
     - Transport
     - Contract
   * - ``TrafficSnapshot``
     - Perception
     - Timing Decision
     - ``/traffic_snapshot_v1``; POSIX queue depth 4; non-blocking
       latest-value access
     - Timestamp and per-approach vehicle count, queue length, occupancy and
       emergency indication.  Missing or stale data remains observable.
   * - ``TimingPlanMessageV1``
     - Timing Decision
     - Signal Controller
     - ``/traffic_timing_plan_v1``; POSIX queue depth 8; non-blocking
       publication; receiver wait at most 200 ms
     - 72-byte envelope with ``TPL1`` format signature, version, message
       size, publisher instance and sequence validation.
   * - ``SignalStateMessageV1``
     - Signal Controller
     - Perception viewer
     - ``/traffic_signal_state_v1``; POSIX queue depth 4; non-blocking
       latest-value publication
     - 40-byte versioned phase, lamp and remaining-time telemetry.  The viewer
       retries open after 1,000 ms and marks data stale after 3,000 ms.

The ``TPL1`` value is a format signature that distinguishes TimingPlan data
from an incompatible queue payload.  It is not a security or encryption
mechanism.

These interfaces are configurable only by changing the implementation and
rebuilding it; they are not loaded from a runtime JSON file.  Their source
locations are:

* ``TrafficSnapshot`` fields and queue names:
  ``ipc/include/traffic_ipc/messages.h``.  Its queue depth and non-blocking
  latest-value behaviour are defined in
  ``ipc/include/traffic_ipc/latest_value_queue.h``.
* ``TimingPlanMessageV1`` layout, queue name, depth, size, ``TPL1`` signature
  and version: ``ipc/include/traffic_ipc/timing_plan_message_v1.h``.  The
  receiver's 200-ms wait is defined in
  ``traffic_signal_controller/include/common/config.h``.
* ``SignalStateMessageV1`` layout, queue name, size, ``SIG1`` signature and
  version: ``ipc/include/traffic_ipc/signal_state_message_v1.h``.  It uses the
  shared latest-value queue depth from
  ``ipc/include/traffic_ipc/latest_value_queue.h``.  The viewer's 1,000-ms
  reopen interval and 3,000-ms stale-data limit are defined in
  ``traffic_perception/src/viewer/opencv_lanes_viewer.cpp``.

Changing a queue name, queue depth, message layout, size, encoding, signature
or version changes the IPC contract.  The producer and consumer must therefore
be updated and rebuilt together.  When developers make an incompatible change
to a message layout, they must define a new message version and queue name,
then update and rebuild both the producer and consumer.  The application
validates the configured contract at runtime but does not create new interface
versions automatically.

External platform interfaces
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Lifecycle control and health supervision are provided through Eclipse S-CORE.
They are external framework interfaces used by the project, not project-owned
traffic-message contracts.

.. list-table:: S-CORE platform interfaces
   :header-rows: 1
   :widths: 20 20 20 20 20

   * - Interface
     - Provider
     - User
     - Mechanism
     - Role in this system
   * - Lifecycle command
     - S-CORE Lifecycle Manager
     - Managed application processes
     - S-CORE lifecycle control channel
     - Requests Startup, Running and Stop transitions and applies readiness or
       shutdown timeouts.
   * - Health supervision
     - S-CORE Health Monitor and Lifecycle Manager
     - Managed application processes and operator
     - Heartbeat, deadline and Alive supervision; console/DLT diagnostics
     - Makes process and timing failures observable without representing them
       as valid traffic data.

Interaction and failure-containment rules
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table:: Interaction rules
   :header-rows: 1
   :widths: 23 25 27 25

   * - Boundary
     - Ordering and backpressure
     - Consumer behaviour
     - Failure containment
   * - Capture -> Perception pipeline
     - Each lane exposes one latest frame; new data replaces old data.
     - Pipeline takes the latest available frame at its own release.
     - Slow inference drops intermediate observations instead of growing a
       backlog.
   * - Perception -> Timing Decision
     - Full depth-four queue drops its oldest value; consumer drains to newest.
     - Validate timestamp, frame ID and measurements before calculating a plan.
     - Missing input keeps the previous plan; 24 consecutive misses fail the
       Decision service.
   * - Timing Decision -> Signal Controller
     - Publisher retains its newest pending value if the depth-eight queue is
       temporarily full; receiver drains available messages.
     - Select newest transport-valid message, then validate durations and
       translate it to six FSM phases.
     - Invalid plan or stale transport sequence leaves the active FSM plan
       unchanged.  Plan age is not an acceptance gate.
   * - Plan receiver -> Signal FSM
     - A newer normal pending plan supersedes an older pending plan.
     - Apply a normal plan only at an ``ALL_RED`` boundary; evaluate emergency
       input during a matching green.
     - Yellow and all-red are non-interruptible.
   * - Signal FSM -> viewer
     - Output uses an in-process latest display plus the SignalState queue.
     - Viewer renders phase, lamps and remaining time only.
     - Telemetry loss never becomes a control failure.
   * - Managed processes -> Lifecycle Manager
     - Alive reports are asynchronous and independent of domain queues.
     - Lifecycle evaluates readiness and configured recovery.
     - A queue value cannot be interpreted as proof that a process is healthy.

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

A larger Linux real-time priority wins within the same policy.  CPU numbers are
logical CPU indices and are valid only on a target exposing those CPUs.
Deployment must provide ``RLIMIT_RTPRIO``/``CAP_SYS_NICE`` and sufficient
``RLIMIT_MEMLOCK`` or ``CAP_IPC_LOCK``.  Failure to create required Timing
Decision or Controller real-time resources fails initialization.

Configuration ownership
^^^^^^^^^^^^^^^^^^^^^^^

The table below identifies the file to edit for each configuration layer.
JSON and environment values are deployment configuration.  Values stored in
C++ headers or source files are compiled design parameters and require a
rebuild.

.. list-table:: Configuration layers
   :header-rows: 1
   :widths: 20 27 30 23

   * - Layer
     - Source
     - Controls
     - Change rule
   * - Deployment
     - ``config/traffic_light_lifecycle.json``
     - Process policy, dependency order, health reporting, readiness, shutdown
       and recovery
     - Review as a system change.
   * - Perception runtime
     - ``config/traffic_perception_config.json``
     - Inputs, ROIs, model, periods, phases, CPU placement and priorities
     - Validate at startup.
   * - Interface
     - ``ipc/include/traffic_ipc/messages.h``,
       ``ipc/include/traffic_ipc/latest_value_queue.h``,
       ``ipc/include/traffic_ipc/timing_plan_message_v1.h`` and
       ``ipc/include/traffic_ipc/signal_state_message_v1.h``
     - Queue names and depths, payload sizes, signatures, versions and encoding
     - Change producers and consumers together; introduce a new version when
       compatibility changes.
   * - Algorithm and safety
     - ``traffic_timing_decision/include/decision_constants.h``,
       ``traffic_timing_decision/src/decision_engine.cpp`` and
       ``traffic_signal_controller/include/common/config.h``
     - Demand thresholds and weights, duration bounds, emergency window and FSM
       tick
     - Treat as reviewed design parameters.
   * - Runtime override
     - ``config/traffic_light_lifecycle.json`` and
       ``deployment/run_traffic_light_system.sh``
     - Timing report directory, Controller FSM priority and analytics paths
     - Edit Lifecycle JSON for managed-process values.  The deployment script
       stages wrapper-level paths; invalid Controller priority uses the logged
       default from ``traffic_signal_controller/include/common/config.h``.

Runtime and failure behaviour
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Normal operation
^^^^^^^^^^^^^^^^

During normal operation, the three application processes run at independent
rates and communicate only through bounded latest-useful-value interfaces.
Each process also reports heartbeat and deadline observations through the
separate Lifecycle/Health control plane.

Degraded operation
^^^^^^^^^^^^^^^^^^

* Missing snapshot: Timing Decision keeps the previously published plan,
  reports ``NO_NEW_DATA`` and retries pending publication.  After 24
  consecutive misses, it reports an input timeout and fails its cycle.
* Invalid or missing plan: Signal Controller keeps its current plan.  Invalid
  messages are reported; ordinary receive timeouts do not produce an absence
  diagnostic.
* Required producer queue failure: Perception or Timing Decision fails
  initialization when its output queue cannot be opened or validated.
* TimingPlan queue unavailable: Controller retries a missing queue every
  100 ms.  A non-recoverable open or contract error fails its receiver worker
  and is reported as an application runtime failure.
* SignalState queue unavailable: Controller logs the visualization failure and
  continues signal control.
* Process or health failure: Lifecycle applies the configured restart or
  fallback action.
* Stop request: each process stops periodic work, releases resources and
  acknowledges shutdown within its configured timeout.

.. sys_des:: Preserve safe signal state when an input plan is unusable
   :id: sys_des__safe_plan_fallback
   :status: valid
   :satisfies: sys_req__apply_safe_signal_transitions
   :req_covered: yes

   Signal Controller validates a timing plan before use.  If the plan is
   absent or rejected, Controller preserves its current plan and advances only
   through the signal state machine.  The baseline implements neither a
   maximum plan-hold time nor a forced fallback phase.

Implementation status and limitations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Implemented baseline
^^^^^^^^^^^^^^^^^^^^

Static review of ``origin/dev`` confirms:

* one deployment target stages Perception, Timing Decision, Signal Controller,
  Control Daemon, Lifecycle Manager, configuration, models and video inputs;
* the managed application processes exchange TrafficSnapshot and TimingPlan
  messages through the complete data path;
* Controller returns versioned SignalState telemetry to the Perception viewer;
* Startup, Running, ordered shutdown, health supervision and the control-only
  fallback target are configured; and
* the end-to-end runner collects DLT logs and produces timestamped analytics
  reports and plots.

This static review establishes implementation presence, not runtime acceptance
on the reviewer's machine.

Current limitations
^^^^^^^^^^^^^^^^^^^

* Controller has no maximum hold time for its active plan.  Without a newly
  accepted plan, the FSM continues cycling with the current plan.
* Timing Decision rejects a snapshot older than 6 seconds or more than 100 ms
  in the future, but Controller does not reject a TimingPlan by age.
* Runtime fallback stops Signal Controller and retains only Control Daemon.
  The baseline has no physical output adapter and therefore defines no physical
  lamp command for fallback entry.
* No production target hardware or resource budget is selected; runtime
  acceptance is currently limited to the reviewed development environment.
* POSIX queue payload compatibility still depends on controlled compiler and
  architecture settings despite versioned envelopes and layout assertions.
* The integrated Bazel tree contains only one focused IPC envelope test.
  Runtime scripts and analytics do not replace repeatable component and
  end-to-end tests.

Verification plan
~~~~~~~~~~~~~~~~~

* Unit-test message validation, latest-value replacement and FSM transitions,
  including rejected and missing inputs.
* Integration-test the TrafficSnapshot, TimingPlan and SignalState queues with
  producer/consumer processes and incompatible queue attributes.
* Exercise Startup, Running, recovery and Stop through Lifecycle Manager.
* Measure capture, inference, decision, reception and signal-control timing
  under representative target load.
* Measure complete perception-to-control latency rather than inferring it from
  individual periods.
* Inject stale data, queue failure, deadline overrun and process termination,
  then confirm the documented containment and recovery behaviour.
* Build the RST documentation with warnings as errors and inspect all rendered
  diagrams and requirement links.

Docs-as-code maintenance
~~~~~~~~~~~~~~~~~~~~~~~~

This description is versioned and reviewed with the implementation:

#. Update the ``.rst`` source when a component boundary, interface, period,
   deadline, priority, safety bound or recovery action changes.
#. Update the authoritative ``.drawio`` source and re-export the affected SVG
   when diagram behaviour changes.  Do not hand-edit exported SVG XML.
#. Update the implementation baseline and document date.
#. Build Sphinx with warnings treated as errors.
#. Require lead/code-owner review for timing, safety, lifecycle, recovery and
   wire-contract changes.

Detailed diagram sources and exported artifacts are cataloged in
:doc:`diagram_catalog`.  System requirements are defined in
:doc:`../02_requirement/system_requirements`.

Decision and review scope
~~~~~~~~~~~~~~~~~~~~~~~~~

This draft requests lead approval of the component boundaries, data-flow and
IPC contracts, current fallback behaviour, and separation of traffic decisions
from lifecycle and health supervision.  Any unacceptable baseline limitation
must become a tracked requirement and implementation change before production
approval.

Design Explanation
------------------

This part explains why the structures and mechanisms in the Design Description
were selected.  It records applied design patterns, the forces behind the
decisions, their consequences and the alternatives considered.  Values and
runtime contracts remain authoritative in the Design Description; this part
must not redefine them.

Applied design patterns
~~~~~~~~~~~~~~~~~~~~~~~

.. list-table:: Patterns and architectural idioms in the implementation baseline
   :header-rows: 1
   :widths: 20 28 28 24

   * - Pattern / idiom
     - Realization
     - Purpose
     - Consequence
   * - Pipes and Filters
     - Perception produces ``TrafficSnapshot``; Timing Decision transforms it
       into ``TimingPlan``; Signal Controller transforms the plan into signal
       state and output.
     - Separates sensing, policy and actuation into independently supervised
       timing domains.
     - Intermediate contracts and failure handling must be explicit at both
       IPC boundaries.
   * - Latest-Value Mailbox
     - Per-lane ``AtomicFrameBuffer`` and bounded snapshot/signal-state queues
       replace or drain older values.  Timing-plan reception drains available
       entries and selects the newest transport-valid sequence before domain
       validation.
     - Traffic control needs the freshest usable state rather than complete
       historical delivery.
     - Memory and backlog are bounded, but intermediate observations may be
       intentionally discarded.
   * - Strategy
     - ``IModelBackend`` defines inference, model-name and drawing operations;
       YOLOv8, YOLOv8-OIV7 and RT-DETRv2 are selected through configuration.
     - Keeps capture, analysis and publication independent of a particular
       inference implementation.
     - A backend must preserve the same output and timing expectations; a new
       backend still requires target-specific performance evidence.
   * - Object Pool
     - ``FramePool`` pre-creates and recycles a fixed set of image-frame
       objects shared by capture and analysis.
     - Bounds frame ownership and reduces repeated allocation in periodic
       processing.
     - Pool exhaustion becomes an explicit dropped-work condition; pool size
       and frame resolution directly affect memory consumption.
   * - State
     - ``SignalFSMEngine`` represents ``NS_GREEN``, ``YELLOW``, ``ALL_RED`` and
       ``EW_GREEN`` and owns the legal transition order.
     - Makes safety-relevant phase transitions explicit and prevents a timing
       plan from directly writing arbitrary lamp states.
     - New plan semantics must be translated into the state machine instead of
       bypassing it.
   * - Producer-Consumer with Active Workers
     - Dedicated stream, inference pipeline, MQ receiver, FSM, output and
       health-monitor workers exchange bounded data or notifications.
     - Isolates blocking I/O and compute-heavy inference from the signal FSM
       and Lifecycle application thread.
     - Scheduling policy, priority, CPU affinity, shutdown wake-up and
       ownership rules are part of the design contract.
   * - Supervisor
     - S-CORE Lifecycle Manager starts ordered run targets; per-process Health
       Monitors convert local deadline/heartbeat results into Alive reports and
       configured recovery.
     - Centralizes process-state transitions and keeps application failures
       from being hidden inside the domain pipeline.
     - Supervision detects and contains process failure; it does not by itself
       define the physical fail-safe lamp state.
   * - Adapter
     - Lifecycle application classes adapt domain services to managed
       ``Initialize``/``Run``/stop semantics.  MQ sender/receiver classes adapt
       in-process domain types to wire messages, and ``OutputSimulator`` adapts
       FSM display state to the current non-physical output boundary.
     - Keeps framework, transport and output concerns outside core decision
       logic.
     - Replacing Lifecycle, POSIX MQ or the simulator requires a new adapter
       while preserving the documented domain contract.

These names describe observable structure in the baseline; they do not imply
that every implementation is a textbook GoF pattern.  ``Latest-Value
Mailbox``, for example, is an architectural messaging idiom used to document
freshness and backpressure semantics.

Design rationale and trade-offs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Component boundaries and control direction
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Perception, Timing Decision and Signal Controller have different rates,
resource profiles and failure consequences.  Keeping them as separate managed
processes prevents four-lane inference load from directly blocking the
signal-state machine and allows Lifecycle supervision to attribute a failure
to one component.  It also prevents Timing Decision from driving lamps
directly: every plan must cross the Controller validation and FSM boundary.

The primary domain path is deliberately one-way.  Signal-state data returned
to the Perception viewer is telemetry, not decision feedback.  This avoids an
accidental control loop in which display availability or rendering latency
could modify perception results or signal timing.

Time model and real-time mechanisms
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Periodic activities use ``CLOCK_MONOTONIC``/``steady_clock`` and absolute
release times.  A wall-clock adjustment therefore cannot move a release, and
execution time does not accumulate into the phase as it would with repeated
relative sleeps.  When work is late, the loops advance or re-synchronize to a
future release instead of running an unbounded burst of catch-up cycles.

The scheduling hierarchy protects the most time-sensitive control work:
Signal FSM priority 80 is above plan reception 70 and output/health work 60;
Timing Decision runs at priority 80; Perception uses round-robin workers on
separate CPUs.  CPU affinity limits interference between inference, decision,
controller and health work.  Memory locking and stack prefaulting reduce page
faults in critical execution, while bounded pools and queues limit runtime
growth.

These mechanisms improve predictability but do not prove hard real-time
behaviour.  Linux kernel configuration, real-time throttling, device drivers,
cache/memory contention and the selected hardware remain external sources of
jitter.  This is why the design records wake-up, execution and response-time
measurements and classifies the baseline as soft real-time until WCET and
schedulability evidence exists.

Functional deadlines and health windows are separated because they answer
different questions.  The 2.5-second Timing Decision deadline measures whether
a result was useful for its release.  The wider Health Monitor window detects
whether the process remains responsive despite platform jitter or slower
inference.  Treating the health window as the functional deadline would hide
missed control objectives; treating every functional miss as immediate process
failure would make recovery overly sensitive to a single late cycle.

Freshness, backpressure and IPC contracts
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Bounded latest-value communication is chosen because traffic observations and
plans lose value with age.  If a consumer is slower than its producer, keeping
every sample increases latency while preserving data that can no longer guide
the current intersection.  Replacing/draining older entries bounds memory and
lets a recovering consumer resume from the newest state.

Non-blocking publication prevents a slow or failed downstream process from
suspending an upstream periodic thread.  For the shared latest-value helper
used by TrafficSnapshot and SignalState, a named semaphore makes each
replace/drain operation coherent across processes.  TimingPlan instead uses a
non-blocking producer, a bounded receive wait and consumer-side draining.  The
cost is that delivery is not guaranteed and the consumer must distinguish
empty, busy, stale, invalid and incompatible states.

``TimingPlanMessageV1`` adds a fixed-size versioned envelope because Timing
Decision and Signal Controller evolve independently.  The ``TPL1`` format
signature, version, size, publisher instance and sequence reject incompatible
or replayed transport data before it reaches plan validation.  Reserved bytes
allow strict validation and make unintended layout changes visible.  The raw
``TrafficSnapshot`` contract is less strongly versioned, so compiler/layout
compatibility remains a known integration risk.

Safety-oriented plan application
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The Controller uses two validation layers.  Transport validation establishes
that bytes belong to the expected wire contract; domain validation checks plan
identity, emergency flags, duration ranges and cycle consistency.  Only then
is a plan translated into the six-phase representation consumed by the FSM.

Normal plans are staged until an ``ALL_RED`` boundary.  This prevents a new
plan from shortening or replacing yellow/all-red clearance while vehicles may
still be clearing the intersection.  Yellow and all-red are non-interruptible.
Emergency handling may wake a green-phase wait, but it is accepted only for
the matching direction and within the configured remaining-time window.  The
FSM, rather than the message receiver, remains the sole owner of applied
signal state.

Keeping the previous valid plan when input is missing avoids an arbitrary
transition caused by communication loss.  The baseline does not impose a
plan-hold timeout or physical fallback lamp command, however, so this mechanism
is containment rather than a complete production safety concept.

Lifecycle, health and recovery separation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Lifecycle commands and health indications use a control plane separate from
domain queues.  Consequently, an application can be unhealthy even when an
old queue value exists, and an empty queue cannot be mistaken for a process
failure without additional evidence.  Dependency ordering ensures producers
are started before their consumers in the ``Running`` target, while readiness
and shutdown timeouts bound orchestration rather than domain computation.

One readiness restart handles a transient initialization failure without
creating an unlimited restart loop.  Runtime recovery switches to the
control-only target so management access remains available for diagnosis.  It
does not claim to be a physical traffic-signal fail-safe because the baseline
uses an output simulator and stops the Controller in fallback.

Configuration and observability
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Configuration is layered according to ownership.  Lifecycle JSON controls
deployment-wide process policy and recovery; Perception JSON controls inputs,
model and worker timing; shared headers control wire compatibility; module
constants control algorithm and safety bounds; environment variables provide
explicit deployment overrides.  This prevents an operator-level logging path
change from silently altering an IPC or signal-safety contract.

DLT records, wake-up samples, execution timing and propagated timestamps are
kept outside the domain decision values.  They make scheduling and end-to-end
latency reviewable without feeding diagnostic delays back into control logic.
Timing records are captured at defined boundaries so measurements from
different processes use the same monotonic time domain.

Summary of trade-offs
^^^^^^^^^^^^^^^^^^^^^

.. list-table:: Principal design trade-offs
   :header-rows: 1
   :widths: 23 27 25 25

   * - Decision
     - Benefit
     - Cost / limitation
     - Mitigation
   * - Separate managed processes
     - Failure isolation and independent timing domains
     - IPC, deployment and versioning complexity
     - Shared contracts, validation and Lifecycle ordering
   * - Latest-value delivery
     - Bounded backlog and fresher control input
     - Intermediate data can be dropped
     - Durable diagnostic logging and explicit sequence/freshness checks
   * - Fixed RT priorities and CPU affinity
     - Predictable interference hierarchy
     - Hardware-specific and privilege-dependent
     - Startup validation, degraded logging and target measurements
   * - Apply normal plan at ``ALL_RED``
     - Preserves clearance transitions
     - Adds phase-dependent command latency
     - End-to-end latency measurement and emergency-specific path
   * - Continue current valid plan on missing input
     - Avoids arbitrary state changes
     - Can retain an obsolete plan indefinitely
     - Expose as a baseline limitation; define timeout/fallback by requirement
   * - Wider health windows
     - Avoids recovery from isolated scheduling jitter
     - Health success does not prove functional deadline success
     - Separate deadline analytics and health reporting

Alternatives considered
~~~~~~~~~~~~~~~~~~~~~~~

Single-process pipeline
^^^^^^^^^^^^^^^^^^^^^^^

Running capture, inference, decision and signal control in one process would
remove serialization and queue overhead.  It was not selected because compute
or memory failure in Perception would share the Controller's failure domain,
and per-component lifecycle/health attribution would be lost.  Separate
processes retain explicit isolation at the cost of IPC complexity.

Deliver every observation and plan through FIFO queues
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A durable FIFO would preserve complete history, but a slow consumer would act
on increasingly obsolete traffic state and queue capacity would become a
latency budget.  Latest-value channels are retained for control; complete
history belongs in diagnostics rather than the live decision path.

Synchronous request/response calls
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

RPC-style calls would make request ownership and immediate success/failure
clear, but blocking and timeout propagation would couple process schedules.
Asynchronous bounded queues let each component keep its periodic release and
degrade independently.

Relative periodic sleeps
^^^^^^^^^^^^^^^^^^^^^^^^

Sleeping for one period after each execution is simpler, but execution time
and jitter accumulate as schedule drift.  Absolute monotonic releases preserve
the intended phase and make wake-up latency measurable against a stable
deadline.

Shared-memory frame and plan transport
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Shared memory could reduce copies and support larger payloads, but it requires
more complex lifetime, synchronization and crash-recovery rules.  Current
domain messages are small enough for POSIX queues, while large video frames
remain inside Perception.  Shared memory remains an option only if measured
queue overhead becomes material.

Immediate application of every accepted plan
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Applying a plan as soon as it arrives would minimize command latency but could
interrupt yellow/all-red clearance or produce an unsafe phase jump.  Staging
normal plans until ``ALL_RED`` keeps transitions under FSM ownership; the
emergency path is separately constrained.

Dynamic allocation throughout periodic processing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Unbounded dynamic allocation would simplify ownership but add allocator
latency, fragmentation and failure variability.  The fixed frame pool,
bounded queues and fixed-capacity timing-sample buffer are retained in the
time-sensitive paths.  Initialization may still allocate resources before
regular periodic execution begins.
