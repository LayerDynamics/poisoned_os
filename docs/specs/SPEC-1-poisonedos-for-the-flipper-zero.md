# SPEC-1: PoisonedOS for the Flipper Zero

> A professional, field-first Flipper Zero operating system combining auditable security and education tools, a unified file and evidence workspace, deep customization, browser-based control, and managed JavaScript and Rust workloads.

- **Date:** 2026-08-21
- **Author:** PoisonedOS project owner + Codex
- **Status:** Draft
**Version:** 1.0

---

## 1. Background

### 1.1 Product Thesis

PoisonedOS turns the Flipper Zero from a primarily device-operated handheld into a connected field-work and education platform. A cybersecurity professional, student, or educator must be able to pair a phone or computer, control the device, run tools and applications, develop and execute JavaScript and Rust workloads, organize results into case workspaces, and export verifiable evidence without fighting the 128×64 display or a collection of disconnected file formats.

The browser dashboard is a first-class control plane rather than a screen-mirroring accessory. Native on-device operation remains fully supported so the Flipper does not become dependent on a phone, account, cloud service, or network connection.

### 1.2 Problem Statement

Current Flipper workflows are fragmented across the physical device, command-line tools, firmware-specific applications, SD-card files, and separate desktop/mobile utilities. That creates five launch-defining problems:

1. Important workflows remain device-only and constrained by the small display and input controls.
2. Files, tool output, notes, and evidence are fragmented rather than organized around a case, lab, or lesson.
3. There is no consistent, easy way to operate the full device from both phones and computers.
4. Customization and field configuration require too many disconnected device-side steps.
5. JavaScript and Rust lack one coherent browser-to-device development, execution, debugging, and output workflow.

PoisonedOS must solve all five together. A dashboard without a real data model would merely mirror the fragmentation, while an improved firmware without a browser control plane would preserve the device-only bottleneck.

### 1.3 Current State

The workspace is based on the Flipper firmware architecture and already contains several foundations that PoisonedOS can extend:

- The root product statement identifies ease of use, customization, security/education tooling, field professionals, a real filesystem, and a phone-accessible web dashboard as the product direction (`README.md:1-5`).
- The current JavaScript system app is an mJS-based runner with GUI, event-loop, notification, BadUSB, serial, GPIO, math, and storage plugins (`applications/system/js_app/application.fam:1-225`). It executes a script file through `mjs_exec_file()` (`applications/system/js_app/js_thread.c:236-338`) and exposes a CLI command that streams output and errors (`applications/system/js_app/js_app.c:135-216`).
- The RPC application service can start apps, load files, send button actions, retrieve errors, and exchange application data (`applications/services/rpc/rpc_app.c:441-478`).
- The RPC GUI service can stream the device framebuffer, send input, and operate a virtual display (`applications/services/rpc/rpc_gui.c:423-439`).
- The RPC storage service already exposes information, timestamps, stat, list, read, write, delete, mkdir, checksum, rename, backup, restore, and tar extraction handlers (`applications/services/rpc/rpc_storage.c:741-778`).
- The storage API exposes `/int` and `/ext` namespaces (`applications/services/storage/storage.h:15-22`).
- Native FAP loading already validates a manifest, target, and firmware API before loading ELF sections (`lib/flipper_application/flipper_application.c:99-178`).
- The inspected firmware tree contains no PoisonedOS Rust SDK or Rust workload runner. Rust is therefore a new managed build and execution path, not a rename of an existing implementation.

The comparison guide establishes the product tradeoff that informs this design: official firmware offers the clearest support baseline, while Momentum demonstrates how deeply customization, protocol expansion, global settings, browser-like file management, and a large app surface can alter the product (`documentation/development/Momentum_Vs_Flipper.md:3-14`, `documentation/development/Momentum_Vs_Flipper.md:91-112`). PoisonedOS will use the official-style architecture as the maintainable base and selectively implement or port audited capabilities instead of inheriting an unaudited application bundle wholesale.

### 1.4 Target Users

All three audiences are first-class V1 users:

- **Field cybersecurity professionals:** need fast, reliable, offline workflows, structured output, repeatable configuration, and defensible evidence handling.
- **Students:** need safe tools, guided labs, explanations, reproducible datasets, and a development path that does not require mastering the entire firmware toolchain first.
- **Educators:** need lesson packs, classroom device management, resettable environments, progress/export workflows, and the ability to demonstrate the device from a browser or projector.

Secondary users are firmware developers, security-tool authors, curriculum authors, and administrators maintaining device fleets or build infrastructure.

### 1.5 Motivation

The opportunity is to combine capabilities that currently exist separately—portable radio/credential hardware, a script engine, RPC, storage, external applications, and browser tooling—into one professional operating model. The resulting product is valuable even if the eventual distribution model remains undecided because its local-first architecture supports open-source, self-hosted, and managed-service delivery without changing the firmware trust model.

### 1.6 Assumptions

- Flipper Zero remains the primary hardware target for V1.
- The firmware continues to use Furi/FreeRTOS, the existing C/C++ HAL, `.fam` manifests, protobuf RPC, and FAP compatibility conventions.
- Phones and computers may have different browser transport capabilities; a local bridge is an accepted compatibility layer.
- The dashboard must remain useful offline after installation.
- Cloud services are optional for device control and evidence access.
- Rust source is compiled off-device. PoisonedOS accepts Rust source, produces a policy-checked artifact, deploys it, and runs it on the Flipper; it does not embed a full Rust compiler in the MCU firmware.
- Security tools are intended for authorized professional, laboratory, and educational use.

---

## 2. Requirements

### 2.1 Functional Requirements

#### Connection and Browser Control

| ID | Priority | Requirement |
|---|---|---|
| FR-1 | MUST | The system MUST let a user pair a supported phone or computer with a physical PoisonedOS device using an authenticated local connection. |
| FR-2 | MUST | The system MUST support USB and BLE device transports and MUST expose one transport-independent session API to the dashboard. |
| FR-3 | MUST | The dashboard MUST remain installable and usable as an offline-capable web application after its initial load. |
| FR-4 | MUST | The system MUST provide a local bridge for platforms whose browsers cannot access the required USB or BLE APIs directly. |
| FR-5 | MUST | The dashboard MUST show connection state, firmware/API version, battery state, storage state, active application, and transport health. |
| FR-6 | MUST | The dashboard MUST stream the physical device display and MUST send all supported button press, release, short, long, and repeat events. |
| FR-7 | MUST | The dashboard MUST start, stop, and inspect built-in and external applications. |
| FR-8 | MUST | Integrated applications MUST expose structured remote views and events; legacy applications MUST remain operable through framebuffer streaming and remote input. |
| FR-9 | MUST | The dashboard MUST stream application logs, progress, warnings, structured results, and generated artifacts without requiring a separate terminal. |
| FR-10 | MUST | Losing the dashboard connection MUST leave the device in a defined safe state and MUST NOT corrupt an active case or file transaction. |

#### Unified Filesystem and Evidence

