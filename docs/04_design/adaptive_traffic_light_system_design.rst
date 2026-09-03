Adaptive Traffic Light - System Design
======================================

Document control
----------------

:Author: Adaptive Traffic Light team
:Document status: Draft
:Required approver: Lead
:Target branch: ``dev``
:Last updated: 2026-09-03
:Implementation baseline: ``origin/feat/integrate-full`` at ``06b3d77``
:Related issues: BDCI-2160, BDCI-2161

The document status describes the review lifecycle of this page.  The
``valid`` status on individual needs below has the separate S-CORE meaning
"sufficiently defined to enter review".  Approval is recorded by the Merge
Request, not by changing a need to a non-configured status.

Design Description
------------------

This part defines what the system contains, its interfaces, configuration and
observable runtime behaviour.  It is the Design Description deliverable for
BDCI-2161 and applies the RST/docs-as-code method investigated in BDCI-2160.

Context and scope
~~~~~~~~~~~~~~~~~

The project contains perception, timing-decision, signal-control and lifecycle
code, plus editable draw.io diagrams, but it does not yet have a reviewed
system-design page tying those assets to requirements.  The implementation
baseline on ``origin/feat/integrate-full`` contains a unified Bazel workspace,
one Lifecycle deployment and the complete three-process data path.  This
proposal documents that integrated architecture and separates demonstrated
implementation from remaining acceptance decisions.

Goals and non-goals
~~~~~~~~~~~~~~~~~~~

Goals
^^^^^

* Establish the system component boundaries and ownership.
* Define the traffic-snapshot and timing-plan data flow.
* Define periodic, lifecycle and failure behaviour at system level.
* Trace each architecture decision to a minimal system requirement.

Non-goals
^^^^^^^^^

* Specify perception algorithms or model accuracy.
* Specify detailed C++ class design for every component.
* Claim production readiness, functional-safety certification or complete
  integration where the implementation still contains placeholders.
* Select deployment hardware beyond the Linux targets already represented in
  the repository.

Requirements and constraints
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The proposal addresses the requirements in
:doc:`../02_requirement/system_requirements`.  The current implementation also
imposes these constraints:

* Processes run on Linux and communicate domain data through non-blocking
  POSIX latest-value message queues.
* The timing-decision process runs every 2.5 seconds; its integrated Health
  Monitor configuration allows a 10-second processing deadline.
* Perception captures frames every 500 milliseconds and runs its configured
  four-lane inference pipeline every 10 seconds in the reviewed baseline.
* Managed processes use Eclipse S-CORE Lifecycle v0.3.0 and Health Monitor.
* Native ``x86_64-linux`` output can run on the development host; an
  ``arm64-linux`` output must run on an AArch64 target or configured runner.
* Real-time scheduling and memory locking depend on deployment privileges and
  resource limits.
* The current design is a monitored, best-effort real-time Linux design.  It
  does not claim hard real-time behaviour: WCET, blocking-time, schedulability
  and target-load evidence have not yet been established.

System architecture
~~~~~~~~~~~~~~~~~~~

System overview
^^^^^^^^^^^^^^^

The SVG below is the reviewable web artifact for the editable
:download:`System Component.drawio <Component Diagrams/System Component.drawio>`
source.  The source remains authoritative for graphical edits.

.. image:: Component\ Diagrams/System\ Component.drawio.svg
   :alt: Adaptive Traffic Light system components and principal data flow
   :align: center
   :width: 100%

.. sys_des:: Separate the system into supervised pipeline components
   :id: sys_des__supervised_pipeline_components
   :status: valid
   :satisfies: sys_req__publish_traffic_snapshot, sys_req__produce_adaptive_timing_plan, sys_req__apply_safe_signal_transitions, sys_req__publish_signal_state, sys_req__supervise_managed_processes
   :req_covered: yes

   The system is decomposed into Traffic Perception and Acquisition, Traffic
   Timing Decision, Traffic Signal Controller, and Lifecycle Manager.  Domain
   data flows in one direction through the first three components; lifecycle
   commands and health reporting form a separate control plane.

Components and responsibilities
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Traffic Perception and Acquisition
   Owns input capture, analysis and construction of ``TrafficSnapshot``.  It
   publishes the latest snapshot without making signal-timing decisions.  In
   the implementation baseline, its managed Lifecycle application starts the
   real capture, ONNX inference, analysis and publication threads and consumes
   signal-state telemetry for visualization.

