const GREETING = "hi";
const N = 5;
const PI = 3.5;

struct P { name: str, tags: [str] }

fn main() -> int {
    let s = GREETING + ", " + "world";
    print(s);
    print(len(s));
    print(s[0]);
    print(substr(s, 4, 5));
    print("apple" < "banana");
    print("b" == "b");
    print(str(42) + str(true) + str(1.5));
    print(parse_int("  -17x"));
    print(chr(65) + chr(66));
    let bits = (0xFF & 0x0F) | (1 << 4) ^ 3;
    print(bits);
    print(~5);
    print(-16 >> 2);
    let mut x = 10;
    x += N;
    x <<= 2;
    x |= 1;
    print(x);
    let b = new [byte](4);
    b[0] = 300;
    b[1] = 65;
    print(b[0]);
    print(str_from_bytes(b, 2) == chr(44) + "A");
    let names = new [str](3);
    names[1] = "mid";
    print(names[0] == "");
    print(names[1]);
    let grid = new [[int]](2);
    grid[0] = new [int](3);
    grid[0][2] = 9;
    print(grid[0][2]);
    let p = P { name: "n", tags: names };
    print(p.tags[1]);
    print(floor(2.7));
    print(pow(2.0, 10.0));
    print(PI);
    assert(N == 5);
    return 0;
}
