fn mandel(cr: float, ci: float, maxit: int) -> int {
    let mut zr = 0.0;
    let mut zi = 0.0;
    let mut i = 0;
    while i < maxit && zr * zr + zi * zi <= 4.0 {
        let t = zr * zr - zi * zi + cr;
        zi = 2.0 * zr * zi + ci;
        zr = t;
        i = i + 1;
    }
    return i;
}

fn main() -> int {
    let mut total = 0;
    for y in 0..40 {
        for x in 0..80 {
            total = total + mandel(float(x) / 80.0 * 3.5 - 2.5, float(y) / 40.0 * 2.0 - 1.0, 200);
        }
    }
    print(total);
    return 0;
}
