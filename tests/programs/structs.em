struct Vec { x: float, y: float }
struct Node { val: int, next: Node }
struct Tree { key: int, left: Tree, right: Tree }

fn push(head: Node, v: int) -> Node {
    return Node { val: v, next: head };
}

fn sum(n: Node) -> int {
    let mut s = 0;
    let mut c = n;
    while c != null {
        s = s + c.val;
        c = c.next;
    }
    return s;
}

fn insert(t: Tree, k: int) -> Tree {
    if t == null { return Tree { key: k, left: null, right: null }; }
    if k < t.key { t.left = insert(t.left, k); } else { t.right = insert(t.right, k); }
    return t;
}

fn height(t: Tree) -> int {
    if t == null { return 0; }
    let l = height(t.left);
    let r = height(t.right);
    if l > r { return l + 1; }
    return r + 1;
}

fn inorder(t: Tree, out: [int], pos: int) -> int {
    if t == null { return pos; }
    let p = inorder(t.left, out, pos);
    out[p] = t.key;
    return inorder(t.right, out, p + 1);
}

fn add(a: Vec, b: Vec) -> Vec {
    return Vec { y: a.y + b.y, x: a.x + b.x };
}

fn main() -> int {
    let mut head: Node = null;
    for i in 1..11 { head = push(head, i * i); }
    print(sum(head));
    print(head.val);

    let mut root: Tree = null;
    let mut seed = 7;
    for i in 0..200 {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        root = insert(root, seed % 1000);
    }
    print(height(root));
    let out = new_array(200);
    let n = inorder(root, out, 0);
    print(n);
    let mut sorted = true;
    for i in 1..n { if out[i - 1] > out[i] { sorted = false; } }
    print(sorted);
    print(out[0]);
    print(out[n - 1]);

    let v = add(Vec { x: 1.5, y: 2.5 }, Vec { x: 0.25, y: 0.5 });
    print(v.x);
    print(v.y);
    v.x = v.x * 2.0;
    print(v.x);
    return 0;
}
