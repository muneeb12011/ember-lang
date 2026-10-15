// GC torture: millions of small strings, 50,000 of them kept alive at any time in a ring of slots
fn main() -> int {
    let keep = new [str](50000);
    let mut total = 0;
    for i in 0..3000000 {
        let s = "k" + str(i % 977) + ":" + str(i);
        keep[i % 50000] = s;
        total += len(s);
    }
    let mut live = 0;
    for i in 0..50000 { live += len(keep[i]); }
    print(total);
    print(live);
    print(keep[12345]);
    return 0;
}
