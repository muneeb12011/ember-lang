// wasm.hpp - the WebAssembly back end (embc --target wasm): optimized IR to a wasm module, encoded directly.
//
//   * every virtual register is an i64 local; pointers are 32-bit addresses in the shared linear memory, kept
//     zero-extended in i64 values, so the IR (and every optimization) is the same as for x86-64;
//   * the IR's arbitrary jumps become a dispatch loop: the basic blocks are nested `block`s inside one `loop`,
//     entered through a `br_table` on a block-number local; a block that falls through to the next one just ends;
//   * runtime calls are imports from the runtime module (rt/ember_rt_wasm.c), which shares the memory;
//   * function values index the module's table, and calls through them are `call_indirect`;
//   * every trap the x86-64 code checks (null, bounds, division by zero, negative sizes) is checked explicitly with
//     the same message and line, so programs behave identically, which the test suite verifies.
#pragma once
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "ir.hpp"

class WasmGen {
    const Module& m;
    using Bytes = std::vector<uint8_t>;

    struct Sig { int nparams; bool result; bool operator<(const Sig& o) const { return nparams != o.nparams ? nparams < o.nparams : result < o.result; } };
    std::map<Sig, int> typeIdx;
    std::vector<Sig> types;
    std::vector<std::pair<std::string, Sig>> imports;
    std::unordered_map<std::string, int> importIdx, funcIdx, tableIdx;
    std::vector<std::string> tableFns;
    std::vector<uint32_t> strAddr;
    Bytes data;
    static constexpr uint32_t kDataBase = 1024, kDataLimit = 16u << 20;

    static void uleb(Bytes& b, uint64_t v) {
        do {
            uint8_t c = v & 0x7f;
            v >>= 7;
            if (v) c |= 0x80;
            b.push_back(c);
        } while (v);
    }
    static void sleb(Bytes& b, int64_t v) {
        bool more = true;
        while (more) {
            uint8_t c = v & 0x7f;
            v >>= 7;  // arithmetic
            if ((v == 0 && !(c & 0x40)) || (v == -1 && (c & 0x40))) more = false;
            else c |= 0x80;
            b.push_back(c);
        }
    }
    static void name(Bytes& b, const std::string& s) {
        uleb(b, s.size());
        b.insert(b.end(), s.begin(), s.end());
    }
    static void section(Bytes& out, uint8_t id, const Bytes& body) {
        out.push_back(id);
        uleb(out, body.size());
        out.insert(out.end(), body.begin(), body.end());
    }
    int typeOf(int nparams, bool result) {
        Sig s{nparams, result};
        auto it = typeIdx.find(s);
        if (it != typeIdx.end()) return it->second;
        typeIdx[s] = (int)types.size();
        types.push_back(s);
        return (int)types.size() - 1;
    }
    void useImport(const std::string& n, int nparams, bool result) {
        if (importIdx.count(n)) return;
        importIdx[n] = (int)imports.size();
        imports.push_back({n, Sig{nparams, result}});
    }

    // ---- per-function emission
    Bytes c;  // code of the current function
    const IRFunc* f = nullptr;
    int pcLocal = 0, fLocal = 0;

    void op(uint8_t o) { c.push_back(o); }
    void get(const Opd& v) {
        if (v.isImm()) { op(0x42); sleb(c, v.v); }
        else { op(0x20); uleb(c, (uint64_t)v.v); }
    }
    void set(int r) { op(0x21); uleb(c, (uint64_t)r); }
    void i64c(long long v) { op(0x42); sleb(c, v); }
    void i32c(int32_t v) { op(0x41); sleb(c, v); }
    void call(const std::string& rtName) { op(0x10); uleb(c, (uint64_t)importIdx.at(rtName)); }
    void panicIf(int kind, int line) {  // condition (i32) on the stack
        op(0x04); op(0x40);
        i64c(kind);
        i64c(line);
        call("ember_panic");
        op(0x00);
        op(0x0B);
    }
    void addr32(const Opd& base) { get(base); op(0xA7); }  // i32.wrap_i64
    void mem(uint8_t o, int align, uint32_t offset) { op(o); uleb(c, (uint64_t)align); uleb(c, offset); }