Traffic Timing Decision
   Consumes the latest snapshot, executes one deterministic decision cycle and
   publishes ``TimingPlan``.  It owns the 2.5-second periodic release, deadline
   measurement and timing diagnostics, but it does not directly drive signal
   outputs.

Traffic Signal Controller
   Validates and applies accepted timing plans through its signal state
   machine, emits the observable signal output and publishes the applied state
   for visualization.  The implementation baseline runs separate real-time
   plan-receiver, FSM and output workers under its managed application.

Lifecycle Manager
   Starts, transitions, stops and supervises each managed process.  Health
   reporting is isolated from domain IPC so a data-flow fault and a process
   health fault remain distinguishable.  The integrated deployment orders the
   pipeline through explicit component dependencies.

Real-time model and configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Real-time classification
^^^^^^^^^^^^^^^^^^^^^^^^

The control path is designed to prefer fresh data, bounded memory use and
absolute periodic releases.  A deadline miss is observable and can trigger
supervision, but the present Linux/POSIX deployment does not prove that a
deadline can never be missed.  Therefore this document classifies the system
as **soft real-time with safety-oriented degraded behaviour**, pending WCET
and schedulability evidence on the selected target hardware.

The following terms are intentionally distinct:

Period
   Nominal interval between releases of a periodic activity.
Phase
   Offset used to stagger the first release relative to the common startup
   gate.
Functional deadline
   Latest useful completion time used by timing analytics.  Timing Decision
   uses its 2.5-second period as this deadline.
Health window
   Wider S-CORE Health Monitor acceptance window.  It detects loss of
   responsiveness but is not evidence that the functional deadline was met.
Freshness limit
   Maximum acceptable age of input data.  A miss-count threshold is not the
   same as a timestamp-based freshness check.

System timing budget and freshness
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table:: End-to-end timing attributes in the implementation baseline
   :header-rows: 1
   :widths: 23 17 18 20 22

   * - Activity
     - Release/trigger
     - Configured timing
     - Monitoring/freshness
     - Overrun behaviour
   * - Four-lane frame capture
     - Absolute periodic stream workers
     - Period 500 ms; phase 0 ms
     - No independent end-to-end freshness limit
     - Latest frame replaces the previous frame; processing does not wait for
       a backlog.
   * - Perception inference and snapshot publication
     - Absolute periodic pipeline worker
     - Period 10,000 ms; phase 50 ms
     - Perception health deadline 0..30,000 ms
     - A delayed release is recorded; the latest usable observation is
       published when complete.
   * - Perception content view / signal overlay
     - Periodic application loop
     - Content 10,000 ms, phase 260 ms; overlay refresh 250 ms
     - Heartbeat 100..10,000 ms
     - Missed display releases are skipped; visualization cannot affect
       control output.
   * - Timing decision
     - Absolute ``CLOCK_MONOTONIC`` release
     - Period and functional deadline 2,500 ms
     - Snapshot age at most 6,000 ms (100 ms future tolerance); health
       deadline 0..10,000 ms; heartbeat 500..15,000 ms
     - Wake-up, execution and response-time overruns are logged.  After 24
       consecutive missing snapshots (60 s), the cycle fails.
   * - Timing-plan reception
     - Blocking receive with bounded timeout
     - Receive timeout 200 ms; reopen retry 100 ms
     - Envelope, publisher instance and sequence are validated
     - Available entries are drained and only the newest valid plan is handed
       to the controller.
   * - Signal FSM
     - Absolute ``CLOCK_MONOTONIC`` tick
     - Tick 1,000 ms
     - Wake-up latency samples; application health deadline 0..5,000 ms
     - Normal plans wait for an ``ALL_RED`` boundary; emergency plans may
       interrupt a green phase only within the configured safe window.
   * - Lifecycle supervision
     - S-CORE evaluation loop
     - Evaluation cycle 100 ms
     - Perception reporting window 10 s; Decision and Controller 5 s
     - Readiness gets one restart attempt; runtime failure switches to the
       control-only fallback target.

