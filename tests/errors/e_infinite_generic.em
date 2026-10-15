// expect: is instantiated without end
fn grow<T>(x: T, n: int) -> int { if n == 0 { return 0; } return grow([x], n - 1); }
fn main() -> int { return grow(1, 3); }
