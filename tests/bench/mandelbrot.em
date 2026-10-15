// iterations summed over a 1000x1000 Mandelbrot grid, 100 iterations max
fn main() -> int {
    let size = 1000;
    let mut total = 0;
    for y in 0..size {
        let ci = float(y) * 2.0 / float(size) - 1.0;
        for x in 0..size {
            let cr = float(x) * 3.0 / float(size) - 2.0;
            let mut zr = 0.0;
            let mut zi = 0.0;
            let mut i = 0;
            while i < 100 && zr * zr + zi * zi <= 4.0 {
                let t = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = t;
                i += 1;
            }
            total += i;
        }
    }
    print(total);
    return 0;
}
