// Every array access is bounds-checked at run time.
fn main() -> int {
    let scores = [90, 75, 88];
    for i in 0..4 {
        print(scores[i]);
    }
    return 0;
}
