// files, environment and exit
fn main() -> int {
    let dir = env("EMBER_TEST_DIR");
    let path = dir + "/ember-files-test.txt";
    write_file(path, "first line\n");
    append_file(path, "second line\n");
    let text = read_file(path);
    print(len(text));
    put(text);
    print(file_exists(path));
    print(file_exists(dir + "/no-such-file-here.txt"));
    print(has_env("EMBER_TEST_DIR"));
    print(has_env("EMBER_SURELY_UNSET_VARIABLE"));
    print("[" + env("EMBER_SURELY_UNSET_VARIABLE") + "]");
    let t0 = clock_ns();
    let t1 = clock_ns();
    print(t1 >= t0);
    write_file(path, "");
    print(len(read_file(path)));
    print("exiting");
    exit(7);
    print("not reached");
    return 0;
}
