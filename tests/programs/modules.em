// modules: `import ... as name` gives qualified access to pub names; `import ...;` gives unqualified access
import "std/math.em" as math;
import "mods/one.em" as one;
import "mods/two.em" as two;
import "mods/geo.em";

const VERSION = 3;
fn helper(x: int) -> int { return x + 1; }
// a file may define a name that a built-in also uses: inside this file, `pow` is this function
fn pow(a: int, b: int) -> int { return math.pow(a, b); }

fn main() -> int {
    print(one.name());
    print(two.name());
    print(one.scaled(4));
    print(two.scaled(4));
    print(helper(4));
    print(VERSION + one.VERSION * 10 + two.VERSION * 100);
    let a: one.Item = one.item(5);
    print(a.label);
    let b = two.Item { id: 9, weight: 2.5 };
    print(b.weight);
    let p = Point { x: 3, y: -4 };
    print(manhattan(p, origin()));
    print(math.gcd(84, 36));
    print(math.PI > 3.14);
    print(pow(2, 10));
    print(one.secret_value());
    return 0;
}
