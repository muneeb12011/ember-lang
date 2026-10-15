# When things go wrong

Ember splits mistakes into two kinds.

**Compile errors** are found before anything runs: a misspelled name, the wrong type, a missing `match` arm.
The compiler reports all of them at once, each with the line and a caret under the exact spot. Click a location
in the output to jump to it.

**Runtime errors** can only happen while the program runs: an index past the end of an array, a division by
zero, a failed `assert`. The program stops with the reason and the line, instead of carrying on with garbage.

You can stop a program on purpose with `panic("message")`, and check your assumptions with `assert(condition)`.

That's the tour. Open the [playground](play.html) to write your own programs, and look at the IR and
x86-64 stages to see what the compiler made of them.

**Try it:** call `safe_divide(10, 0)` and see which kind of error that is.

```ember
fn safe_divide(a: int, b: int) -> int {
    assert(b != 0);
    return a / b;
}

fn main() -> int {
    print(safe_divide(10, 2));
    let marks = [70, 85, 90];
    let i = 3;
    print(marks[i]);
    print("this line never runs");
    return 0;
}
```