    void emitInst(const Inst& in, int depthToLoop, const std::vector<int>& blockOfLabel, int nextBlock, bool last) {
        auto jumpTo = [&](int label) {
            int b = blockOfLabel[label];
            if (last && b == nextBlock) return;  // falls through
            i32c(b);
            set(pcLocal);
            op(0x0C); uleb(c, (uint64_t)depthToLoop);
        };
        auto bin = [&](uint8_t o) { get(in.a); get(in.b); op(o); set(in.dst); };
        auto cmp = [&](uint8_t o) { get(in.a); get(in.b); op(o); op(0xAD); set(in.dst); };
        auto fbin = [&](uint8_t o) { get(in.a); op(0xBF); get(in.b); op(0xBF); op(o); op(0xBD); set(in.dst); };
        auto fcmp = [&](uint8_t o) { get(in.a); op(0xBF); get(in.b); op(0xBF); op(o); op(0xAD); set(in.dst); };
        auto nullCheck = [&](const Opd& base) {
            if (in.nonnull) return;
            get(base); op(0x50);  // i64.eqz
            panicIf(4, in.line);
        };
        switch (in.op) {
            case Op::Label: break;
            case Op::Mov: get(in.a); set(in.dst); break;
            case Op::Add: bin(0x7C); break;
            case Op::Sub: bin(0x7D); break;
            case Op::Mul: bin(0x7E); break;
            case Op::And: bin(0x83); break;
            case Op::Or: bin(0x84); break;
            case Op::Xor: bin(0x85); break;
            case Op::Shl: bin(0x86); break;   // wasm masks the count to 6 bits, as the language says
            case Op::Shr: bin(0x87); break;
            case Op::Div: case Op::Mod:
                get(in.b); op(0x50);
                panicIf(1, in.line);
                get(in.b); i64c(-1); op(0x51);  // x / -1 wraps (wasm would trap on INT64_MIN / -1)
                op(0x04); op(0x7E);
                if (in.op == Op::Div) { i64c(0); get(in.a); op(0x7D); }
                else i64c(0);
                op(0x05);
                get(in.a); get(in.b); op(in.op == Op::Div ? 0x7F : 0x81);
                op(0x0B);
                set(in.dst);
                break;
            case Op::Eq: cmp(0x51); break;
            case Op::Ne: cmp(0x52); break;
            case Op::Lt: cmp(0x53); break;
            case Op::Le: cmp(0x57); break;
            case Op::Gt: cmp(0x55); break;
            case Op::Ge: cmp(0x59); break;
            case Op::Neg: i64c(0); get(in.a); op(0x7D); set(in.dst); break;
            case Op::Not: get(in.a); i64c(1); op(0x85); set(in.dst); break;
            case Op::BNot: get(in.a); i64c(-1); op(0x85); set(in.dst); break;
            case Op::Jmp: jumpTo(in.t1); break;
            case Op::Br: {
                int b1 = blockOfLabel[in.t1], b2 = blockOfLabel[in.t2];
                if (last && b2 == nextBlock) {  // br_if to the true target, else fall through
                    i32c(b1); set(pcLocal);
                    get(in.a); op(0x50); op(0x45);  // (a != 0) as i32
                    op(0x0D); uleb(c, (uint64_t)depthToLoop);
                } else {
                    i32c(b1); i32c(b2);
                    get(in.a); op(0x50); op(0x45);
                    op(0x1B);  // select
                    set(pcLocal);
                    op(0x0C); uleb(c, (uint64_t)depthToLoop);
                }
                break;
            }
            case Op::Call:
                for (const Opd& a : in.args) get(a);
                op(0x10); uleb(c, (uint64_t)funcIdx.at(in.name));
                if (in.dst >= 0) set(in.dst);
                break;
            case Op::Ret:
                if (in.a.k != Opd::None && !f->retUnit) get(in.a);
                op(0x0F);
                break;
            case Op::Load: case Op::Store: {
                nullCheck(in.a);
                bool byte = in.w == 1;
                if (!in.nocheck) {  // (unsigned) index >= length
                    get(in.b); addr32(in.a); mem(0x29, 3, 0); op(0x5A);
                    panicIf(0, in.line);
                }
                uint32_t off = 8;
                if (in.b.isImm() && in.b.v >= 0 && in.b.v < (1LL << 26)) {
                    addr32(in.a);
                    off += (uint32_t)(byte ? in.b.v : 8 * in.b.v);
                } else {
                    addr32(in.a);
                    get(in.b); op(0xA7);
                    if (!byte) { i32c(3); op(0x74); }
                    op(0x6A);
                }
                if (in.op == Op::Load) {
                    mem(byte ? 0x31 : 0x29, byte ? 0 : 3, off);
                    set(in.dst);
                } else {
                    get(in.c);
                    mem(byte ? 0x3C : 0x37, byte ? 0 : 3, off);
                }
                break;
            }
            case Op::Len: nullCheck(in.a); addr32(in.a); mem(0x29, 3, 0); set(in.dst); break;
            case Op::NewArr:
                get(in.a); i64c(in.w); i64c(in.noscan ? 1 : 0); i64c(in.line);
                call("ember_new_array");
                set(in.dst);
                break;
            case Op::Str: i64c(strAddr.at((size_t)in.a.v)); set(in.dst); break;
            case Op::PrintInt: get(in.a); call("ember_print_int"); break;
            case Op::PrintBool: get(in.a); call("ember_print_bool"); break;
            case Op::PrintStr: get(in.a); call("ember_print_str"); break;
            case Op::PrintFloat: get(in.a); call("ember_print_float"); break;
            case Op::ReadInt: call("ember_read_int"); set(in.dst); break;
            case Op::FAdd: fbin(0xA0); break;
            case Op::FSub: fbin(0xA1); break;
            case Op::FMul: fbin(0xA2); break;
            case Op::FDiv: fbin(0xA3); break;
            case Op::FEq: fcmp(0x61); break;
            case Op::FNe: fcmp(0x62); break;
            case Op::FLt: fcmp(0x63); break;
            case Op::FLe: fcmp(0x65); break;
            case Op::FGt: fcmp(0x64); break;
            case Op::FGe: fcmp(0x66); break;
            case Op::FNeg: get(in.a); op(0xBF); op(0x9A); op(0xBD); set(in.dst); break;
            case Op::IToF: get(in.a); op(0xB9); op(0xBD); set(in.dst); break;
            case Op::FSqrt: get(in.a); op(0xBF); op(0x9F); op(0xBD); set(in.dst); break;
            case Op::FToI: {  // as cvttsd2si: NaN and out-of-range give INT64_MIN
                get(in.a); op(0xBF); op(0x21); uleb(c, (uint64_t)fLocal);
                auto fget = [&]() { op(0x20); uleb(c, (uint64_t)fLocal); };
                auto fconst = [&](double d) { op(0x44); uint8_t b[8]; std::memcpy(b, &d, 8); c.insert(c.end(), b, b + 8); };
                fget(); fget(); op(0x62);                      // x != x
                fget(); fconst(9223372036854775808.0); op(0x66); op(0x72);   // || x >= 2^63
                fget(); fconst(-9223372036854775808.0); op(0x63); op(0x72);  // || x < -2^63
                op(0x04); op(0x7E);
                i64c(INT64_MIN);
                op(0x05);
                fget(); op(0xB0);
                op(0x0B);
                set(in.dst);
                break;
            }
            case Op::Rt:
                for (const Opd& a : in.args) get(a);
                call(in.name);
                if (in.dst >= 0) set(in.dst);
                break;
            case Op::FnAddr: i64c(tableIdx.at(in.name)); set(in.dst); break;
            case Op::CallInd: {
                nullCheck(in.a);
                for (const Opd& a : in.args) get(a);
                get(in.a);                                    // the closure, as the hidden last argument
                addr32(in.a); mem(0x29, 3, 8); op(0xA7);      // its code: the table index in element 0
                op(0x11); uleb(c, (uint64_t)typeOf((int)in.args.size() + 1, in.dst >= 0)); op(0x00);
                if (in.dst >= 0) set(in.dst);
                break;
            }
        }
    }

