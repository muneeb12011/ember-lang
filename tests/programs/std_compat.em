// the pre-module prefixed names still work (through std/compat/*.em) for one release
import "std/vec.em";
import "std/math.em";
import "std/sort.em";

fn main() -> int {
    let v = vec_new();
    for i in 0..20 { vec_push(v, i * i); }
    print(vec_len(v));
    print(vec_get(v, 7));
    vec_set(v, 7, -1);
    print(vec_get(v, 7));
    print(vec_pop(v));
    print(vec_len(v));
    print(vec_sum(v));
    print(vec_index_of(v, 16));
    print(vec_index_of(v, 17));
    print(vec_contains(v, 100));
    vec_reverse(v);
    print(vec_get(v, 0));
    print(vec_get(v, 18));
    let a = vec_to_array(v);
    print(len(a));
    let w = vec_from_array(a);
    print(vec_sum(w));
    vec_clear(v);
    print(vec_len(v));
    let s = strvec_new();
    strvec_push(s, "a");
    strvec_push(s, "bb");
    for i in 0..9 { strvec_push(s, str(i)); }
    print(strvec_len(s));
    print(strvec_get(s, 1));
    print(strvec_get(s, 10));
    print(len(strvec_to_array(s)));
    print(math_gcd(84, 36));
    print(sort_is_sorted(vec_to_array(w)));
    return 0;
}
