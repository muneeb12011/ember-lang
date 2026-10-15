// expect: `print` takes an int, float, bool or str, found `T`
fn show<T>(a: T) { print(a); }
fn main() -> int { show(1); return 0; }
