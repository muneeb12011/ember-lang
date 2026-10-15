// GC torture: very deep structures. The collector marks with an explicit stack, so a 2,000,000-node list and a
// 1,000,000-deep chain of enum values must not overflow anything.
struct Cell { v: int, next: Cell }
enum Chain { Link(Chain, int), End }

fn main() -> int {
    let mut head: Cell = null;
    for i in 0..2000000 { head = Cell { v: i, next: head }; }
    let mut c = Chain.End;
    for i in 0..1000000 { c = Chain.Link(c, i); }
    let mut junk = 0;
    for i in 0..200000 { junk += len(str(i) + "x"); }  // allocate enough to force collections while both are live
    let mut n = 0;
    let mut s = 0;
    let mut p = head;
    while p != null {
        n += 1;
        s += p.v;
        p = p.next;
    }
    print(n);
    print(s);
    let mut depth = 0;
    let mut done = false;
    while !done {
        match c {
            Link(rest, _) => { depth += 1; c = rest; }
            End => { done = true; }
        }
    }
    print(depth);
    print(junk);
    return 0;
}
