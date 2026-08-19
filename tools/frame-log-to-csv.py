# SPDX-FileCopyrightText: 2026 David Edmundson <davidedmundson@kde.org>


#!/usr/bin/env python3

import argparse
import csv
import re
import sys
from collections import OrderedDict
from pathlib import Path
from typing import TextIO


TIMING_LOG = re.compile(r"(?:^|.*\b)LOG FRAME (?P<frame_id>-?\d+),(?P<action>received|pushed|encoded|queued|sent|acknowledged|),(?P<time>-?\d+)$")


def parse_log(log: TextIO) -> OrderedDict[str, dict[str, str]]:
    frames: OrderedDict[str, dict[str, str]] = OrderedDict()
    for line in log:
        line = line.rstrip("\n")
        timing = TIMING_LOG.match(line)
        if timing:
            frame = frames.setdefault(timing["frame_id"], {})
            frame[timing["action"]] = timing["time"]
    return frames

def main() -> None:
    parser = argparse.ArgumentParser(description="Convert PipeWire frame timing logs to CSV.")
    parser.add_argument("log", nargs="?", type=Path, help="stderr log file (defaults to standard input)")
    args = parser.parse_args()

    if args.log:
        with args.log.open(encoding="utf-8", errors="replace") as log:
            frames = parse_log(log)
    else:
        frames = parse_log(sys.stdin)

    writer = csv.DictWriter(sys.stdout, fieldnames=("frame_id", "received", "pushed", "encoded", "queued", "sent", "acknowledged"), lineterminator="\n")
    writer.writeheader()
    for frame_id, values in frames.items():
        if "encoded" in values and "pushed" in values: # filter out only mouse moves
            writer.writerow({"frame_id": frame_id, **values})

if __name__ == "__main__":
    main()
