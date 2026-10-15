// expect: missing field `y`
struct P { x: int, y: int }
fn main() -> int {
    let p = P { x: 1 };
    return 0;
}
