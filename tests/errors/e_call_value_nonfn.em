// expect: only functions can be called; this is a `[int]`
fn main() -> int { let a = [[1]]; return a[0](0); }
