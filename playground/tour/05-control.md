# Loops and decisions

`if` and `while` need no parentheses, but always use braces. `for i in 0..n` counts from `0` up to `n - 1`,
like Python's `range(n)`.

`break` leaves a loop early and `continue` skips to the next round.

Ember also has `&&`, `||` and `!` for combining conditions. They short-circuit: the right side only runs when
it's needed.

**Try it:** change the loop to count to 30 and stop at the first number divisible by both 4 and 6.

```ember
fn main() -> int {
    for i in 1..16 {
        if i % 2 == 0 {
            continue;
        }
        if i > 11 {
            break;
        }
        print(i);
    }

    let mut n = 27;
    let mut steps = 0;
    while n != 1 {
        if n % 2 == 0 { n = n / 2; } else { n = 3 * n + 1; }
        steps += 1;
    }
    print("27 reaches 1 in " + str(steps) + " steps");
    return 0;
}
```
