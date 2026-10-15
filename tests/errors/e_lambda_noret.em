// expect: this anonymous function must return `int` on every path
fn main() -> int { let f = fn(x: int) -> int { if x > 0 { return 1; } }; return 0; }
