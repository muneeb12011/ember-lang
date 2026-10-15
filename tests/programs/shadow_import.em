// a file's own definition wins over a name brought in by an `as`-less import (here the deprecated math_abs)
import "std/math.em";

fn math_abs(x: int) -> int { return x; }

fn main() -> int {
    print(math_abs(-5));
    print(gcd(12, 18));
    return 0;
}
