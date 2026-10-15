# Ember performance measurements

Everything here was measured, on an Intel Xeon @ 2.80 GHz VM with 2 vCPUs, 7 GB RAM, Linux, gcc 13.3, node 22,
Python 3.13. Nothing is extrapolated.

## Method

* Benchmarks: `tests/bench/*.em`. Each prints a checksum, and the runner checks that the output does not change
  between runs or builds.
* **Wall time** is the best of 5 to 7 runs (`tests/bench/run.py`). This VM is noisy: the same binary varied by up to
  ±30% between runs, so a time difference under about 15% means nothing on its own.
* **Instructions executed** are counted with valgrind's cachegrind (`run.py --instr`). The count is exact and
  repeatable, so it decided whether an optimizer step was kept. It cannot see the cost of individual instructions:
  one `idiv` costs tens of cycles, a shift one. Strength reduction was therefore judged with a paired timing
  (`tests/bench/ab.py`: two builds alternating, 15 runs each, minimum and median compared).
* **Peak RSS** is `ru_maxrss` of the benchmark process.
* Each step can be switched off without rebuilding (`EMBER_NO_OPT=coalesce,cse,licm,strength,gconst,bce,inline-nonleaf`),
  so "with" and "without" are measured on the same compiler binary. Step 0 is the compiler from before Phase 5
  (commit "Phase 4"), built from git.

## Phase 5: register allocator and optimizer

Instructions executed, cumulative. Each row adds one step to the row above it.

| step | fib | hashmap | mandelbrot | nbody | sieve | sort | strbuild |
|---|---|---|---|---|---|---|---|
| 0. before: 5 hottest values in callee-saved registers, everything else on the stack | 942M | 985M | 2,000M | 6,202M | 1,076M | 3,971M | 4,855M |
| 1. linear-scan allocation (11 registers, intervals from CFG liveness) | 918M | 598M | 1,810M | 4,836M | 650M | 2,520M | 3,754M |
| 2. + copy coalescing (`t = a op b; x = t` becomes `x = a op b`) | 918M | 583M | 1,716M | 4,816M | 513M | 2,251M | 3,709M |
| 3. + common subexpressions within blocks (pure ops, lengths, loads) | 918M | 583M | 1,716M | 4,586M | 513M | 2,251M | 3,709M |
| 4. + loop-invariant code motion | 918M | 577M | 1,712M | 4,568M | 513M | 2,251M | 3,709M |
| 5. + strength reduction (x*2^k, x*3/5/9, x/2^k, x%2^k) | 918M | 574M | 1,712M | 4,568M | 513M | 2,252M | 3,709M |
| 6. + range-based bounds-check elimination, constants across blocks | 918M | 574M | 1,712M | 4,568M | 473M | 2,232M | 3,709M |
| 7. + inlining of small non-leaf functions (≤ 40 instructions, caller ≤ 4000) | 918M | 542M | 1,712M | 4,568M | 473M | 2,232M | 3,622M |

Before and after the whole phase:

| benchmark | time before (s) | time after (s) | speedup | instructions before → after | peak RSS before → after (MB) |
|---|---|---|---|---|---|
| fib (recursive, fib(36)) | 0.236 | 0.112 | 2.10x | 942M → 918M (1.03x) | 11.3 → 11.2 |
| hashmap (1M inserts, 2M lookups) | 0.514 | 0.256 | 2.01x | 985M → 542M (1.82x) | 54.5 → 54.4 |
| mandelbrot (1000×1000, 100 iterations) | 0.419 | 0.396 | 1.06x | 2,000M → 1,712M (1.17x) | 11.3 → 11.3 |
| nbody (2,000,000 steps) | 1.138 | 0.689 | 1.65x | 6,202M → 4,568M (1.36x) | 11.2 → 11.1 |
| sieve (primes below 20M) | 0.321 | 0.357 | 0.90x | 1,076M → 473M (2.28x) | 21.1 → 21.1 |
| sort (5M ints, quicksort) | 1.305 | 0.791 | 1.65x | 3,971M → 2,232M (1.78x) | 40.3 → 40.3 |
| strbuild (20 MB string from 3M numbers) | 0.627 | 0.417 | 1.50x | 4,855M → 3,622M (1.34x) | 51.8 → 51.7 |

What each step is worth, and why:

* **Linear scan** is most of the gain. Values now stay in registers across loops instead of going through the stack.
  fib barely changes in instruction count, but its time halves: its cost is memory traffic and calls, which
  instruction counts do not show.
* **Coalescing** removes the copy after almost every arithmetic instruction (−21% on sieve, −11% on sort).
* **CSE** only helps nbody (−5%), where the same struct fields are loaded several times per statement.
* **LICM** is worth 0.2–1% here. The lowering already computes most invariants outside the loop, and the pass only
  moves pure, non-trapping arithmetic (no loads), so there is little left to move. It was kept because it never added
  instructions.
