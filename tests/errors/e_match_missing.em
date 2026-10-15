// expect: missing `Rect`, `Empty`
enum Shape { Circle(float), Rect(float, float), Empty }
fn main() -> int { let s = Shape.Empty; match s { Circle(r) => { print(r); } } return 0; }
