import "std/math.em" as math;

pub struct Point { x: int, y: int }
pub fn manhattan(a: Point, b: Point) -> int { return math.abs(a.x - b.x) + math.abs(a.y - b.y); }
pub fn origin() -> Point { return Point { x: 0, y: 0 }; }
