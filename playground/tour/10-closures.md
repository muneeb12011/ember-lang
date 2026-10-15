# Functions as values

Functions are values too: store them in variables, pass them to other functions, return them. The type of a
function that takes an `int` and returns an `int` is written `fn(int) -> int`.

An anonymous function, `fn(x: int) -> int { ... }`, can use variables from around it. Ember captures them by
value: the closure gets a copy when it's created. To share something that changes, capture a struct and change
its fields, as `make_counter` does.

**Try it:** write `make_adder(n: int) -> fn(int) -> int` that returns a function adding `n`, and use it
with `apply_twice`.

```ember
struct Count { n: int }

fn apply_twice(f: fn(int) -> int, x: int) -> int {
    return f(f(x));
}

fn make_counter() -> fn() -> int {
    let c = Count { n: 0 };
    return fn() -> int { c.n += 1; return c.n; };
}

fn main() -> int {
    let rate = 3;
    let triple = fn(x: int) -> int { return x * rate; };
    print(apply_twice(triple, 2));

    let next = make_counter();
    next();
    next();
    print(next());
    return 0;
}
```
