// expect: cannot infer the type argument `T` of `make`
fn make<T>() -> [T] { return new [T](3); }
fn main() -> int { let a = make(); return 0; }
