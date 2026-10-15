// expect: 3 errors
fn main() -> int {
    let a = 1 + true;
    let b = missing;
    if 5 { print(a); }
    return 0;
}
