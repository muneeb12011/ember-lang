// expect: variant `Empty` is matched twice
enum Shape { Circle(float), Rect(float, float), Empty }
fn main() -> int { match Shape.Empty { Empty => { } Empty => { } _ => { } } return 0; }
