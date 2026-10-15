fn main() -> int {
    let n = read_int();
    let mut s = 0;
    for i in 0..n { s = s + read_int(); }
    print(s);
    return 0;
}
