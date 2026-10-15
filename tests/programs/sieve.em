fn main() -> int {
    let n = 100000;
    let sieve = new_array(n + 1);
    let mut count = 0;
    let mut last = 0;
    for i in 2..n + 1 {
        if sieve[i] == 0 {
            count = count + 1;
            last = i;
            let mut j = i * i;
            while j <= n {
                sieve[j] = 1;
                j = j + i;
            }
        }
    }
    print(count);
    print(last);
    return 0;
}
