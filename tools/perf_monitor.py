# /// script
# requires-python = ">=3.10"
# dependencies = ["psutil"]
# ///
"""Watches what Composer costs in a running host, live, and keeps a record.

    uv run tools/perf_monitor.py --host mscore --label "binaural, editor open"

It shows a table that updates in place once a second: the CPU of four groups, in % of one
core, now and as mean / median / 95th percentile / max since it started.

    host     the DAW process (for Dorico use --host VSTAudioEngine: its audio and plugin
             windows live there). This is the host's own work plus Composer's audio.
    page     the web processes drawing Composer's editor. 0 when no editor is open.
    gpu      the processes compositing that page.
    tracker  Agogica Tracker with its own window's web processes, if it runs.

Ctrl+C (or --seconds N) stops it and appends one line with those figures to --csv
(default build/perf.csv), so runs can be compared later. Piped into a file it prints
one line per reading instead.

It cannot split the host's number into "the host" and "Composer's audio": for that, the
plugin's own load readout (Alt+click the version in the editor footer) and
`build/release/ComposerTests "[perf]" -s` are the tools. Compare runs of the same project
with and without the thing you changed, each for a minute or more: single 30 s readings
of the same setup differed by several points.

macOS and Windows. The Windows half is written from the WebView2 process model and has
not been run on a PC yet.
"""

import argparse
import csv
import ctypes
import datetime
import statistics
import sys
import time
from pathlib import Path

import psutil

GROUPS = ["host", "page", "gpu", "tracker"]
TRACKER_NAMES = ("agogica tracker", "composertracker")


def responsible_pid_macos():
    """macOS runs a web view's processes as separate services with no parent; the system
    still knows which app each one works for."""
    f = ctypes.CDLL(None).responsibility_get_pid_responsible_for_pid
    f.restype = ctypes.c_int
    f.argtypes = [ctypes.c_int]
    return f


def is_tracker(process):
    try:
        return any(n in process.name().lower() for n in TRACKER_NAMES)
    except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
        return False


def group_of(process, host_pids, tracker_pids, responsible):
    """Which group a process belongs to, or None. A web view's processes count for the app
    they work for: the host's are its editor page, the Tracker's are part of the Tracker."""
    try:
        if process.pid in host_pids:
            return "host"
        if process.pid in tracker_pids:
            return "tracker"
        name = process.name().lower()
        if sys.platform == "darwin":
            if not name.startswith("com.apple.webkit."):
                return None
            owner = responsible(process.pid)
            kind = "page" if "webcontent" in name else "gpu" if "gpu" in name else None
        elif name == "msedgewebview2.exe":
            ancestors = {parent.pid for parent in process.parents()}
            owner = next(iter(ancestors & (host_pids | tracker_pids)), None)
            command = " ".join(process.cmdline())
            kind = "page" if "--type=renderer" in command else "gpu" if "--type=gpu-process" in command else None
        else:
            return None
        if owner in tracker_pids:
            return "tracker"
        return kind if owner in host_pids else None
    except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
        return None


def summary(samples):
    """mean, median, 95th percentile and max of a list of readings."""
    if not samples:
        return {"mean": 0.0, "median": 0.0, "p95": 0.0, "max": 0.0}
    ordered = sorted(samples)
    return {
        "mean": statistics.fmean(ordered),
        "median": statistics.median(ordered),
        "p95": ordered[min(len(ordered) - 1, round(0.95 * (len(ordered) - 1)))],
        "max": ordered[-1],
    }


COLUMNS = ("now", "mean", "median", "p95", "max")


def table(title, reading, samples, running):
    """The whole display as lines: a title, a header and one row per group."""
    lines = [title, "", f"{'':<9}" + "".join(f"{c:>9}" for c in COLUMNS) + "    % of one core", "-" * 54]
    rows = [(g, reading.get(g, 0.0), samples[g], g in running) for g in GROUPS]
    rows.append(("sum", sum(reading.values()), [sum(r) for r in zip(*(samples[g] for g in GROUPS))], True))
    for name, now, history, alive in rows:
        if name == "sum":
            lines.append("-" * 54)
        if not alive and not any(history):
            lines.append(f"{name:<9}" + f"{'-':>9}" * len(COLUMNS) + "    not running")
            continue
        stats = summary(history)
        lines.append(f"{name:<9}{now:9.1f}" + "".join(f"{stats[k]:9.1f}" for k in COLUMNS[1:]))
    return lines


def self_test():
    s = summary([1.0, 2.0, 3.0, 4.0, 100.0])
    assert s["mean"] == 22.0 and s["median"] == 3.0 and s["max"] == 100.0 and s["p95"] == 100.0
    assert summary([])["mean"] == 0.0
    assert summary([5.0]) == {"mean": 5.0, "median": 5.0, "p95": 5.0, "max": 5.0}
    lines = table("t", {"host": 2.0, "page": 1.0, "gpu": 0.0, "tracker": 0.0}, {"host": [4.0, 2.0], "page": [1.0, 1.0], "gpu": [0.0, 0.0], "tracker": [0.0, 0.0]}, {"host", "page", "gpu"})
    assert lines[4].split() == ["host", "2.0", "3.0", "3.0", "4.0", "4.0"]
    assert lines[7].endswith("not running") and lines[9].split()[:3] == ["sum", "3.0", "4.0"]
    print("ok")


