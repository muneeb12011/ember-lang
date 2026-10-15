#!/usr/bin/env python3
"""Allocation-heavy benchmarks: Ember (generational collector, and with EMBER_GC_FULL=1 the old full-collection
policy) next to equivalent C (malloc/free) and Python programs. Reports wall time (best of --runs), peak RSS, and for
Ember the collector's pause statistics (EMBER_GC_STATS)."""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
EMBC = os.path.join(os.path.dirname(os.path.dirname(HERE)), "embc")
RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3
tmp = tempfile.mkdtemp()

WRAP = ("import resource,subprocess,sys,time,os;t=time.perf_counter();p=subprocess.run(sys.argv[1:],capture_output=True,"
        "env=os.environ);dt=time.perf_counter()-t;r=resource.getrusage(resource.RUSAGE_CHILDREN);"
        "sys.stdout.write(repr((dt,r.ru_maxrss,p.returncode,p.stdout.decode(),p.stderr.decode())))")


def measure(cmd, env=None):
    best = None
    for _ in range(RUNS):
        res = subprocess.run([sys.executable, "-c", WRAP] + cmd, capture_output=True, text=True, env=dict(os.environ, **(env or {})))
        dt, rss, rc, out, err = eval(res.stdout)
        if best is None or dt < best[0]:
            best = (dt, rss, out, err)
    return best


print("| benchmark | implementation | time (s) | peak RSS (MB) | collections | GC pause total / max / mean (ms) |")
print("|---|---|---|---|---|---|")
for name in ["binarytrees", "strbuild", "hashmap"]:
    exe = os.path.join(tmp, name)
    subprocess.run([EMBC, os.path.join(HERE, name + ".em"), "-o", exe], check=True)
    cexe = os.path.join(tmp, name + "_c")
    subprocess.run(["gcc", "-O2", os.path.join(HERE, "c", name + ".c"), "-o", cexe], check=True)
    rows = [("Ember (generational)", [exe], {"EMBER_GC_STATS": "1"}),
            ("Ember (EMBER_GC_FULL=1)", [exe], {"EMBER_GC_STATS": "1", "EMBER_GC_FULL": "1"}),
            ("C, gcc -O2", [cexe], None),
            ("Python 3", [sys.executable, os.path.join(HERE, "py", name + ".py")], None)]
    outs = set()
    for label, cmd, env in rows:
        dt, rss, out, err = measure(cmd, env)
        outs.add(out)
        gc = ""
        coll = ""
        m = re.search(r"gc: (\d+) collections \((\d+) minor, (\d+) major\), pauses: total ([\d.]+) ms, max ([\d.]+) ms, mean ([\d.]+) ms", err)
        if m:
            coll = f"{m.group(1)} ({m.group(2)} minor, {m.group(3)} major)"
            gc = f"{float(m.group(4)):.1f} / {float(m.group(5)):.2f} / {float(m.group(6)):.2f}"
        print(f"| {name} | {label} | {dt:.3f} | {rss / 1024:.1f} | {coll} | {gc} |", flush=True)
    if len(outs) != 1:
        print(f"| {name} | OUTPUTS DIFFER | | | | |")
