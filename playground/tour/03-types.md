# Types never mix by accident

Ember has four basic types: `int` (64-bit whole numbers), `float` (decimals), `bool` and `str`.

Ints and floats never convert silently. If you want `7 / 2` as a decimal, convert first with `float(...)`. To
go back, `int(...)` cuts off the fraction. Turning anything into text is `str(...)`.

Integer division drops the remainder: `7 / 2` is `3`, and `7 % 2` is the remainder, `1`.

**Try it:** change `float(a) / float(b)` to `a / b` and run. The answer is now an int, and the compiler won't
let you store it in a `float`.

```ember
fn main() -> int {
    let a = 7;
    let b = 2;
    print(a / b);
    print(a % b);
    let exact: float = float(a) / float(b);
    print(exact);
    print("a is " + str(a));
    print(int(9.99));
    return 0;
}
```
