// builtins.hpp - built-in functions that are plain calls into the C runtime. One table drives the type checker,
// the lowering and the interpreter, so they cannot drift apart.
#pragma once
#include <string>
#include <vector>

#include "ast.hpp"

struct Builtin {
    const char* name;
    std::vector<Ty> params;
    Ty ret;
    const char* rt;   // runtime symbol
    int fmath;        // >= 0: call ember_fmath(fmath, a, b) (all math functions share one entry point)
    bool passLine;    // append the source line as a last argument (for runtime errors)
};

inline const std::vector<Builtin>& builtinTable() {
    static const std::vector<Builtin> t = {
        {"substr", {TyStr, TyInt, TyInt}, TyStr, "ember_substr", -1, true},
        {"chr", {TyInt}, TyStr, "ember_chr", -1, false},
        {"parse_int", {TyStr}, TyInt, "ember_parse_int", -1, false},
        {"put", {TyStr}, TyUnit, "ember_put_str", -1, false},
        {"read_line", {}, TyStr, "ember_read_line", -1, false},
        {"at_eof", {}, TyBool, "ember_at_eof", -1, false},
        {"arg_count", {}, TyInt, "ember_arg_count", -1, false},
        {"arg", {TyInt}, TyStr, "ember_arg", -1, true},
        {"panic", {TyStr}, TyUnit, "ember_panic_str", -1, true},
        {"str_from_bytes", {arrayOf(TyByte), TyInt}, TyStr, "ember_str_from_bytes", -1, true},
        {"sin", {TyFloat}, TyFloat, "ember_fmath", 0, false},
        {"cos", {TyFloat}, TyFloat, "ember_fmath", 1, false},
        {"tan", {TyFloat}, TyFloat, "ember_fmath", 2, false},
        {"atan", {TyFloat}, TyFloat, "ember_fmath", 3, false},
        {"atan2", {TyFloat, TyFloat}, TyFloat, "ember_fmath", 4, false},
        {"exp", {TyFloat}, TyFloat, "ember_fmath", 5, false},
        {"log", {TyFloat}, TyFloat, "ember_fmath", 6, false},
        {"pow", {TyFloat, TyFloat}, TyFloat, "ember_fmath", 7, false},
        {"floor", {TyFloat}, TyFloat, "ember_fmath", 8, false},
        {"ceil", {TyFloat}, TyFloat, "ember_fmath", 9, false},
        // files and the process; an I/O failure is a runtime error naming the file and the reason
        {"read_file", {TyStr}, TyStr, "ember_read_file", -1, true},
        {"write_file", {TyStr, TyStr}, TyUnit, "ember_write_file", -1, true},
        {"append_file", {TyStr, TyStr}, TyUnit, "ember_append_file", -1, true},
        {"file_exists", {TyStr}, TyBool, "ember_file_exists", -1, false},
        {"env", {TyStr}, TyStr, "ember_env", -1, false},          // "" when the variable is not set
        {"has_env", {TyStr}, TyBool, "ember_has_env", -1, false},
        {"exit", {TyInt}, TyUnit, "ember_exit", -1, false},       // flushes output, exits with code & 255
        {"clock_ns", {}, TyInt, "ember_clock_ns", -1, false},     // monotonic clock, for timing
    };
    return t;
}

inline const Builtin* findBuiltin(const std::string& n) {
    for (const Builtin& b : builtinTable())
        if (n == b.name) return &b;
    return nullptr;
}

// built-ins with special typing rules, handled by hand in sema and lowering
inline bool isSpecialBuiltin(const std::string& n) {
    return n == "print" || n == "len" || n == "new_array" || n == "new_floats" || n == "read_int" || n == "float" ||
           n == "int" || n == "sqrt" || n == "str" || n == "assert" || n == "new";
}
