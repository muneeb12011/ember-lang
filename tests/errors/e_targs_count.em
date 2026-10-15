// expect: `Pair` takes 2 type argument(s), got 1
struct Pair<A, B> { a: A, b: B }
fn main() -> int { let p: Pair<int> = null; return 0; }
