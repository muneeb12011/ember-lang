## Types

| Type | Meaning |
|---|---|
| `int` | 64-bit signed integer; arithmetic wraps around on overflow |
| `float` | IEEE 754 double |
| `bool` | `true` or `false` |
| `str` | immutable string of bytes; `s[i]` is the byte at `i` (an int), `len(s)` its length |
| `[T]` | array of T, fixed length, mutable elements; `[byte]` and `[bool]` use one byte per element |
| `Name` | a declared struct or enum (a reference; may be `null`) |
| `Name<T, ...>` | an instance of a generic struct or enum |
| `fn(A, B) -> R` | a function value (a top-level function or a closure); `fn(A)` returns nothing |

`byte` exists only as an array element type: `[byte]` elements read as ints from 0 to 255 and store the low 8 bits.

Structs, enums, arrays and function values are references: `==` compares identity, and they may be `null`
(`let p: Point = null;`). Every other type, `str` included, is compared by value.

## Declarations

```ember
import "std/math.em" as math;          // qualified access: math.abs(x), math.PI
import "util.em";                      // unqualified access to util.em's pub names
pub const LIMIT = 100;                 // constants: int, float, bool, char or str literals
pub struct Point { x: int, y: int }
struct Pair<A, B> { first: A, second: B }
pub enum Option<T> { Some(T), None }   // variants carry 0 to 6 values
pub fn add(a: int, b: int) -> int { return a + b; }
fn swap<A, B>(p: Pair<A, B>) -> Pair<B, A> { return Pair { first: p.second, second: p.first }; }
fn main() -> int { return 0; }         // the entry point; its result is the exit code
```

Every top-level name is private to its file unless declared `pub`. A file's own declarations come first, then the
built-in functions, then the names imported without `as`. If two `as`-less imports both define a name, using it
unqualified is an error: qualify it. Functions take at most 6 parameters (5 when used as values).

## Statements

```ember
let x = 1;                  // immutable
let mut y: float = 2.0;     // can be reassigned
y = y * 2.0;
x += 1;                     // also -= *= /= %= &= |= ^= <<= >>=
if c { ... } else if d { ... } else { ... }
while c { ... }
for i in 0..n { ... }       // i runs from 0 up to n - 1
break;  continue;  return v;
match shape {
    Circle(r) => { ... }
    Rect(w, _) => { ... }   // _ ignores a value
    _ => { ... }            // anything else
}
```

A `match` must cover every variant or have a `_` arm; a missing variant is a compile error that names it. Only
calls can be expression statements. A function that returns a value must return on every path.

## Expressions

Operators, from loosest to tightest binding, all left-associative:

| Operator | Meaning |
|---|---|
| `\|\|` | logical or (short-circuit) |
| `&&` | logical and (short-circuit) |
| `== !=` | equality |
| `< <= > >=` | ordering (ints, floats, strs) |
| `\|` | bitwise or |
| `^` | bitwise xor |
| `&` | bitwise and |
| `<< >>` | shifts (`>>` is arithmetic; the count is taken mod 64) |
| `+ -` | add, subtract (`+` also joins strs) |
| `* / %` | multiply, divide (truncates toward zero), remainder (takes the sign of the dividend) |
| `- ! ~` | unary negate, not, bitwise not |

Bitwise operators bind tighter than comparisons, so `x & 1 == 1` means `(x & 1) == 1`, as in Rust and Go. Ints
and floats never mix implicitly: write `float(i)` or `int(x)`.

**Literals:** `42`, `0xFF`, `0b1010`, `1_000_000`, `1.5`, `2.0e-3`, `'a'` (an int), `"text\n"` (escapes
`\n \t \r \0 \\ \" \' \xHH`), `true`, `false`, `null`, arrays `[1, 2, 3]`.

**Other expressions:** `a[i]`, `p.x`, `f(x)`, `math.abs(x)`, `Point { x: 1, y: 2 }`, `Pair<int, str> { ... }`,
`Shape.Circle(1.0)`, `Option<int>.None`, `new [int](n)` (a zeroed array: 0, 0.0, false, "" or null), `id<str>` (a
generic function as a value), and anonymous functions `fn(x: int) -> int { return x + k; }`.