The nominal sensor-to-actuator latency is not a single constant.  A snapshot
can wait up to one Timing Decision release (2.5 s), and a normal accepted plan
can wait until the next safe ``ALL_RED`` application boundary.  In addition,
Perception currently publishes only every 10 s.  The worst-case latency is
therefore phase- and plan-dependent and must be measured; it must not be
inferred by adding only the process periods.

Scheduling and resource allocation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table:: Process and worker real-time configuration
   :header-rows: 1
   :widths: 24 20 16 16 24

   * - Process / worker
     - Policy
     - Priority
     - CPU affinity
     - Memory behaviour
   * - Perception process
     - ``SCHED_RR``
     - 70
     - Workers below; main/viewer CPU 5
     - Fixed frame pool of 20 1920x1080 frames in the reviewed configuration.
   * - Perception stream workers (4)
     - ``SCHED_RR``
     - 70 each
     - CPU 4 each
     - Atomic latest-frame exchange bounds queued work.
   * - Perception pipeline
     - ``SCHED_RR``
     - 70
     - CPU 2
     - OpenCV helper parallelism is disabled; inference backend owns its
       configured workers.
   * - Perception Health Monitor
     - ``SCHED_RR``
     - 70
     - CPU 5
     - Internal evaluation 100 ms; supervisor API cycle 2,000 ms.
   * - Timing Decision process / periodic thread
     - ``SCHED_FIFO``
     - 80 from Lifecycle
     - CPU 1
     - ``mlockall(MCL_CURRENT|MCL_FUTURE)`` is mandatory; 64 KiB of the
       periodic stack is prefaulted.
   * - Timing Decision Health Monitor
     - ``SCHED_FIFO``
     - 50
     - CPU 5
     - Internal evaluation 500 ms; supervisor API cycle 1,000 ms.
   * - Signal Controller process
     - ``SCHED_FIFO``
     - 80 from Lifecycle
     - CPU 3
     - Memory lock is attempted and each relevant stack prefaults 64 KiB.
   * - Signal FSM / plan receiver / output worker
     - ``SCHED_FIFO``
     - 80 / 70 / 60
     - CPU 3
     - FSM priority must remain greater than plan-receiver priority.
   * - Signal Controller Health Monitor
     - ``SCHED_FIFO``
     - 60
     - CPU 5
     - Internal evaluation 500 ms; supervisor API cycle 1,000 ms.

The priority values are Linux real-time priorities: a larger number wins
within the same real-time policy.  CPU numbers are logical CPU indices and are
valid only on a target exposing those CPUs.  The deployment must provide
``RLIMIT_RTPRIO``/``CAP_SYS_NICE`` and sufficient ``RLIMIT_MEMLOCK`` or
``CAP_IPC_LOCK``.  Failure to create required Timing Decision or Controller
real-time execution resources is an initialization failure; Perception has a
development fallback to ``SCHED_OTHER`` when worker creation returns
``EPERM``, which must be reported as degraded operation.

Configuration ownership and precedence
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table:: Configuration layers
   :header-rows: 1
   :widths: 18 27 27 28

   * - Layer
     - Owner / source
     - Examples
     - Change rule
   * - Deployment
     - ``config/traffic_light_lifecycle.json`` in the integrated baseline
     - Process policy/priority, dependency order, health reporting windows,
       readiness/shutdown timeout and recovery target
     - Review as a system change because it affects several processes.
   * - Module runtime
     - ``config/traffic_perception_config.json`` and deployment environment
     - Capture/pipeline/view periods and phases, model, videos, ROI, worker
       CPU/priority and log/report paths
     - Validate at startup; an invalid or incomplete configuration must fail
       initialization.
   * - Interface
     - Shared IPC headers
     - Queue names/depths, payload size, magic, version and field encoding
     - Producer and consumer must change atomically in one Merge Request; add
       a new version instead of silently changing a deployed payload.
   * - Algorithm / safety constants
     - Module headers and implementation constants
     - Demand weights, green-time bounds, emergency window and FSM tick
     - Treat as reviewed design parameters, not incidental implementation
       details.  Move to validated runtime configuration only when required.
   * - Runtime override
     - Environment variables staged by Lifecycle
     - ``TIMING_REPORT_LOG_DIR`` and Controller FSM priority/analytics paths
     - Explicit environment value overrides the compiled default; invalid
       priority falls back to the documented default and is logged.

