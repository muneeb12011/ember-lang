fn safe(cols: [int], row: int, c: int) -> bool {
    for r in 0..row {
        let d = row - r;
        if cols[r] == c || cols[r] == c - d || cols[r] == c + d { return false; }
    }
    return true;
}

fn solve(cols: [int], row: int, n: int) -> int {
    if row == n { return 1; }
    let mut total = 0;
    for c in 0..n {
        if safe(cols, row, c) {
            cols[row] = c;
            total = total + solve(cols, row + 1, n);
        }
    }
    return total;
}

fn main() -> int {
    for n in 4..10 {
        let cols = new_array(n);
        print(solve(cols, 0, n));
    }
    return 0;
}
