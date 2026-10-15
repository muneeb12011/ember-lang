import "std/io.em" as io;

fn main() -> int {
    let n = read_int();
    let xs = io.read_ints(n);
    let mut s = 0;
    for i in 0..n { s += xs[i]; }
    print(s);
    let lines = io.read_lines();
    print(len(lines));
    for i in 0..len(lines) { print("[" + lines[i] + "]"); }
    print(at_eof());
    return 0;
}