| ID | Priority | Requirement |
|---|---|---|
| FR-11 | MUST | PoisonedOS MUST expose a unified virtual filesystem over the existing internal and SD-card storage backends without replacing those proven low-level filesystems. |
| FR-12 | MUST | The browser file manager MUST support list, search, sort, stat, preview, read, upload, download, create directory, rename, copy, move, delete, and checksum operations. |
| FR-13 | MUST | Mutating file operations MUST be transactional or recoverable after power loss, card removal, transport loss, or dashboard termination. |
| FR-14 | MUST | The system MUST expose reserved namespaces for cases, evidence, apps, scripts, workloads, profiles, exports, and system data. |
| FR-15 | MUST | A user MUST be able to create a case or lab workspace before, during, or after starting a tool run. |
| FR-16 | MUST | Every captured result MUST be attachable to a case and MUST preserve its raw artifact separately from derived previews or annotations. |
| FR-17 | MUST | Evidence metadata MUST include a stable identifier, creation time, device identity, firmware version, operator, tool and version, parameters, source path, media type, byte length, and SHA-256 digest. |
| FR-18 | MUST | Evidence notes and tags MUST be additive and MUST NOT silently alter the raw artifact or its original digest. |
| FR-19 | MUST | The system MUST maintain an append-only audit chain for evidence creation, import, annotation, export, and deletion events. |
| FR-20 | MUST | A user MUST be able to export a portable evidence package containing raw artifacts, metadata, audit history, checksums, and a human-readable report. |
| FR-21 | MUST | The importer MUST verify an evidence package before accepting it and MUST report every missing file, digest mismatch, schema error, or unsupported version. |
| FR-22 | MUST | Classroom workspaces MUST be resettable from a known lesson snapshot without affecting unrelated cases or personal device data. |

#### Applications, Tools, and Customization

| ID | Priority | Requirement |
|---|---|---|
| FR-23 | MUST | The dashboard MUST list installed, available, running, incompatible, disabled, and quarantined applications with their provenance and permissions. |
| FR-24 | MUST | The system MUST verify package integrity, target compatibility, firmware API compatibility, signatures, and declared capabilities before installing or running an application. |
| FR-25 | MUST | The user MUST be able to install, update, disable, remove, and roll back applications from the browser. |
| FR-26 | MUST | PoisonedOS MUST ship a curated set of field security and education tools covering NFC, LF RFID, iButton, infrared, Sub-GHz, GPIO, USB/HID, BLE, serial, and storage workflows supported by the hardware. |
| FR-27 | MUST | Every curated tool MUST provide a purpose statement, authorized-use guidance, required hardware, parameter descriptions, structured output schema, and at least one safe sample or lab dataset. |
| FR-28 | MUST | The system MUST let users create field profiles that atomically apply menus, shortcuts, UI theme, tool defaults, transport policy, logging policy, and evidence policy. |
| FR-29 | MUST | The dashboard MUST edit themes, icon/font packs, menus, shortcuts, status presentation, lock behavior, and application visibility with preview and rollback. |
| FR-30 | MUST | Invalid or incompatible customization assets MUST be rejected before activation and MUST NOT prevent recovery into a known-good UI profile. |
| FR-31 | MUST | Educators MUST be able to create, import, assign, and export lesson packs containing instructions, datasets, permitted tools, starter code, and reset state. |
| FR-32 | MUST | Education mode MUST be able to restrict tools, frequencies, buses, native code, and destructive actions per lesson or classroom policy. |

#### JavaScript Development, Execution, and Serving

| ID | Priority | Requirement |
|---|---|---|
| FR-33 | MUST | The dashboard MUST create, edit, validate, save, version, upload, run, stop, and rerun JavaScript projects. |
| FR-34 | MUST | JavaScript MUST execute on the Flipper through the embedded runtime and MUST expose only capability-authorized device APIs. |
| FR-35 | MUST | JavaScript stdout, stderr, logs, exceptions, stack traces, lifecycle state, structured events, and generated files MUST stream to the dashboard. |
| FR-36 | MUST | JavaScript projects MUST declare runtime API version, entry point, requested capabilities, resource limits, dependencies, and dashboard-extension assets in a manifest. |
| FR-37 | MUST | The device MUST serve an installed JavaScript project's dashboard-extension bundle to the connected dashboard through the authenticated session transport. |
| FR-38 | MUST | Dashboard JavaScript extensions MUST execute in an isolated worker or sandboxed frame without direct access to dashboard credentials, transport primitives, unrelated case data, or the top-level DOM. |
| FR-39 | MUST | Raw firmware-symbol FFI MUST be disabled for untrusted JavaScript and MUST require an explicitly enabled developer policy for locally trusted signed code. |
| FR-40 | MUST | JavaScript projects MUST be runnable entirely offline once their source and dependencies are installed. |

#### Rust Development and Execution

| ID | Priority | Requirement |
|---|---|---|
| FR-41 | MUST | The dashboard MUST accept a Rust source file or Cargo project and MUST display source diagnostics and build progress. |
| FR-42 | MUST | Rust compilation MUST run off-device in a pinned, reproducible, network-restricted build sandbox. |
| FR-43 | MUST | The Rust toolchain MUST support a constrained PoisonedOS SDK and MUST reject unsupported target features, dependencies, ABI versions, or capabilities before deployment. |
| FR-44 | MUST | Trusted Rust projects MUST be buildable as signed native FAP artifacts compatible with the existing ELF/FAP loader. |
| FR-45 | MUST | Restricted Rust projects MUST be buildable as WebAssembly components and runnable through a capability-scoped on-device runtime when their resource budget fits the device. |
| FR-46 | MUST | The dashboard MUST accept precompiled Rust-derived artifacts only when their signature, manifest, target, API version, provenance, and digest validate. |
| FR-47 | MUST | Rust workloads MUST support deploy, start, stop, progress, stdout, stderr, structured events, crash diagnostics, and generated-artifact collection from the dashboard. |
| FR-48 | MUST | A native Rust workload MUST NOT run unless the user explicitly approves its declared capabilities and trust level. |
| FR-49 | MUST | Build artifacts MUST record source digest, toolchain image digest, dependency lockfile digest, compiler version, target, SDK/API version, capabilities, signer, and build timestamp. |
| FR-50 | MUST | The system MUST preserve the Rust source project independently of generated device artifacts so builds remain reproducible and auditable. |

#### Security, Updates, and Recovery

| ID | Priority | Requirement |
|---|---|---|
| FR-51 | MUST | Pairing MUST require proof of possession and user confirmation on the physical device. |
| FR-52 | MUST | The device MUST enforce Owner, Operator, Instructor, Student, and Observer roles through session and capability policy. |
| FR-53 | MUST | Destructive storage, firmware, radio-policy, identity, and native-code operations MUST require an explicit confirmation describing the exact target and consequence. |
| FR-54 | MUST | Firmware, application, lesson, tool-data, and UI-pack updates MUST be signed, versioned, verified before activation, and recoverable to the last known-good version. |
| FR-55 | MUST | The system MUST provide a recovery mode that works without the dashboard and can restore a valid firmware, settings profile, and storage index. |
| FR-56 | MUST | The dashboard MUST expose an audit timeline for pairing, authentication, policy changes, workload execution, evidence operations, app installation, and updates. |
| FR-57 | MUST | A user MUST be able to revoke a paired client from the device itself and invalidate that client's active and resumable sessions. |
| FR-58 | MUST | The system MUST support a local-only mode that disables hosted services without disabling device control, files, workloads already installed, or evidence export. |

#### Post-V1 Capabilities

| ID | Priority | Requirement |
|---|---|---|
| FR-59 | SHOULD | The dashboard SHOULD manage up to 30 classroom devices from one instructor workspace through multiple local bridges. |
| FR-60 | SHOULD | The system SHOULD support optional encrypted synchronization of selected cases, profiles, projects, and lesson packs across user devices. |
| FR-61 | SHOULD | The system SHOULD provide a reviewed catalog for signed applications, workloads, themes, tool-data packs, and lessons. |
| FR-62 | SHOULD | The dashboard SHOULD support collaborative case notes with explicit author attribution and conflict resolution. |
| FR-63 | COULD | The platform COULD support an external Wi-Fi devboard as a local network transport without changing the device API. |
| FR-64 | COULD | The platform COULD support organization-managed policy and fleet reporting. |

