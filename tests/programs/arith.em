fn main() -> int {
    let big = 9223372036854775807;
    print(big + 1);
    print(big * 2);
    let m = 0 - big - 1;
    print(m);
    print(m / (0 - 1));
    print(m % (0 - 1));
    print(7 / 2);
    print(-7 / 2);
    print(7 / (0 - 2));
    print(-7 % 3);
    print(7 % (0 - 3));
    print(-7 % (0 - 3));
    print(-big);
    print(-m);
    let k = 0 - 1;
    print(m / k);
    print(m % k);
    print(1 - 2 - 3);
    print(2 + 3 * 4 - 10 / 3);
    print(100 % 7 * 3);
    return 0;
}
