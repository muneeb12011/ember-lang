// expect: `Box` is generic: write `Box<...>`
struct Box<T> { v: T }
fn main() -> int { let b: Box = null; return 0; }