### 2.2 Non-Functional Requirements

#### Performance

| Metric | Target | Measurement |
|---|---:|---|
| Dashboard cold start from local cache | p95 ≤ 2 seconds | Browser performance test on supported baseline phones/computers |
| Paired-session establishment | p95 ≤ 3 seconds | Physical USB and BLE connection suites |
| Command acknowledgement over USB | p95 ≤ 150 ms | Browser-to-device hardware E2E test |
| Command acknowledgement over BLE | p95 ≤ 350 ms | Browser-to-device hardware E2E test |
| Remote input-to-frame feedback over USB | p95 ≤ 250 ms | Timestamped physical-device E2E test |
| Remote input-to-frame feedback over BLE | p95 ≤ 600 ms | Timestamped physical-device E2E test |
| Directory listing | p95 ≤ 1 second for 1,000 entries | Real SD-card corpus test |
| Evidence search | p95 ≤ 500 ms for 10,000 indexed artifacts | Dashboard/bridge benchmark |
| JavaScript launch | p95 ≤ 1 second for a 100 KiB installed project | Physical-device workload suite |
| Native Rust FAP launch | p95 ≤ 2 seconds after installation | Physical-device workload suite |
| Rust build feedback | First diagnostic or progress event ≤ 2 seconds | Build-service integration test |
| Streaming output loss | 0 silently lost events | Sequence-number reconciliation under saturation |

#### Reliability

| Metric | Target |
|---|---:|
| Successful control commands under nominal local conditions | ≥ 99.9% across 10,000-command runs |
| Crash-free dashboard sessions | ≥ 99.5% |
| Evidence digest correctness | 100% in test corpus and recovery tests |
| Power-loss filesystem recovery | No committed artifact corruption across 1,000 fault-injection cycles |
| Update recovery | 100% rollback to last known-good version in induced interruption suite |
| Audit continuity | No accepted event sequence with an unexplained gap or invalid chain link |
| Offline core operation | 100% of MUST local workflows work with WAN disabled |

#### Security and Compliance

- Application-layer sessions MUST use ephemeral P-256 ECDH, HKDF-SHA-256 key derivation, AES-256-GCM authenticated encryption, monotonic message counters, and replay rejection. The current tree enables the required ECDH, ECDSA, GCM, SHA-256, and P-256 primitives in `lib/mbedtls_cfg.h:37-89`.
- Long-term device and client keys MUST be stored using the strongest hardware-backed or OS-protected facility available on each platform.
- Signed packages and updates MUST use an offline-root/online-intermediate signing hierarchy with revocation metadata.
- Secrets, raw credential captures, case data, and private keys MUST never appear in ordinary logs or analytics.
- Evidence at rest in the dashboard or bridge MUST be encrypted when the host platform provides secure key storage.
- Hosted services, if enabled, MUST meet OWASP ASVS Level 2 controls before public availability.
- The platform MUST document radio-region policy and MUST preserve regulatory enforcement by default.
- The platform MUST make no claim of legal forensic admissibility without an independent validation program.
- Privacy and retention controls MUST support export and deletion of user-owned hosted data if hosted accounts are introduced.

#### Scalability and Capacity

- A single dashboard session MUST remain responsive with 10,000 evidence artifacts, 1,000 filesystem entries in one directory, 250 installed packages, and 100 saved profiles.
- One bridge process MUST support four concurrently connected devices without output loss.
- Instructor mode SHOULD support 30 devices through multiple bridges while preserving per-device isolation.
- The optional build service MUST horizontally scale by queue depth and MUST isolate every build in a fresh sandbox.
- Device RPC MUST apply bounded queues and credit-based streaming so dashboard or build-service load cannot exhaust firmware memory.

#### Accessibility and Usability

- All primary dashboard workflows MUST be operable by keyboard, touch, and screen reader.
- The dashboard MUST meet WCAG 2.2 AA for its own interface.
- Color MUST NOT be the sole carrier of device, evidence, risk, or error state.
- Dangerous operations MUST use plain-language consequences and exact targets.
- A first-time user MUST complete pairing, create a case, run a safe sample, and export its result in ≤ 10 minutes without external documentation during usability testing.

### 2.3 Constraints

- The target firmware is constrained by the STM32WB55 memory map: 1 MiB flash and approximately 192 KiB primary RAM are declared in `targets/f7/stm32wb55xx_flash.ld:4-12`.
- BLE bandwidth and browser transport support differ materially from USB.
- Existing FAPs depend on firmware API compatibility and cannot be assumed safe merely because they load.
- Native code on this MCU has no general-purpose process isolation.
- SD cards can be removed, corrupted, or replaced while the device is running.
- Core operation must not depend on continuous internet access.
- The upstream firmware snapshot and PoisonedOS root code are GPLv3-only, and the exact GPLv3 text has been restored at the workspace root. The component audit currently records two distribution blockers: the pinned `flipperzero-protobuf` snapshot has no published license grant, and GPLv2-only mJS is linked into the GPLv3-only firmware without a documented compatible commercial grant. PoisonedOS MUST NOT be distributed until both are resolved.
- The final commercial model is undecided; architecture must support open-source, self-hosted, and managed variants without weakening local ownership.

### 2.4 Explicit Non-Goals

- PoisonedOS V1 WILL NOT compile Rust source on the Flipper MCU itself.
- PoisonedOS WILL NOT require a cloud account for local device control, files, evidence, customization, or installed workload execution.
- PoisonedOS WILL NOT silently bypass radio-region restrictions.
- PoisonedOS WILL NOT promise that arbitrary community apps are secure, compatible, or supported.
- PoisonedOS WILL NOT execute unsigned native code under the default security policy.
- PoisonedOS WILL NOT modify raw evidence bytes when adding notes, tags, previews, or reports.
- PoisonedOS WILL NOT claim forensic certification or evidentiary admissibility without external validation.
- PoisonedOS WILL NOT market tools for unauthorized access, surveillance, interference, or credential misuse.
- PoisonedOS WILL NOT make the browser dashboard the only way to recover or operate the device.

---

## 3. Architecture

### 3.1 System Overview

```text
┌──────────────────────────── Phones and Computers ────────────────────────────┐
│                                                                              │
│  Poisoned Dashboard PWA                                                     │
│  ├─ Device console and structured app UI                                    │
│  ├─ Files, cases, evidence, reports                                          │
│  ├─ JavaScript/Rust IDE and build client                                    │
│  ├─ Profiles, themes, lessons, package manager                              │
│  └─ Encrypted local database                                                 │
│          │ direct WebUSB/WebSerial/Web Bluetooth where available            │
│          │                                                                  │
│          └──────── Poisoned Bridge ──────── optional hosted services         │
│                    ├─ USB/BLE adapters       ├─ sandboxed Rust builds         │
│                    ├─ local encrypted store  ├─ signed package catalog       │
│                    └─ local build/cache      └─ encrypted opt-in sync         │
└─────────────────────────────────┬────────────────────────────────────────────┘
                                  │ authenticated protobuf RPC v2
┌─────────────────────────────────▼────────────────────────────────────────────┐
│ PoisonedOS Firmware                                                         │
│  ├─ Session gateway and policy engine                                       │
│  ├─ Unified virtual filesystem and evidence journal                         │
│  ├─ Structured tool/app adapter + legacy screen/input bridge                │
│  ├─ JavaScript runtime + dashboard-extension server                         │
│  ├─ Native FAP loader + restricted WebAssembly runtime                      │
│  ├─ Package/update verifier and recovery manager                            │
│  └─ Curated field tools, data, lessons, and device UI                       │
│                 │                         │                                  │
│        Internal storage              SD-card storage                         │
└──────────────────────────────────────────────────────────────────────────────┘
```

