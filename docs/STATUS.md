# Ember: status (2026-10-15)

Read this first. It says what exists, how it was verified, what is known to be missing, and where everything lives.
All eight phases in `docs/PHASE_PROMPTS.md` are done. That file now ends with suggestions for what to build next.

## Resume in 60 seconds

```
make                              # builds ./embc (g++; gcc to link programs; python3 for the tests)
make test                         # 408 checks: every program on 6 paths, error tests, tooling checks
make stress                       # every program on x86-64 with a collection on every allocation
python3 tests/fuzz.py 1000        # differential fuzzer (about 15 minutes on 2 cores)
./embc examples/showcase.em --run
```

## How it is verified

**Six execution paths.** Every program in `tests/programs` runs on the IR interpreter, native x86-64 and
WebAssembly (node), each at -O0 and -O1. All six must produce exactly the expected stdout and exit code, and the
expected outputs come from independent Python (`tests/reference.py`), never from running Ember. The interpreter also
re-checks every array access whose bounds check the optimizer removed, so an unsound proof shows up as an internal
error.

**Latest numbers, measured on the final tree:**

| check | result |
|---|---|
| `make test` | **408 passed, 0 failed**: 52 programs × 6 paths (312), 71 compile-error tests, and 25 checks of the bounds proofs, formatter, `--test`, gdb, heap limit and GC stats |
| `make stress` (`EMBER_GC_STRESS=1`, native) | **200 passed, 0 failed** (a collection on every allocation, mostly minor, so the write barrier is exercised) |
| fuzzer, all six paths | **1000 programs, 1000 agreed** (68 ended in the same deliberate runtime trap on every path) |
| fuzzer with `EMBER_GC_STRESS=1` | **500 programs, 500 agreed** |
| fuzzer after each Phase 5 optimizer step | 1000 programs each, all agreed |
| formatter | idempotent on all 160 `.em` files in `std/` and `tests/`; every formatted test program gives the same output |

**Checking the checks.** Two of these checks were themselves tested:

- The fuzzer, by putting the Phase 0 inliner bug back: it caught it in 3 of 30 programs, and the shrinker cut one
  from 51 lines to 16.
- The GC stress suite, by disabling the write barrier: eight stress runs failed.

## Bugs found and fixed (root causes)

| bug | found by | cause |
|---|---|---|
| `std_str`, `std_strbuf`, `std_vec` gave different results at -O1 | Phase 0 repro | the inliner renumbered registers in an instruction's `a b c` operands but not in the argument list of runtime calls, so inlined helpers read the caller's registers (not a `[byte]` bug) |
| `x & 1 == 1` parsed as `x & (1 == 1)` | known | `& ^ \|` had lower precedence than comparisons (the C mistake); now Rust/Go order |
| `parse_int("  -17")` returned 0 | Python reference disagreed | it did not skip leading whitespace, unlike `read_int`; fixed in the runtime and the interpreter |
| native allowed bigger arrays than the interpreter | review | size limits differed (2^40 bytes vs 2^31 elements); now both 2^31 elements |
| compiler crash on `Box { v: x }` inside a generic function | fuzzer | `instantiate` took its argument list by reference into the type table, which the instantiation itself grew |
| `-nan` vs `nan` from `pow(-1.0, 0.5)` on wasm | fuzzer | JS `Math` returns a positive NaN; glibc returns the x86 default (negative) NaN; the wasm runtime now does the same |
| float literals lost their line break under `embc fmt` | review | float tokens bypassed the lexer's newline bookkeeping |

## What exists

**Language.**
- Types: `int` (64-bit, wrapping), `float`, `bool`, `str`, `[T]` (one byte per element for `[byte]` and `[bool]`),
  structs, enums with payloads, generics, function types and closures, `null` for references.
- Modules with `pub`, `import ... as`, array literals, constants, compound assignment, exhaustive `match`.
- See `README.md` for the full reference.

**Front end.**
- Lexer, recursive-descent parser, and module loader (diamond imports load once; cycles are errors).
- Name resolution with private/public checking.
- A type checker that reports every error with a caret, plus a note pointing at the declaration where relevant.
- Generic inference from arguments and from the expected type, with two passes, so context-dependent arguments such
  as `Tree.Leaf` are typed last.
- Monomorphization (`src/mono.hpp`).

**Optimizer** (`src/opt.hpp`), every pass preserving runtime errors exactly:
- folding and propagation of constants (within and across blocks), copy propagation, algebraic simplification;
- branch folding, unreachable code, jump threading, liveness DCE, copy coalescing;
- CSE within blocks, LICM, self tail calls to loops;
- inlining (leaf functions ≤ 30 instructions, non-leaf ≤ 40 under a caller budget), dead-function pruning;
- bounds-check elimination from `for` ranges (in the checker) and from induction-variable ranges (in the optimizer).

**x86-64 back end** (`src/codegen.hpp`):
- linear-scan register allocation over CFG liveness, with 11 registers (callee-saved only for values live across
  calls);
- spilling by weight per unit of length, and parallel moves for arguments;
- fused compare-and-branch, strength reduction, the GC write barrier;
- DWARF line info with `-g` (gdb breaks on Ember lines; tested).

**WebAssembly back end** (`src/wasm.hpp`, `rt/ember_rt_wasm.c`, `rt/ember_wasm.js`):
- The compiler encodes the wasm module itself; the runtime is freestanding C built by clang.
- A node loader runs the program on a worker thread with a 1 GB stack.
- It passes the whole suite and the fuzzer.

**Runtime** (`rt/ember_rt.c`):
- A generational, non-moving, conservative mark-sweep collector (sticky mark bits, card-marking write barrier),
  with size-class blocks for small objects and malloc for large ones.
