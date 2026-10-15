fn next(seed: int) -> int {
    return (seed * 1103515245 + 12345) % 2147483648;
}

fn main() -> int {
    let n = 300;
    let a = new_array(n);
    let mut seed = 42;
    for i in 0..n {
        seed = next(seed);
        a[i] = seed % 1000;
    }
    for i in 1..n {
        let key = a[i];
        let mut j = i - 1;
        while j >= 0 && a[j] > key {
            a[j + 1] = a[j];
            j = j - 1;
        }
        a[j + 1] = key;
    }
    let mut ok = true;
    let mut sum = 0;
    for i in 0..n {
        if i > 0 && a[i - 1] > a[i] { ok = false; }
        sum = sum + (i + 1) * a[i];
    }
    for i in 0..10 { print(a[i]); }
    print(a[n - 1]);
    print(ok);
    print(sum);
    return 0;
}
