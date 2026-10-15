// expect: variant `Rect` carries 2 value(s)
enum Shape { Circle(float), Rect(float, float), Empty }
fn main() -> int { match Shape.Empty { Rect(w) => { } _ => { } } return 0; }
