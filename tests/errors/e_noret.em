// expect: must return `int` on every path
fn f(a: int) -> int {
    if a > 0 { return 1; }
}

fn main() -> int { return f(1); }
