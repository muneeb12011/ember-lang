# let and let mut

`let` gives a value a name. Once set, it can't change. If you want to change it later, say so up front with
`let mut`.

This is deliberate: when you read `let`, you know that name means the same thing on every line below it. The
compiler enforces it, so it's a guarantee, not a convention.

Changing a variable has shortcuts: `x += 1` means `x = x + 1`, and the same works for `-= *= /= %=`.

**Try it:** delete the word `mut` and run. The compiler stops you at the line that tries to change `score`.

```ember
fn main() -> int {
    let name = "Ember";
    let mut score = 10;
    score += 5;
    score *= 2;
    print(name);
    print(score);
    return 0;
}
```
