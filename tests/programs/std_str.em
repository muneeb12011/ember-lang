import "std/str.em" as strings;

fn show(parts: [str]) {
    for i in 0..len(parts) { print("[" + parts[i] + "]"); }
}

fn main() -> int {
    print("[" + strings.trim("  Hello, World!  ") + "]");
    print(strings.upper("abc XyZ 09"));
    print(strings.lower("ABC XyZ 09"));
    print(strings.reverse("stressed"));
    print(strings.repeat("ab", 3));
    print("[" + strings.repeat("x", 0) + "]");
    print(strings.find("hello world", "o w"));
    print(strings.find("hello", "xyz"));
    print(strings.find("aaa", ""));
    print(strings.contains("hello", "ell"));
    print(strings.starts_with("hello", "he"));
    print(strings.starts_with("he", "hello"));
    print(strings.ends_with("hello", "llo"));
    print(strings.ends_with("hello", ""));
    print(strings.count("abababa", "aba"));
    print(strings.count("aaaa", "aa"));
    print(strings.replace("a-b-c", "-", "--"));
    print(strings.replace("abc", "x", "y"));
    print(strings.replace("aaa", "aa", "b"));
    print(strings.replace("hello", "l", ""));
    show(strings.split("a,b,,c", ","));
    show(strings.split("", ","));
    show(strings.split("one::two::", "::"));
    print(strings.join(strings.split("1 2 3", " "), "+"));
    print("[" + strings.join(new [str](0), ",") + "]");
    print(strings.pad_left("42", 5, 48));
    print(strings.pad_left("12345", 3, 32));
    print(strings.is_digit(53));
    print(strings.is_alpha(53));
    print(strings.is_space(10));
    return 0;
}