The device API is transport-independent. Direct browser transport, the local bridge, and an optional Wi-Fi devboard all carry the same framed protocol and security session. The bridge is a compatibility and acceleration component, not a trusted cloud dependency.

### 3.2 Component Design

#### Firmware Core

- **Responsibility:** Preserve hardware control, scheduling, drivers, protocol libraries, and standalone device operation.
- **Technology:** Existing C/C++, Furi/FreeRTOS, STM32WB HAL, FBT/SCons.
- **Interfaces:** Furi services, HAL APIs, exported FAP API.
- **Dependencies:** Upstream firmware components and pinned third-party libraries.

#### Session Gateway

- **Responsibility:** Authenticate clients and multiplex versioned command, event, file, screen, and workload streams.
- **Technology:** Nanopb/protobuf over USB, BLE, UART, or bridge transport.
- **Interfaces:** Poisoned RPC v2 envelope and capability negotiation.
- **Dependencies:** Existing RPC service, mbedTLS, transport drivers, policy engine.

#### Policy Engine

- **Responsibility:** Decide whether a principal, package, workload, or lesson may perform a requested capability.
- **Technology:** Firmware service with signed policy records and role/capability evaluation.
- **Interfaces:** `authorize(principal, capability, resource, context)` and auditable decisions.
- **Dependencies:** Pairing store, device lock state, package manifests, classroom policy.

#### Unified Virtual Filesystem

- **Responsibility:** Present one stable namespace and transactional operations over `/int` and `/ext`.
- **Technology:** Firmware VFS service over existing storage APIs, journaled metadata index, checksums.
- **Interfaces:** Versioned file RPC, case/evidence operations, storage events.
- **Dependencies:** Internal storage, SD-card storage, recovery manager.

#### Evidence Service

- **Responsibility:** Create immutable raw artifacts, metadata, hashes, annotations, audit chains, and exports.
- **Technology:** Firmware capture adapter plus dashboard/bridge indexing and report generation.
- **Interfaces:** Case, run, artifact, annotation, verify, import, and export APIs.
- **Dependencies:** VFS, clock, operator identity, tool adapters, cryptographic digests.

#### Application and Tool Adapter

- **Responsibility:** Give integrated tools structured remote commands, views, progress, output, and artifact emission.
- **Technology:** Versioned firmware interface and protobuf schemas; legacy framebuffer/input fallback.
- **Interfaces:** App lifecycle, view schema, command schema, result/event streams.
- **Dependencies:** Loader, GUI RPC, policy engine, evidence service.

#### JavaScript Workload Manager

- **Responsibility:** Validate manifests, provision a capability-scoped mJS context, run/stop scripts, stream output, and serve dashboard extensions.
- **Technology:** Existing mJS engine with a new restricted API resolver and workload protocol.
- **Interfaces:** Project install, validate, run, stop, console, events, artifacts, extension bundle.
- **Dependencies:** VFS, policy engine, JS modules, evidence service.

#### Rust Build and Runtime Manager

- **Responsibility:** Accept Rust projects, orchestrate reproducible off-device builds, verify artifacts, and execute native or WebAssembly outputs under policy.
- **Technology:** Pinned Rust toolchain containers, constrained SDK, FAP/ELF output, compact WebAssembly runtime.
- **Interfaces:** Build submission/events, artifact manifest, deploy, run, stop, output, crash report.
- **Dependencies:** Bridge or hosted builder, package verifier, FAP loader, policy engine.

#### Package and Update Manager

- **Responsibility:** Verify, install, activate, roll back, quarantine, and inventory firmware and content packages.
- **Technology:** Signed manifests, content-addressed packages, A/B metadata and last-known-good records.
- **Interfaces:** Check, download/import, verify, stage, activate, roll back, revoke.
- **Dependencies:** VFS, cryptography, recovery manager, optional catalog.

#### Poisoned Dashboard

- **Responsibility:** Provide the complete user-facing phone/computer experience.
- **Technology:** TypeScript progressive web application with service worker, IndexedDB, Web Workers, sandboxed frames, and accessible component system.
- **Interfaces:** Session API, local bridge WebSocket, optional HTTPS services.
- **Dependencies:** Browser platform, bridge when required, schema-generated client.

#### Poisoned Bridge

- **Responsibility:** Adapt local USB/BLE devices to the browser, cache data, and run local builds without becoming the source of truth.
- **Technology:** Rust service with a loopback-only authenticated API and optional desktop/mobile shell.
- **Interfaces:** Local WebSocket/HTTP API, USB/BLE adapters, build worker.
- **Dependencies:** OS device APIs, secure credential storage, pinned toolchains.

#### Optional Hosted Platform

- **Responsibility:** Provide opt-in builds, catalog distribution, encrypted synchronization, and organization features.
- **Technology:** Stateless API/control services, isolated build workers, encrypted object storage, relational metadata store.
- **Interfaces:** HTTPS build, catalog, sync, and account APIs.
- **Dependencies:** Identity provider, signing infrastructure, queue, storage, observability.

### 3.3 Filesystem Design

The VFS maps stable PoisonedOS paths onto existing physical storage:

| Namespace | Purpose | Default backing |
|---|---|---|
| `/system` | Read-only firmware metadata and recovery descriptors | Internal/compiled |
| `/config` | Versioned settings and profiles | Internal with SD backup |
| `/apps` | Installed app packages and metadata | SD card |
| `/scripts` | JavaScript projects | SD card |
| `/workloads` | Rust source, manifests, and build references | SD card |
| `/cases` | Case metadata and evidence indexes | SD card |
| `/evidence` | Content-addressed immutable raw artifacts | SD card |
| `/lessons` | Education packs and reset snapshots | SD card |
| `/exports` | Portable packages and reports | SD card |
| `/int` | Explicit compatibility view of internal storage | Existing internal backend |
| `/ext` | Explicit compatibility view of SD storage | Existing external backend |

Mutations use write-to-new-object, flush, digest verification, and atomic metadata replacement. A compact journal records the intended operation and its commit state. Boot and mount recovery either completes a committed operation or removes an uncommitted temporary object. The index is rebuildable from authoritative manifests and content-addressed artifacts.

### 3.4 Data Model

| Entity | Key Fields | Relationships |
|---|---|---|
| Device | device ID, hardware revision, firmware/API version, public key, policy | Has pairings, profiles, runs, audit events |
| Principal | principal ID, display name, role, public key | Owns sessions, cases, annotations, builds |
| Pairing | device, principal, granted roles, key ID, created/revoked time | Authorizes sessions |
| Session | session ID, transport, counters, capabilities, expiry | Carries commands and events |
| Case | case ID, name, purpose, owner, participants, retention policy | Contains runs and evidence |
| ToolRun | run ID, case, tool/version, parameters, state, timestamps | Produces events and artifacts |
| EvidenceArtifact | artifact ID, content digest, media type, size, provenance | Belongs to case and optional run |
| Annotation | annotation ID, artifact, author, timestamp, text/tags | Adds context without changing raw bytes |
| AuditEvent | event ID, predecessor digest, actor, action, resource, timestamp | Forms an append-only chain |
| AppPackage | package ID, version, digest, signer, capabilities, compatibility | Installs onto devices |
| WorkloadProject | project ID, language, source digest, manifest, owner | Produces build artifacts |
| BuildArtifact | artifact ID, source/toolchain digests, target, ABI, signature | Deploys as FAP or WebAssembly |
| UIProfile | profile ID, theme, menu, shortcuts, policies, version | Activates on devices |
| LessonPack | lesson ID, instructions, data, tools, policy, reset image | Assigned to classroom workspaces |
| UpdateBundle | version, target, components, digest, signature, rollback metadata | Updates firmware/content |

