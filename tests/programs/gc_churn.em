struct Node { s: str, next: Node, v: int }

fn main() -> int {
    let mut head: Node = null;
    let mut i = 0;
    while i < 3000 {
        let junk = new [int](50);
        junk[3] = i;
        let t = str(i) + ":" + str(i * 7);
        head = Node { s: t, next: head, v: junk[3] };
        i += 1;
    }
    let mut n = head;
    let mut sum = 0;
    let mut chars = 0;
    let mut cnt = 0;
    while n != null {
        sum += n.v;
        chars += len(n.s);
        cnt += 1;
        n = n.next;
    }
    print(cnt);
    print(sum);
    print(chars);
    print(head.s);
    return 0;
}
