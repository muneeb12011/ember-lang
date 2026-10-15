struct Point { x: int, y: int }

fn add(a: Point, b: Point) -> Point {
    return Point { x: a.x + b.x, y: a.y + b.y };
}

fn show(p: Point) -> str {
    return "(" + str(p.x) + ", " + str(p.y) + ")";
}

fn main() -> int {
    let a = Point { x: 1, y: 2 };
    let b = Point { x: 10, y: 20 };
    let c = add(a, b);
    print(show(a) + " + " + show(b) + " = " + show(c));

    // structs are references: both names see the change
    let d = c;
    d.x = 99;
    print("c is now " + show(c));
    return 0;
}
