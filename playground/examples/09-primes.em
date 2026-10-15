// Sieve of Eratosthenes: every prime below 100
fn main() -> int {
    let n = 100;
    let composite = new [bool](n);
    let mut line = "";
    for i in 2..n {
        if !composite[i] {
            line = line + str(i) + " ";
            let mut j = i * i;
            while j < n {
                composite[j] = true;
                j += i;
            }
        }
    }
    print(line);
    return 0;
}
