// expect: module `math` (std/math.em) has no function `nope`
import "std/math.em" as math;
fn main() -> int { return math.nope(1); }
