// expect: takes 1 argument(s), got 2
fn main() -> int { let f = fn(x: int) -> int { return x; }; return f(1, 2); }
