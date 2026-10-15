// expect: field `x` should be `int`, found `float`
struct P { x: int }
fn main() -> int {
    let p = P { x: 1.5 };
    return 0;
}
