# Functions

A function names its parameters with their types, and says what it returns after the arrow. Leave out the arrow
if it returns nothing.

The compiler checks every path: if a function promises an `int`, every way through it must end in a `return`.

Functions can call themselves. `fib` below is the classic recursive version, which is slow on purpose; it's
one of Ember's benchmarks.

**Try it:** delete the line `return b;` in `bigger` and run. The compiler points out that when `a` isn't
bigger, the function never returns.

```ember
fn bigger(a: int, b: int) -> int {
    if a > b {
        return a;
    }
    return b;
}

fn fib(n: int) -> int {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}

fn greet(who: str) {
    print("Hello, " + who);
}

fn main() -> int {
    print(bigger(3, 8));
    print(fib(20));
    greet("Lahore");
    return 0;
}
```
