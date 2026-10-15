enum Shape { Circle(float), Rect(float, float), Triangle(float, float) }

fn area(s: Shape) -> float {
    match s {
        Circle(r) => { return 3.14159 * r * r; }
        Rect(w, h) => { return w * h; }
        Triangle(b, h) => { return 0.5 * b * h; }
    }
}

fn name(s: Shape) -> str {
    match s {
        Circle(_) => { return "circle"; }
        Rect(_, _) => { return "rectangle"; }
        _ => { return "triangle"; }
    }
}

fn main() -> int {
    let shapes = [Shape.Circle(1.0), Shape.Rect(2.0, 3.0), Shape.Triangle(4.0, 5.0)];
    for i in 0..len(shapes) {
        print(name(shapes[i]) + ": " + str(area(shapes[i])));
    }
    // Try deleting the Triangle arm in area(): the compiler
    // refuses, because match must cover every variant.
    return 0;
}
