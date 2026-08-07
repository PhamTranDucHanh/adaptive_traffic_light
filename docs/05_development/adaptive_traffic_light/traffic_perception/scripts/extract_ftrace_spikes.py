#!/usr/bin/env python3
"""Write one ftrace text report containing the top N pipeline timing spikes."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


NS_PER_MS = 1_000_000
NS_PER_SEC = 1_000_000_000
FIELD_RE = re.compile(r"\b(CycleId|LaneId|FrameId|ExpectedWakeup|Begin|End)=\s*(\d+)")
CPU_SHARD_RE = re.compile(r"^(?P<base>.+\.dat)\.cpu\d+$")
TRACE_CMD_MAGIC = b"\x17\x08Dtracing"


@dataclass(frozen=True)
class Spike:
    line: int
    label: str
    wakeup_ms: float
    execution_ms: float
    begin_ns: int
    end_ns: int
    reasons: tuple[str, ...]

    @property
    def severity_ms(self) -> float:
        return max(
            self.wakeup_ms if "wakeup" in self.reasons else 0.0,
            self.execution_ms if "execution" in self.reasons else 0.0,
        )


def positive_float(value: str) -> float:
    number = float(value)
    if number < 0:
        raise argparse.ArgumentTypeError("must be >= 0")
    return number


def positive_int(value: str) -> int:
    number = int(value)
    if number <= 0:
        raise argparse.ArgumentTypeError("must be > 0")
    return number


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Find the top N timing spikes in pipeline.log and write their short "
            "trace-cmd report excerpts to one text file."
        )
    )
    parser.add_argument("log", type=Path, help="pipeline.log containing ExpectedWakeup/Begin/End")
    parser.add_argument("trace", type=Path, help="complete trace-cmd .dat file")
    parser.add_argument("-n", "--count", type=positive_int, default=10,
                        help="number of largest spike records to extract (default: 10)")
    parser.add_argument("-o", "--output", type=Path,
                        help="output .txt file (default: <trace stem>_top<N>_spikes.txt)")
    parser.add_argument("--cpu", type=int, default=2,
                        help="CPU to include in the report (default: 2)")
    parser.add_argument("--all-cpus", action="store_true",
                        help="include every CPU instead of only --cpu")
    parser.add_argument("--wakeup-ms", type=positive_float, default=20.0,
                        help="minimum wakeup latency in ms (default: 20)")
    parser.add_argument("--exec-ms", type=positive_float, default=20.0,
                        help="minimum execution time in ms (default: 20)")
    parser.add_argument("--before-ms", type=positive_float, default=50.0,
                        help="report context before each spike point in ms (default: 50)")
    parser.add_argument("--after-ms", type=positive_float, default=50.0,
                        help="report context after each spike point in ms (default: 50)")
    parser.add_argument("--dry-run", action="store_true",
                        help="show the selected top N without producing the report")
    return parser.parse_args()


def validate_trace(trace: Path) -> None:
    """Reject per-CPU recorder leftovers and malformed containers up front."""
    shard = CPU_SHARD_RE.match(str(trace))
    if shard:
        base = Path(shard.group("base"))
        raise RuntimeError(
            f"{trace} is a temporary CPU shard, not a readable trace container; "
            f"use the complete file {base}"
        )
    if trace.stat().st_size == 0:
        raise RuntimeError(f"trace file is empty: {trace}")
    with trace.open("rb") as stream:
        magic = stream.read(len(TRACE_CMD_MAGIC))
    if magic != TRACE_CMD_MAGIC:
        raise RuntimeError(f"not a readable trace-cmd .dat container: {trace}")


def read_spikes(log_path: Path, wakeup_limit: float,
                execution_limit: float) -> tuple[list[Spike], int]:
    spikes: list[Spike] = []
    records = 0
    with log_path.open("r", encoding="utf-8", errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            fields = {name: int(value) for name, value in FIELD_RE.findall(line)}
            if not {"ExpectedWakeup", "Begin", "End"}.issubset(fields):
                continue
            records += 1
            expected, begin, end = (fields[key] for key in ("ExpectedWakeup", "Begin", "End"))
            wakeup_ms = (begin - expected) / NS_PER_MS
            execution_ms = (end - begin) / NS_PER_MS
            reasons = []
            if wakeup_ms >= wakeup_limit:
                reasons.append("wakeup")
            if execution_ms >= execution_limit:
                reasons.append("execution")
            if not reasons:
                continue

            if "CycleId" in fields:
                label = f"cycle-{fields['CycleId']}"
            elif "LaneId" in fields and "FrameId" in fields:
                label = f"lane-{fields['LaneId']}-frame-{fields['FrameId']}"
            elif "FrameId" in fields:
                label = f"frame-{fields['FrameId']}"
            else:
                label = f"line-{line_number}"
            spikes.append(Spike(line_number, label, wakeup_ms, execution_ms,
                                begin, end, tuple(reasons)))
    return spikes, records


def seconds(ns: int) -> str:
    return f"{ns // NS_PER_SEC}.{ns % NS_PER_SEC:09d}"


def run_trace_cmd(command: list[str], stdout=None) -> None:
    result = subprocess.run(command, stdout=stdout or subprocess.PIPE,
                            stderr=subprocess.PIPE, text=stdout is None)
    if result.returncode == 0:
        return
    detail = result.stderr
    if isinstance(detail, bytes):
        detail = detail.decode("utf-8", errors="replace")
    if detail:
        print(detail.rstrip(), file=sys.stderr)
    raise subprocess.CalledProcessError(result.returncode, command)


def report_points(spike: Spike) -> list[tuple[str, int]]:
    """Use actual wakeup and completion as bounded diagnostic points."""
    points = []
    if "wakeup" in spike.reasons:
        points.append(("wakeup-at-Begin", spike.begin_ns))
    if "execution" in spike.reasons:
        points.append(("execution-at-End", spike.end_ns))
    return points


def write_report(trace: Path, output: Path, selected: list[Spike], cpu: int | None,
                 before_ns: int, after_ns: int, total_records: int,
                 total_candidates: int) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", encoding="utf-8") as report:
        report.write(f"source_trace={trace}\n")
        report.write(f"timing_records={total_records}\n")
        report.write(f"spike_candidates={total_candidates}\n")
        report.write(f"selected_spikes={len(selected)}\n")
        report.write(f"cpu={'all' if cpu is None else cpu}\n")
        report.write(f"context_ms={before_ns / NS_PER_MS:g} before, {after_ns / NS_PER_MS:g} after\n")

        with tempfile.TemporaryDirectory(prefix=".ftrace-spikes-", dir=output.parent) as temp_name:
            temp_dir = Path(temp_name)
            for rank, spike in enumerate(selected, 1):
                report.write("\n" + "=" * 96 + "\n")
                report.write(
                    f"RANK {rank}/{len(selected)}  {spike.label}  log_line={spike.line}  "
                    f"reasons={'+'.join(spike.reasons)}  wakeup_ms={spike.wakeup_ms:.3f}  "
                    f"execution_ms={spike.execution_ms:.3f}\n"
                )
                for point_index, (point_name, point_ns) in enumerate(report_points(spike), 1):
                    start_ns = max(0, point_ns - before_ns)
                    end_ns = point_ns + after_ns
                    report.write("\n" + "-" * 96 + "\n")
                    report.write(
                        f"{point_name}={seconds(point_ns)}s  "
                        f"window={seconds(start_ns)}..{seconds(end_ns)}s\n"
                    )
                    report.flush()

                    base = temp_dir / f"rank_{rank:03d}_{point_index}.dat"
                    run_trace_cmd([
                        "trace-cmd", "split", "-i", str(trace), "-o", str(base),
                        seconds(start_ns), seconds(end_ns),
                    ])
                    slices = sorted(path for path in temp_dir.glob(base.name + "*")
                                    if path.is_file())
                    if len(slices) != 1:
                        raise RuntimeError(
                            f"trace-cmd split produced {len(slices)} files for rank {rank}"
                        )
                    command = ["trace-cmd", "report", "-t", "-i", str(slices[0])]
                    if cpu is not None:
                        command.extend(["--cpu", str(cpu)])
                    run_trace_cmd(command, stdout=report)
                    slices[0].unlink()
                print(f"Extracted [{rank}/{len(selected)}] {spike.label}", flush=True)


def main() -> int:
    args = parse_args()
    for path, description in ((args.log, "log"), (args.trace, "trace")):
        if not path.is_file():
            print(f"error: {description} file not found: {path}", file=sys.stderr)
            return 2
    validate_trace(args.trace)
    spikes, record_count = read_spikes(args.log, args.wakeup_ms, args.exec_ms)
    selected = sorted(spikes, key=lambda spike: (-spike.severity_ms, spike.line))[:args.count]
    print(
        f"Parsed {record_count} timing records; found {len(spikes)} candidates; "
        f"selected top {len(selected)}"
    )
    if not selected:
        return 0
    for rank, spike in enumerate(selected, 1):
        print(
            f"[{rank:02d}] {spike.label} reasons={'+'.join(spike.reasons)} "
            f"wakeup={spike.wakeup_ms:.3f}ms execution={spike.execution_ms:.3f}ms"
        )
    if args.dry_run:
        return 0

    output = args.output or args.trace.with_name(
        f"{args.trace.stem}_top{len(selected)}_spikes.txt"
    )
    cpu = None if args.all_cpus else args.cpu
    write_report(args.trace, output, selected, cpu,
                 round(args.before_ms * NS_PER_MS), round(args.after_ms * NS_PER_MS),
                 record_count, len(spikes))
    print(f"Saved one report: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(f"error: trace-cmd failed with status {error.returncode}", file=sys.stderr)
        raise SystemExit(error.returncode)
    except (OSError, RuntimeError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