System lifecycle configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table:: Integrated Lifecycle configuration
   :header-rows: 1
   :widths: 23 16 17 18 26

   * - Component
     - Ready timeout
     - Shutdown timeout
     - Alive reporting cycle
     - Dependency in ``Running``
   * - Control Daemon
     - 30 s
     - 30 s
     - Not supervised (minimum indications 0)
     - First component; owns run-target commands.
   * - Traffic Perception
     - 120 s
     - 120 s
     - 10 s; 1..8 indications; tolerance 6 failed cycles
     - Depends on Control Daemon through the run target.
   * - Timing Decision
     - 60 s
     - 60 s
     - 5 s; 1..8 indications; tolerance 6 failed cycles
     - Depends on Traffic Perception.
   * - Traffic Signal Controller
     - 60 s
     - 60 s
     - 5 s; 1..8 indications; tolerance 6 failed cycles
     - Depends on Timing Decision.

The initial run target is ``Startup`` (Control Daemon only).  ``Running``
starts the ordered pipeline and has a 180-second transition timeout.  Default
readiness recovery is one restart after 100 ms.  A runtime recovery switches
to ``fallback_run_target``, which retains only Control Daemon and has a
120-second transition timeout.  These timeouts bound lifecycle coordination;
they are not domain-processing deadlines.

Module functional configuration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Traffic Perception and Acquisition
   The reviewed configuration selects ``yolov8_oiv7``, model
   ``yolov8m-oiv7.onnx``, emergency class ``Van``, four 1920x1080 video inputs
   and one polygonal ROI per North/South/East/West approach.  Capture timing,
   pipeline timing and thread placement are listed above.  Model/video paths
   are resolved into the staged deployment tree at startup.

Traffic Timing Decision
   Demand score weights are queue length 0.2, normalized vehicle count 0.6
   and occupancy 0.2.  The average vehicle count is multiplied by 6 and
   capped at 100 before weighting.  Score thresholds are 30/60/90.  Green
   duration changes toward the selected target in 5,000-ms steps and is
   constrained to 10,000..60,000 ms.  The initial/moderate target is 30,000
   ms, low 20,000 ms, high 40,000 ms and very-high 50,000 ms.  Yellow is 3,000
   ms, all-red is 1,000 ms and maximum complete cycle length is 100,000 ms.
   Input validation also requires an increasing, non-zero frame ID; at most
   10,000 vehicles per direction; queue length 0..1,000; and occupancy 0..1.

Traffic Signal Controller
   A received plan must have a non-zero plan ID, exactly one or zero emergency
   directions, green duration 1,000..500,000 ms, yellow 1,000..10,000 ms and
   all-red 500..10,000 ms.  A non-zero declared cycle length must equal the
   sum of all six phases.  The startup plan is 30 s NS green, 3 s yellow, 1 s
   all-red, 30 s EW green, 3 s yellow and 1 s all-red.  Emergency changes are
   accepted only when the matching green phase has strictly between 3,000 and
   15,000 ms remaining and request a 20,000-ms remaining green duration.

The Decision producer's green and cycle output ranges are a subset of the
Controller input ranges.  Every structurally valid plan produced by Decision
is therefore within the Controller's configured duration bounds.  Plan
timestamps are currently used for latency diagnostics, not for rejection by
age.

Implementation status
~~~~~~~~~~~~~~~~~~~~~

Static review of ``origin/feat/integrate-full`` at ``06b3d77`` confirms:

* one Bazel deployment target stages Perception, Timing Decision, Signal
  Controller, Control Daemon, Lifecycle Manager, configuration, models and
  video inputs;
* the three real managed processes exchange ``TrafficSnapshot`` and versioned
  ``TimingPlanMessageV1`` messages end to end;
* Signal Controller publishes versioned ``SignalStateMessageV1`` telemetry
  back to the Perception viewer without using it in control decisions;
* Startup, Running, ordered shutdown, health supervision and a control-only
  fallback run target are configured; and
* the end-to-end runner collects DLT logs and produces timestamped analytics
  reports and plots.

This review establishes implementation presence, not runtime acceptance on the
reviewer's machine.  Reproducing the Bazel build and deployment remains part
of the verification plan.

Interfaces and data flow
~~~~~~~~~~~~~~~~~~~~~~~~

