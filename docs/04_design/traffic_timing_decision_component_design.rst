Traffic Timing Decision - Component Design
==========================================

This page describes the internal structure, processing flow, configuration,
timing and failure behaviour of the Traffic Timing Decision component.  System
context, cross-component interactions and system-level design rationale are
documented in :doc:`adaptive_traffic_light_system_design`.

Source-code and configuration file paths in this document are relative to
``docs/05_development/adaptive_traffic_light/``.

Purpose
-------

Traffic Timing Decision converts current traffic measurements into proposed
signal durations.  It owns the demand calculation, green-time constraints,
periodic decision release and timing diagnostics.  It does not drive lamps or
select the active signal phase.

Its input is the latest valid ``TrafficSnapshot``.  Its output is a versioned
``TimingPlanMessageV1`` for Signal Controller.

Structure
---------

.. figure:: Component\ Diagrams/Traffic\ Timing\ Decision-Page-2.drawio.svg
   :alt: Traffic Timing Decision component structure
   :align: center
   :width: 100%

   Snapshot reception, validation, demand calculation, plan generation,
   publication, health reporting and timing analytics are separate concerns.

The principal internal objects are:

``PeriodicService``
   Coordinates one decision cycle and tracks consecutive cycles without a
   usable snapshot.

``TrafficDataReceiver``
   Reads the latest snapshot and validates its timestamp, frame identifier and
   measurement ranges before returning it to the decision logic.

``DecisionEngine``
   Calculates the two directional demand scores, selects target green times
   and retains the previous plan used for gradual adjustment.

``ConstraintManager``
   Applies minimum and maximum green durations, fixed clearance times and the
   maximum complete-cycle duration.

``TimingPlanPublisher``
   Encodes the domain plan as ``TimingPlanMessageV1`` and publishes without
   blocking.  If publication is deferred, it retains only the newest pending
   plan for a later cycle.

``HealthReporter`` and ``TimingReportLogger``
   Keep liveness, deadline and timing observations separate from the timing
   plan itself.

Decision flow
-------------

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
---------------------------------------

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
     - 20s
   * - At least 30 but less than 60
     - Moderate
     - 30s
   * - At least 60 but less than 90
     - High
     - 40s
   * - At least 90
     - Very high
     - 50s

For example, a queue percentage of 40, vehicle score of 60 and occupancy
percentage of 50 produce ``0.2 * 40 + 0.6 * 60 + 0.2 * 50 = 54``.  The score
therefore selects the moderate target of 30,000 ms.

The selected value is a target rather than an immediate output.  On each
2.5-second decision cycle, the module moves the previous green duration toward
the target by no more than 5s.  For example, a current value of 20s
and a high-demand target of 40,000 ms produce successive requested values of
25s, 30s, 35s and 40s if the demand remains high.  This limits
abrupt timing changes between consecutive plans.

After both direction targets are updated, the constraint manager enforces the
10..60s green bounds, adds the fixed yellow and all-red clearance
times, and proportionally reduces green durations if the complete cycle would
exceed 100s.  Emergency input bypasses demand-based target selection: the
module preserves the previous green durations and forwards the relevant
emergency flags to Signal Controller.

Decision configuration
----------------------

Timing Decision currently has no module runtime JSON.  Its functional values
are compiled and require a source change plus rebuild:

* Decision period and consecutive-miss limit:
  ``traffic_timing_decision/include/periodic_service.h``.
* Green-time bounds, steps and demand thresholds:
  ``traffic_timing_decision/include/decision_constants.h``.
* Demand-score calculation and weights:
  ``traffic_timing_decision/src/decision_engine.cpp``.
* Snapshot freshness and content validation:
  ``traffic_timing_decision/src/traffic_data_receiver.cpp``.
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
-----------------------

When no usable snapshot is available, Timing Decision keeps the previously
published plan, reports ``NO_NEW_DATA`` and retries any pending publication.
After 24 consecutive missed 2.5-second cycles, equivalent to 60 seconds, it
reports an input timeout and fails the service cycle.

The TimingPlan publisher is non-blocking.  If its depth-eight queue is full,
the newest pending plan is retained locally and retried during a later decision
cycle.
