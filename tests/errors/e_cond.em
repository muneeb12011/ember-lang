// expect: `while` condition must be a bool
fn main() -> int {
    let mut i = 0;
    while i { i = i + 1; }
    return 0;
}
