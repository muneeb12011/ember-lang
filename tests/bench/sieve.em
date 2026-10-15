// primes below 20,000,000 with a [bool] sieve
fn main() -> int {
    let n = 20000000;
    let composite = new [bool](n + 1);
    let mut count = 0;
    let mut i = 2;
    while i <= n {
        if !composite[i] {
            count += 1;
            let mut j = i * i;
            while j <= n {
                composite[j] = true;
                j += i;
            }
        }
        i += 1;
    }
    print(count);
    return 0;
}
