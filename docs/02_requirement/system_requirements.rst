Adaptive Traffic Light - System Requirements
============================================

Purpose
-------

These requirements define the minimum behaviour that the system design must
address.  ``status: valid`` means that a requirement is sufficiently defined
to enter review; approval of the document is managed by the Merge Request.

Traffic observation
-------------------

.. sys_req:: Publish a current traffic snapshot
   :id: sys_req__publish_traffic_snapshot
   :status: valid
   :req_covered: no

   The system shall convert the configured traffic input into a timestamped
   traffic snapshot containing, for every supported approach, vehicle count,
   queue length, occupancy and emergency-vehicle indication, and shall make
   the latest snapshot available to the timing-decision function.

Adaptive timing decision
------------------------

.. sys_req:: Produce an adaptive timing plan
   :id: sys_req__produce_adaptive_timing_plan
   :status: valid
   :req_covered: no

   The system shall evaluate the latest available traffic snapshot once per
   2.5-second decision cycle and publish a timing plan for the signal-control
   function.  When no new snapshot is available, the decision function shall
   preserve the most recently published plan, retry any pending publication
   and report that no new data was consumed.  It shall report an input timeout
   after the configured consecutive-miss limit.

Safe signal control
-------------------

.. sys_req:: Apply timing plans through safe signal transitions
   :id: sys_req__apply_safe_signal_transitions
   :status: valid
   :req_covered: no

   The system shall validate each received timing plan before applying it and
   shall change signal outputs only through the defined signal state machine,
   preserving the current safe plan when a new plan is missing or rejected.

Observable signal state
-----------------------

.. sys_req:: Publish the applied signal state for visualization
   :id: sys_req__publish_signal_state
   :status: valid
   :req_covered: no

   The system shall publish the signal phase, lamp states and remaining times
   applied by the signal-control function for operator visualization.  This
   telemetry shall not feed back into timing or signal-control decisions.

Lifecycle and health supervision
--------------------------------

.. sys_req:: Supervise managed processes
   :id: sys_req__supervise_managed_processes
   :status: valid
   :req_covered: no

   The system shall start, stop and supervise the perception, timing-decision
   and signal-control processes through the configured lifecycle manager.  A
   managed process shall report heartbeat and execution-deadline health, and a
   missed health contract shall be observable in system diagnostics.
