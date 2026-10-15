// expect: use `match` to look inside an enum value
enum Shape { Circle(float), Rect(float, float), Empty }
fn main() -> int { let s = Shape.Circle(1.0); print(s.r); return 0; }
