# PoisonedOS structured tool adapter contract

An adapter owns no radio, GPIO, storage, or USB backend. It requests one existing device engine through a bounded structured-app session, receives sequence/credit-controlled events, and releases ownership on completion, cancellation, disconnect, or error.

Each adapter declares:

- a stable family and adapter identifier/version;
- observe and mutation capabilities separately;
- bounded typed parameters and a non-destructive sample;
- structured progress, logs, results, and artifact references;
- raw-versus-derived evidence behavior and redaction rules;
- hardware, region, classroom, and resource requirements;
- explicit cancellation and safe-stop behavior.

An adapter is not catalog-installable until its firmware validator, dashboard view, policy tests, device-only path, and physical HIL evidence all pass.
