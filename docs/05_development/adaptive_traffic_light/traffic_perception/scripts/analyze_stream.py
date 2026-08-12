#!/usr/bin/env python3

import argparse
import math
import re
from collections import defaultdict


PATTERN = re.compile(
    r"LaneId=\s*(\d+)\s+"
    r"FrameId=\s*(\d+)\s+"
    r"ExpectedWakeup=\s*(\d+)\s+"
    r"Begin=\s*(\d+)\s+"
    r"AcquireBegin=\s*(\d+)\s+"
    r"AcquireEnd=\s*(\d+)\s+"
    r"GrabBegin=\s*(\d+)\s+"
    r"GrabEnd=\s*(\d+)\s+"
    r"DecodeBegin=\s*(\d+)\s+"
    r"DecodeEnd=\s*(\d+)\s+"
    r"End=\s*(\d+)"
)

NS_TO_MS = 1e6
METRICS = (
    "WakeupLatency",
    "AcquireTime",
    "GrabTime",
    "DecodeTime",
    "PublishTime",
    "Execution",
)


def percentile(values, percent):
    ordered = sorted(values)
    position = (len(ordered) - 1) * percent / 100.0
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction


def summarize(values):
    average = sum(values) / len(values)
    variance = sum((value - average) ** 2 for value in values) / len(values)
    return {
        "Count": len(values),
        "Min": min(values),
        "Avg": average,
        "P50": percentile(values, 50),
        "P90": percentile(values, 90),
        "P95": percentile(values, 95),
        "P99": percentile(values, 99),
        "Max": max(values),
        "Std": math.sqrt(variance),
    }


def print_table(name, rows, metric):
    print(f"\n{name}")
    print("-" * 110)
    print(
        f"{'Lane':<8}{'Count':>8}{'Min':>12}{'Avg':>12}{'P50':>12}"
        f"{'P90':>12}{'P95':>12}{'P99':>12}{'Max':>12}{'Std':>12}"
    )

    lanes = defaultdict(list)
    for row in rows:
        lanes[row["LaneId"]].append(row[metric])

    for lane in sorted(lanes):
        stat = summarize(lanes[lane])
        print(
            f"{lane:<8}{stat['Count']:>8}{stat['Min']:>12.3f}"
            f"{stat['Avg']:>12.3f}{stat['P50']:>12.3f}"
            f"{stat['P90']:>12.3f}{stat['P95']:>12.3f}"
            f"{stat['P99']:>12.3f}{stat['Max']:>12.3f}"
            f"{stat['Std']:>12.3f}"
        )


def print_rows(title, rows):
    print(f"\n{'=' * 51}\n{title}\n{'=' * 51}")
    print(
        f"{'Lane':>6} {'Frame':>8} {'Wakeup(ms)':>12} "
        f"{'Grab(ms)':>10} {'Decode(ms)':>12} {'Execution(ms)':>14}"
    )
    for row in rows:
        print(
            f"{row['LaneId']:>6} {row['FrameId']:>8} "
            f"{row['WakeupLatency']:>12.3f} {row['GrabTime']:>10.3f} "
            f"{row['DecodeTime']:>12.3f} {row['Execution']:>14.3f}"
        )


def parse_rows(log_path):
    rows = []
    with open(log_path, encoding="utf-8", errors="replace") as log_file:
        for line in log_file:
            match = PATTERN.search(line)
            if not match:
                continue

            values = [int(value) for value in match.groups()]
            lane, frame = values[:2]
            (expected, begin, acquire_begin, acquire_end, grab_begin, grab_end,
             decode_begin, decode_end, end) = values[2:]
            rows.append(
                {
                    "LaneId": lane,
                    "FrameId": frame,
                    "WakeupLatency": (begin - expected) / NS_TO_MS,
                    "AcquireTime": (acquire_end - acquire_begin) / NS_TO_MS,
                    "GrabTime": (grab_end - grab_begin) / NS_TO_MS,
                    "DecodeTime": (decode_end - decode_begin) / NS_TO_MS,
                    "PublishTime": (end - decode_end) / NS_TO_MS,
                    "Execution": (end - begin) / NS_TO_MS,
                }
            )
    return rows


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    args = parser.parse_args()
    rows = parse_rows(args.log)

    if not rows:
        parser.error(f"no stream analytics records found in {args.log}")

    print("\n===================================================")
    print("Overall")
    print("===================================================")
    for metric in METRICS:
        stat = summarize([row[metric] for row in rows])
        print(f"\n{metric}")
        for name, value in stat.items():
            if name == "Count":
                print(f"{name:10}: {value}")
            else:
                print(f"{name:10}: {value:.3f} ms")

    print("\n===================================================")
    print("Per Lane")
    print("===================================================")
    for metric in METRICS:
        print_table(f"{metric} (ms)", rows, metric)

    print_rows(
        "Largest Execution",
        sorted(rows, key=lambda row: row["Execution"], reverse=True)[:20],
    )
    print_rows(
        "Largest Wakeup Latency",
        sorted(rows, key=lambda row: row["WakeupLatency"], reverse=True)[:20],
    )
    print_rows(
        "Largest Grab Time",
        sorted(rows, key=lambda row: row["GrabTime"], reverse=True)[:20],
    )
    print_rows(
        "Wakeup > 20 ms",
        sorted(
            (row for row in rows if row["WakeupLatency"] > 20),
            key=lambda row: row["WakeupLatency"],
            reverse=True,
        ),
    )
    print_rows(
        "Grab > 20 ms",
        sorted(
            (row for row in rows if row["GrabTime"] > 20),
            key=lambda row: row["GrabTime"],
            reverse=True,
        ),
    )


if __name__ == "__main__":
    main()
