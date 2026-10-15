import "std/map.em" as map;
import "std/sort.em" as sort;

fn main() -> int {
    let m = map.make();
    for i in 0..1000 { map.put(m, i * 7919 - 3000, i); }
    print(map.size(m));
    print(map.get(m, 7919 * 5 - 3000, -1));
    print(map.get(m, 1, -1));
    print(map.has(m, -3000));
    print(map.has(m, 12345));
    map.put(m, -3000, 99);
    print(map.get(m, -3000, -1));
    print(map.size(m));
    let ks = map.keys(m);
    sort.ints(ks);
    print(len(ks));
    print(ks[0]);
    print(ks[999]);
    let c = map.make();
    for i in 0..500 { map.put(c, i * 1024, i); }
    let mut ok = true;
    for i in 0..500 { if map.get(c, i * 1024, -1) != i { ok = false; } }
    print(ok);
    print(map.size(c));
    return 0;
}