## Generics

Structs, enums and functions can be generic. Type arguments are inferred from the arguments, then from the type the
context expects, and can always be written out: `vec.make<int>()`.

Generic code is checked once, with each type parameter treated as opaque: a value of type `T` can be stored, passed,
returned and compared with `==`, but not printed or added. The compiler then makes one copy of each generic function
per list of type arguments it's used with (monomorphization), so generic code runs as fast as hand-written code.

## Closures

An anonymous function can use the variables around it. **Closures capture by value**: each one gets a copy of the
variables it uses, made when it's created, and assigning to a captured variable is a compile error. Local variables
live in registers and stack slots that disappear when their function returns, while a closure can outlive that
function, so a copy is the behaviour that stays predictable. To share state that changes, capture a struct or an
array and change its contents:

```ember
fn make_counter() -> fn() -> int {
    let c = Counter { n: 0 };
    return fn() -> int { c.n += 1; return c.n; };
}
```

## Built-in functions

| Group | Functions |
|---|---|
| Output | `print(x)` (int, float, bool or str, plus a line break; floats print with 6 decimals), `put(s)` (no line break) |
| Input | `read_int()`, `read_line()`, `at_eof()` |
| Strings | `len(s)`, `substr(s, start, count)`, `chr(c)`, `str(x)`, `parse_int(s)`, `str_from_bytes(bytes, n)` |
| Arrays | `len(a)`, `new [T](n)` |
| Numbers | `int(x)`, `float(i)`, `sqrt` `sin` `cos` `tan` `atan` `atan2` `exp` `log` `pow` `floor` `ceil` |
| Files | `read_file(path)`, `write_file(path, s)`, `append_file(path, s)`, `file_exists(path)` |
| Process | `arg_count()`, `arg(i)`, `env(name)` ("" if unset), `has_env(name)`, `exit(code)`, `clock_ns()` |
| Errors | `panic(message)`, `assert(condition)` |

Files and the process environment are not available in the browser playground.

## Runtime errors

These stop the program with `runtime error: <what> (line N)` and exit code 1: index out of bounds, division by zero,
negative array size, out of memory, null dereference, failed assert, panic, a file that can't be read or written, and
stack overflow. `int(x)` of a NaN or out-of-range float gives the smallest int, as the hardware does.

## Standard library

Written in Ember and built into the compiler: `import "std/<name>.em" as <name>;`

| Module | Contents |
|---|---|
| `vec` | `Vec<T>`: make, with_capacity, from_array, size, is_empty, push, get, get_opt, set, pop, last, clear, to_array, index_of, contains, reverse, sum |
| `option` | `Option<T>` (Some, None): some, none, is_some, is_none, unwrap, unwrap_or |
| `iter` | map, filter, fold, any, all, count, each, find_index, range, tabulate, zip_with |
| `str` | split, join, find, contains, count, replace, trim, upper, lower, reverse, repeat, pad_left, starts_with, ends_with |
| `strbuf` | `StrBuf`, a string builder: make, push, push_char, push_int, size, clear, to_str |
| `map` | `IntMap`, a hash map from int to int: make, put, get, has, size, keys |
| `sort` | ints, floats (quicksort), strs (stable merge sort), is_sorted, search |
| `math` | abs, sign, min, max, clamp, mod, gcd, lcm, pow, isqrt, is_prime, fabs, fmin, fmax, PI, E |
| `rand` | `Rng`, a deterministic 64-bit generator: make, next, range, next_float, shuffle |
| `io` | read_ints, read_lines |

## Command line

```
embc prog.em -o prog && ./prog       native x86-64 executable
embc prog.em --run -- arg1 arg2      compile and run, with program arguments
embc prog.em --interp                run on the reference interpreter
embc prog.em --target wasm -o p.js   WebAssembly, run with node p.js
embc prog.em --test                  run every fn test_*() in the file
embc fmt prog.em -w                  format the file in place
embc prog.em -g -o prog              with debug info: gdb can break on Ember lines
embc prog.em --emit-ir | --emit-asm | --stats | -O0
```
