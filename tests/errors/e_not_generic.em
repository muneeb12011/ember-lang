// expect: `Point` is not generic
struct Point { x: int }
fn main() -> int { let p: Point<int> = null; return 0; }
