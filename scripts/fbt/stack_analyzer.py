#!/usr/bin/env python3

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from dataclasses import dataclass, field
from pathlib import Path


FUNCTION_HEADER = re.compile(r"^(?P<address>[0-9a-fA-F]+) <(?P<name>[^>]+)>:$")
INSTRUCTION = re.compile(
    r"^\s*[0-9a-fA-F]+:\s+(?P<opcode>[a-z][a-z0-9.]*)\s*(?P<operands>.*)$"
)
DIRECT_TARGET = re.compile(r"\b(?P<address>[0-9a-fA-F]+) <(?P<name>[^>]+)>")
FDE_HEADER = re.compile(
    r"\bFDE\b.*\bpc=(?P<start>[0-9a-fA-F]+)\.\.(?P<end>[0-9a-fA-F]+)"
)
CFA_OFFSET = re.compile(r"DW_CFA_def_cfa_offset:\s*(?P<bytes>\d+)")
DIE_HEADER = re.compile(r"^\s*<\d+><[0-9a-fA-F]+>:.*\(DW_TAG_(?P<tag>[^)]+)\)")
RPC_HANDLER_ASSIGNMENT = re.compile(
    r"\b(?:message_handler|decode_submessage)\s*=\s*(?P<name>[A-Za-z_]\w*)"
)


class StackAnalysisError(RuntimeError):
    pass


@dataclass
class Function:
    name: str
    address: int
    frame_bytes: int = 0
    calls: set[str] = field(default_factory=set)
    tail_calls: set[str] = field(default_factory=set)
    indirect_calls: int = 0


@dataclass(frozen=True)
class StackPath:
    bytes: int
    functions: tuple[str, ...]


