// expect: is a module, not a value
import "std/math.em" as math;
fn main() -> int { let x = math; return 0; }