- An explicit mark stack, so deep structures cannot overflow the C stack.
- `EMBER_HEAP_MB` heap limit with a clean error, `EMBER_GC_STATS` with pause times, `EMBER_GC_STRESS`, `EMBER_GC_FULL`.
- Buffered I/O, file and process builtins, stack-overflow detection.

**Tooling.**
- `embc fmt` (idempotent, comment-preserving).
- `embc --test`, which runs each `fn test_*()` in its own process and reports pass/fail.
- `--emit-ir`, `--emit-asm`, `--stats`.

**Standard library**, written in Ember: vec (generic), option, iter, str, strbuf, map, sort, math, rand, io, plus
`std/compat/` for the old prefixed names.

## Design decisions worth knowing

* **Closures capture by value.** A copy is made when the closure is created, and assigning to a captured variable is
  a compile error. Locals live in registers and frames that die with their function, while closures can outlive it.
  Shared state goes through a struct or array.
* **Generics are checked once, with type parameters opaque.** A `T` can be stored, passed, returned and compared with
  `==`, nothing more. There are no trait bounds, so `<` or `print` on a `T` is a compile error, but errors appear at
  the definition rather than at each use.
* **Generational, not incremental.** Most Ember allocations die young. Generational collection cuts total collection
  work and pauses (binary trees: 2.6x less GC time). Incremental marking would only spread the same work out, and
  would need a barrier on every pointer store anyway.
* **No precise stack maps (Phase 6, item 2): conservative scanning was kept.** Precise maps would need:
  - a map per call site of which frame slots and callee-saved registers hold pointers;
  - frame walking that knows where each callee saved each callee-saved register;
  - a way to stop at the C runtime's frames, which hold pointers in registers that no map describes (`ember_str_concat`
    holds both strings while it allocates).

  That is a large, risky change. What it would buy is moving objects and no false retention. Neither is needed by
  the current non-moving design, and false retention was not observable in the torture tests: under `EMBER_HEAP_MB`
  limits, programs that allocate many times the limit in garbage run fine. The register allocator already guarantees
  the one property conservative scanning needs: every pointer live across a call is in a callee-saved register or on
  the stack.
* **Bitwise operators bind tighter than comparisons**, as in Rust and Go.

## Known limits

* Linux x86-64; native output needs gcc to assemble and link. The wasm target needs clang and wasm-ld to build its
  runtime once, and node to run.
* **The wasm target has no garbage collector.** Memory is bump-allocated up to wasm32's 4 GB. A conservative
  collector cannot see wasm locals, so collecting needs a shadow stack, which is future work. It ignores
  `EMBER_HEAP_MB`.
* **The wasm math functions come from JavaScript's `Math`.** Results agree with glibc's to the printed six decimals
  in every test and fuzz run so far, but they are not guaranteed bit-identical.
* **Floats are not register-allocated**: they live in general-purpose registers or stack slots and go through
  `xmm0`/`xmm1` for arithmetic. This is why mandelbrot and nbody gained least in Phase 5.
* `IntMap` is int to int only. A generic `HashMap<K, V>` needs hashing for `T`, which needs trait-like bounds that
  generics do not have.
* The collector is conservative: an integer that looks like a pointer can keep an object alive.
* Recursion depth: 1 GB native stack (the interpreter stops at 1,000,000 frames); about 1 GB on the wasm worker.
* The interpreter is for testing, not speed.

## Source map

| File | Job |
|---|---|
| `src/lexer.hpp` | tokens; numbers (hex, binary, float), chars, strings with escapes; comments for the formatter |
| `src/modules.hpp` | one Unit per file, symbols, `pub`, name resolution and mangling |
| `src/ast.hpp` | types (interned; arrays, structs, enums, generic templates and instances, function types), the AST, deep copies |
| `src/parser.hpp` | recursive descent, precedence climbing, types resolved while parsing, generics, `match`, lambdas |
| `src/builtins.hpp` | table of runtime-backed builtins |
| `src/sema.hpp` | type checking, generic inference, closures and captures, exhaustiveness, the `for`-range bounds proof |
| `src/mono.hpp` | monomorphization |
| `src/ir.hpp` | IR definition and lowering (enums, match, closures, write-barrier flags) |
| `src/opt.hpp` | optimizer passes, loops and dominators, liveness, inliner |
| `src/codegen.hpp` | x86-64: linear-scan allocator, parallel moves, instruction selection, write barrier |
| `src/wasm.hpp` | WebAssembly encoder |
| `src/interp.hpp` | reference interpreter (mirrors the C runtime exactly) |
| `src/fmt.hpp` | `embc fmt` |
| `src/toolchain.hpp` | runtime caches (gcc object, clang wasm module), linking, the wasm output file |
| `src/main.cpp` | driver, module loader, `--test`, `fmt` |
| `rt/ember_rt.c` | native runtime: generational GC, I/O, strings, files, panics |
| `rt/ember_rt_wasm.c`, `rt/ember_wasm.js` | wasm runtime and node loader |
| `std/*.em`, `std/compat/*.em` | standard library, and the deprecated prefixed names |
| `tools/embed.py` | embeds the runtimes, the loader and the stdlib into `src/rt_data.hpp` |
| `tests/run_tests.py`, `reference.py` | the suite, and the independent expected outputs |
| `tests/fuzz.py` | differential fuzzer with a shrinker (`--shrink file.em`) |
| `tests/bench/` | benchmarks, C and Python equivalents, runners (`run.py`, `ab.py`, `gc_compare.py`), results |
