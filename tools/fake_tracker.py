"""Stands in for the Agogica Tracker: writes a head pose into the shared head-tracking file,
sweeping the head slowly left and right (or holding one angle).

    python3 tools/fake_tracker.py            # sweep ±60° every 10 s
    python3 tools/fake_tracker.py --yaw 90   # hold the head turned right

Protocol: Source/stage/HeadTracking.h (64 bytes, seqlock, yaw clockwise = head turned right).
The file is created once at its final size by an atomic rename and then only written in place.
"""

import argparse
import math
import mmap
import os
import pathlib
import struct
import sys
import time

SIZE = 64
# magic[4], version, sequence, flags (u32), yaw, pitch, roll (f32), 4 bytes padding, timestampMs (u64)
OFFSETS = {"version": 4, "sequence": 8, "flags": 12, "yaw": 16, "pitch": 20, "roll": 24, "timestamp": 32}


def default_file() -> pathlib.Path:
    if override := os.environ.get("AGOGICA_HEAD_TRACKING_FILE"):
        return pathlib.Path(override)
    if sys.platform == "win32":
        return pathlib.Path(os.environ["LOCALAPPDATA"]) / "Agogica" / "head-tracking"
    return pathlib.Path.home() / "Library" / "Application Support" / "Agogica" / "head-tracking"


def open_record(path: pathlib.Path) -> mmap.mmap:
    if not path.exists() or path.stat().st_size != SIZE:
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name(path.name + ".new")
        temp.write_bytes(bytes(SIZE))
        os.replace(temp, path)  # atomic rename
    f = open(path, "r+b")
    m = mmap.mmap(f.fileno(), SIZE)
    struct.pack_into("<I", m, OFFSETS["version"], 1)
    m[0:4] = b"AGHT"
    return m


def write(m: mmap.mmap, yaw: float):
    seq = struct.unpack_from("<I", m, OFFSETS["sequence"])[0]
    struct.pack_into("<I", m, OFFSETS["sequence"], seq + 1)  # odd: writing
    struct.pack_into("<fff", m, OFFSETS["yaw"], yaw, 0.0, 0.0)
    struct.pack_into("<Q", m, OFFSETS["timestamp"], int(time.time() * 1000))
    struct.pack_into("<I", m, OFFSETS["flags"], 1)
    struct.pack_into("<I", m, OFFSETS["sequence"], seq + 2)  # even: done


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--yaw", type=float, help="hold this angle instead of sweeping")
    args = parser.parse_args()

    path = default_file()
    m = open_record(path)
    print(f"writing {path} (Ctrl+C to stop)")
    start = time.time()
    while True:
        yaw = args.yaw if args.yaw is not None else 60.0 * math.sin(2 * math.pi * (time.time() - start) / 10.0)
        write(m, yaw)
        time.sleep(1 / 60)


if __name__ == "__main__":
    main()