Strong consistency is required per device for pairing, policy, app lifecycle, VFS mutations, evidence creation, and audit order. Optional cross-device synchronization may be eventually consistent but must preserve immutable artifact identity and surface metadata conflicts.

### 3.5 API and Interface Design

#### Device Session Envelope

```protobuf
message PoisonEnvelope {
  uint32 protocol_version = 1;
  uint64 session_id = 2;
  uint64 sequence = 3;
  uint64 acknowledgement = 4;
  string channel = 5;
  bytes payload = 6;
  bytes authentication_tag = 7;
}
```

Channels are versioned independently: `device`, `files`, `screen`, `apps`, `tools`, `workloads`, `evidence`, `settings`, `updates`, and `audit`. Unknown optional fields are ignored; unsupported required capabilities fail negotiation explicitly.

#### Structured Tool Event

```json
{
  "schema": "poison.tool-event/v1",
  "runId": "01J...",
  "sequence": 42,
  "time": "2026-08-21T18:42:31.250Z",
  "level": "info",
  "kind": "measurement",
  "message": "Frame decoded",
  "data": {
    "protocol": "example",
    "confidence": 0.98
  },
  "artifactIds": ["sha256:..."]
}
```

#### Workload Manifest

```json
{
  "schema": "poison.workload/v1",
  "id": "org.example.field-helper",
  "version": "1.0.0",
  "language": "rust",
  "entry": "field_helper",
  "runtime": "native-fap",
  "firmwareApi": ">=1.0 <2.0",
  "capabilities": ["storage.project", "gpio.read", "console.write"],
  "limits": {
    "memoryBytes": 32768,
    "maxRunSeconds": 300,
    "maxArtifactBytes": 10485760
  }
}
```

#### Bridge API

- `GET /v1/devices` lists locally visible devices without exposing secrets.
- `POST /v1/devices/{id}/sessions` begins a user-confirmed pairing or authenticated session.
- `GET /v1/sessions/{id}/stream` upgrades to an authenticated WebSocket carrying session envelopes.
- `POST /v1/builds` submits a JavaScript validation or Rust build job.
- `GET /v1/builds/{id}` returns immutable build metadata.
- `GET /v1/builds/{id}/events` streams ordered build events.
- The bridge binds to loopback by default, requires an origin-bound token, validates browser origins, and never accepts unauthenticated LAN traffic.

### 3.6 Primary Data Flows

#### Pair and Control

1. Dashboard discovers a device directly or through the bridge.
2. Device and client negotiate protocol and cryptographic suites.
3. User confirms a short authentication code on both screens.
4. Each side stores the paired public identity and granted role.
5. Dashboard opens an encrypted RPC session and requests device state.
6. User starts an integrated app or legacy screen stream.
7. Commands and events are sequence-checked, authorized, executed, and audited.

#### Capture Evidence

1. User creates/selects a case and starts a tool run.
2. Tool emits ordered structured events and raw artifacts.
3. Firmware writes each raw artifact transactionally and computes SHA-256.
4. Evidence service commits metadata and an audit-chain event.
5. Dashboard indexes metadata, renders previews, and streams progress.
6. Notes create separate annotation records.
7. Exporter verifies every artifact and produces a signed manifest plus report.

#### Run JavaScript

1. User edits a project in the dashboard or opens one from `/scripts`.
2. Validator checks syntax, manifest, API compatibility, dependencies, and capabilities.
3. User approves requested capabilities.
4. Project is installed transactionally and started in a scoped mJS context.
5. Console/events/artifacts stream to the dashboard and optional case.
6. A dashboard extension is fetched through the authenticated workload channel and loaded in a sandbox.
7. Stop, timeout, disconnect, or policy revocation terminates the workload and releases resources.

#### Build and Run Rust

1. User imports or edits a locked Rust project.
2. Dashboard sends source and manifest to a local or hosted isolated builder.
3. Builder resolves only approved cached dependencies, compiles with the pinned SDK, runs checks, and emits provenance.
4. Package verifier checks target, ABI, capabilities, limits, digest, and signature.
5. User approves native-code trust or selects restricted WebAssembly mode.
6. Artifact installs transactionally, starts through the workload manager, and streams output/events.
7. Crash data and generated artifacts attach to the selected case or project history.

### 3.7 Security Architecture

#### Trust Zones

- **Device trusted core:** boot, storage, policy, package verifier, evidence journal, recovery.
- **Capability-scoped firmware apps:** curated apps with declared APIs.
- **Untrusted scripts/WebAssembly:** restricted runtime APIs and resource budgets.
- **Trusted native FAPs:** signed but not memory-isolated; require elevated approval.
- **Dashboard sandbox:** untrusted extensions isolated from credentials and transport.
- **Bridge:** local privileged adapter with minimal persistent secrets.
- **Hosted platform:** optional remote zone that never receives device private keys.

#### Authorization

- Owner controls pairing, policy, updates, native trust, revocation, and destructive actions.
- Operator controls authorized field tools, cases, evidence, and approved workloads.
- Instructor manages lessons, student restrictions, resets, and class exports.
- Student runs assigned tools/workloads within lesson policy.
- Observer receives read-only device, run, or case views.

Capabilities are specific verbs such as `storage.case.read`, `storage.project.write`, `nfc.inspect`, `subghz.receive`, `gpio.read`, `gpio.write`, `usb.identity`, `native.execute`, and `update.install`. Roles grant capability sets; manifests request subsets; runtime policy intersects role, device, lesson, lock state, and user approval.

#### Supply Chain

- Dependencies are pinned by digest.
- Builds emit SBOM and provenance.
- Native artifacts and updates require trusted signatures.
- Catalog revocations quarantine affected packages without deleting evidence.
- Reproducible-build checks compare independent outputs before promotion.
- Root signing keys remain offline; intermediates are scoped and rotatable.

### 3.8 Resilience Design

- Every request has an idempotency key or is explicitly non-repeatable.
- Transport retries use bounded exponential backoff with jitter.
- Stream channels use credit windows and sequence acknowledgements for backpressure.
- File writes and installs use temporary objects, digest verification, atomic activation, and journal recovery.
- Evidence raw objects are content-addressed and immutable.
- Dashboard caches schema, projects, indexes, and queued annotations locally; it never treats an unacknowledged device mutation as committed.
- Build queues impose per-principal limits and cancel abandoned work.
- Firmware watchdog and workload manager terminate stuck workloads without rebooting when possible.
- Update activation records the last known-good bundle and recovery instructions before changing active state.

### 3.9 Observability

The system emits structured logs with correlation IDs across dashboard, bridge, builder, and device. Metrics include connection success, transport latency, command failures, dropped/retried frames, storage recovery, artifact verification, app/workload crashes, build queue time, build failures, update rollback, and policy denials.

Device logs are bounded and redact secrets. User-facing diagnostics provide a downloadable support bundle containing versions, public configuration, recent redacted events, and integrity results. Hosted telemetry is opt-in and must not contain raw captures, case contents, source code, device private identity, or credential data.

### 3.10 Infrastructure and Deployment

