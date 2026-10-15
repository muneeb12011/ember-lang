// expect: value 1 of `Shape.Circle` should be `float`, found `str`
enum Shape { Circle(float), Rect(float, float), Empty }
fn main() -> int { let s = Shape.Circle("x"); return 0; }