.. list-table:: System interface contracts
   :header-rows: 1
   :widths: 18 16 16 20 30

   * - Interface
     - Producer
     - Consumer
     - Transport/timing
     - Contract
   * - ``TrafficSnapshot``
     - Perception
     - Timing Decision
     - POSIX queue ``/traffic_snapshot_v1``; depth 4; latest value;
       non-blocking
     - Timestamp plus per-approach vehicle count, queue length, occupancy and
       emergency indication. A stale or absent value must be observable.
   * - ``TimingPlan``
     - Timing Decision
     - Signal Controller
     - POSIX queue ``/traffic_timing_plan_v1``; depth 8; bounded,
       non-blocking publish
     - ``TimingPlanMessageV1`` is a 72-byte envelope.  Its format signature
       (``0x54504C31``, ASCII ``TPL1``), version, message size, publisher
       instance and sequence are validated before decoding.  The signature
       distinguishes a timing-plan message from incompatible queue data; it
       is not a security or encryption mechanism.  The consumer drains
       available entries and applies only the newest accepted plan.
   * - ``SignalState``
     - Signal Controller
     - Perception viewer
     - POSIX queue ``/traffic_signal_state_v1``; depth 4; latest value;
       non-blocking publish
     - ``SignalStateMessageV1`` is a 40-byte, versioned visualization contract
       containing phase, lamp states and remaining times. It is telemetry only.
   * - Lifecycle command
     - Lifecycle Manager
     - Each managed process
     - S-CORE lifecycle control channel; event driven
     - Supports Startup, Running and Stop transitions with bounded activation
       and shutdown handling.
   * - Health and diagnostics
     - Each managed process
     - Lifecycle Manager / operator
     - Heartbeat, deadline monitor, console and DLT records
     - Identifies the process, cycle and observed failure; diagnostics must not
       masquerade as successful domain output.

Module interaction rules
^^^^^^^^^^^^^^^^^^^^^^^^

The end-to-end sequence below shows the domain data path together with the
separate lifecycle and health-control path.

:download:`Editable system-sequence source <Sequence Diagrams/system.drawio>`

.. figure:: Sequence\ Diagrams/system-sequence.drawio.svg
   :alt: End-to-end sequence between the Adaptive Traffic Light modules
   :align: center
   :width: 100%

   End-to-end publication, decision, signal-control and supervision sequence.

.. list-table:: Interaction, synchronization and failure containment
   :header-rows: 1
   :widths: 19 19 21 21 20

   * - Boundary
     - Synchronization
     - Backpressure / ordering
     - Consumer action
     - Failure containment
   * - Capture -> Perception pipeline
     - Atomic per-lane frame exchange
     - New frame replaces old frame; no unbounded queue
     - At its release, the pipeline takes the latest frame for each lane
     - Slow inference drops intermediate observations instead of delaying all
       later work.
   * - Perception -> Timing Decision
     - Named semaphore protects queue replace/drain operation
     - Publisher drops oldest on a full four-slot queue; consumer drains to
       newest
     - Validate frame/timestamp/metrics, then compute one plan
     - No value keeps the previous plan; 24 consecutive misses fail the
       decision service.
   * - Timing Decision -> Signal Controller
     - POSIX MQ; receiver waits at most 200 ms per receive pass
     - Versioned 72-byte envelope; newest valid publisher-instance/sequence
       wins
     - Validate timing ranges, translate to six phases, then stage the plan
     - Invalid/stale data is rejected without changing the active FSM plan.
   * - Plan receiver -> Signal FSM
     - In-process synchronized latest pending plan
     - A newer normal plan supersedes an older pending plan
     - Apply a normal plan only after ``ALL_RED``; evaluate emergency plan
       during green
     - Yellow and all-red remain non-interruptible.
   * - Signal FSM -> Output / Perception viewer
     - In-process latest display plus versioned SignalState queue
     - Output worker publishes each applied display; viewer consumes latest
     - Render lamp state and remaining time only
     - Loss of telemetry affects display/diagnostics, never the control
       decision.
   * - Managed processes -> Lifecycle Manager
     - Asynchronous Alive notifications derived from local health monitors
     - Independent of domain queues
     - Evaluate process readiness and configured recovery
     - A health fault cannot be interpreted as a valid snapshot or plan.

