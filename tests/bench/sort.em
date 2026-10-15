// sorting 5,000,000 pseudo-random ints with std/sort (quicksort)
import "std/sort.em" as sort;

fn main() -> int {
    let n = 5000000;
    let a = new [int](n);
    let mut s = 12345;
    for i in 0..n {
        s = s * 6364136223846793005 + 1442695040888963407;
        a[i] = (s >> 33) & 0x7FFFFFFF;
    }
    sort.ints(a);
    print(sort.is_sorted(a));
    let mut chk = 0;
    for i in 0..n { chk = (chk * 31 + a[i]) & 0xFFFFFFFFFFFF; }
    print(chk);
    return 0;
}
