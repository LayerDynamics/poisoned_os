"""Recovery and rollback exercise for the physical HIL runner."""
import sys
RECOVERY_STEPS = ("journal-replay", "evidence-integrity", "workload-cleanup", "update-rollback", "device-only-recovery")
def run(context, devices) -> None:
    context.observations["recoverySteps"] = list(RECOVERY_STEPS)
    for role, device in devices.items():
        port = context.wait_for_port(device)
        context.run(f"recovery probe {role}", [sys.executable, "scripts/testops.py", "-p", port, "await_flipper"], timeout=context.timeout + 30)
        if device.recovery:
            context.wait_for_dfu(device)
