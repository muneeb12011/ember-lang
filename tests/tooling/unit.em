// for `embc --test`: two passing tests, a failing assertion and a runtime error
fn add(a: int, b: int) -> int { return a + b; }
fn test_add() { assert(add(2, 2) == 4); }
fn test_fails() { print("some output"); assert(add(1, 1) == 3); }
fn test_strings() { assert("ab" + "c" == "abc"); }
fn test_traps() { let a = [1]; print(a[5]); }
fn main() -> int { return 0; }