def main():
    parser = argparse.ArgumentParser(description="Live CPU of a host running Composer, its editor page and the Tracker.")
    parser.add_argument("--host", action="append", help="part of the host's process name, e.g. mscore, VSTAudioEngine, Live, Composer (repeatable)")
    parser.add_argument("--label", default="", help="what this run is, for the CSV")
    parser.add_argument("--seconds", type=float, help="stop after this long (default: until Ctrl+C)")
    parser.add_argument("--interval", type=float, default=1.0, help="seconds between readings")
    parser.add_argument("--csv", default="build/perf.csv", help="summary is appended here ('' for none)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.host:
        parser.error("--host is needed, e.g. --host mscore")

    wanted = [h.lower() for h in args.host]
    host_pids = {p.pid for p in psutil.process_iter(["name"]) if any(w in (p.info["name"] or "").lower() for w in wanted)}
    if not host_pids:
        sys.exit(f"no running process has {' or '.join(args.host)} in its name")

    responsible = responsible_pid_macos() if sys.platform == "darwin" else None
    watched = {}  # pid -> (Process, group); psutil measures CPU between two calls on the same object
    samples = {g: [] for g in GROUPS}
    reading, running = {}, set()

    # On a terminal the table is redrawn in place, so the header stays where it is;
    # into a file or a pipe each reading is one line.
    live = sys.stdout.isatty()
    if live and sys.platform == "win32":
        import os

        os.system("")  # switches the Windows console to understand cursor movement
    drawn_lines = 0

    def show(final=False):
        nonlocal drawn_lines
        elapsed = int(time.monotonic() - started)
        title = f"Composer in {' + '.join(args.host)}" + (f" - {args.label}" if args.label else "")
        title += f"   {elapsed // 60:02d}:{elapsed % 60:02d}, {len(samples['host'])} readings" + ("" if final else "   (Ctrl+C to stop)")
        lines = table(title, reading, samples, running)
        if live:
            sys.stdout.write(f"\x1b[{drawn_lines}A" if drawn_lines else "")
            sys.stdout.write("".join(f"\x1b[2K{line}\n" for line in lines))
            drawn_lines = len(lines)
        elif final:
            sys.stdout.write("\n" + "\n".join(lines) + "\n")
        else:
            clock = datetime.datetime.now().strftime("%H:%M:%S")
            if len(samples["host"]) == 1:
                print(f"{'time':>8}  " + "  ".join(f"{g:>8}" for g in GROUPS) + f"  {'sum':>8}   (% of one core)")
            print(f"{clock:>8}  " + "  ".join(f"{reading[g]:8.1f}" for g in GROUPS) + f"  {sum(reading.values()):8.1f}")
        sys.stdout.flush()

    started = time.monotonic()
    try:
        while args.seconds is None or time.monotonic() - started < args.seconds:
            # Editors and the Tracker open and close while we watch, so the process list is read every time.
            processes = list(psutil.process_iter())
            tracker_pids = {p.pid for p in processes if is_tracker(p)}
            for process in processes:
                if process.pid not in watched and (group := group_of(process, host_pids, tracker_pids, responsible)):
                    watched[process.pid] = (process, group)
                    process.cpu_percent(None)  # the first call only starts the measurement

            time.sleep(args.interval)

            reading = {g: 0.0 for g in GROUPS}
            for pid, (process, group) in list(watched.items()):
                try:
                    reading[group] += process.cpu_percent(None)
                except (psutil.NoSuchProcess, psutil.AccessDenied, psutil.ZombieProcess):
                    del watched[pid]
            running = {group for _, group in watched.values()}
            if "host" not in running:
                print("the host has quit")
                break

            for g in GROUPS:
                samples[g].append(reading[g])
            show()
    except KeyboardInterrupt:
        pass

    if not samples["host"]:
        return
    show(final=True)
    stats = {g: summary(samples[g]) for g in GROUPS}

    if args.csv:
        path = Path(args.csv)
        path.parent.mkdir(parents=True, exist_ok=True)
        is_new = not path.exists()
        with path.open("a", newline="") as f:
            writer = csv.writer(f)
            if is_new:
                writer.writerow(["when", "label", "host", "readings", "interval_s"] + [f"{g}_{k}" for g in GROUPS for k in ("mean", "median", "p95", "max")])
            writer.writerow(
                [datetime.datetime.now().isoformat(timespec="seconds"), args.label, "+".join(args.host), len(samples["host"]), args.interval]
                + [f"{stats[g][k]:.1f}" for g in GROUPS for k in ("mean", "median", "p95", "max")]
            )
        print(f"\nappended to {path}")


if __name__ == "__main__":
    main()
