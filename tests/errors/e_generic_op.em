// expect: `<` needs two ints, two floats or two strs, found `T` and `T`
fn less<T>(a: T, b: T) -> bool { return a < b; }
fn main() -> int { print(less(1, 2)); return 0; }
