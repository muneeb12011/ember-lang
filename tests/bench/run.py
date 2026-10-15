#!/usr/bin/env python3
"""Benchmark runner: compiles every tests/bench/*.em at -O1, runs it --runs times, and reports the best wall time and
the peak RSS (ru_maxrss of the child, in MB). Wall time on a shared VM is noisy (+-30% run to run was observed), so
with --instr it also counts the instructions the program executes under valgrind's cachegrind, which is exact and
repeatable and is what decides whether an optimization is kept. Results are also written to tests/bench/results/<label>.json so that two
runs can be compared:

    python3 tests/bench/run.py --label before
    python3 tests/bench/run.py --label after --compare before
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
EMBC = os.path.join(ROOT, "embc")


def run_one(cmd):
    # a tiny wrapper process runs the program, so its RUSAGE_CHILDREN is exactly the program's peak RSS
    code = ("import resource,subprocess,sys,time;t=time.perf_counter();p=subprocess.run(sys.argv[1:],capture_output=True);"
            "dt=time.perf_counter()-t;r=resource.getrusage(resource.RUSAGE_CHILDREN);"
            "sys.stdout.write(repr((dt,r.ru_maxrss,p.returncode,p.stdout.decode())))")
    res = subprocess.run([sys.executable, "-c", code] + cmd, capture_output=True, text=True)
    return eval(res.stdout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default="current")
    ap.add_argument("--runs", type=int, default=7)
    ap.add_argument("--compare")
    ap.add_argument("--only")
    ap.add_argument("--embc", default=EMBC, help="compiler to use (to measure an older build)")
    ap.add_argument("--instr", action="store_true", help="also count executed instructions with cachegrind")
    ap.add_argument("--flags", default="-O1")
    ap.add_argument("--no-opt", default="", help="passes to turn off while compiling (EMBER_NO_OPT)")
    a = ap.parse_args()
    tmp = tempfile.mkdtemp(prefix="ember-bench-")
    results = {}
    for src in sorted(glob.glob(os.path.join(HERE, "*.em"))):
        name = os.path.basename(src)[:-3]
        if a.only and name not in a.only.split(","):
            continue
        exe = os.path.join(tmp, name)
        env = dict(os.environ, EMBER_NO_OPT=a.no_opt)
        c = subprocess.run([a.embc, src, "-o", exe] + a.flags.split(), capture_output=True, text=True, env=env)
        if c.returncode:
            print(f"{name}: compile failed\n{c.stderr}")
            continue
        best, rss, out = None, 0, None
        for _ in range(a.runs):
            dt, maxrss, rc, stdout = run_one([exe])
            if rc != 0:
                print(f"{name}: exit {rc}")
            best = dt if best is None else min(best, dt)
            rss = max(rss, maxrss)
            if out is not None and stdout != out:
                print(f"{name}: output changed between runs")
            out = stdout
        results[name] = {"time": best, "rss_mb": rss / 1024.0, "out": out}
        if a.instr:
            cg = subprocess.run(["valgrind", "--tool=cachegrind", "--cache-sim=no", "--cachegrind-out-file=/dev/null", exe],
                                capture_output=True, text=True)
            for ln in cg.stderr.splitlines():
                if "I" in ln and "refs:" in ln:
                    results[name]["instr"] = int(ln.split("refs:")[1].strip().replace(",", ""))
    os.makedirs(os.path.join(HERE, "results"), exist_ok=True)
    with open(os.path.join(HERE, "results", a.label + ".json"), "w") as fh:
        json.dump(results, fh, indent=1)
    base = None
    if a.compare:
        with open(os.path.join(HERE, "results", a.compare + ".json")) as fh:
            base = json.load(fh)
    hdr = f"| benchmark | time (s) | peak RSS (MB) |" + (" instructions |" if a.instr else "") + (f" time vs {a.compare} |" if base else "") + \
        (f" instr vs {a.compare} |" if base and a.instr else "")
    print(hdr)
    print("|" + "---|" * (hdr.count("|") - 1))
    for n, r in results.items():
        line = f"| {n} | {r['time']:.3f} | {r['rss_mb']:.1f} |"
        if a.instr:
            line += f" {r.get('instr', 0) / 1e6:.1f}M |"
        if base and n in base:
            b = base[n]
            same = "" if b["out"] == r["out"] else " (OUTPUT DIFFERS)"
            line += f" {b['time'] / r['time']:.2f}x{same} |"
            if a.instr:
                line += f" {b.get('instr', 0) / max(r.get('instr', 1), 1):.3f}x |"
        print(line)


main()
