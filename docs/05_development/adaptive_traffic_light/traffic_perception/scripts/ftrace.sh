mkdir -p traffic_perception/output/ftrace

 

TRACE_OUTPUT="$PWD/traffic_perception/output/ftrace/perception_$(date +%Y%m%d_%H%M%S).dat"

 

sudo trace-cmd record \
  -p nop \
  -C mono \
  -M 5 \
  -b 32768 \
  --compression zstd \
  -e sched:sched_waking \
  -e sched:sched_wakeup \
  -e sched:sched_switch \
  -e sched:sched_migrate_task \
  -e timer:hrtimer_start \
  -e timer:hrtimer_cancel \
  -e timer:hrtimer_expire_entry \
  -e timer:hrtimer_expire_exit \
  -e irq:irq_handler_entry \
  -e irq:irq_handler_exit \
  -e irq:softirq_entry \
  -e irq:softirq_exit \
  -o "$TRACE_OUTPUT"

 

TRACE_STATUS=$?

 

if [[ "$TRACE_STATUS" -eq 0 || "$TRACE_STATUS" -eq 130 ]]; then
  echo "Trace stopped and saved: $TRACE_OUTPUT"
else
  echo "Trace failed with status $TRACE_STATUS"
fi
