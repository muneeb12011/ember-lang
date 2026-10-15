// Run this, then compare the IR and Optimized stages:
// the optimizer folds the constant 10 into the comparison,
// removes the copy of i, and merges the add into total.
// The x86-64 stage shows total and i living in registers.
fn main() -> int {
    let mut total = 0;
    for i in 0..10 {
        total += i * i;
    }
    print(total);
    return 0;
}
