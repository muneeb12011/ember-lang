# Your first program

Every Ember program starts at `main`. It takes nothing and returns an `int`: that number becomes the program's
exit code, and `0` means everything went well.

`print` writes one value and a line break. It accepts an `int`, a `float`, a `bool` or a `str`.

When you press **Run**, the compiler checks the program, turns it into IR, optimizes it, and runs it on its
interpreter, all inside this page.

**Try it:** change `return 0;` to `return 3;` and run again. The exit code under the output changes to 3.

```ember
fn main() -> int {
    print("Hello from Ember!");
    print(42);
    print(true);
    return 0;
}
```
