#!/usr/bin/env python3
"""Ember test-suite.

For every program in tests/programs the expected output comes from tests/reference.py (independent Python).
Each program is run four ways - IR interpreter and native x86-64, each at -O0 and -O1 - and every way must
produce exactly the expected stdout and exit code. When node and clang are installed, the WebAssembly target adds two
more ways (wasm at -O0 and -O1).

Also checks: compile errors (tests/errors), that provably-safe loops lose their bounds checks and unsafe
ones keep them, and that -O1 really shrinks the IR.
"""
import glob
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMBC = os.path.join(ROOT, "embc")
PROG = os.path.join(ROOT, "tests", "programs")
ERRS = os.path.join(ROOT, "tests", "errors")
TMP = tempfile.mkdtemp(prefix="ember-")

passed = failed = 0


def check(ok, label, detail=""):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
        print(f"FAIL  {label}\n      {detail}")


def run(cmd, stdin=None, timeout=120, env=None):
    return subprocess.run(cmd, input=stdin, capture_output=True, text=True, timeout=timeout, env=env)


def read(path, default=""):
    return open(path).read() if os.path.exists(path) else default


NATIVE_ONLY = "--native-only" in sys.argv   # used with EMBER_GC_STRESS=1: collect on every allocation
MODES = [("interp", "-O0"), ("interp", "-O1"), ("native", "-O0"), ("native", "-O1")]
# the WebAssembly target joins as two more paths when node and clang are available (--no-wasm leaves it out)
WASM = shutil.which("node") and shutil.which("clang") and shutil.which("wasm-ld") and "--no-wasm" not in sys.argv
if WASM:
    MODES += [("wasm", "-O0"), ("wasm", "-O1")]
if NATIVE_ONLY:
    MODES = [m for m in MODES if m[0] == "native"]

for src in sorted(glob.glob(os.path.join(PROG, "*.em"))):
    name = os.path.basename(src)[:-3]
    want_out = read(src[:-3] + ".out")
    want_code = int(read(src[:-3] + ".code", "0").strip() or 0)
    stdin = read(src[:-3] + ".in") or None
    env = dict(os.environ)  # name.env: KEY=VALUE lines for the native runs (e.g. a heap limit)
    for ln in read(src[:-3] + ".env").splitlines():
        if "=" in ln:
            env[ln.split("=", 1)[0].strip()] = ln.split("=", 1)[1].strip()
    if "EMBER_GC_STRESS" in env and read(src[:-3] + ".stress"):  # name.stress: a gentler stress interval for big programs
        env["EMBER_GC_STRESS"] = read(src[:-3] + ".stress").strip()
    for kind, lvl in MODES:
        label = f"{name} [{kind} {lvl}]"
        if kind == "interp":
            r = run([EMBC, src, "--interp", lvl], stdin, env=env)
        elif kind == "wasm":
            js = os.path.join(TMP, f"{name}{lvl}.js")
            c = run([EMBC, src, lvl, "--target", "wasm", "-o", js])
            if c.returncode != 0:
                check(False, label, "compile failed: " + c.stderr[:300])
                continue
            r = run(["node", js], stdin, env=env)
        else:
            exe = os.path.join(TMP, f"{name}{lvl}")
            c = run([EMBC, src, lvl, "-o", exe])
            if c.returncode != 0:
                check(False, label, "compile failed: " + c.stderr[:300])
                continue
            r = run([exe], stdin, env=env)
        ok = r.stdout == want_out and r.returncode == want_code
        if want_code == 1:
            ok = ok and "runtime error" in r.stderr
        want_err = read(src[:-3] + ".err").strip()  # name.err: text the error message must contain
        if want_err:
            ok = ok and want_err in r.stderr
        check(ok, label, f"exit {r.returncode} (want {want_code})\n      got:  {r.stdout[:120]!r}\n      want: {want_out[:120]!r}\n      stderr: {r.stderr[:160]!r}")

for src in sorted(glob.glob(os.path.join(ERRS, "*.em"))):
    name = os.path.basename(src)[:-3]
    expect = read(src).splitlines()[0].replace("// expect:", "").strip()
    r = run([EMBC, src, "--interp"])
    check(r.returncode == 1 and expect in r.stderr, f"error test {name}", f"want {expect!r} in stderr, got: {r.stderr[:200]!r}")

