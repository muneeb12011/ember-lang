# Phase prompts for Claude

**Status (2026-10-15): phases 0 to 7 are all done.** `docs/STATUS.md` has what each produced and how it was verified;
`docs/PERF.md` has the measurements. The prompts are kept below as the record of what was asked. New prompts for the
next steps are at the end, under "Next".

Paste one prompt per conversation, in order. Each is self-contained. Attach or unzip the project first.

Ground rules every prompt repeats, because they are what keeps this project trustworthy:

- Every program must give identical stdout and exit code on four paths: interpreter and native, at -O0 and -O1.
- Expected outputs come from independent Python (`tests/reference.py`), never from running Ember.
- Do not claim a speed or memory number without measuring it. Do not say "done" without running `make test`.

---

## Phase 0: stabilize (do this first)

```
You are continuing work on Ember, a statically typed language with a native x86-64 compiler written in C++17
(unzip the project; read docs/STATUS.md first, then docs/PHASE_PROMPTS.md for the rules).

Goal: get `make && python3 tests/run_tests.py` fully green, then run the stress and fuzz targets.

Do these in order:
1. Fix the bitwise precedence bug in src/parser.hpp prec(): & ^ | must bind tighter than == and <.
   Target table: || 1, && 2, == != 3, < <= > >= 4, | 5, ^ 6, & 7, << >> 8, + - 9, * / % 10.
   Add a test program proving `x & 1 == 1`, `a | b ^ c & d` and shifts parse as intended.
2. Find and fix why std_str and std_strbuf behave differently on the interpreter and on native code
   (STATUS.md items 2 and 3). Reproduce first, minimise to a few lines, find the root cause, fix it, and add a
   regression test for the minimal case. Suspects: [byte] width handling in src/codegen.hpp and src/interp.hpp,
   new [byte](n), str_from_bytes.
3. Add independently computed expected outputs for the 12 programs without a .out file to tests/reference.py
   (STATUS.md item 4). Compute them in plain Python that does not call Ember. Any disagreement between Python
   and Ember is a bug to investigate, not a number to copy.
4. Run `make test`, `make stress` (EMBER_GC_STRESS=1), and `python3 tests/fuzz.py 300`. Fix everything they find.
5. Update docs/STATUS.md with the real numbers.

Do not change the language. Report what each failure's root cause was.
```

## Phase 1: make the fuzzer cover the whole language

```
Continue Ember (read docs/STATUS.md). The differential fuzzer tests/fuzz.py only generates ints, arrays of ints and
function calls. Extend it so it also generates: strings (+, comparisons, len, indexing, substr, str()), [byte] and
[bool] arrays, bitwise operators and shifts, floats, structs and null checks, and multi-function programs that
import std modules. Keep generated programs terminating and deterministic. Runtime traps must be generated on
purpose and must agree on all four paths (message and line).

Add a shrinker: when a generated program mismatches, delete statements and simplify expressions while the
mismatch remains, then save the minimal program to tests/fuzz_failures/.
Run 1000 programs. Fix every real compiler or runtime bug found, each with a regression test.
Report: programs run, bugs found with root causes, bugs fixed.
```

## Phase 2: namespaces and visibility

```
Continue Ember. Today every top-level name is global, so the standard library uses prefixes (math_abs, vec_push).
Design and implement modules properly:
- `import "std/math.em" as math;` then `math.abs(x)`, and `math.PI`; also `import "x.em";` keeps working.
- Names are private to their file unless declared `pub`; using a private name from another module is a compile error
  that points at the use and mentions the declaration.
- Imported structs are used as `vec.IntVec`.
- Two modules may define the same name without clashing.
Implement as name mangling at the loader level or in sema, whichever keeps IR, optimizer and codegen untouched.
Migrate std/*.em to the new syntax (drop the prefixes), keep the old prefixed names working for one release behind a
compatibility file, update all tests, and add error tests for private access and unknown module members.
Verify on four paths as always.
```

## Phase 3: enums, pattern matching, and generics

```
Continue Ember. Add:
1. Enums with payloads (tagged unions) and `match` with exhaustiveness checking (a missing case is a compile error that
   names the missing variants). Lower to a tagged heap block; the GC must scan payload pointers correctly.
2. Generics for structs and functions, implemented by monomorphization: `struct Vec<T> { data: [T], len: int }`,
   `fn vec_push<T>(v: Vec<T>, x: T)`. Type arguments are inferred at call sites where possible.
3. Rewrite std/vec.em as one generic Vec<T> and delete IntVec/StrVec; add Option<T>.
Every feature needs positive tests with Python-computed output and negative tests for the error messages.
Keep byte/bool one-byte elements working inside generic arrays.
```

## Phase 4: first-class functions and closures

```
Continue Ember. Add function types `fn(int) -> int`, function values (top-level functions and anonymous functions),
and closures that capture variables by value (state clearly whether captured variables are copied or shared and why).
A closure is a heap object (code pointer + captured values) that the GC scans. Calls through function values use an
indirect call in codegen and are handled by the interpreter. Add std/iter.em: map, filter, fold, any, all over arrays,
and use them in tests. The optimizer must not inline or drop calls through function values incorrectly.
```

