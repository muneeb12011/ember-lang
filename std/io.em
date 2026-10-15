// std/io.em - reading standard input.
import "std/vec.em" as vec;

// the next n whitespace-separated ints
pub fn read_ints(n: int) -> [int] {
    let out = new [int](n);
    for i in 0..n { out[i] = read_int(); }
    return out;
}

// every remaining line (without its newline)
pub fn read_lines() -> [str] {
    let v: vec.Vec<str> = vec.make();
    while !at_eof() { vec.push(v, read_line()); }
    return vec.to_array(v);
}
