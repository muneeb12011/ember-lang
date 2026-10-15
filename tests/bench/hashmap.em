// std/map IntMap: 1,000,000 inserts, 2,000,000 lookups
import "std/map.em" as map;

fn main() -> int {
    let m = map.make();
    let n = 1000000;
    for i in 0..n { map.put(m, i * 7919 + 13, i); }
    let mut hits = 0;
    let mut sum = 0;
    for i in 0..2 * n {
        let v = map.get(m, i * 7919 + 13, -1);
        if v >= 0 {
            hits += 1;
            sum += v;
        }
    }
    print(map.size(m));
    print(hits);
    print(sum);
    return 0;
}
