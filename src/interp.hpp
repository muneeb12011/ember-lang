// interp.hpp - reference interpreter for the IR.
//
// It exists for correctness, not speed: tests run every program through the interpreter AND the native
// backend, at -O0 and -O1, and require identical output. It also re-checks every bounds-check the
// compiler claimed was unnecessary, so an unsound proof shows up as an internal error instead of a crash.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <ctime>
#include <unistd.h>

#include "opt.hpp"

struct RuntimeError {
    std::string msg;
    int line;
};
struct ExitRequest {  // exit(code) in the interpreted program
    int code;
};

class Interp {
    const Module& m;
    std::unordered_map<std::string, int> fidx;
    std::vector<std::vector<int>> labelPc;
    std::vector<std::vector<long long>> arrays;  // handle = index + 1 (0 is never a valid array)
    std::vector<long long> strHandle;            // string literal index -> array handle (a str is an array of bytes)
    int depth = 0;
    static constexpr int kMaxDepth = 1000000;
    static constexpr long long kFnBase = 0x7E00000000000000LL;  // a function "address" in the interpreter: base + index
    std::vector<std::string> progArgs;
    long long emptyStr = 0;  // handle of the shared "" used to fill new [str] arrays

    // ---- strings are byte arrays; these mirror the C runtime exactly
    long long newStr(const std::string& s) {
        arrays.emplace_back();
        for (unsigned char c : s) arrays.back().push_back(c);
        return (long long)arrays.size();
    }
    std::string getStr(long long h, int line) {
        const auto& a = arr(h, line);
        std::string s;
        for (long long c : a) s += (char)c;
        return s;
    }
    static int ipeek() { int c = std::getchar(); if (c != EOF) std::ungetc(c, stdin); return c == EOF ? -1 : c; }
    static long long readInt() {
        int c;
        while ((c = ipeek()) == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') std::getchar();
        bool neg = false;
        if (c == '-' || c == '+') { neg = c == '-'; std::getchar(); }
        unsigned long long v = 0;
        while ((c = ipeek()) >= '0' && c <= '9') { v = v * 10 + (unsigned long long)(c - '0'); std::getchar(); }
        return (long long)(neg ? 0 - v : v);
    }
    static const char* panicMsg(long long kind) {
        static const char* m[] = {"index out of bounds", "division by zero", "negative array size", "out of memory",
                                  "null dereference", "assertion failed", "stack overflow"};
        return (kind >= 0 && kind < 7) ? m[kind] : "error";
    }

    long long rtCall(const std::string& n, const std::vector<long long>& a, int line) {
        if (n == "ember_str_concat") return newStr(getStr(a[0], line) + getStr(a[1], line));
        if (n == "ember_str_cmp") { int c = getStr(a[0], line).compare(getStr(a[1], line)); return c < 0 ? -1 : c > 0 ? 1 : 0; }
        if (n == "ember_substr") {
            std::string s = getStr(a[0], line);
            long long start = a[1], count = a[2], len = (long long)s.size();
            if (start < 0 || count < 0 || start > len || count > len - start) throw RuntimeError{panicMsg(0), (int)a[3]};
            return newStr(s.substr((size_t)start, (size_t)count));
        }
        if (n == "ember_chr") return newStr(std::string(1, (char)a[0]));
        if (n == "ember_str_from_int") return newStr(std::to_string(a[0]));
        if (n == "ember_str_from_float") { char b[400]; std::snprintf(b, sizeof b, "%.6f", dbl(a[0])); return newStr(b); }
        if (n == "ember_str_from_bool") return newStr(a[0] ? "true" : "false");
        if (n == "ember_parse_int") {
            std::string s = getStr(a[0], line);
            size_t i = 0;
            while (i < s.size() && (s[i] == ' ' || (s[i] >= 9 && s[i] <= 13))) i++;  // leading whitespace, like read_int
            bool neg = false;
            if (i < s.size() && (s[i] == '-' || s[i] == '+')) neg = s[i++] == '-';
            unsigned long long v = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') v = v * 10 + (unsigned long long)(s[i++] - '0');
            return (long long)(neg ? 0 - v : v);
        }
        if (n == "ember_put_str") { std::string s = getStr(a[0], line); std::fwrite(s.data(), 1, s.size(), stdout); return 0; }
        if (n == "ember_read_line") {
            std::string s;
            int c;
            while ((c = std::getchar()) != EOF && c != '\n') s += (char)c;
            return newStr(s);
        }
        if (n == "ember_at_eof") return ipeek() < 0;
        if (n == "ember_arg_count") return (long long)progArgs.size();
        if (n == "ember_arg") {
            if (a[0] < 0 || a[0] >= (long long)progArgs.size()) throw RuntimeError{panicMsg(0), (int)a[1]};
            return newStr(progArgs[(size_t)a[0]]);
        }
        if (n == "ember_panic_str") throw RuntimeError{"panic: " + getStr(a[0], line), (int)a[1]};
        if (n == "ember_panic") throw RuntimeError{panicMsg(a[0]), (int)a[1]};
        if (n == "ember_fmath") {
            double x = dbl(a[1]), y = dbl(a[2]);
            switch (a[0]) {
                case 0: return bits(std::sin(x));
                case 1: return bits(std::cos(x));
                case 2: return bits(std::tan(x));
                case 3: return bits(std::atan(x));
                case 4: return bits(std::atan2(x, y));
                case 5: return bits(std::exp(x));
                case 6: return bits(std::log(x));
                case 7: return bits(std::pow(x, y));
                case 8: return bits(std::floor(x));
                default: return bits(std::ceil(x));
            }
        }
        if (n == "ember_new_str_array") {
            if (a[0] < 0) throw RuntimeError{panicMsg(2), (int)a[1]};
            if (a[0] > (1LL << 31)) throw RuntimeError{panicMsg(3), (int)a[1]};
            arrays.emplace_back((size_t)a[0], emptyStr);
            return (long long)arrays.size();
        }
        if (n == "ember_str_from_bytes") {
            if (a[0] == 0) throw RuntimeError{panicMsg(4), (int)a[2]};
            const auto& b = arr(a[0], line);
            if (a[1] < 0 || a[1] > (long long)b.size()) throw RuntimeError{panicMsg(0), (int)a[2]};
            std::string s;
            for (long long i = 0; i < a[1]; i++) s += (char)b[(size_t)i];
            return newStr(s);
        }
        if (n == "ember_read_file") {
            std::string path = getStr(a[0], line);
            FILE* fh = std::fopen(path.c_str(), "rb");
            if (!fh) throw RuntimeError{"cannot read `" + path + "`: " + std::strerror(errno), (int)a[1]};
            std::string data;
            char buf[65536];
            size_t k;
            while ((k = std::fread(buf, 1, sizeof buf, fh)) > 0) data.append(buf, k);
            bool bad = std::ferror(fh);
            std::fclose(fh);
            if (bad) throw RuntimeError{"cannot read `" + path + "`: " + std::strerror(errno), (int)a[1]};
            return newStr(data);
        }
        if (n == "ember_write_file" || n == "ember_append_file") {
            std::string path = getStr(a[0], line), data = getStr(a[1], line);
            std::fflush(stdout);
            FILE* fh = std::fopen(path.c_str(), n == "ember_write_file" ? "wb" : "ab");
            if (!fh || std::fwrite(data.data(), 1, data.size(), fh) != data.size() || std::fclose(fh) != 0)
                throw RuntimeError{"cannot write `" + path + "`: " + std::strerror(errno), (int)a[2]};
            return 0;
        }
        if (n == "ember_file_exists") return access(getStr(a[0], line).c_str(), F_OK) == 0;
        if (n == "ember_env") {
            const char* v = std::getenv(getStr(a[0], line).c_str());
            return newStr(v ? v : "");
        }
        if (n == "ember_has_env") return std::getenv(getStr(a[0], line).c_str()) != nullptr;
        if (n == "ember_exit") throw ExitRequest{(int)(a[0] & 255)};
        if (n == "ember_clock_ns") {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
        }
        throw RuntimeError{"INTERNAL: interpreter does not know runtime function " + n, line};
    }

    static double dbl(long long b) { double d; std::memcpy(&d, &b, 8); return d; }
    static long long bits(double d) { long long b; std::memcpy(&b, &d, 8); return b; }

    long long val(const std::vector<long long>& r, const Opd& o) const { return o.isImm() ? o.v : r[o.v]; }

    std::vector<long long>& arr(long long h, int line) {
        if (h == 0) throw RuntimeError{"null dereference", line};
        if (h < 0 || h > (long long)arrays.size()) throw RuntimeError{"invalid reference", line};
        return arrays[h - 1];
    }

    long long run(int fi, const std::vector<long long>& args) {
        const IRFunc& f = m.funcs[fi];
        if (++depth > kMaxDepth) throw RuntimeError{"stack overflow (recursion too deep)", 0};
        std::vector<long long> r(f.nregs > 0 ? f.nregs : 1, 0);
        for (size_t i = 0; i < args.size(); i++) r[i] = args[i];
        const auto& lp = labelPc[fi];
        size_t pc = 0, n = f.code.size();
        long long ret = 0;
        while (pc < n) {
            const Inst& in = f.code[pc];
            switch (in.op) {
                case Op::Label: break;
                case Op::Mov: r[in.dst] = val(r, in.a); break;
                case Op::Add: case Op::Sub: case Op::Mul: case Op::Div: case Op::Mod:
                case Op::And: case Op::Or: case Op::Xor: case Op::Shl: case Op::Shr:
                case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: {
                    long long out;
                    if (!opt::evalBin(in.op, val(r, in.a), val(r, in.b), out)) throw RuntimeError{"division by zero", in.line};
                    r[in.dst] = out;
                    break;
                }
                case Op::Neg: r[in.dst] = opt::wsub(0, val(r, in.a)); break;
                case Op::Not: r[in.dst] = val(r, in.a) ^ 1; break;
                case Op::BNot: r[in.dst] = ~val(r, in.a); break;
                case Op::Rt: {
                    std::vector<long long> a;
                    for (const Opd& o : in.args) a.push_back(val(r, o));
                    long long v = rtCall(in.name, a, in.line);
                    if (in.dst >= 0) r[in.dst] = v;
                    break;
                }
                case Op::Jmp: pc = lp[in.t1]; continue;
                case Op::Br: pc = lp[val(r, in.a) != 0 ? in.t1 : in.t2]; continue;
                case Op::Call: {
                    std::vector<long long> a;
                    for (const Opd& o : in.args) a.push_back(val(r, o));
                    long long v = run(fidx.at(in.name), a);
                    if (in.dst >= 0) r[in.dst] = v;
                    break;
                }
                case Op::FnAddr: r[in.dst] = kFnBase + fidx.at(in.name); break;
                case Op::CallInd: {
                    long long h = val(r, in.a);
                    long long code = arr(h, in.line)[0];
                    if (code < kFnBase || code >= kFnBase + (long long)m.funcs.size())
                        throw RuntimeError{"INTERNAL: a closure without code", in.line};
                    std::vector<long long> a;
                    for (const Opd& o : in.args) a.push_back(val(r, o));
                    a.push_back(h);  // the closure itself is the hidden last argument
                    long long v = run((int)(code - kFnBase), a);
                    if (in.dst >= 0) r[in.dst] = v;
                    break;
                }
                case Op::Ret:
                    ret = in.a.k == Opd::None ? 0 : val(r, in.a);
                    depth--;
                    return ret;
                case Op::Load: {
                    auto& a = arr(val(r, in.a), in.line);
                    long long i = val(r, in.b);
                    if (i < 0 || i >= (long long)a.size()) {
                        if (in.nocheck) throw RuntimeError{"INTERNAL: a bounds check was removed but the access is out of range", in.line};
                        throw RuntimeError{"index out of bounds", in.line};
                    }
                    r[in.dst] = a[i];
                    break;
                }
                case Op::Store: {
                    auto& a = arr(val(r, in.a), in.line);
                    long long i = val(r, in.b);
                    if (i < 0 || i >= (long long)a.size()) {
                        if (in.nocheck) throw RuntimeError{"INTERNAL: a bounds check was removed but the access is out of range", in.line};
                        throw RuntimeError{"index out of bounds", in.line};
                    }
                    a[i] = in.w == 1 ? (val(r, in.c) & 0xFF) : val(r, in.c);  // [byte]/[bool] store one byte
                    break;
                }
                case Op::Len: r[in.dst] = (long long)arr(val(r, in.a), in.line).size(); break;
                case Op::NewArr: {
                    long long sz = val(r, in.a);
                    if (sz < 0) throw RuntimeError{"negative array size", in.line};
                    if (sz > (1LL << 31)) throw RuntimeError{"out of memory", in.line};
                    arrays.emplace_back((size_t)sz, 0);
                    r[in.dst] = (long long)arrays.size();
                    break;
                }
                case Op::FAdd: case Op::FSub: case Op::FMul: case Op::FDiv: case Op::FEq: case Op::FNe:
                case Op::FLt: case Op::FLe: case Op::FGt: case Op::FGe: {
                    double a = dbl(val(r, in.a)), b = dbl(val(r, in.b));
                    switch (in.op) {
                        case Op::FAdd: r[in.dst] = bits(a + b); break;
                        case Op::FSub: r[in.dst] = bits(a - b); break;
                        case Op::FMul: r[in.dst] = bits(a * b); break;
                        case Op::FDiv: r[in.dst] = bits(a / b); break;
                        case Op::FEq: r[in.dst] = a == b; break;
                        case Op::FNe: r[in.dst] = a != b; break;
                        case Op::FLt: r[in.dst] = a < b; break;
                        case Op::FLe: r[in.dst] = a <= b; break;
                        case Op::FGt: r[in.dst] = a > b; break;
                        default: r[in.dst] = a >= b; break;
                    }
                    break;
                }
                case Op::FNeg: r[in.dst] = (long long)((unsigned long long)val(r, in.a) ^ 0x8000000000000000ULL); break;
                case Op::IToF: r[in.dst] = bits((double)val(r, in.a)); break;
                case Op::FToI: {  // same result as cvttsd2si: out of range and NaN give INT64_MIN
                    double d = dbl(val(r, in.a));
                    r[in.dst] = (d != d || d >= 9223372036854775808.0 || d < -9223372036854775808.0) ? INT64_MIN : (long long)d;
                    break;
                }
                case Op::FSqrt: r[in.dst] = bits(std::sqrt(dbl(val(r, in.a)))); break;
                case Op::PrintFloat: std::printf("%.6f\n", dbl(val(r, in.a))); break;
                case Op::Str: r[in.dst] = strHandle[in.a.v]; break;
                case Op::PrintInt: std::printf("%lld\n", val(r, in.a)); break;
                case Op::PrintBool: std::printf("%s\n", val(r, in.a) ? "true" : "false"); break;
                case Op::PrintStr: {
                    const auto& a = arr(val(r, in.a), in.line);
                    for (long long c : a) std::putchar((int)c);
                    std::putchar('\n');
                    break;
                }
                case Op::ReadInt: r[in.dst] = readInt(); break;
            }
            pc++;
        }
        depth--;
        return ret;
    }

public:
    explicit Interp(const Module& mod, std::vector<std::string> args = {}) : m(mod), progArgs(std::move(args)) {
        arrays.emplace_back();  // handle 1: the shared empty string
        emptyStr = 1;
        for (const std::string& s : m.strings) {
            arrays.emplace_back();
            for (unsigned char c : s) arrays.back().push_back(c);
            strHandle.push_back((long long)arrays.size());
        }
        for (size_t i = 0; i < m.funcs.size(); i++) {
            fidx[m.funcs[i].name] = (int)i;
            std::vector<int> lp(m.funcs[i].nlabels + 1, 0);
            for (size_t j = 0; j < m.funcs[i].code.size(); j++)
                if (m.funcs[i].code[j].op == Op::Label) lp[m.funcs[i].code[j].t1] = (int)j;
            labelPc.push_back(std::move(lp));
        }
    }

    // Returns main's result; throws RuntimeError on a runtime failure.
    long long runMain() { return run(fidx.at("main"), {}); }
};
