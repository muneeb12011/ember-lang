// the n-body benchmark (Jovian planets), 2,000,000 steps
struct Body { x: float, y: float, z: float, vx: float, vy: float, vz: float, m: float }

fn energy(b: [Body]) -> float {
    let mut e = 0.0;
    for i in 0..len(b) {
        let p = b[i];
        e = e + 0.5 * p.m * (p.vx * p.vx + p.vy * p.vy + p.vz * p.vz);
        for j in i + 1..len(b) {
            let q = b[j];
            let dx = p.x - q.x;
            let dy = p.y - q.y;
            let dz = p.z - q.z;
            e = e - p.m * q.m / sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    return e;
}

fn advance(b: [Body], dt: float) {
    let n = len(b);
    for i in 0..n {
        let p = b[i];
        for j in i + 1..n {
            let q = b[j];
            let dx = p.x - q.x;
            let dy = p.y - q.y;
            let dz = p.z - q.z;
            let d2 = dx * dx + dy * dy + dz * dz;
            let mag = dt / (d2 * sqrt(d2));
            p.vx = p.vx - dx * q.m * mag;
            p.vy = p.vy - dy * q.m * mag;
            p.vz = p.vz - dz * q.m * mag;
            q.vx = q.vx + dx * p.m * mag;
            q.vy = q.vy + dy * p.m * mag;
            q.vz = q.vz + dz * p.m * mag;
        }
    }
    for i in 0..n {
        let p = b[i];
        p.x = p.x + dt * p.vx;
        p.y = p.y + dt * p.vy;
        p.z = p.z + dt * p.vz;
    }
}

fn main() -> int {
    let pi = 3.141592653589793;
    let sm = 4.0 * pi * pi;
    let dp = 365.24;
    let b = [
        Body { x: 0.0, y: 0.0, z: 0.0, vx: 0.0, vy: 0.0, vz: 0.0, m: sm },
        Body { x: 4.84143144246472090e+00, y: -1.16032004402742839e+00, z: -1.03622044471123109e-01,
               vx: 1.66007664274403694e-03 * dp, vy: 7.69901118419740425e-03 * dp, vz: -6.90460016972063023e-05 * dp,
               m: 9.54791938424326609e-04 * sm },
        Body { x: 8.34336671824457987e+00, y: 4.12479856412430479e+00, z: -4.03523417114321381e-01,
               vx: -2.76742510726862411e-03 * dp, vy: 4.99852801234917238e-03 * dp, vz: 2.30417297573763929e-05 * dp,
               m: 2.85885980666130812e-04 * sm },
        Body { x: 1.28943695621391310e+01, y: -1.51111514016986312e+01, z: -2.23307578892655734e-01,
               vx: 2.96460137564761618e-03 * dp, vy: 2.37847173959480950e-03 * dp, vz: -2.96589568540237556e-05 * dp,
               m: 4.36624404335156298e-05 * sm },
        Body { x: 1.53796971148509165e+01, y: -2.59193146099879641e+01, z: 1.79258772950371181e-01,
               vx: 2.68067772490389322e-03 * dp, vy: 1.62824170038242295e-03 * dp, vz: -9.51592254519715870e-05 * dp,
               m: 5.15138902046611451e-05 * sm }
    ];
    let mut px = 0.0;
    let mut py = 0.0;
    let mut pz = 0.0;
    for i in 0..len(b) {
        px = px + b[i].vx * b[i].m;
        py = py + b[i].vy * b[i].m;
        pz = pz + b[i].vz * b[i].m;
    }
    b[0].vx = -px / sm;
    b[0].vy = -py / sm;
    b[0].vz = -pz / sm;
    print(energy(b));
    for s in 0..2000000 { advance(b, 0.01); }
    print(energy(b));
    return 0;
}
