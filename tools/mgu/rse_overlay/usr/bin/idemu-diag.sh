#!/bin/sh
i=0
while true; do
  sleep 40; i=$((i+1))
  echo "=== DIAG $i up=$(cut -d' ' -f1 /proc/uptime)"
  systemctl list-units --failed --no-legend 2>&1 | head -15
  echo "--- hmi:"; systemctl status hmi_rse_left winston vsomeipd NodeStateManager --no-pager -n 3 2>&1 | grep -E "●|Active|Loaded|Main PID" | head -16
  echo "--- procs:"; ps -o pid,stat,rss,comm 2>/dev/null | grep -E "Hmi|winston|vsomeip|Node|persod|codingshm" | head
done
