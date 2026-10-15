import "multi/a.em";
import "multi/b.em";
import "multi/util.em";

fn main() -> int {
    print(a_value());
    print(b_value());
    let p = util_pair(5, 6);
    print(p.a * p.b);
    print(BASE);
    return 0;
}