def _run_objdump(objdump: str, *arguments: str) -> str:
    result = subprocess.run(
        [objdump, *arguments],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        raise StackAnalysisError(
            f"{objdump} {' '.join(arguments)} failed: {result.stderr.strip()}"
        )
    return result.stdout


def parse_dwarf_frames(output: str) -> dict[int, int]:
    frames: dict[int, int] = {}
    address: int | None = None
    maximum = 0
    for line in output.splitlines():
        if match := FDE_HEADER.search(line):
            if address is not None:
                frames[address] = maximum
            address = int(match.group("start"), 16)
            maximum = 0
            continue
        if address is not None and (match := CFA_OFFSET.search(line)):
            maximum = max(maximum, int(match.group("bytes")))
    if address is not None:
        frames[address] = maximum
    return frames


def parse_noreturn_functions(output: str) -> set[str]:
    functions: set[str] = set()
    name: str | None = None
    noreturn = False

    def finish() -> None:
        if name and noreturn:
            functions.add(name)

    active = False
    for line in output.splitlines():
        if die := DIE_HEADER.match(line):
            finish()
            name = None
            noreturn = False
            active = die.group("tag") == "subprogram"
        elif active and "DW_AT_name" in line:
            name = line.rsplit(": ", 1)[-1].strip()
        elif active and "DW_AT_noreturn" in line:
            noreturn = line.rsplit(":", 1)[-1].strip() not in {"0", "false"}
    finish()
    return functions


def _base_symbol(name: str) -> str:
    return name.split("+", 1)[0]


def parse_disassembly(output: str, frames: dict[int, int]) -> dict[str, Function]:
    functions: dict[str, Function] = {}
    current: Function | None = None
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if match := FUNCTION_HEADER.match(line):
            address = int(match.group("address"), 16)
            if address not in frames:
                continue
            current = Function(
                name=match.group("name"),
                address=address,
            )
            current.frame_bytes = frames.get(current.address, 0)
            functions[current.name] = current
            continue
        if current is None or not (match := INSTRUCTION.match(raw_line)):
            continue
        opcode = match.group("opcode")
        operands = match.group("operands")
        target_match = DIRECT_TARGET.search(operands)
        if opcode in {"bl", "bl.w", "blx"}:
            if target_match:
                current.calls.add(_base_symbol(target_match.group("name")))
            else:
                current.indirect_calls += 1
        elif opcode in {"b", "b.n", "b.w"} and target_match:
            target = _base_symbol(target_match.group("name"))
            if target != current.name:
                current.tail_calls.add(target)
    return functions


def _resolve_symbol(functions: dict[str, Function], requested: str) -> str:
    if requested in functions:
        return requested
    candidates = sorted(name for name in functions if name.startswith(f"{requested}."))
    if len(candidates) != 1:
        raise StackAnalysisError(
            f"ELF symbol {requested!r} resolved to {len(candidates)} candidates"
        )
    return candidates[0]


def calculate_stack_path(
    functions: dict[str, Function],
    root: str,
    synthetic_calls: dict[str, tuple[str, ...]] | None = None,
    terminal_functions: set[str] | None = None,
) -> tuple[StackPath, tuple[str, ...], tuple[str, ...]]:
    synthetic_calls = synthetic_calls or {}
    terminal_functions = terminal_functions or set()
    unresolved_indirect: set[str] = set()
    recursive_edges: set[str] = set()
    memo: dict[str, StackPath] = {}

    def visit(name: str, active: tuple[str, ...]) -> StackPath:
        if name in memo:
            return memo[name]
        if name in active:
            cycle = " -> ".join((*active[active.index(name) :], name))
            recursive_edges.add(cycle)
            return StackPath(0, ())
        function = functions.get(name)
        if function is None:
            return StackPath(0, (name,))
        if function.indirect_calls:
            unresolved_indirect.add(name)

        best = StackPath(function.frame_bytes, (name,))
        if name in terminal_functions:
            memo[name] = best
            return best
        direct_calls = set(function.calls)
        direct_calls.update(synthetic_calls.get(name, ()))
        for callee in sorted(direct_calls):
            if callee not in functions:
                continue
            child = visit(callee, (*active, name))
            candidate = StackPath(
                function.frame_bytes + child.bytes,
                (name, *child.functions),
            )
            if candidate.bytes > best.bytes:
                best = candidate
        for callee in sorted(function.tail_calls):
            if callee not in functions:
                continue
            child = visit(callee, (*active, name))
            if child.bytes > best.bytes:
                best = StackPath(child.bytes, (name, *child.functions))
        memo[name] = best
        return best

    return (
        visit(root, ()),
        tuple(sorted(unresolved_indirect)),
        tuple(sorted(recursive_edges)),
    )


def parse_rpc_handler_sources(source_paths: tuple[str | Path, ...]) -> tuple[str, ...]:
    handlers: set[str] = set()
    for source_path in source_paths:
        source = Path(source_path)
        try:
            contents = source.read_text(encoding="utf-8")
        except OSError as error:
            raise StackAnalysisError(
                f"cannot read RPC source {source}: {error}"
            ) from error
        handlers.update(
            match.group("name")
            for match in RPC_HANDLER_ASSIGNMENT.finditer(contents)
            if match.group("name") != "NULL"
        )
    if not handlers:
        raise StackAnalysisError("no RPC handlers found in the configured sources")
    return tuple(sorted(handlers))


def read_stack_budget(path: str | Path, macro: str) -> int:
    header = Path(path)
    try:
        contents = header.read_text(encoding="utf-8")
    except OSError as error:
        raise StackAnalysisError(
            f"cannot read stack budget header {header}: {error}"
        ) from error
    match = re.search(
        rf"^\s*#define\s+{re.escape(macro)}\s+(?P<bytes>\d+)u?\s*$",
        contents,
        re.MULTILINE,
    )
    if not match:
        raise StackAnalysisError(f"{header} does not define integer macro {macro}")
    budget = int(match.group("bytes"))
    if budget <= 0:
        raise StackAnalysisError(f"{macro} must be positive")
    return budget


def validate_startup_order(startup_apps: tuple[str, ...] | list[str]) -> None:
    if len(startup_apps) != len(set(startup_apps)):
        raise StackAnalysisError("firmware startup order contains duplicate apps")
    positions = {appid: index for index, appid in enumerate(startup_apps)}
    required_edges = (
        ("storage_start", "rpc_start"),
        ("rpc_start", "expansion_start"),
    )
    for required, dependent in required_edges:
        if required not in positions or dependent not in positions:
            raise StackAnalysisError(
                f"firmware startup order is missing {required} or {dependent}"
            )
        if positions[required] >= positions[dependent]:
            raise StackAnalysisError(f"{required} must run before {dependent}")


def calculate_rpc_worker_stack(
    functions: dict[str, Function],
    root: str,
    handlers: tuple[str, ...],
    nested_handler: str,
    stack_budget: int,
    terminal_functions: set[str] | None = None,
) -> dict[str, object]:
    resolved_root = _resolve_symbol(functions, root)
    resolved_handlers = tuple(
        _resolve_symbol(functions, handler) for handler in handlers
    )
    resolved_nested_handler = _resolve_symbol(functions, nested_handler)
    synthetic_calls = {
        resolved_root: resolved_handlers,
        resolved_nested_handler: resolved_handlers,
    }
    path, indirect_calls, recursive_edges = calculate_stack_path(
        functions,
        resolved_root,
        synthetic_calls,
        terminal_functions,
    )
    result: dict[str, object] = {
        "stack_budget": stack_budget,
        "maximum_stack": path.bytes,
        "maximum_path": list(path.functions),
        "root": resolved_root,
        "handlers": list(resolved_handlers),
        "nested_handler": resolved_nested_handler,
        "reachable_indirect_calls": list(indirect_calls),
        "recursive_call_edges": list(recursive_edges),
        "passed": path.bytes <= stack_budget,
    }
    if path.bytes > stack_budget:
        raise StackAnalysisError(
            f"RPC worker stack requires {path.bytes} bytes but {root} has {stack_budget}: "
            + " -> ".join(path.functions)
        )
    return result


def analyze_startup_stack(
    elf_path: str | Path,
    objdump: str,
    root: str,
    startup_hooks: tuple[str, ...],
    stack_budget: int,
    *,
    rpc_sources: tuple[str | Path, ...] = (),
    rpc_root: str | None = None,
    rpc_stack_budget: int | None = None,
    rpc_nested_handler: str | None = None,
    startup_apps: tuple[str, ...] = (),
) -> dict[str, object]:
    elf = Path(elf_path)
    frames = parse_dwarf_frames(_run_objdump(objdump, "--dwarf=frames", str(elf)))
    noreturn_functions = parse_noreturn_functions(
        _run_objdump(objdump, "--dwarf=info", str(elf))
    )
    functions = parse_disassembly(
        _run_objdump(objdump, "-d", "--no-show-raw-insn", str(elf)), frames
    )
    resolved_root = _resolve_symbol(functions, root)
    resolved_hooks = tuple(_resolve_symbol(functions, hook) for hook in startup_hooks)
    path, indirect_calls, recursive_edges = calculate_stack_path(
        functions,
        resolved_root,
        {resolved_root: resolved_hooks},
        noreturn_functions,
    )
    if (
        not rpc_sources
        or not rpc_root
        or rpc_stack_budget is None
        or not rpc_nested_handler
    ):
        raise StackAnalysisError("RPC worker stack configuration is required")
    rpc_worker = calculate_rpc_worker_stack(
        functions,
        rpc_root,
        parse_rpc_handler_sources(rpc_sources),
        rpc_nested_handler,
        rpc_stack_budget,
        noreturn_functions,
    )
    validate_startup_order(startup_apps)
    result: dict[str, object] = {
        "schema": "poison.firmware-stack/v2",
        "elf_sha256": hashlib.sha256(elf.read_bytes()).hexdigest(),
        "stack_budget": stack_budget,
        "maximum_stack": path.bytes,
        "maximum_path": list(path.functions),
        "root": resolved_root,
        "startup_hooks": list(resolved_hooks),
        "startup_order": list(startup_apps),
        "reachable_indirect_calls": list(indirect_calls),
        "recursive_call_edges": list(recursive_edges),
        "noreturn_functions": sorted(noreturn_functions),
        "rpc_worker": rpc_worker,
        "passed": path.bytes <= stack_budget and rpc_worker["passed"] is True,
    }
    if path.bytes > stack_budget:
        raise StackAnalysisError(
            f"startup stack requires {path.bytes} bytes but {root} has {stack_budget}: "
            + " -> ".join(path.functions)
        )
    return result


def validate_stack_report(
    report_path: str | Path, elf_path: str | Path
) -> dict[str, object]:
    report_file = Path(report_path)
    elf_file = Path(elf_path)
    try:
        report = json.loads(report_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise StackAnalysisError(
            f"cannot read startup stack report: {error}"
        ) from error
    if (
        report.get("schema") != "poison.firmware-stack/v2"
        or report.get("passed") is not True
    ):
        raise StackAnalysisError("firmware stack report is not a passing v2 report")
    actual_hash = hashlib.sha256(elf_file.read_bytes()).hexdigest()
    if report.get("elf_sha256") != actual_hash:
        raise StackAnalysisError("startup stack report does not match the firmware ELF")
    maximum = report.get("maximum_stack")
    budget = report.get("stack_budget")
    if not isinstance(maximum, int) or not isinstance(budget, int) or maximum > budget:
        raise StackAnalysisError("startup stack report contains an invalid stack bound")
    rpc_worker = report.get("rpc_worker")
    if not isinstance(rpc_worker, dict):
        raise StackAnalysisError("firmware stack report has no RPC worker bound")
    rpc_maximum = rpc_worker.get("maximum_stack")
    rpc_budget = rpc_worker.get("stack_budget")
    if (
        rpc_worker.get("passed") is not True
        or not isinstance(rpc_maximum, int)
        or not isinstance(rpc_budget, int)
        or rpc_maximum > rpc_budget
        or not rpc_worker.get("handlers")
    ):
        raise StackAnalysisError(
            "firmware stack report contains an invalid RPC worker bound"
        )
    startup_order = report.get("startup_order")
    if not isinstance(startup_order, list) or not all(
        isinstance(appid, str) and appid for appid in startup_order
    ):
        raise StackAnalysisError("firmware stack report has no valid startup order")
    validate_startup_order(startup_order)
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate firmware startup call paths against their thread stack"
    )
    parser.add_argument("--elf", required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--hook", action="append", default=[])
    parser.add_argument("--startup-app", action="append", default=[], required=True)
    parser.add_argument("--budget", required=True, type=int)
    parser.add_argument("--rpc-root", required=True)
    parser.add_argument("--rpc-source", action="append", default=[], required=True)
    parser.add_argument("--rpc-budget-header", required=True)
    parser.add_argument("--rpc-budget-macro", required=True)
    parser.add_argument("--rpc-nested-handler", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    try:
        report = analyze_startup_stack(
            args.elf,
            args.objdump,
            args.root,
            tuple(args.hook),
            args.budget,
            rpc_sources=tuple(args.rpc_source),
            rpc_root=args.rpc_root,
            rpc_stack_budget=read_stack_budget(
                args.rpc_budget_header, args.rpc_budget_macro
            ),
            rpc_nested_handler=args.rpc_nested_handler,
            startup_apps=tuple(args.startup_app),
        )
    except StackAnalysisError as error:
        parser.error(str(error))
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
