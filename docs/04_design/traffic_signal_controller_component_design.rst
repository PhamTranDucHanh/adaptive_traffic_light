Traffic Signal Controller - Component Design
============================================

This page describes the internal structure, processing flow, configuration,
timing and failure behaviour of the Traffic Signal Controller component.
System context, cross-component interactions and system-level design rationale
are documented in :doc:`adaptive_traffic_light_system_design`.

Source-code and configuration file paths in this document are relative to
``docs/05_development/adaptive_traffic_light/``.

Purpose
-------

Traffic Signal Controller is the only component allowed to apply signal state.
It validates incoming plans, translates them into a six-phase representation,
owns the active plan, advances the signal state machine, produces simulated
lamp output and publishes the applied state for visualization.

It does not calculate traffic demand.  Receipt of a ``TimingPlan`` is a
proposal, not permission to write arbitrary lamp values.

Plan reception and application
------------------------------

.. figure:: Sequence\ Diagrams/Traffic\ Signal\ Controller\ Sequence\ Diagram.drawio.svg
   :alt: Traffic Signal Controller processing sequence
   :align: center
   :width: 100%

   Plan reception and validation are separated from the real-time finite-state
   machine (FSM) and output workers.

The internal objects divide the receive-to-application path as follows:

``MqTimingPlanReceiverWorker``
   Owns the POSIX queue connection.  It waits at most 200 ms for a message,
   retries opening a missing queue every 100 ms, drains the available entries
   and selects the newest message with the expected format, size, version,
   publisher instance and sequence.

``PlanReceiver``
   Checks the plan identifier, emergency flags, duration ranges and cycle
   consistency, then translates an accepted message into the six internal
   phase durations used by the signal state machine.

``PlanSyncChannel``
   Synchronizes the receiver worker with the FSM worker.  It stores at most one
   newest normal pending plan and one emergency plan.  A newer normal plan
   replaces an older normal plan that the FSM has not yet consumed.

``SignalFSMEngine``
   Owns the active plan, the current phase and every legal phase transition.
   It consumes a normal pending plan only at the start of an all-red phase.

``OutputSimulator``
   Receives the latest ``SignalDisplay`` from the FSM through a single
   in-process atomic mailbox, prints the simulated output and publishes
   ``SignalStateMessageV1`` for the Perception viewer.

The internal flow is:

``MqTimingPlanReceiverWorker -> PlanReceiver -> PlanSyncChannel -> SignalFSMEngine -> OutputSimulator``.

If that newest message contains an invalid timing plan, the active FSM plan
remains unchanged; the receiver does not fall back to an older message already
removed while draining the queue.

Signal state machine
--------------------

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

Internal plan and display handoff
---------------------------------

For a normal plan, ``PlanSyncChannel`` is a latest-value handoff.  The receiver
may publish multiple accepted plans while the FSM is completing its current
green, yellow and all-red sequence; only the newest pending normal plan is
retained.  At the start of ``ALL_RED``, ``SignalFSMEngine`` consumes that plan
and makes it active.  If no plan is pending, the FSM continues with its current
active plan.

Emergency plans use a separate slot and notification path.  They can wake an
FSM wait only while the matching direction is green.  They cannot introduce a
direct transition out of yellow or all-red.

The FSM-to-output path also uses latest-value semantics.  Each FSM tick submits
a ``SignalDisplay`` containing the phase and remaining times to an atomic
mailbox inside the Controller process.  A slow output worker may skip
intermediate countdown displays, but it reads the newest state and cannot delay
the FSM.  The output worker then publishes that state to the external
``/traffic_signal_state_v1`` queue for visualization.

Controller configuration
-------------------------

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
