#!/usr/bin/env python3

import re
import argparse
import pandas as pd
import numpy as np

pipe_pattern = re.compile(
    r"PIPE.*CycleId=\s*(\d+).*ExpectedWakeup=\s*(\d+).*Begin=\s*(\d+).*End=\s*(\d+)"
)

view_pattern = re.compile(
    r"VIEW.*CycleId=\s*(\d+).*ExpectedWakeup=\s*(\d+).*Begin=\s*(\d+).*End=\s*(\d+)"
)

NS_TO_MS = 1e6


def summarize(series):
    arr = np.asarray(series)
    if len(arr) == 0:
        return {}

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


def print_tables(df):
    if df.empty:
        return

    print("\n===================================================")
    print("Largest execution")
    print("===================================================")
    print(
        df.sort_values("Execution", ascending=False)[
            ["CycleId", "WakeupLatency", "Execution", "Period"]
        ].head(20)
    )

    print("\n===================================================")
    print("Largest wakeup latency")
    print("===================================================")
    print(
        df.sort_values("WakeupLatency", ascending=False)[
            ["CycleId", "WakeupLatency", "Execution", "Period"]
        ].head(20)
    )

    print("\n===================================================")
    print("Wakeup > 20 ms")
    print("===================================================")
    print(
        df[df["WakeupLatency"] > 20][
            ["CycleId", "WakeupLatency", "Execution", "Period"]
        ].sort_values("WakeupLatency", ascending=False)
    )

    print("\n===================================================")
    print("Execution > 20 ms")
    print("===================================================")
    print(
        df[df["Execution"] > 20][
            ["CycleId", "WakeupLatency", "Execution", "Period"]
        ].sort_values("Execution", ascending=False)
    )


def analyze_component(name, df):
    print("\n===================================================")
    print(name)
    print("===================================================")

    if df.empty:
        print("No data")
        return

    for metric, title in [
        ("WakeupLatency", "Wakeup Latency (ms)"),
        ("Execution", "Execution Time (ms)"),
        ("Period", "Period (ms)"),
    ]:
        stat = summarize(df[metric].dropna())

        print(f"\n{title}\n")
        for k, v in stat.items():
            if k == "Count":
                print(f"{k}: {v}")
            else:
                print(f"{k}: {v:.3f}")

    print_tables(df)


def process_rows(rows):
    df = pd.DataFrame(rows)
    if df.empty:
        return df

    df["WakeupLatency"] = (df["Begin"] - df["ExpectedWakeup"]) / NS_TO_MS
    df["Execution"] = (df["End"] - df["Begin"]) / NS_TO_MS
    df["Period"] = df["Begin"].diff() / NS_TO_MS

    return df


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log")
    args = parser.parse_args()

    pipe_rows = []
    view_rows = []

    with open(args.log) as f:
        for line in f:
            if "PIPE" in line:
                m = pipe_pattern.search(line)
                if m:
                    pipe_rows.append(
                        {
                            "CycleId": int(m.group(1)),
                            "ExpectedWakeup": int(m.group(2)),
                            "Begin": int(m.group(3)),
                            "End": int(m.group(4)),
                        }
                    )
            elif "VIEW" in line:
                m = view_pattern.search(line)
                if m:
                    view_rows.append(
                        {
                            "CycleId": int(m.group(1)),
                            "ExpectedWakeup": int(m.group(2)),
                            "Begin": int(m.group(3)),
                            "End": int(m.group(4)),
                        }
                    )

    pipe_df = process_rows(pipe_rows)
    view_df = process_rows(view_rows)

    analyze_component("Pipeline", pipe_df)
    analyze_component("Viewer", view_df)


if __name__ == "__main__":
    main()
