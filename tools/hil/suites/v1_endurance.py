"""Locked V1 endurance exercise for the physical HIL runner."""
import sys
ACTIVE_HOURS = 24
IDLE_DAYS = 7
def run(context, devices) -> None:
    context.observations.update({"activeHours": ACTIVE_HOURS, "idleDays": IDLE_DAYS, "releaseDuration": True})
    ports = context.discover_ports(devices)
    for role, port in ports.items():
        context.run(f"endurance probe {role}", [sys.executable, "scripts/testops.py", "-p", port, "await_flipper"], timeout=context.timeout + 30)
