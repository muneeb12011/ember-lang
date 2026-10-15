// expect: argument 1 of `id` should be `int`, found `str`
fn id<T>(x: T) -> T { return x; }
fn main() -> int { print(id<int>("s")); return 0; }