* **Strength reduction** leaves instruction counts flat: a 5-instruction shift sequence replaces one `idiv`. Paired
  timing on sort, whose quicksort computes `(hi - lo) / 2`, gave a median 0.877s vs 0.895s and a minimum 0.709s vs
  0.743s (2–5%). hashmap moved within noise (its hot path has no multiply or divide by a small constant). Kept, on that
  measured gain plus the instruction latencies.
* **Range-based bounds-check elimination** removes the per-element check in loops like `while i < len(a)`,
  `for i in 0..n` over `new [T](n)`, and `i <= n` over `new [T](n + 1)` (sieve's outer loop: −8%). Sieve's inner loop
  `j = i*i; j += i` keeps its check: proving `i*i` stays in range needs more than induction-variable ranges.
* **Non-leaf inlining** helps hashmap (−6%: `put` and `get` call `slot` and `hash`) and strbuild (−2%).
* **Sieve's time did not improve** despite 2.28x fewer instructions. It writes a 20 MB array with a stride, so it is
  bound by cache misses, not instructions. The 0.32 vs 0.36 difference is inside this VM's noise.
* **Floats** still go through `xmm0`/`xmm1` as scratch registers: the allocator only hands out general-purpose
  registers. This is why mandelbrot and nbody gained least. Allocating xmm registers is the obvious next step.

Every step was followed by 1000 fuzzer programs (all paths agreed) and the full suite.

## Phase 6: garbage collector

Allocation-heavy programs, Ember next to equivalent C (malloc/free, gcc -O2) and Python 3 programs
(`tests/bench/gc_compare.py`, best of 3 runs):

| benchmark | implementation | time (s) | peak RSS (MB) | collections | GC pause total / max / mean (ms) |
|---|---|---|---|---|---|
| binary trees (depth 16) | Ember, generational | 0.733 | 19.6 | 85 (80 minor, 5 major) | 131.3 / 7.58 / 1.54 |
| | Ember, full collections only (`EMBER_GC_FULL=1`) | 0.989 | 20.4 | 80 (all major) | 345.5 / 14.95 / 4.32 |
| | C | 0.439 | 11.3 | | |
| | Python | 3.201 | 16.6 | | |
| string building (20 MB) | Ember, generational | 0.475 | 46.1 | 16 (13 minor, 3 major) | 11.7 / 5.04 / 0.73 |
| | Ember, full collections only | 0.458 | 51.8 | 10 (all major) | 7.7 / 2.35 / 0.77 |
| | C | 0.308 | 23.6 | | |
| | Python | 3.218 | 221.9 | | |
| hash map (1M keys) | Ember, generational | 0.269 | 53.7 | 8 (6 minor, 2 major) | 1.3 / 0.62 / 0.17 |
| | Ember, full collections only | 0.296 | 54.4 | 7 (all major) | 1.2 / 0.54 / 0.18 |
| | C | 0.186 | 53.2 | | |
| | Python | 1.628 | 123.2 | | |

* **Binary trees** is the case generational collection is for: millions of short-lived nodes next to one long-lived
  tree. Total collection time fell 2.6x (345 → 131 ms), the mean pause 2.8x (4.3 → 1.5 ms), the worst pause 2x
  (15 → 7.6 ms), and the program got 1.35x faster.
* **String building and the hash map** allocate few objects but large ones (a growing byte buffer, three 2M-element
  arrays). Few collections happen, and both policies come out within noise of each other. Generational is slightly
  worse in total pause time on strbuild (11.7 vs 7.7 ms): its minor collections rescan the old large objects that
  hold pointers.
* **Against C:** 1.4–1.7x the time, and up to 2x the peak RSS. Most of the RSS gap is heap headroom: the next
  collection waits for 4 MB of new objects, and the old generation may double before a major collection.
* **Against Python:** 4.4–6.8x faster. Memory: 4.8x less on string building (Python holds 3 million small string
  objects) and 2.3x less on the hash map, but slightly more on binary trees (19.6 vs 16.6 MB, the collector's headroom).
* Pause times come from `EMBER_GC_STATS=1` (CLOCK_MONOTONIC around each collection).

## The WebAssembly target

Same programs, same output, best of 3 (node 22, including node's startup time of about 40 ms):

| benchmark | x86-64 (s) | wasm on node (s) | ratio |
|---|---|---|---|
| fib | 0.162 | 0.311 | 1.9x |
| nbody | 0.625 | 0.914 | 1.5x |
| mandelbrot | 0.467 | 0.720 | 1.5x |
| sort | 0.689 | 2.307 | 3.3x |

The wasm code is a dispatch loop over basic blocks (`br_table`), so jumps cost more than on x86-64. sort is the most
jump-heavy and slows the most. Reconstructing structured control flow (a relooper) would close much of the gap.

## Reproducing

```
python3 tests/bench/run.py --instr                          # all benchmarks: time, RSS, instructions
python3 tests/bench/run.py --no-opt cse --label no-cse      # one pass off
python3 tests/bench/run.py --compare no-cse --label all     # and compare
python3 tests/bench/ab.py sort strength 15                  # paired timing of one pass
python3 tests/bench/gc_compare.py                           # the collector against C and Python
```
