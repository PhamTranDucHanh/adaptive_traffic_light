Lifecycle Manager - Component Design
====================================

This page describes the internal structure, processing flow, configuration,
timing and failure behaviour of the Lifecycle Manager component.  System
context, cross-component interactions and system-level design rationale are
documented in :doc:`adaptive_traffic_light_system_design`.

Source-code and configuration file paths in this document are relative to
``docs/05_development/adaptive_traffic_light/``.

Purpose
-------

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
-----------------------

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
-----------------------

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
