import "std/sort.em" as sort;
import "std/rand.em" as rand;

fn main() -> int {
    let r = rand.make(12345);
    let n = 2000;
    let a = new [int](n);
    for i in 0..n { a[i] = rand.range(r, 1000) - 500; }
    sort.ints(a);
    print(sort.is_sorted(a));
    print(a[0]);
    print(a[n - 1]);
    print(a[1000]);
    let mut chk = 0;
    for i in 0..n { chk = (chk * 31 + a[i]) % 1000000007; }
    print(chk);
    print(sort.search(a, a[777]) >= 0);
    print(sort.search(a, 100000));
    let b = new [int](500);
    for i in 0..500 { b[i] = 500 - i; }
    sort.ints(b);
    print(sort.is_sorted(b));
    print(b[0]);
    print(b[499]);
    let c = new [int](300);
    for i in 0..300 { c[i] = 7; }
    sort.ints(c);
    print(sort.is_sorted(c));
    let f = new [float](100);
    for i in 0..100 { f[i] = float(rand.range(r, 1000)) / 8.0; }
    sort.floats(f);
    print(f[0]);
    print(f[99]);
    print(f[50]);
    let s = new [str](6);
    s[0] = "pear";
    s[1] = "apple";
    s[2] = "fig";
    s[3] = "apple";
    s[4] = "banana";
    s[5] = "";
    sort.strs(s);
    for i in 0..6 { print(s[i]); }
    return 0;
}