## Phase 5: a real register allocator and a stronger optimizer

```
Continue Ember. The backend currently keeps the five hottest values in callee-saved registers and everything else on
the stack. Replace that with linear-scan register allocation over live intervals computed from the CFG liveness
already in src/opt.hpp, using all caller-saved registers between calls and spilling by interval length and use density.
Then add, each as a separate measured step: common subexpression elimination within blocks, loop-invariant code
motion, strength reduction of multiplications by constants, bounds-check elimination from simple range analysis
(induction variables with known bounds), and inlining of non-leaf functions under a size budget.
For every step: build a benchmark set in tests/bench (sieve, nbody, mandelbrot, sorting 5M ints, string building, a hash
map workload, recursive fib), record before/after wall time and peak RSS in docs/PERF.md, and keep only changes that
help. All tests must stay green on four paths; run the fuzzer for 1000 programs after each step.
```

## Phase 6: runtime and memory

```
Continue Ember. Improve the C runtime in rt/ember_rt.c without changing language behaviour:
1. Make collection incremental or generational (state which and why), keeping EMBER_GC_STRESS working.
2. Replace conservative stack scanning by precise stack maps emitted by the code generator, or document why not.
3. Add a heap limit (EMBER_HEAP_MB) with a clean "out of memory" runtime error, and an EMBER_GC_STATS report with
   pause times.
4. Measure peak RSS and pause times on allocation-heavy benchmarks (binary trees, string building, a hash map with 1M keys)
   and record them in docs/PERF.md next to equivalent C and Python programs.
Add torture tests: cycles, deep structures (recursion depth in the collector must not overflow), huge arrays,
many small strings.
```

## Phase 7: files, tooling, and a second target

```
Continue Ember. Pick and complete as many as fit, each with tests:
1. File I/O and process builtins: read_file, write_file, exit(code), env(name); errors as runtime errors with messages.
2. A formatter: `embc fmt file.em`, idempotent, with a test that formatting every file in std/ and tests/ twice gives
   the same text.
3. `embc --test`: run functions named test_* in a file and report pass/fail.
4. DWARF line info for `-g` so gdb can break on Ember source lines (verify with gdb in a test).
5. A second backend (AArch64 assembly or WebAssembly) behind the same IR, tested through the same differential suite
   (use qemu-user or a wasm runtime if available; otherwise say what could not be verified).
Update README.md and docs with the final language reference.
```

---

# Next

Each of these addresses a limit recorded in `docs/STATUS.md`. Same ground rules: every program identical on every
path (now six: interpreter, x86-64 and wasm, each at -O0 and -O1), expected outputs from independent Python, no
performance claim without a measurement, `make test`, `make stress` and 1000 fuzzer programs before saying "done".

## Phase 8: floats in SSE registers

```
Continue Ember (read docs/STATUS.md and docs/PERF.md). The x86-64 allocator only assigns general-purpose registers,
so every float operation moves its operands through xmm0/xmm1. Give float-typed virtual registers xmm registers
(xmm2-xmm15; all are caller-saved, so a float live across a call must be spilled or kept in a GPR), which means the
IR must know which registers hold floats (record it during lowering). Pass float arguments to runtime math in xmm
registers instead of as raw bits. Measure mandelbrot and nbody before and after (time and instructions, as
docs/PERF.md does) and record the result there.
```

## Phase 9: structured control flow for WebAssembly

```
Continue Ember. The wasm back end (src/wasm.hpp) turns every function into a br_table dispatch loop, which is why sort
runs 3.3x slower on node than natively. Reconstruct structured control flow from the IR's CFG (the Relooper, or the
"Beyond Relooper" algorithm by Ramsey) so loops become wasm `loop`s and if/else become `if`, with the dispatch loop
kept only as the fallback for irreducible control flow. Keep the suite and the fuzzer green on all six paths and
record the speed in docs/PERF.md.
```

## Phase 10: garbage collection for WebAssembly

```
Continue Ember. The wasm target never frees memory, because a conservative collector cannot see wasm locals. Add a
shadow stack: at every call that can allocate, the code generator spills the pointer-holding locals that are live
across the call into a frame in linear memory (the IR knows which values are pointers; the Store `ptr` flag shows
how), and the wasm runtime's collector scans that shadow stack instead of the machine stack. Port the generational
collector from rt/ember_rt.c. Then make EMBER_HEAP_MB work on wasm and run the GC torture tests there.
```

## Phase 11: traits and a generic hash map

```
Continue Ember. Generics have no bounds, so a generic HashMap<K, V> is impossible (it needs hashing and equality for
K). Add interfaces (traits) with explicit implementations, as in `trait Hash { fn hash(self) -> int; }` and
`impl Hash for str { ... }`, bounds on type parameters (`fn f<T: Hash>(x: T)`), and method-call syntax `x.hash()`.
Resolve calls statically during monomorphization (no vtables needed). Provide Hash and Eq for int, str, bool and
float, rewrite std/map.em as HashMap<K, V>, and keep IntMap working through std/compat for one release.
```