There is exactly one domain-control direction:
``Perception -> Timing Decision -> Signal Controller -> signal output``.
``Signal Controller -> Perception viewer`` is an observability return path,
not a closed-loop decision input.  Lifecycle commands and health indications
form a separate control plane and must not carry domain values.

.. sys_des:: Exchange domain data through bounded latest-value channels
   :id: sys_des__latest_value_data_flow
   :status: valid
   :satisfies: sys_req__publish_traffic_snapshot, sys_req__produce_adaptive_timing_plan
   :req_covered: yes

   Perception publishes ``TrafficSnapshot`` to ``/traffic_snapshot_v1`` and
   Timing Decision publishes ``TimingPlanMessageV1`` to
   ``/traffic_timing_plan_v1``.  Both channels are non-blocking and bounded;
   their producer/consumer logic favors the newest usable value so a slow
   consumer does not create an unbounded backlog of obsolete traffic
   decisions.

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
   to the Lifecycle Manager and diagnostics sinks; they do not modify domain
   messages.

Runtime and failure behaviour
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Startup
^^^^^^^

#. Deployment removes or validates stale IPC resources and starts the
   Lifecycle Manager and control service.
#. Each managed process initializes its domain resources and health monitors.
#. The Lifecycle Manager requests ``Running`` only after initialization
   succeeds.
#. Producers open their queues before regular periodic publication begins.

:download:`Editable Lifecycle sequence source <Sequence Diagrams/Life Manager Seq.drawio>`

.. figure:: Sequence\ Diagrams/Life\ Manager\ Seq.drawio.svg
   :alt: Lifecycle Manager startup, state-transition and shutdown sequence
   :align: center
   :width: 100%

   Lifecycle startup, ``Running`` transition, supervision and managed
   shutdown sequence.

Normal operation
^^^^^^^^^^^^^^^^

#. Perception captures and analyzes configured input, then replaces the latest
   traffic snapshot.
#. At each 2.5-second release, Timing Decision consumes the newest available
   snapshot, computes a plan and replaces the latest timing plan.
#. Signal Controller validates an available plan and advances only through
   legal state-machine transitions.
#. Signal Controller publishes its applied state to the Perception viewer for
   display; this does not close a control feedback loop.
#. Each managed process reports heartbeat and deadline observations; timing
   diagnostics are recorded separately from the domain messages.

The Timing Decision processing flow and Signal Controller state machine are
shown below.  The flowchart follows a snapshot through demand evaluation and
plan constraints; the FSM shows the legal order of signal phases and safe
transition points.

:download:`Editable Timing Decision flowchart source <Flowchart/Traffic Timing Decision Flowchart.drawio>`

.. figure:: Flowchart/Traffic\ Timing\ Decision\ Flowchart.drawio.svg
   :alt: Traffic Timing Decision processing flow
   :align: center
   :width: 100%

   Traffic snapshot validation, demand calculation and constrained timing-plan
   generation.

:download:`Editable Signal Controller FSM source <FSM/Traffic Signal Controller FSM.drawio>`

.. figure:: FSM/Traffic\ Signal\ Controller\ FSM.drawio.svg
   :alt: Traffic Signal Controller finite-state machine
   :align: center
   :width: 100%

   Legal green, yellow and all-red transitions applied by Signal Controller.

Degraded and failure operation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* Missing snapshot: Timing Decision keeps the previously published plan,
  reports ``NO_NEW_DATA`` and retries a pending publication.  After 24
  consecutive 2.5-second misses (60 seconds), the integrated service reports
  input timeout and fails its cycle.
* Invalid or missing plan: Signal Controller retains the current safe plan and
  reports rejection or absence; it must not jump to an arbitrary signal state.
* Queue attribute mismatch or open failure: the affected process fails
  initialization rather than communicating with an incompatible endpoint.
* Deadline, heartbeat or application failure: Health Monitor reports the
  failure to lifecycle supervision and diagnostics.  Readiness recovery makes
  one restart attempt; configured runtime recovery switches to the fallback
  run target that keeps only Control Daemon active.
* Stop request: each process stops periodic work, releases domain resources and
  acknowledges shutdown within the deployment timeout.

