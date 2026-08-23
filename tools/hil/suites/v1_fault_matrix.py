"""Controlled transport/storage/workload fault exercise for V1 HIL."""
FAULTS = ("usb-loss", "ble-loss", "sd-removal", "media-full", "torn-write", "device-reset", "malformed-traffic", "update-interruption")
def run(context, devices) -> None:
    context.observations["faults"] = list(FAULTS)
    for device in devices.values():
        context.power(device, "cycle")
        context.wait_for_port(device)
