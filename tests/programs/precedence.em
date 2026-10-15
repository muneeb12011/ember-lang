// operator precedence: bitwise operators bind tighter than comparisons (Rust/Go order), & over ^ over |,
// shifts tighter than & but looser than + and -
fn main() -> int {
    for x in 0..6 { print(x & 1 == 1); }
    let a = 12;
    let b = 10;
    let c = 6;
    let d = 3;
    print(a | b ^ c & d);
    print(a | (b ^ (c & d)));
    print(1 << 2 + 1);
    print((1 << 2) + 1);
    print(0xF0 >> 4 & 3);
    print(5 & 3 < 2);
    print(6 ^ 3 != 5);
    print(1 | 2 == 3);
    print(7 & 4 > 0);
    return 0;
}
