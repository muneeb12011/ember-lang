// expect: function `f` is defined twice
fn f() -> int { return 1; }
fn f() -> int { return 2; }
fn main() -> int { return f(); }