# bounds-check elimination: proven in safe_loop, kept when the array variable is mutable or the bound differs
ir = run([EMBC, os.path.join(PROG, "safe_loop.em"), "--emit-ir"]).stdout
check(ir.count("unchecked") == 2, "bounds proof removes checks in `for i in 0..len(a)`", f"unchecked count {ir.count('unchecked')}")
for label, body in [
    ("mutable array variable keeps checks", "let mut a = new_array(4); for i in 0..len(a) { a[i] = i; } print(a[3]);"),
    ("non-length bound keeps checks", "let a = new_array(4); for i in 0..5 { a[i] = i; } print(a[3]);"),
    ("negative start keeps checks", "let a = new_array(4); for i in 0 - 1..len(a) { a[i] = i; } print(a[3]);"),
    ("different index keeps checks", "let a = new_array(4); for i in 0..len(a) { a[i + 1] = i; } print(a[3]);"),
]:
    p = os.path.join(TMP, "b.em")
    open(p, "w").write("fn main() -> int { " + body + " return 0; }\n")
    out = run([EMBC, p, "--emit-ir"]).stdout
    check("unchecked" not in out, label, out)

# range-based bounds-check elimination (optimizer): proven cases lose their checks, near misses keep them and trap
for label, body, want_unchecked in [
    ("while i < len(a)", "let a = new [int](10); let mut i = 0; while i < len(a) { a[i] = i; i += 1; } print(a[9]);", 1),
    ("for i in 0..n with a = new [T](n)", "let n = 10; let a = new [int](n); for i in 0..n { a[i] = i; } print(a[9]);", 1),
    ("i <= n with size n + 1", "let n = 10; let a = new [bool](n + 1); let mut i = 0; while i <= n { a[i] = true; i += 1; } print(a[10]);", 1),
]:
    p = os.path.join(TMP, "r.em")
    open(p, "w").write("fn main() -> int { " + body + " return 0; }\n")
    out = run([EMBC, p, "--emit-ir"]).stdout
    check(out.count("unchecked") >= want_unchecked, "range proof: " + label, out)
for label, body in [
    ("i <= n with size n", "let n = 10; let a = new [int](n); let mut i = 0; while i <= n { a[i] = i; i += 1; } print(a[0]);"),
    ("array replaced in the loop", "let mut a = new [int](10); let mut i = 0; while i < len(a) { a[i] = i; a = new [int](5); i += 1; } print(a[0]);"),
    ("bound grows in the loop", "let a = new [int](10); let mut n = 10; let mut i = 0; while i < n { a[i] = i; n += 1; i += 1; } print(a[0]);"),
    ("index decremented", "let a = new [int](10); let mut i = 5; while i < len(a) { a[i] = i; i -= 1; } print(a[0]);"),
    ("negative start", "let a = new [int](10); let mut i = 0 - 3; while i < len(a) { a[i] = i; i += 1; } print(a[0]);"),
    ("increment before the access", "let a = new [int](10); let mut i = 0; while i < len(a) { i += 1; a[i] = i; } print(a[0]);"),
]:
    p = os.path.join(TMP, "r.em")
    open(p, "w").write("fn main() -> int { " + body + " return 0; }\n")
    out = run([EMBC, p, "--emit-ir"]).stdout
    r = run([EMBC, p, "--interp"])
    check("unchecked" not in out and "INTERNAL" not in r.stderr, "range proof keeps the check: " + label, out + r.stderr)

# the unproven-but-actually-safe loop must still be correct, and an out-of-range one must still trap
p = os.path.join(TMP, "c.em")
open(p, "w").write("fn main() -> int { let a = new_array(4); for i in 0..5 { a[i] = i; } return 0; }\n")
r = run([EMBC, p, "--interp"])
check(r.returncode == 1 and "index out of bounds" in r.stderr, "kept check still traps", r.stderr)

# the heap limit: a program that keeps everything live fails cleanly instead of growing past EMBER_HEAP_MB
p = os.path.join(TMP, "oom.em")
open(p, "w").write("struct N { v: [int], next: N }\nfn main() -> int { let mut h: N = null; for i in 0..100000 { h = N { v: new [int](1000), next: h }; } print(1); return 0; }\n")
exe = os.path.join(TMP, "oom")
run([EMBC, p, "-o", exe])
calm = {k: v for k, v in os.environ.items() if k != "EMBER_GC_STRESS"}
r = run([exe], env=dict(calm, EMBER_HEAP_MB="32"))
check(r.returncode == 1 and "out of memory: the heap limit EMBER_HEAP_MB=32 was reached (line 2)" in r.stderr, "EMBER_HEAP_MB limit", r.stderr)
r = run([exe], env=dict(calm, EMBER_GC_STATS="1"))
check(r.returncode == 0 and "pauses: total" in r.stderr, "EMBER_GC_STATS report", r.stderr)