    Bytes function(const IRFunc& fn) {
        f = &fn;
        c.clear();
        int R = std::max(fn.nregs, fn.nparams);
        pcLocal = R;
        fLocal = R + 1;
        // basic blocks: a new one at every label and after every terminator
        std::vector<std::pair<int, int>> blocks;  // [begin, end)
        std::vector<int> blockOfLabel(fn.nlabels + 1, 0);
        int n = (int)fn.code.size(), b = 0;
        for (int i = 0; i <= n; i++) {
            bool cut = i == n || (fn.code[i].op == Op::Label && i > b) || (i > 0 && isTerm(fn.code[i - 1].op) && i > b);
            if (cut) {
                blocks.push_back({b, i});
                b = i;
            }
        }
        if (blocks.empty()) blocks.push_back({0, 0});
        for (size_t k = 0; k < blocks.size(); k++)
            for (int i = blocks[k].first; i < blocks[k].second && fn.code[i].op == Op::Label; i++)
                blockOfLabel[fn.code[i].t1] = (int)k;
        int nb = (int)blocks.size();
        Bytes body;
        // locals: the virtual registers beyond the parameters (i64), the block number (i32), a float scratch (f64)
        uleb(body, 3);
        uleb(body, (uint64_t)(R - fn.nparams)); body.push_back(0x7E);
        uleb(body, 1); body.push_back(0x7F);
        uleb(body, 1); body.push_back(0x7C);
        op(0x03); op(0x40);                     // loop $dispatch
        for (int k = 0; k < nb; k++) { op(0x02); op(0x40); }
        op(0x20); uleb(c, (uint64_t)pcLocal);    // br_table on the block number
        op(0x0E); uleb(c, (uint64_t)nb);
        for (int k = 0; k < nb; k++) uleb(c, (uint64_t)k);
        uleb(c, 0);
        for (int k = 0; k < nb; k++) {
            op(0x0B);  // end of block k: its code follows
            int depthToLoop = nb - 1 - k;
            for (int i = blocks[k].first; i < blocks[k].second; i++)
                emitInst(fn.code[i], depthToLoop, blockOfLabel, k + 1, i == blocks[k].second - 1);
        }
        op(0x0B);  // end loop
        op(0x00);  // unreachable: every path returned
        op(0x0B);  // end function
        body.insert(body.end(), c.begin(), c.end());
        Bytes out;
        uleb(out, body.size());
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

public:
    explicit WasmGen(const Module& mod) : m(mod) {}

    // Returns "" on success, else why the program cannot be compiled for wasm.
    std::string emit(Bytes& out) {
        // string literals: [len][bytes][0], 8-aligned, from address 1024
        for (const std::string& s : m.strings) {
            while (data.size() % 8) data.push_back(0);
            strAddr.push_back(kDataBase + (uint32_t)data.size());
            uint64_t len = s.size();
            for (int k = 0; k < 8; k++) data.push_back((uint8_t)(len >> (8 * k)));
            data.insert(data.end(), s.begin(), s.end());
            data.push_back(0);
        }
        if (kDataBase + data.size() > kDataLimit) return "the program's string literals exceed the 15 MB the wasm layout reserves";
        // imports: every runtime function the code uses
        useImport("ember_panic", 2, false);
        for (const IRFunc& fn : m.funcs)
            for (const Inst& in : fn.code) {
                if (in.op == Op::Rt) useImport(in.name, (int)in.args.size(), in.dst >= 0);
                if (in.op == Op::NewArr) useImport("ember_new_array", 4, true);
                if (in.op == Op::PrintInt) useImport("ember_print_int", 1, false);
                if (in.op == Op::PrintBool) useImport("ember_print_bool", 1, false);
                if (in.op == Op::PrintStr) useImport("ember_print_str", 1, false);
                if (in.op == Op::PrintFloat) useImport("ember_print_float", 1, false);
                if (in.op == Op::ReadInt) useImport("ember_read_int", 0, true);
                if (in.op == Op::FnAddr && !tableIdx.count(in.name)) {
                    tableIdx[in.name] = (int)tableFns.size();
                    tableFns.push_back(in.name);
                }
            }
        for (size_t i = 0; i < m.funcs.size(); i++) funcIdx[m.funcs[i].name] = (int)(imports.size() + i);
        std::vector<int> ftype;
        for (const IRFunc& fn : m.funcs) ftype.push_back(typeOf(fn.nparams, !fn.retUnit));
        for (auto& im : imports) typeOf(im.second.nparams, im.second.result);
        for (const IRFunc& fn : m.funcs)
            for (const Inst& in : fn.code)
                if (in.op == Op::CallInd) typeOf((int)in.args.size() + 1, in.dst >= 0);
        std::vector<Bytes> bodies;
        for (const IRFunc& fn : m.funcs) bodies.push_back(function(fn));

        out = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
        Bytes s;
        uleb(s, types.size());  // 1: types
        for (const Sig& t : types) {
            s.push_back(0x60);
            uleb(s, (uint64_t)t.nparams);
            for (int k = 0; k < t.nparams; k++) s.push_back(0x7E);
            uleb(s, t.result ? 1 : 0);
            if (t.result) s.push_back(0x7E);
        }
        section(out, 1, s);
        s.clear();  // 2: imports (the memory, then the runtime's functions)
        uleb(s, imports.size() + 1);
        name(s, "env"); name(s, "memory"); s.push_back(0x02); s.push_back(0x00); uleb(s, 1);
        for (auto& im : imports) {
            name(s, "rt"); name(s, im.first); s.push_back(0x00);
            uleb(s, (uint64_t)typeOf(im.second.nparams, im.second.result));
        }
        section(out, 2, s);
        s.clear();  // 3: functions
        uleb(s, ftype.size());
        for (int t : ftype) uleb(s, (uint64_t)t);
        section(out, 3, s);
        s.clear();  // 4: the table of functions used as values
        uleb(s, 1); s.push_back(0x70); s.push_back(0x00); uleb(s, tableFns.size());
        section(out, 4, s);
        s.clear();  // 7: exports
        uleb(s, 1);
        name(s, "main"); s.push_back(0x00); uleb(s, (uint64_t)funcIdx.at("main"));
        section(out, 7, s);
        if (!tableFns.empty()) {
            s.clear();  // 9: table contents
            uleb(s, 1);
            uleb(s, 0);
            s.push_back(0x41); sleb(s, 0); s.push_back(0x0B);
            uleb(s, tableFns.size());
            for (const std::string& n : tableFns) uleb(s, (uint64_t)funcIdx.at(n));
            section(out, 9, s);
        }
        s.clear();  // 10: code
        uleb(s, bodies.size());
        for (const Bytes& bd : bodies) s.insert(s.end(), bd.begin(), bd.end());
        section(out, 10, s);
        if (!data.empty()) {
            s.clear();  // 11: data (the string literals)
            uleb(s, 1);
            uleb(s, 0);
            s.push_back(0x41); sleb(s, kDataBase); s.push_back(0x0B);
            uleb(s, data.size());
            s.insert(s.end(), data.begin(), data.end());
            section(out, 11, s);
        }
        return "";
    }
};
