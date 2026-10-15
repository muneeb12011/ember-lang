fn sum_to(n: int, acc: int) -> int {
    if n == 0 { return acc; }
    return sum_to(n - 1, acc + n);
}

fn count_down(n: int) {
    if n == 0 {
        print("done");
        return;
    }
    count_down(n - 1);
}

fn swap_args(a: int, b: int, k: int) -> int {
    if k == 0 { return a * 1000 + b; }
    return swap_args(b, a, k - 1);
}

fn main() -> int {
    print(sum_to(50000, 0));
    count_down(30000);
    print(swap_args(1, 2, 5));
    print(swap_args(1, 2, 6));
    return 0;
}
