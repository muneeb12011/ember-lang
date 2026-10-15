# Generics

Write a function or a struct once and use it with any type. Type parameters go in angle brackets: `<T>`.

You rarely write the types at the call: the compiler works them out from the arguments. Behind the scenes it
makes one copy of `largest` for each type you use it with, so generic code runs exactly as fast as code
written for one type.

Inside generic code, a `T` can be stored, passed, returned and compared with `==`, but not printed or added,
because not every type supports that. That's why `largest` takes the comparison as a parameter.

**Try it:** add `print(items[0]);` inside `largest`. The compiler explains why a `T` can't be printed.

```ember
struct Pair<A, B> { first: A, second: B }

fn largest<T>(items: [T], greater: fn(T, T) -> bool) -> T {
    let mut best = items[0];
    for i in 1..len(items) {
        if greater(items[i], best) { best = items[i]; }
    }
    return best;
}

fn int_gt(a: int, b: int) -> bool { return a > b; }
fn longer(a: str, b: str) -> bool { return len(a) > len(b); }

fn main() -> int {
    print(largest([4, 19, 7], int_gt));
    print(largest(["fig", "banana", "kiwi"], longer));
    let p = Pair { first: "year", second: 2026 };
    print(p.first + " " + str(p.second));
    return 0;
}
```
