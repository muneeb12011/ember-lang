// reading a file that does not exist is a runtime error naming the file and the reason
fn main() -> int {
    print("before");
    let s = read_file("/definitely/not/here.txt");
    print(len(s));
    return 0;
}