- **Firmware:** reproducible FBT build, signed updater bundle, stable/beta/developer channels.
- **Dashboard:** static PWA deployable from official hosting, self-hosting, or the bridge; content-addressed assets and signed release manifest.
- **Bridge:** signed Rust binaries/packages for supported desktop/mobile platforms; loopback-only service by default.
- **Builder:** local OCI-compatible sandbox or hosted ephemeral worker using the same pinned image digest.
- **Hosted services:** optional regional deployments with separate build, catalog, and encrypted-sync trust domains.
- **Environments:** development, hardware-in-loop test, staging/signing candidate, production.
- **Rollout:** internal devices, opt-in developer channel, beta cohort, then stable release with staged update percentages and automatic halt thresholds.

---

## 4. Implementation Plan

### 4.1 Build Phases

#### Phase 0: Baseline and Governance

- **Goal:** Establish a reproducible, legally complete, measurable firmware baseline.
- **Scope:** Upstream baseline policy, dependency inventory, restored root licensing/notices, build reproducibility, API snapshot, threat-model seed, hardware test rack, release signing design.
- **Exit criteria:** Clean build from pinned toolchain; SBOM produced; licenses complete; official comparison tests pass; two physical recovery devices available; baseline performance recorded.
- **Requirements:** Enables all requirements; directly satisfies provenance constraints for FR-24 and FR-54.

#### Phase 1: Secure Browser-to-Device Vertical Slice

- **Goal:** Prove professional phone/computer control of a physical device.
- **Scope:** RPC v2 envelope, pairing, USB/BLE adapters, bridge, PWA shell, device status, live screen, remote input, app start/stop, audit events.
- **Exit criteria:** Real browser → real transport → real firmware E2E passes on supported phone and computer baselines; disconnect recovery passes; latency targets met.
- **Requirements:** FR-1–FR-10, FR-51, FR-52, FR-57.

#### Phase 2: Unified Files and Evidence

- **Goal:** Make field data coherent, durable, searchable, and exportable.
- **Scope:** VFS namespaces, transaction journal, browser file manager, cases, raw artifacts, metadata, annotations, audit chain, portable evidence packages.
- **Exit criteria:** Fault-injection suite passes; 10,000-artifact corpus remains searchable; import/export round-trip verifies byte-for-byte; SD-card removal recovery passes.
- **Requirements:** FR-11–FR-22, FR-56.

#### Phase 3: Apps, Tools, Education, and Customization

- **Goal:** Deliver the complete professional and classroom operating experience.
- **Scope:** Package manager, structured app API, legacy fallback, curated tools/data, field profiles, theme/menu editor, education mode, lesson packs.
- **Exit criteria:** Every curated tool has provenance, structured output, safe sample data, and browser workflow; invalid pack recovery passes; classroom reset is isolated and repeatable.
- **Requirements:** FR-23–FR-32, FR-53–FR-55.

#### Phase 4: JavaScript Platform

- **Goal:** Provide a safe browser-to-device JavaScript development and serving workflow.
- **Scope:** Project model, editor integration, validator, restricted mJS APIs, capabilities, console/events, artifact capture, sandboxed dashboard extensions, offline dependencies.
- **Exit criteria:** Reference projects exercise every approved module; raw FFI denial and sandbox escape suites pass; output sequence has zero silent loss; offline workflow passes.
- **Requirements:** FR-33–FR-40.

#### Phase 5: Rust Platform

- **Goal:** Accept Rust source and safely build, deploy, run, debug, and reproduce its artifacts.
- **Scope:** Rust SDK, pinned sandbox builder, Cargo policy, native FAP target, WebAssembly target/runtime, signatures, provenance, console/events/crash capture.
- **Exit criteria:** Independent reproducible builds match; malicious dependency/network tests are contained; native and WebAssembly reference apps pass hardware E2E; incompatible artifacts are rejected.
- **Requirements:** FR-41–FR-50.

#### Phase 6: Hardening and Stable Release

- **Goal:** Release a supportable V1 containing every MUST requirement.
- **Scope:** Performance, accessibility, security review, fuzzing, recovery drills, updater staging, documentation, support bundle, educator/professional usability trials.
- **Exit criteria:** All quality gates and MUST requirements pass; no unresolved critical/high security defects; rollback drill succeeds; launch metrics instrumentation is active.
- **Requirements:** All MUST requirements and NFRs.

### 4.2 Testing Strategy

- **Unit tests:** Policy evaluation, manifests, serializers, path validation, journal state machine, audit chain, package verification, build provenance, dashboard state reducers.
- **Property tests:** Filesystem operation sequences, protocol envelope parsing, evidence import/export, version negotiation, capability intersections.
- **Integration tests:** Firmware services with real storage images; dashboard with bridge; builder with pinned SDK; update/package signing and revocation.
- **Hardware-in-loop tests:** USB, BLE, SD insertion/removal, battery interruption, button injection, screen streaming, workload start/stop, recovery boot.
- **True E2E tests:** A complete user workflow through a real browser/client → actual network or local transport → actual bridge when required → actual firmware and services → physical storage/hardware, with no mocked components. Separate E2E suites cover pairing/control, case capture/export, JavaScript, Rust, app installation, customization rollback, classroom reset, and firmware update rollback.
- **Security tests:** Protocol fuzzing, malformed packages, replay, origin attacks, capability escalation, JS escape, WebAssembly escape, native-signature bypass, dependency substitution, secret scanning, and physical revocation.
- **Load tests:** 10,000 artifacts, 1,000-entry directories, saturated BLE streams, four bridge devices, and 30-device instructor orchestration.
- **Usability/accessibility tests:** Field gloves/touch constraints, keyboard-only, screen reader, projector/classroom, novice ten-minute happy path.

No test may be labeled E2E unless the real browser/client, transport, firmware, storage, and relevant hardware execute together.

### 4.3 CI/CD and Quality Gates

Every pull request runs formatting, static analysis, unit/property tests, protocol compatibility, dashboard build, bridge tests, Rust SDK samples, license/SBOM checks, and documentation validation. Hardware-impacting changes enter a queued physical-device suite before merge.

Release candidates require:

1. Reproducible firmware/dashboard/bridge/build-image outputs.
2. Signed SBOM and provenance.
3. All supported hardware E2E suites passing with zero regressions.
4. Security fuzz corpus and dependency audit passing.
5. Upgrade from prior stable and rollback to prior stable passing.
6. Recovery from interrupted firmware, app, profile, and filesystem operations passing.
7. Accessibility and launch-metric gates passing.

### 4.4 Rollout and Rollback

- Feature flags guard protocol changes, hosted synchronization, new runtimes, and organization features.
- Firmware channels are developer, beta, and stable.
- Stable rollout advances through 1%, 10%, 25%, 50%, and 100% cohorts only when halt thresholds remain clear.
- Device keeps last-known-good updater metadata and supports offline recovery.
- Dashboard and bridge retain the previous compatible version and protocol schemas.
- Package updates preserve the previous activated object until post-start health confirmation.
- A rollback never deletes cases, evidence, source projects, or audit history.

### 4.5 Operational Readiness

Before stable release, the project must have signed release procedures, key-rotation and revocation procedures, incident severity definitions, update rollback runbooks, corruption recovery, build-worker isolation verification, public compatibility policy, data retention documentation, supported-platform matrix, and named maintainers for firmware, dashboard, security, and releases.

---

## 5. Milestones

Because no calendar deadline or team size has been committed, target dates are expressed as elapsed windows from project kickoff and are re-estimated after Phase 0.

