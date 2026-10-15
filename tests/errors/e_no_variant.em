// expect: enum `Shape` has no variant `Square`
enum Shape { Circle(float), Rect(float, float), Empty }
fn main() -> int { let s = Shape.Square(1.0); return 0; }