.. sys_des:: Preserve safe signal state when an input plan is unusable
   :id: sys_des__safe_plan_fallback
   :status: valid
   :satisfies: sys_req__apply_safe_signal_transitions
   :req_covered: yes

   Signal Controller validates a timing plan before use.  If the plan is
   absent or rejected, the controller preserves its current safe plan and
   advances only through the signal state machine.  The baseline implements
   neither a maximum plan-hold time nor a forced fallback phase.

Docs-as-code maintenance
~~~~~~~~~~~~~~~~~~~~~~~~

This design is maintained with the implementation and follows the same review
workflow:

#. Edit the ``.rst`` source and the authoritative ``.drawio`` source in the
   same branch as the related design or implementation change.
#. Re-export affected diagrams as SVG.  Do not hand-edit exported SVG XML.
#. Update the implementation baseline, document date, configuration tables,
   interface contracts and linked S-CORE needs when behaviour changes.
#. Build Sphinx with warnings treated as errors and inspect image rendering,
   cross-references and requirement links.
#. Require code-owner/lead review for timing, safety, lifecycle, recovery or
   wire-contract changes.  Record approval in the Merge Request.

Reviewers should compare values in this page with the shared IPC headers,
Lifecycle JSON, Perception JSON and module constants.  A code change that
alters a period, phase, deadline, priority, affinity, queue contract, safety
bound or recovery action is incomplete until this design description and the
corresponding diagram are updated.  This keeps prose, diagrams, requirements
and executable configuration reviewable as one versioned change.

Verification plan
~~~~~~~~~~~~~~~~~

* Unit-test message validation, latest-value replacement and signal-state
  transitions, including rejected and missing inputs.
* Integration-test both POSIX queues with producer/consumer processes and
  incompatible queue attributes.
* Run the managed deployment through Startup, Running and Stop and confirm
  health visibility for every process.
* Measure wake-up latency, execution time and end-to-end perception-to-control
  latency under representative load; verify the 2.5-second periodic release
  and configured 10-second monitored processing window.
* Inject stale data, queue failure, deadline overrun and process termination;
  confirm the approved degraded-mode and recovery policy.

Known constraints and open items
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Risks
^^^^^

* The integrated code is on ``origin/feat/integrate-full`` rather than this
  documentation branch; the design and implementation can diverge until both
  are merged into ``dev``.
* POSIX queue payloads are in-memory C++ structures; compiler, architecture and
  version compatibility must remain controlled despite the versioned envelope
  and compile-time size/layout assertions.
* Real-time behaviour depends on scheduling privileges, memory-lock limits and
  deployment load that local functional tests do not establish.
* The integrated branch contains only one focused IPC envelope test in its
  Bazel tree; runtime scripts and analytics provide evidence but do not replace
  repeatable automated component and end-to-end tests.
* Existing detailed draw.io diagrams can drift from interfaces in code unless
  diagram review is included in relevant Merge Requests.

Current baseline behaviour and limitations
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* Signal Controller does not impose a maximum hold time on the active plan.
  Without a newly accepted plan, the FSM continues cycling with its current
  plan.
* The build supports native ``x86_64-linux`` and cross-built ``arm64-linux``.
  CPU allocation is defined by logical CPU indices, but no production target
  hardware or resource budget is specified.
* Timing Decision rejects a snapshot older than 6 seconds or more than 100 ms
  in the future.  It keeps the previous plan while input is unavailable and
  fails after 24 consecutive 2.5-second misses, equivalent to 60 seconds.
* Lifecycle runtime recovery switches to ``fallback_run_target``, which keeps
  only Control Daemon active.  Signal Controller is stopped; the baseline has
  no physical signal-I/O adapter and therefore defines no lamp command on
  fallback entry.
* Signal Controller does not reject ``TimingPlan`` by age.  The generation and
  perception timestamps are used for latency diagnostics only; envelope and
  transport-sequence validation still apply.

Decision and review scope
~~~~~~~~~~~~~~~~~~~~~~~~~

This draft requests lead approval of the component boundaries, IPC/data-flow
contracts, current fallback behaviour and lifecycle/health separation.  Any
baseline limitation that is not acceptable for the intended deployment must
be converted into a requirement and tracked change before production
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
       entries and selects the newest valid transport sequence.
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
suspending an upstream periodic thread.  A named semaphore makes each
replace/drain operation coherent across processes.  The cost is that delivery
is not guaranteed and the consumer must distinguish empty, busy, stale,
invalid and incompatible states.

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
