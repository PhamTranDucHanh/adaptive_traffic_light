Adaptive Traffic Light - Project Description
============================================

Project overview
----------------

The Adaptive Traffic Light project is a software prototype that adjusts traffic
signal timing according to observed traffic demand.  Instead of always using a
fixed green-light duration, it analyzes configured video inputs, estimates the
traffic on each approach to an intersection, and produces a timing plan for the
traffic signals.

The current project demonstrates the complete software flow from video input to
an applied signal state.  It is intended for architecture, integration and
real-time behaviour evaluation.  It does not currently connect to physical
traffic lights and is not a production or safety-certified traffic-control
system.

What problem does it address?
-----------------------------

A fixed-time traffic signal cannot react when one direction is busy and another
is nearly empty.  This project provides a foundation for adapting the green
time using recent observations while keeping all signal changes under the
control of a defined state machine.

The system is designed to:

* Observe traffic on the North, South, East and West approaches.
* Estimate vehicle count, queue length, lane occupancy and configured
  Emergency-vehicle presence.
* Calculate green times for the North/South and East/West directions.
* Apply normal timing plans only at a safe all-red boundary, without
  interrupting yellow or all-red clearance phases.
* Expose the applied signal state for visualization and diagnostics.
* Supervise the participating software processes during startup, operation and
  shutdown.

How the system works
--------------------

The following diagram shows the main software components and the direction of
their communication.

.. image:: Component\ Diagrams/System\ Component.drawio.svg
   :alt: Adaptive Traffic Light system components and principal data flow
   :align: center
   :width: 100%

The end-to-end flow is:

#. **Traffic Perception and Acquisition** captures frames from four configured
   video sources and runs an object-detection model.  It summarizes the latest
   observation as a ``TrafficSnapshot``.
#. **Traffic Timing Decision** reads the latest snapshot and compares demand in
   the North/South and East/West directions.  It generates a ``TimingPlan``
   containing the proposed green, yellow and all-red durations.
#. **Traffic Signal Controller** validates the plan and applies it through a
   signal state machine.  A normal plan waits for a safe all-red boundary
   instead of changing the active phase immediately.
#. The Controller publishes the applied ``SignalState`` so the Perception
   viewer can display the current lamps and remaining time.  This return path
   is for visualization only and does not influence traffic decisions.
#. **Lifecycle Manager** starts, stops and supervises the three application
   processes.  Health reports and diagnostic logs remain separate from the
   traffic-data flow.

Main components
---------------

.. list-table:: Component responsibilities
   :header-rows: 1
   :widths: 25 45 30

   * - Component
     - Main responsibility
     - Responsibility boundary
   * - Traffic Perception and Acquisition
     - Converts video frames into current traffic measurements for four
       approaches.
     - Does not select signal durations or directly control signal phases.
   * - Traffic Timing Decision
     - Converts the latest traffic measurements into an adaptive timing plan.
     - Does not directly change lamps or bypass Controller validation.
   * - Traffic Signal Controller
     - Validates timing plans and owns all legal signal-state transitions.
     - Does not perform traffic detection or calculate traffic demand.
   * - Lifecycle Manager
     - Coordinates process startup, readiness, supervision, recovery and
       shutdown.
     - Does not carry traffic observations or timing-plan values.

Important information exchanged
--------------------------------

``TrafficSnapshot``
   A recent summary of detected traffic.  It contains a timestamp and, for each
   approach, vehicle count, queue length, occupancy and an emergency indication.

``TimingPlan``
   The proposed signal timing calculated from the latest usable snapshot.  It
   defines green durations for both travel directions together with yellow and
   all-red clearance durations.

``SignalState``
   The signal phase, lamp states and remaining times currently applied by the
   Controller.  It is published for display and diagnostics only.

Typical operating scenarios
---------------------------

Normal traffic
   Perception periodically publishes the newest traffic measurements.  Timing
   Decision calculates a plan, and the Controller stages it until the signal
   state machine reaches a safe all-red boundary.  The next signal cycle then
   uses the accepted timing.

Emergency indication
   When the configured emergency class is detected, Timing Decision marks the
   relevant direction in the plan.  The Controller may adjust the matching
   green phase only when its configured safety conditions are satisfied.
   Yellow and all-red phases remain non-interruptible.

Missing or invalid data
   The system does not apply arbitrary values.  Timing Decision retains the
   previous plan when no new snapshot is available, and the Controller keeps
   its current plan when a received plan is missing or rejected.  Repeated or
   process-level failures are reported through health and lifecycle supervision.

Current implementation scope
----------------------------

The prototype currently:

* Runs as separate managed processes on Linux.
* Uses configured video files or streams as traffic input.
* Uses ONNX-based object-detection backends in the Perception component.
* Exchanges bounded messages through POSIX message queues.
* Uses periodic workers, fixed resource limits and health monitoring to support
  predictable operation.
* Provides a software signal-output simulator and visual overlay.
* Produces logs and timing analytics for integration and real-time evaluation.

The design is best described as monitored soft real-time.  Production hardware,
a physical lamp I/O adapter, verified worst-case execution times and a complete
physical fail-safe policy are outside the current implementation baseline.

Further reading
---------------

* :doc:`adaptive_traffic_light_system_design` contains the system-level Design
  Description and Design Explanation, including interactions, interfaces,
  system-wide behaviour and architectural rationale.
* The dedicated component pages describe
  :doc:`Traffic Perception <traffic_perception_component_design>`,
  :doc:`Traffic Timing Decision <traffic_timing_decision_component_design>`,
  :doc:`Traffic Signal Controller <traffic_signal_controller_component_design>`
  and :doc:`Lifecycle Manager <lifecycle_manager_component_design>` in detail.
* :doc:`diagram_catalog` provides the complete catalog of exported design
  diagrams.
* :doc:`../02_requirement/system_requirements` defines the system requirements
  covered by the design.
