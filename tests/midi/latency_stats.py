"""Summarize a matched logic-analyser export: received_us,applied_us.

Use isolated Note Ons changing NOTE_SET, after the drivers have woken. Pair
edges by decoded UART sequence/notes, not by row position in arbitrary music.
The timestamps must come from the SAME analyser timebase.
"""
import argparse
import csv
import json
import math
import statistics

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("csv_file")
args = parser.parse_args()
with open(args.csv_file, newline="", encoding="utf-8-sig") as stream:
    values = [float(row["applied_us"]) - float(row["received_us"]) for row in csv.DictReader(stream)]
if not values or any(not math.isfinite(v) or v < 0 for v in values):
    raise SystemExit("Expected nonempty, finite, nonnegative matched latency samples")
print(json.dumps({"samples": len(values), "min_us": min(values),
                  "avg_us": statistics.mean(values), "max_us": max(values),
                  "jitter_peak_to_peak_us": max(values) - min(values),
                  "stddev_us": statistics.pstdev(values)}, indent=2))
