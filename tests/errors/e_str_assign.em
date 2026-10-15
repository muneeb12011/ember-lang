// expect: strs are immutable
fn main() -> int {
    let mut s = "abc";
    s[0] = 65;
    return 0;
}
