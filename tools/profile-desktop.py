"""Compare the instrumented native ARGUS desktop on Windows."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import sqlite3
import subprocess
from tempfile import TemporaryDirectory
from threading import Event, Thread


class MemoryCounters(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD),
        *((name, ctypes.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
            "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage",
        )),
    ]


def measure(executable, mode, directory):
    system = Path(os.environ["SystemRoot"])
    environment = {**os.environ, "ARGUS_PROFILE_DESKTOP": "1",
                   "PATH": os.pathsep.join((str(system / "System32"), str(system)))}
    api = ctypes.WinDLL("psapi").GetProcessMemoryInfo
    api.argtypes = [wintypes.HANDLE, ctypes.POINTER(MemoryCounters), wintypes.DWORD]
    api.restype = wintypes.BOOL
    done = Event()
    peak = 0
    with subprocess.Popen([executable, mode, directory], env=environment, cwd=directory,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as process:
        def sample_memory():
            nonlocal peak
            counters = MemoryCounters()
            counters.cb = ctypes.sizeof(counters)
            while not done.is_set():
                if api(int(process._handle), ctypes.byref(counters), counters.cb):
                    peak = max(peak, counters.PeakWorkingSetSize)
                done.wait(0.025)

        sampler = Thread(target=sample_memory)
        sampler.start()
        try:
            output, diagnostics = process.communicate(timeout=55)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        finally:
            done.set()
            sampler.join()
    if process.returncode or not peak:
        raise RuntimeError(f"{mode} failed: {output}\n{diagnostics}")
    reports = [json.loads(line)["desktopMetrics"] for line in diagnostics.splitlines()
               if line.startswith('{"desktopMetrics":')]
    if len(reports) != 1:
        raise RuntimeError(f"{mode} did not emit its benchmark: {diagnostics}")
    return {**reports[0], "peakWorkingSetBytes": peak}


def expand_report(directory):
    products = [[f"999-01-{index:04}", f"Fictional stockroom product {index:03}", 0, 2, 0, []]
                for index in range(1, 201)]
    report = json.dumps({"version": 2, "products": products, "unidentified": [], "unresolved": []})
    connection = sqlite3.connect(directory / "missions.db")
    try:
        with connection:
            updated = connection.execute(
                "UPDATE shelf_audit SET report=? WHERE location=?",
                (report, "A10J05"),
            )
            if updated.rowcount != 1:
                raise RuntimeError("Expected one A10J05 audit in the disposable fixture")
    finally:
        connection.close()


def run(executable):
    with TemporaryDirectory(prefix="argus-profile-") as root:
        root = Path(root)
        fleet = root / "fleet"
        traffic = root / "traffic"
        fleet.mkdir()
        traffic.mkdir()
        runs = {
            "parallel": measure(executable, "--ui-fleet-smoke", fleet),
            "contention": measure(executable, "--ui-fleet-traffic", traffic),
            "review": measure(executable, "--ui-fleet-reopen", fleet),
        }
        expand_report(fleet)
        runs["longReport"] = measure(executable, "--ui-fleet-profile-report", fleet)
        return runs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--pairs", type=int, default=3, choices=range(1, 11))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    builds = {"baseline": args.baseline.resolve(strict=True)}
    if args.candidate:
        builds["candidate"] = args.candidate.resolve(strict=True)
    results = {label: [] for label in builds}
    for index in range(args.pairs):
        order = list(builds) if index % 2 == 0 else list(reversed(builds))
        for label in order:
            results[label].append(run(builds[label]))
            args.output.write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
            print(f"{label} trial {index + 1} complete", flush=True)


if __name__ == "__main__":
    main()
