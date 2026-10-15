// GC torture: huge arrays allocated and dropped repeatedly (about 1.6 GB in total) under EMBER_HEAP_MB=400
fn main() -> int {
    let mut chk = 0;
    for round in 0..10 {
        let a = new [int](10000000);
        let b = new [byte](30000000);
        a[9999999] = round;
        b[29999999] = round * 3;
        let strs = new [str](100000);
        strs[99999] = str(round);
        chk += a[9999999] + b[29999999] + len(strs[99999]) + len(a) / 1000000;
    }
    print(chk);
    return 0;
}
