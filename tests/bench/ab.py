#!/usr/bin/env python3
"""Paired timing of one benchmark under two compiler settings: the two binaries run alternately (so slow phases of a
noisy machine hit both), and the minimum and median of each are reported.

    python3 tests/bench/ab.py sort strength 15      # EMBER_NO_OPT=strength vs everything on
"""
import os
import statistics
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
EMBC = os.path.join(os.path.dirname(os.path.dirname(HERE)), "embc")
name, off, n = sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 15
tmp = tempfile.mkdtemp()
exes = {}
for label, env in (("without " + off, off), ("with all passes", "")):
    exe = os.path.join(tmp, label.replace(" ", "_"))
    subprocess.run([EMBC, os.path.join(HERE, name + ".em"), "-o", exe], check=True, env=dict(os.environ, EMBER_NO_OPT=env))
    exes[label] = exe
times = {k: [] for k in exes}
for _ in range(n):
    for k, exe in exes.items():
        t = time.perf_counter()
        subprocess.run([exe], capture_output=True)
        times[k].append(time.perf_counter() - t)
for k, v in times.items():
    print(f"{name:10} {k:22} min {min(v):.3f}s  median {statistics.median(v):.3f}s")
