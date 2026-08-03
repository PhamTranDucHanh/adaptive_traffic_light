#!/usr/bin/env python3

import re
import argparse
import pandas as pd
import numpy as np

pattern = re.compile(
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


def summarize(series):
    arr = np.asarray(series)

    return {
        "Count": len(arr),
        "Min": np.min(arr),
        "Avg": np.mean(arr),
        "P50": np.percentile(arr, 50),
        "P90": np.percentile(arr, 90),
        "P95": np.percentile(arr, 95),
        "P99": np.percentile(arr, 99),
        "Max": np.max(arr),
        "Std": np.std(arr),
    }


def print_table(name, df, column):
    print(f"\n{name}")
    print("-" * 110)

    header = (
        f"{'Lane':<8}"
        f"{'Count':>8}"
        f"{'Min':>12}"
        f"{'Avg':>12}"
        f"{'P50':>12}"
        f"{'P90':>12}"
        f"{'P95':>12}"
        f"{'P99':>12}"
        f"{'Max':>12}"
        f"{'Std':>12}"
    )

    print(header)

    for lane in sorted(df["LaneId"].unique()):
        stat = summarize(df[df["LaneId"] == lane][column])

        print(
            f"{lane:<8}"
            f"{stat['Count']:>8}"
            f"{stat['Min']:>12.3f}"
            f"{stat['Avg']:>12.3f}"
            f"{stat['P50']:>12.3f}"
            f"{stat['P90']:>12.3f}"
            f"{stat['P95']:>12.3f}"
            f"{stat['P99']:>12.3f}"
            f"{stat['Max']:>12.3f}"
            f"{stat['Std']:>12.3f}"
        )


def main():

    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    args = parser.parse_args()

    rows = []

    with open(args.log) as f:
        for line in f:
            m = pattern.search(line)
            if not m:
                continue

            lane = int(m.group(1))
            frame = int(m.group(2))

            expected = int(m.group(3))
            begin = int(m.group(4))
            acquire_begin = int(m.group(5))
            acquire_end = int(m.group(6))
            grab_begin = int(m.group(7))
            grab_end = int(m.group(8))
            decode_begin = int(m.group(9))
            decode_end = int(m.group(10))
            end = int(m.group(11))

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

    df = pd.DataFrame(rows)

    print("\n===================================================")
    print("Overall")
    print("===================================================")

    for metric in [
        "WakeupLatency",
        "AcquireTime",
        "GrabTime",
        "DecodeTime",
        "PublishTime",
        "Execution",
    ]:
        stat = summarize(df[metric])

        print(f"\n{metric}")
        for k, v in stat.items():
            if k == "Count":
                print(f"{k:10}: {v}")
            else:
                print(f"{k:10}: {v:.3f} ms")

    print("\n===================================================")
    print("Per Lane")
    print("===================================================")

    print_table("Wakeup Latency (ms)", df, "WakeupLatency")
    print_table("Acquire Time (ms)", df, "AcquireTime")
    print_table("Grab Time (ms)", df, "GrabTime")
    print_table("Decode Time (ms)", df, "DecodeTime")
    print_table("Publish Time (ms)", df, "PublishTime")
    print_table("Execution Time (ms)", df, "Execution")

    print("\n===================================================")
    print("Largest Execution")
    print("===================================================")
    print(
        df.sort_values("Execution", ascending=False)[
            [
                "LaneId",
                "FrameId",
                "WakeupLatency",
                "GrabTime",
                "DecodeTime",
                "Execution",
            ]
        ].head(20)
    )

    print("\n===================================================")
    print("Largest Wakeup Latency")
    print("===================================================")
    print(
        df.sort_values("WakeupLatency", ascending=False)[
            [
                "LaneId",
                "FrameId",
                "WakeupLatency",
                "GrabTime",
                "DecodeTime",
                "Execution",
            ]
        ].head(20)
    )

    print("\n===================================================")
    print("Largest Grab Time")
    print("===================================================")
    print(
        df.sort_values("GrabTime", ascending=False)[
            [
                "LaneId",
                "FrameId",
                "WakeupLatency",
                "GrabTime",
                "DecodeTime",
                "Execution",
            ]
        ].head(20)
    )

    print("\n===================================================")
    print("Wakeup > 20 ms")
    print("===================================================")
    print(
        df[df["WakeupLatency"] > 20][
            [
                "LaneId",
                "FrameId",
                "WakeupLatency",
                "GrabTime",
                "DecodeTime",
                "Execution",
            ]
        ].sort_values("WakeupLatency", ascending=False)
    )

    print("\n===================================================")
    print("Grab > 20 ms")
    print("===================================================")
    print(
        df[df["GrabTime"] > 20][
            [
                "LaneId",
                "FrameId",
                "WakeupLatency",
                "GrabTime",
                "DecodeTime",
                "Execution",
            ]
        ].sort_values("GrabTime", ascending=False)
    )


if __name__ == "__main__":
    main()
