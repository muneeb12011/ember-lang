// expect: expected `fn(int) -> int`, found `fn(str) -> int`
fn main() -> int { let f: fn(int) -> int = fn(s: str) -> int { return len(s); }; return 0; }