# ---- tooling
# embc fmt: idempotent on every file of the standard library and the tests, and it never changes what a program does
fmt_files = sorted(glob.glob(os.path.join(ROOT, "std", "**", "*.em"), recursive=True) +
                   glob.glob(os.path.join(ROOT, "tests", "**", "*.em"), recursive=True))
fmt_bad = []
for f in fmt_files:
    if "fuzz_failures" in f:
        continue
    a = run([EMBC, "fmt", f])
    if a.returncode:  # only files that are not even valid tokens (an error test) may fail
        if "/errors/" not in f:
            fmt_bad.append(f + ": " + a.stderr[:120])
        continue
    q = os.path.join(TMP, "f.em")
    open(q, "w").write(a.stdout)
    b = run([EMBC, "fmt", q])
    if b.stdout != a.stdout:
        fmt_bad.append(f + ": formatting twice changes the text")
check(not fmt_bad, f"fmt is idempotent on {len(fmt_files)} files", "; ".join(fmt_bad[:5]))
tree = os.path.join(TMP, "fmt-tree")
shutil.copytree(PROG, tree)
sem_bad = []
for src in sorted(glob.glob(os.path.join(tree, "*.em"))):
    name = os.path.basename(src)[:-3]
    formatted = run([EMBC, "fmt", src]).stdout
    open(src, "w").write(formatted)
    env = dict(os.environ, EMBER_TEST_DIR=TMP)
    r = run([EMBC, src, "--interp"], read(os.path.join(PROG, name + ".in")) or None, env=env)
    if r.stdout != read(os.path.join(PROG, name + ".out")):
        sem_bad.append(name)
check(not sem_bad, "formatted programs give the same output", ", ".join(sem_bad))
r = run([EMBC, "fmt", os.path.join(ROOT, "tests", "tooling", "fmt_floats.em"), "--check"])
check(r.returncode == 0, "fmt leaves an already formatted file alone (line breaks before float literals)", r.stdout + r.stderr)
r = run([EMBC, "fmt", os.path.join(ROOT, "tests", "tooling", "unit.em"), "--check"])
check(r.returncode == 1, "fmt --check notices an unformatted file", str(r.returncode))

# embc --test: every fn test_*() runs on its own; failures show the test's output and the error
for mode in ([], ["--interp"]):
    r = run([EMBC, os.path.join(ROOT, "tests", "tooling", "unit.em"), "--test"] + mode)
    want = ["running 4 tests", "test test_add ... ok", "test test_fails ... FAILED", "    some output",
            "assertion failed (line 4)", "test test_strings ... ok", "test test_traps ... FAILED",
            "index out of bounds (line 6)", "4 tests: 2 passed, 2 failed"]
    missing = [w for w in want if w not in r.stdout]
    check(r.returncode == 1 and not missing, "embc --test " + " ".join(mode), r.stdout + r.stderr)

# -g: gdb can break on an Ember source line and shows Ember lines in the backtrace
if shutil.which("gdb"):
    exe = os.path.join(TMP, "fibg")
    run([EMBC, os.path.join(ROOT, "examples", "fib.em"), "-g", "-o", exe])
    r = run(["gdb", "-batch", "-ex", "break fib.em:3", "-ex", "run", "-ex", "bt", exe])
    check(re.search(r"hit Breakpoint 1, f_fib \(\) at \S*fib\.em:3", r.stdout) is not None and
          re.search(r"in ember_main \(\) at \S*fib\.em:8", r.stdout) is not None,
          "gdb breaks on Ember lines (-g)", r.stdout[-600:])

# optimiser shrinks code
r = run([EMBC, os.path.join(PROG, "arith.em"), "--interp", "--stats", "-O1"])
check("->" in r.stderr, "stats printed", r.stderr)

print(f"\n{passed} passed, {failed} failed")
sys.exit(1 if failed else 0)