| Milestone | Goal | Exit Criteria | Target Window | Owner Role |
|---|---|---|---|---|
| M0 | Reproducible baseline | Phase 0 exit criteria met | Weeks 0–4 | Firmware/Release |
| M1 | Secure physical-device control | Pair, stream, input, and app lifecycle E2E pass | Weeks 5–12 | Firmware/Dashboard |
| M2 | Files and evidence MVP | Transactional case capture/export E2E pass | Weeks 13–22 | Firmware/Data |
| M3 | Professional UI and curated tools | Profiles, packages, lessons, and structured tools pass | Weeks 23–34 | Product/Firmware |
| M4 | JavaScript platform | Offline JS edit/run/serve/debug E2E and sandbox tests pass | Weeks 35–42 | Runtime/Dashboard |
| M5 | Rust platform | Reproducible native/Wasm build-deploy-run E2E passes | Weeks 43–54 | Toolchain/Runtime |
| M6 | Stable V1 | All MUST/NFR/release gates pass | Weeks 55–62 | Release/Security |

### 5.1 Dependency Graph

```text
M0 Baseline
 └─► M1 Secure control
      └─► M2 Files/evidence
           ├─► M3 Apps/tools/customization/education
           │    └─► M4 JavaScript
           │         └─► M5 Rust
           └──────────────────────────────┐
                                          ▼
                                     M6 Stable V1
```

M2 is the minimum product proof: a real browser controls a real device and produces a verified evidence export. V1 is not complete until M6 because the user confirmed every listed capability as launch-blocking.

---

## 6. Success Criteria

### 6.1 Launch Metrics

| Metric | Target | Measurement Method |
|---|---:|---|
| Pair-to-first-control completion | ≥ 95% without support | Instrumented usability trial |
| Safe sample happy-path completion | ≥ 90% within 10 minutes | Professional/student/educator trial |
| Browser-managed field sessions | ≥ 80% complete without CLI fallback | Opt-in local aggregate or study logs |
| Evidence export verification | 100% valid in launch cohort | Export verifier |
| JS reference project success | ≥ 99% across supported devices | Hardware E2E telemetry/test rack |
| Rust reference build/deploy success | ≥ 98% excluding source errors | Builder and hardware E2E |
| Crash-free device sessions | ≥ 99.5% | Redacted diagnostics |
| Update success without manual recovery | ≥ 99.5% | Release-channel metrics |
| Critical/high unresolved security defects | 0 | Security gate |
| Curated tools with complete docs/data/schema | 100% | Catalog validation |
| WCAG 2.2 AA violations in primary flows | 0 | Automated and manual audit |

### 6.2 Ongoing Monitoring

The project reviews release health daily during staged rollout, weekly during beta, and monthly after stable release. Dashboards cover transport reliability, command latency, device/app/workload crashes, evidence integrity, filesystem recovery, update/rollback, builder queue/failure, package revocation, and support-volume categories.

### 6.3 Remediation Triggers

- Any evidence digest or audit-chain corruption halts releases immediately.
- Any confirmed signature, pairing, sandbox, or capability bypass disables affected distribution and triggers revocation.
- Command success below 99.5% or crash-free sessions below 99% halt rollout advancement.
- Update failure above 0.5% or any unrecoverable update failure halts rollout.
- p95 transport latency exceeding target by 50% for two consecutive cohorts requires performance remediation.
- A critical accessibility blocker in pairing, control, evidence, or recovery blocks stable promotion.

---

## 7. Risks

| ID | Risk | Impact | Likelihood | Mitigation | Contingency |
|---|---|---:|---:|---|---|
| R-1 | Browser USB/BLE support differs by platform | High | High | Transport-independent API plus bridge/native shell | Publish supported matrix; require bridge on restricted platforms |
| R-2 | BLE bandwidth cannot sustain rich remote UI | Medium | High | Structured events, delta frames, backpressure, USB preference | Reduce frame rate; keep legacy mirror secondary |
| R-3 | Native FAP or Rust code compromises firmware | Critical | Medium | Signatures, capability approval, provenance, default denial | Quarantine/revoke package; require WebAssembly mode |
| R-4 | mJS raw FFI defeats script isolation | Critical | Medium | Restricted resolver; developer-only trusted FFI | Disable FFI entirely in stable channel |
| R-5 | WebAssembly runtime exceeds memory budget | High | Medium | Compact runtime, static limits, admission checks | Restrict Rust V1 to signed native FAP subset |
| R-6 | SD removal/power loss corrupts cases | Critical | Medium | Journaled commits, immutable objects, fault testing | Read-only recovery and index rebuild |
| R-7 | “Evidence” creates false legal expectations | High | Medium | Precise terminology, verification reports, no admissibility claim | Rename regulated exports and seek external validation |
| R-8 | Tool breadth overwhelms review capacity | High | High | Curated allowlist, ownership, schema/docs gates | Reduce stable catalog; move others to developer channel |
| R-9 | Upstream firmware drift causes long-lived fork debt | High | High | Narrow interfaces, upstream tracking, recurring parity audits | Freeze supported upstream base per major release |
| R-10 | Hosted builder becomes a supply-chain target | Critical | Medium | Ephemeral sandboxes, no default network, pinned images, provenance | Disable hosted builds; retain local builder |
| R-11 | Dashboard extension escapes browser sandbox | Critical | Medium | Worker/iframe isolation, CSP, message schema, fuzzing | Disable extension and quarantine package |
| R-12 | Classroom controls expose sensitive captures | High | Medium | Per-workspace isolation, role policy, minimized instructor access | Local-only class mode and forced purge workflow |
| R-13 | Product name is misread as malicious intent | Medium | High | Professional positioning and explicit authorized-use policy | Brand review before public launch |
| R-14 | Missing root licensing/provenance blocks release | Critical | High | Phase 0 license restoration and component audit | Do not distribute until resolved |
| R-15 | Scope exceeds available team/time | High | High | Milestone gates, vertical slices, stable/developer separation | Extend schedule; do not weaken security or evidence gates |
| R-16 | Commercial-model uncertainty distorts architecture | Medium | Medium | Local-first modular services and open protocols | Ship self-hostable core while model is decided |

---

## 8. Open Questions

These are bounded decisions, not missing implementation requirements. Their owners and decision gates prevent them from silently blocking engineering.

| # | Question | Owner | Due Gate |
|---|---|---|---|
| OQ-1 | Will distribution be public open source, commercial managed, or open-core with paid hosted services? | Project owner | Before M1 completion |
| OQ-2 | Which phone/browser/OS combinations receive direct transport versus the bridge shell? | Product + Dashboard | Before M1 design freeze |
| OQ-3 | Which official firmware branch and upstream-sync cadence define each PoisonedOS major version? | Firmware lead | M0 |
| OQ-4 | Who controls offline root and online package-signing keys? | Security/Release | M0 |
| OQ-5 | Which Rust dependencies are approved for offline/pinned builds? | Toolchain/Security | Before M5 implementation |
| OQ-6 | Which compact WebAssembly runtime meets licensing, memory, and isolation requirements? | Runtime lead | M4 completion |
| OQ-7 | Which evidence package extension and public schema namespace will be standardized? | Data/Product | Before M2 design freeze |
| OQ-8 | Which tool families and datasets form the stable curated V1 catalog? | Product/Security/Education | Before M3 implementation |
| OQ-9 | What retention defaults apply to hosted build source and optional synchronization? | Product/Privacy | Before any hosted beta |
| OQ-10 | Which external laboratory or forensic validation, if any, will be pursued after V1? | Project owner | Before public claims |

---

## Appendices

### Appendix A: Glossary

