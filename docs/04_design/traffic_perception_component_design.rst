Traffic Perception and Acquisition - Component Design
=====================================================

This page describes the internal structure, processing flow, configuration,
timing and failure behaviour of the Traffic Perception and Acquisition
component.  System context, cross-component interactions and system-level
design rationale are documented in
:doc:`adaptive_traffic_light_system_design`.

Source-code and configuration file paths in this document are relative to
``docs/05_development/adaptive_traffic_light/``.

Purpose
-------

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
---------

The component view shows the functional processing blocks and their data flow.

.. figure:: Component\ Diagrams/Traffic\ Perception\ &\ Acquisition\ (Component)-Page-2.drawio.svg
   :alt: Traffic Perception and Acquisition component structure
   :align: center
   :width: 100%

   Video capture feeds a bounded latest-frame path, followed by inference,
   traffic analysis, snapshot publication and visualization.

The component contains these functional blocks:

Capture workers (``StreamWorker``)
   Four periodic workers read the configured video sources.  Each lane keeps
   only its latest available frame, so slow downstream processing cannot build
   an unbounded frame backlog.

Frame handoff (``AtomicFrameBuffer`` and ``FramePool``)
   ``AtomicFrameBuffer`` provides one latest-frame slot for each approach.
   ``FramePool`` owns a fixed number of reusable frame objects so capture and
   inference do not allocate an unbounded number of images.

Inference and traffic analysis (``PipelineManager``)
   The pipeline takes the latest frame from each lane, runs the selected ONNX
   model, filters relevant detections, and calculates the traffic measurements
   needed by Timing Decision.

Snapshot publisher (``SnapshotPublisher``)
   The pipeline publishes the newest ``TrafficSnapshot`` through a bounded
   POSIX message queue.  Publication never blocks the inference worker.

Viewer (``OpenCVLanesViewer``)
   The viewer renders traffic results and the latest applied signal state.
   This block is outside the control loop: display delay or failure cannot
   change a timing decision or signal output.

Internal capture-to-pipeline interaction
----------------------------------------

At each scheduled capture time, a ``StreamWorker`` obtains a reusable frame from
``FramePool``, reads its configured video input and exchanges the result into
the approach's ``AtomicFrameBuffer`` slot.  If that slot still contains an
unconsumed frame, the newer frame replaces it and the older object returns to
the pool.

At its own scheduled interval, ``PipelineManager`` takes the latest
available frame for each approach.  It does not wait for a synchronized history
of all frames captured since the previous cycle.  This is the component's
backpressure rule: slow inference may drop intermediate frames, but it cannot
cause the capture workers to grow an unbounded queue.

Processing sequence
-------------------

The main sequence diagram shows one Perception processing cycle.

.. figure:: Sequence\ Diagrams/Traffic\ Perception\ &\ Acquisition\ (Sequence)-main.drawio.svg
   :alt: Traffic Perception and Acquisition main processing sequence
   :align: center
   :width: 100%

   Periodic capture updates the latest lane frames; the inference pipeline
   analyzes the newest available set and publishes a traffic snapshot.

The four capture workers share a 0.5 s period but are staggered across the
period.  The inference pipeline runs independently every 10 seconds.  It
therefore works from the latest available observation rather than waiting for
a synchronized history of every captured frame.

Configuration and timing
------------------------

Change runtime Perception settings in
``config/traffic_perception_config.json``.  This file owns the video/model
paths, regions of interest (ROIs), image resolution, capture/pipeline/viewer
periods and phases, and
stream/pipeline CPU priorities.  Process-level scheduling, environment and
Lifecycle supervision are configured in
``config/traffic_light_lifecycle.json``.

The remaining values in the table are compiled settings and require a source
change plus rebuild:

* SignalState retry/staleness:
  ``traffic_perception/src/viewer/opencv_lanes_viewer.cpp``.
* Analytics thresholds:
  ``traffic_perception/src/io/timeline_analyzer.cpp``.
* Health Monitor timing:
  ``traffic_perception/src/lifecycle_health_reporter.cpp``.
* Frame-pool size:
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
     - Period 0.5 s; base phase 0 s; lane offsets 0/0.125/0.25/0.375 s
     - A new frame replaces the previous unconsumed frame.
   * - Inference and snapshot
     - Period 10 s; phase 0.05 s
     - The latest usable observation is published when processing completes.
   * - Viewer
     - Content period 10 s; phase 0.26 s; overlay refresh 0.25 s
     - Missed display releases are skipped and cannot delay control.
   * - SignalState display
     - Queue-open retry 1 s; data stale after 3 s
     - Missing or stale telemetry is visible to the viewer only.
   * - Health
     - Deadline 0..30 s; heartbeat 0.1..10 s
     - Local evaluation runs every 0.1 s; supervisor API cycle is 2 s.
   * - Scheduling
     - ``SCHED_RR`` priority 70; stream workers CPU 4; pipeline CPU 2;
       main/viewer and Health Monitor CPU 5
     - If worker creation is denied with ``EPERM``, the component falls back
       to ``SCHED_OTHER`` for development and reports degraded operation.
   * - Memory
     - Fixed pool of 20 pre-created 1920x1080 image frames
     - Pool exhaustion drops work instead of allocating an unbounded backlog.

Failure behaviour
-----------------

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
