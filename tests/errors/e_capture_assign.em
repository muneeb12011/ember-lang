// expect: closures capture variables by value
fn main() -> int { let mut n = 0; let f = fn() { n = n + 1; }; f(); return n; }
