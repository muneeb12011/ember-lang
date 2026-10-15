// expect: `byte` cannot be a type argument
fn first<T>(a: [T]) -> T { return a[0]; }
fn main() -> int { print(first(new [byte](3))); return 0; }
