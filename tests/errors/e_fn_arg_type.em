// expect: argument 1 should be `int`, found `str`
fn main() -> int { let f = fn(x: int) -> int { return x; }; return f("a"); }
