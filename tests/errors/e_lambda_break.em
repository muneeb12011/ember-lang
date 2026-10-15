// expect: `break` outside of a loop
fn main() -> int { while true { let f = fn() { break; }; } return 0; }