| Term | Meaning |
|---|---|
| Bridge | Local Rust service adapting browser sessions to USB/BLE and optional local builds |
| Case | User-defined field investigation, assessment, lab, or lesson workspace |
| Evidence artifact | Immutable raw output with cryptographic digest and provenance metadata |
| FAP | Flipper Application Package containing an ELF-based external application |
| Integrated app | App exposing structured browser commands, views, events, and artifacts |
| Legacy app | App controlled through screen streaming and input injection |
| Lesson pack | Versioned instructions, datasets, policy, starter projects, and reset state |
| Poisoned Dashboard | Offline-capable web control plane for device, files, evidence, apps, and workloads |
| Poisoned RPC v2 | Authenticated, multiplexed, transport-independent device protocol |
| Profile | Atomic set of UI, menu, tool, transport, logging, and evidence settings |
| Workload | JavaScript, native Rust FAP, or Rust-derived WebAssembly project managed by PoisonedOS |

### Appendix B: API Contract Rules

1. Schemas are versioned and generated from one canonical definition.
2. Required capabilities are negotiated before commands are sent.
3. Every mutation has a command ID and idempotency semantics.
4. Every stream event has a channel-local sequence number.
5. Errors contain stable machine code, safe user message, retryability, and correlation ID.
6. Binary artifacts travel in bounded chunks with offset, total length, and final digest.
7. Cancellation is explicit and acknowledged.
8. Unknown enum values and optional fields do not crash either peer.
9. Secrets and raw evidence never appear in generic error objects.
10. Protocol compatibility tests cover the current and previous stable major versions.

### Appendix C: Migration Plan

1. Inventory current `/int` and `/ext` without changing them.
2. Back up internal storage and critical SD metadata.
3. Create PoisonedOS namespaces and journal in a versioned root.
4. Index existing apps, scripts, captures, and settings by reference; do not rewrite raw files.
5. Generate compatibility aliases for standard Flipper paths.
6. Import selected legacy captures into cases only with user confirmation.
7. Validate digests and free-space requirements before committing migration metadata.
8. Preserve a migration manifest and reversible mapping.
9. On failure, remove only uncommitted PoisonedOS metadata and restore prior settings.
10. Require explicit approval before deleting any duplicate or superseded legacy file.

Momentum-specific state, when imported, is treated as an alternative implementation rather than assumed compatible. Asset packs, settings, app bundles, extended radio settings, and fork-specific formats receive dedicated converters or remain read-only legacy data.

### Appendix D: Security Threat Model

| Threat | Boundary | Required Control |
|---|---|---|
| Unauthorized nearby client | Radio/device | Physical confirmation, authenticated encryption, revocation |
| Malicious browser origin | Browser/bridge | Origin validation, loopback token, CSP, no ambient device access |
| Replay or message injection | Transport/session | Counters, AEAD, expiry, channel binding |
| Malicious package | Package/firmware | Signature, provenance, capability policy, quarantine |
| Script escape | JS runtime | Restricted resolver, no raw FFI, resource limits |
| Wasm escape | Wasm runtime | Memory isolation, host-call allowlist, fuzzing |
| Native memory corruption | FAP/firmware | Trusted signer, explicit approval, watchdog, revocation |
| Evidence tampering | Storage/dashboard | Immutable raw object, SHA-256, audit chain, verified export |
| Build substitution | Builder/package | Pinned images/dependencies, reproducibility, signed provenance |
| SD-card attack | Storage/firmware | Strict parsing, path containment, size limits, transactional index |
| Secret leakage | Logs/support/hosted | Redaction, data classification, encrypted storage, opt-in telemetry |
| Abusive tool use | User/tool/hardware | Authorized-use policy, role/capability controls, regulatory defaults |

### Appendix E: Capacity Model

The dashboard/bridge capacity baseline is 10,000 evidence artifacts, 250 packages, 100 profiles, 1,000 entries per directory, four devices per bridge, and 30 classroom devices across bridges. Device allocations are budgeted per workload manifest and admitted only when the requested heap, stack, output queue, runtime, and artifact limits fit current availability.

Build-service capacity is expressed in concurrent isolated workers rather than requests per second. Queue autoscaling uses pending-job count and oldest-job age; per-principal quotas prevent one classroom or project from exhausting the pool.

### Appendix F: Cost Model

The architecture separates costs so the unresolved business model does not force a redesign:

- Local-only use incurs dashboard hosting/download, bridge distribution, signing, and support costs but no per-command cloud cost.
- Self-hosted builds consume the user's compute and storage.
- Hosted builds scale with worker minutes, dependency-cache storage, artifact storage, and egress.
- Optional synchronization scales with encrypted object bytes, metadata operations, retention, and egress.
- Catalog service scales primarily with artifact storage, signing operations, review labor, and distribution bandwidth.
- Hardware-in-loop quality requires dedicated devices, USB/BLE hosts, controllable power, SD fixtures, RF-safe test facilities, and maintenance labor.

Financial figures are intentionally deferred until OQ-1 chooses a distribution model; engineering instrumentation MUST record these unit drivers during beta so pricing or sponsorship decisions use measured costs.

### Appendix G: Decision Log

| Decision | Status | Rationale |
|---|---|---|
| All listed capabilities are V1 launch requirements | Accepted | Project owner answered “all” |
| Professionals, students, and educators are first-class | Accepted | Explicit product direction |
| Dashboard is a control plane, not only screen mirroring | Accepted | Solves device-only workflow and fragmented output |
| Local operation does not require cloud | Accepted | Field reliability, privacy, and business-model neutrality |
| Preserve underlying internal/SD filesystems behind a VFS | Accepted | Reduces firmware/storage risk while providing a real namespace |
| JavaScript runs on device and serves sandboxed dashboard extensions | Accepted | Direct execution and serving are explicit requirements |
| Rust source compiles off-device | Accepted | Meets source-to-run workflow within MCU constraints |
| Rust supports trusted native FAP and restricted WebAssembly outputs | Accepted | Balances performance, compatibility, and isolation |
| Official-style base with curated audited ports | Proposed | Limits provenance and maintenance risk identified in comparison guide |
| Commercial distribution model | Undecided | Does not block local-first architecture; tracked as OQ-1 |

### Appendix H: Minimum Runbooks

#### Lost or Stolen Paired Client

1. Open device security settings.
2. Identify the exact client fingerprint.
3. Revoke it and all resumable sessions.
4. Rotate optional organization credentials if the client held them.
5. Verify the audit event and attempt a denied reconnection test.

#### Interrupted Update

1. Boot recovery mode without relying on the dashboard.
2. Verify active and last-known-good bundle manifests.
3. Resume only if the staged bundle validates completely.
4. Otherwise activate the last-known-good bundle.
5. Preserve user cases, evidence, source projects, and audit records.
6. Export a redacted support bundle after recovery.

#### Filesystem Recovery

1. Mount affected backing storage read-only.
2. Replay or discard incomplete journal entries according to commit markers.
3. Verify immutable object digests.
4. Rebuild indexes from authoritative manifests.
5. Quarantine invalid objects without deleting them.
6. Produce a recovery report listing every affected path and decision.

#### Compromised Package or Signing Key

1. Halt distribution and stable rollout.
2. Publish signed revocation metadata from an unaffected authority.
3. Quarantine affected packages on connected devices.
4. Preserve associated evidence and provenance for investigation.
5. Rotate the compromised intermediate and rebuild affected artifacts.
6. Require independent security approval before distribution resumes.

### Appendix I: Risk Register Ownership

- Security owns R-3, R-4, R-5, R-10, and R-11.
- Firmware/Data owns R-2, R-6, and R-9.
- Product/Legal owns R-7, R-13, R-14, and R-16.
- Product/Security/Education jointly own R-8 and R-12.
- Project leadership owns R-15 and the staffing/schedule response.

Risk review occurs at every milestone exit and weekly during beta. Any risk promoted to critical blocks release until its mitigation is implemented and verified.
