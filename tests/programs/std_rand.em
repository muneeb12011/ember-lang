import "std/rand.em" as rand;

fn main() -> int {
    let r = rand.make(42);
    for i in 0..5 { print(rand.next(r)); }
    print(rand.range(r, 6));
    print(rand.next_float(r));
    let a = new [int](10);
    for i in 0..10 { a[i] = i; }
    rand.shuffle(r, a);
    for i in 0..10 { print(a[i]); }
    let counts = new [int](6);
    for i in 0..6000 {
        let k = rand.range(r, 6);
        counts[k] += 1;
    }
    for i in 0..6 { print(counts[i]); }
    return 0;
}
