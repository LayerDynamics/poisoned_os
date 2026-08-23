# JavaScript capability map

The managed JavaScript runner maps every current native plugin to one named
capability. `js_capability_module_allowed()` is default-deny: unknown module
names and modules whose bit is absent from the granted mask are rejected.

| Module family | Capability |
| --- | --- |
| `flipper`, `device` | `device` |
| `event_loop` | `runtime` |
| GUI and GUI views | `ui` |
| `notification` | `notification` |
| `badusb` | `badusb` |
| `serial` | `serial` |
| `gpio` | `gpio` |
| `storage` | `storage` |
| `crypto` | `crypto` |
| `math` | `compute` |
| `evidence` | `evidence` |

The map is an admission primitive used by the managed workload layer. It does
not grant a project capabilities by itself; the project manifest, role policy,
confirmation state, and resource limits still determine the mask supplied to
the check.

Managed workloads resolve `storage` to the firmware-resident PoisonedOS
storage module. Its virtual root is the workload's immutable
`/versions/<project-sha256>` directory; traversal, malformed segments, sibling
revisions, and device-absolute paths cannot reach outside that root. The
managed `flipper`/`device` module is read-only. The managed `evidence` module
accepts only a project-scoped file and validates its evidence ID, case ID,
bounded size, content SHA-256, and prior audit SHA-256 before asking the M2
evidence service to capture it. Unmanaged scripts retain the legacy plugin
loader and do not receive these managed module substitutions.

Raw `ffi_address` and the mJS firmware symbol resolver are not installed in a
managed runtime. Developer-policy state is validated separately; no current
managed execution path turns that state into unrestricted native FFI.
The existing device-launched JS runner remains an unmanaged compatibility path
for installed/local scripts; it does not accept authenticated workload RPC
requests and is not used by the browser workspace.

Managed workload records belong to the startup workload service rather than an
RPC connection. The service keeps at most two records, keys each by the paired
client identity digest plus workload ID, serializes worker/RPC access, and
retains a disconnect terminal receipt after the connection detaches. A
reconnected client with the same authenticated identity can inspect that
receipt; another identity or workload ID cannot receive its status or console.
