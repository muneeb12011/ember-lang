# Arrays

Write an array with square brackets: `[3, 1, 4]`. Its type is `[int]`. `len(a)` is its length, and `a[i]`
reads or writes element `i`, counting from 0.

`new [int](n)` makes an array of `n` zeros. An array's length is fixed; for a list that grows, see lesson 11.

Every access is checked. Reading past the end doesn't return garbage, it stops the program with a clear error
and the line number. The optimizer removes the check when it can prove the index is in range, as in the loop
below.

**Try it:** change `0..len(temps)` to `0..len(temps) + 1` and run.

```ember
fn main() -> int {
    let temps = [31, 35, 29, 38, 33];
    let mut hottest = temps[0];
    let mut total = 0;
    for i in 0..len(temps) {
        total += temps[i];
        if temps[i] > hottest { hottest = temps[i]; }
    }
    print("hottest: " + str(hottest));
    print("average: " + str(total / len(temps)));

    let squares = new [int](5);
    for i in 0..5 { squares[i] = i * i; }
    print(squares[4]);
    return 0;
}
```
