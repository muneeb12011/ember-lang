// expect: must not contain a call
fn f() -> int { return 0; }

fn main() -> int {
    let a = new [int](3);
    a[f()] += 1;
    return 0;
}
