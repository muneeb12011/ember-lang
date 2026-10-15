// codegen.hpp - x86-64 (System V, Intel syntax) assembly emitter with a linear-scan register allocator.
//
// Register allocation (Poletto & Sarkar linear scan):
//   * live intervals come from the CFG liveness in opt.hpp: one [first, last] range per virtual register, covering
//     every point where it is live (conservative across holes);
//   * 11 registers are allocatable: the caller-saved rsi rdi r8 r9 r10 r11 and the callee-saved rbx r12-r15;
//     rax rcx rdx stay scratch for instruction sequences (division, shifts, address arithmetic);
//   * a value live across anything that calls out (Ember calls, runtime calls, allocation, printing) may only get a
//     callee-saved register, so calls never need caller-side saves. This is also what keeps the conservative
//     collector correct: at a collection (which happens inside a call) every live pointer is in a callee-saved
//     register or a stack slot, and the collector scans both;
//   * when no register is free, the interval with the lowest spill weight (uses and definitions, weighted 8x per loop
//     level, divided by its length) among the current one and the active ones it could take a register from is spilled
//     to a stack slot for its whole life.
// Arguments are placed with a parallel move (the argument registers may hold other arguments), breaking cycles
// through rax. A compare whose only use is the branch right after it becomes a single cmp + jcc.
#pragma once
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>

#include "opt.hpp"

class CodeGen {
    const Module& m;
    std::ostringstream o;
    int uid = 0;
    std::string srcFile;
    bool debug = false;

    static constexpr int kNumRegs = 11;
    static constexpr const char* kRegs[kNumRegs] = {"rsi", "rdi", "r8", "r9", "r10", "r11", "rbx", "r12", "r13", "r14", "r15"};
    static constexpr const char* kRegs32[kNumRegs] = {"esi", "edi", "r8d", "r9d", "r10d", "r11d", "ebx", "r12d", "r13d", "r14d", "r15d"};
    static constexpr const char* kRegs8[kNumRegs] = {"sil", "dil", "r8b", "r9b", "r10b", "r11b", "bl", "r12b", "r13b", "r14b", "r15b"};
    static constexpr int kFirstCallee = 6;
    static constexpr const char* kArgRegs[6] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

    struct Stub {
        std::string label;
        int kind, line;
    };

    // per-function state
    const IRFunc* f = nullptr;
    std::string sym;
    std::vector<int> regOf, slotOf, uses, defs;
    std::vector<int> savedRegs;  // callee-saved registers (indices into kRegs) this function uses
    int nspill = 0;
    std::vector<Stub> stubs;

  public:
    struct Stats { int vregs = 0, inRegs = 0, spilled = 0; };
    static Stats& stats() { static Stats s; return s; }

  private:
    // assembler-safe symbol: module dots stay, anything else outside [A-Za-z0-9_.] (generic instances such as
    // `vec.push<int>`, closures `main$fn3`) is written as .XX hex
    static std::string symbol(const std::string& n) {
        if (n == "main") return "ember_main";
        std::string r = "f_";
        for (unsigned char c : n) {
            if (isalnum(c) || c == '_' || c == '.') r += (char)c;
            else { char b[8]; snprintf(b, sizeof b, ".%02x", c); r += b; }
        }
        return r;
    }
    std::string lab(int id) const { return ".L" + sym + "_" + std::to_string(id); }

    static bool fits32(long long v) { return v >= INT32_MIN && v <= INT32_MAX; }

    int nsaved() const { return (int)savedRegs.size(); }
    bool inReg(long long r) const { return regOf[r] >= 0; }
    std::string loc(long long r) const {
        if (regOf[r] >= 0) return kRegs[regOf[r]];
        return "QWORD PTR [rbp-" + std::to_string(8 * (nsaved() + slotOf[r] + 1)) + "]";
    }
    bool isRegOpd(const Opd& v) const { return v.isReg() && inReg(v.v); }
    // source operand text: a register, a stack slot, or an immediate that fits in 32 bits ("" if it does not)
    std::string src(const Opd& v) const {
        if (v.isImm()) return fits32(v.v) ? std::to_string(v.v) : "";
        return loc(v.v);
    }

    void ld(const char* reg, const Opd& v) {
        if (v.isImm()) {
            if (fits32(v.v)) o << "  mov " << reg << ", " << v.v << "\n";
            else o << "  movabs " << reg << ", " << v.v << "\n";
        } else if (loc(v.v) != reg) {
            o << "  mov " << reg << ", " << loc(v.v) << "\n";
        }
    }
    void st(int dst) {
        if (loc(dst) != "rax") o << "  mov " << loc(dst) << ", rax\n";
    }
    // dst <- v (dst a register name)
    void movTo(const std::string& d, const Opd& v) {
        if (v.isImm()) {
            if (v.v == 0) o << "  xor " << d << ", " << d << "\n";
            else if (fits32(v.v)) o << "  mov " << d << ", " << v.v << "\n";
            else o << "  movabs " << d << ", " << v.v << "\n";
        } else if (loc(v.v) != d) {
            o << "  mov " << d << ", " << loc(v.v) << "\n";
        }
    }
    // floats live in the same 64-bit slots as ints (as raw IEEE bits); SSE registers are scratch only
    void ldx(const char* x, const Opd& v) {
        if (v.isImm()) o << "  movabs rax, " << v.v << "\n  movq " << x << ", rax\n";
        else o << "  movq " << x << ", " << loc(v.v) << "\n";
    }
    void stx(int dst) { o << "  movq " << loc(dst) << ", xmm0\n"; }

    std::string stub(int kind, int line) {
        std::string l = ".Lpanic" + std::to_string(uid++);
        stubs.push_back({l, kind, line});
        return l;
    }

    // Moves into distinct registers, all reading their sources before any is overwritten. A source is a register or
    // memory operand text, or "#<imm>".
    void parallelMove(std::vector<std::pair<std::string, std::string>> p) {
        for (size_t i = 0; i < p.size();)
            if (p[i].first == p[i].second) p.erase(p.begin() + (long)i);
            else i++;
        while (!p.empty()) {
            bool progress = false;
            for (size_t i = 0; i < p.size(); i++) {
                bool blocked = false;
                for (size_t j = 0; j < p.size(); j++)
                    if (j != i && p[j].second == p[i].first) blocked = true;
                if (blocked) continue;
                const std::string& d = p[i].first;
                const std::string& s = p[i].second;
                if (s[0] == '#') movTo(d, Opd::imm(std::stoll(s.substr(1))));
                else o << "  mov " << d << ", " << s << "\n";
                p.erase(p.begin() + (long)i);
                progress = true;
                break;
            }
            if (!progress) {  // a cycle: park one destination's current value in rax
                std::string d = p[0].first;
                o << "  mov rax, " << d << "\n";
                for (auto& x : p)
                    if (x.second == d) x.second = "rax";
            }
        }
    }
    void argMove(const std::vector<std::pair<std::string, Opd>>& mv) {
        std::vector<std::pair<std::string, std::string>> p;
        for (auto& x : mv) p.push_back({x.first, x.second.isImm() ? "#" + std::to_string(x.second.v) : loc(x.second.v)});
        parallelMove(p);
    }

    static const char* cc(Op op) {
        switch (op) {
            case Op::Eq: return "e";
            case Op::Ne: return "ne";
            case Op::Lt: return "l";
            case Op::Le: return "le";
            case Op::Gt: return "g";
            default: return "ge";
        }
    }
    static const char* ccInv(Op op) {
        switch (op) {
            case Op::Eq: return "ne";
            case Op::Ne: return "e";
            case Op::Lt: return "ge";
            case Op::Le: return "g";
            case Op::Gt: return "le";
            default: return "l";
        }
    }

    void cmpOps(const Inst& in) {
        std::string a;
        if (isRegOpd(in.a)) a = loc(in.a.v);
        else if (in.a.isReg() && (in.b.isImm() ? fits32(in.b.v) : isRegOpd(in.b))) a = loc(in.a.v);  // memory, fine
        else { ld("rax", in.a); a = "rax"; }
        std::string b = src(in.b);
        if (b.empty()) { ld("rcx", in.b); b = "rcx"; }
        o << "  cmp " << a << ", " << b << "\n";
    }

    bool nextIs(size_t i, int label) const {
        for (size_t j = i + 1; j < f->code.size() && f->code[j].op == Op::Label; j++)
            if (f->code[j].t1 == label) return true;
        return false;
    }

    static bool callsOut(Op op) {
        switch (op) {
            case Op::Call: case Op::CallInd: case Op::Rt: case Op::NewArr: case Op::PrintInt: case Op::PrintBool:
            case Op::PrintStr: case Op::PrintFloat: case Op::ReadInt:
                return true;
            default:
                return false;
        }
    }

    void allocate() {
        int n = (int)f->code.size(), R = f->nregs;
        // loop nesting depth of each instruction, from back edges in the linear code
        std::vector<int> depth(n + 1, 0);
        std::unordered_map<int, int> labelAt;
        for (int i = 0; i < n; i++)
            if (f->code[i].op == Op::Label) labelAt[f->code[i].t1] = i;
        for (int i = 0; i < n; i++) {
            const Inst& in = f->code[i];
            int targets[2] = {-1, -1};
            if (in.op == Op::Jmp) targets[0] = in.t1;
            if (in.op == Op::Br) { targets[0] = in.t1; targets[1] = in.t2; }
            for (int t : targets) {
                if (t < 0) continue;
                int p = labelAt.count(t) ? labelAt[t] : n;
                if (p <= i) {  // back edge: everything in [p, i] is inside a loop
                    depth[p]++;
                    depth[i + 1]--;
                }
            }
        }
        std::vector<double> weight(R, 0);
        uses.assign(R, 0);
        defs.assign(R, 0);
        int d = 0;
        for (int i = 0; i < n; i++) {
            d += depth[i];
            double w = std::pow(8.0, std::min(std::max(d, 0), 4));
            const Inst& in = f->code[i];
            opt::forUses(in, [&](int r) { weight[r] += w; uses[r]++; });
            if (in.dst >= 0) { weight[in.dst] += w; defs[in.dst]++; }
        }

        // live intervals
        std::vector<int> start(R, INT_MAX), end(R, INT_MIN);
        std::vector<char> crossCall(R, 0);
        auto extend = [&](int r, int pos) {
            start[r] = std::min(start[r], pos);
            end[r] = std::max(end[r], pos);
        };
        for (int p = 0; p < f->nparams; p++) extend(p, -1);
        opt::CFG g = opt::buildCFG(*f);
        std::vector<std::vector<char>> liveIn, liveOut;
        if (n) opt::liveness(*f, g, liveIn, liveOut);
        for (size_t b = 0; b < g.start.size(); b++) {
            int first = g.start[b], last = g.end((int)b, n) - 1;
            std::vector<char> live = liveOut[b];
            for (int r = 0; r < R; r++)
                if (live[r]) extend(r, last);
            for (int i = last; i >= first; i--) {
                const Inst& in = f->code[i];
                if (callsOut(in.op))
                    for (int r = 0; r < R; r++)
                        if (live[r] && r != in.dst) crossCall[r] = 1;
                if (in.dst >= 0) {
                    live[in.dst] = 0;
                    extend(in.dst, i);
                }
                opt::forUses(in, [&](int r) { live[r] = 1; extend(r, i); });
            }
            for (int r = 0; r < R; r++)
                if (live[r]) extend(r, first);
        }

        std::vector<int> order;
        for (int r = 0; r < R; r++)
            if (end[r] >= start[r]) order.push_back(r);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return start[a] != start[b] ? start[a] < start[b] : a < b; });
        auto density = [&](int r) { return weight[r] / (double)(end[r] - start[r] + 2); };

        regOf.assign(R, -1);
        slotOf.assign(R, -1);
        std::vector<int> owner(kNumRegs, -1);  // which virtual register holds each physical one
        std::vector<int> active;
        for (int r : order) {
            for (size_t k = 0; k < active.size();) {  // expire intervals that ended before this one starts
                int a = active[k];
                if (end[a] < start[r]) {
                    owner[regOf[a]] = -1;
                    active.erase(active.begin() + (long)k);
                } else k++;
            }
            int lo = crossCall[r] ? kFirstCallee : 0;
            int pick = -1;
            for (int p = lo; p < kNumRegs && pick < 0; p++)
                if (owner[p] < 0) pick = p;
            if (pick < 0) {  // spill the cheapest of r and the active intervals holding a register r may use
                int victim = -1;
                for (int a : active)
                    if (regOf[a] >= lo && (victim < 0 || density(a) < density(victim))) victim = a;
                if (victim >= 0 && density(victim) < density(r)) {
                    pick = regOf[victim];
                    regOf[victim] = -1;
                    active.erase(std::find(active.begin(), active.end(), victim));
                }
            }
            if (pick >= 0) {
                regOf[r] = pick;
                owner[pick] = r;
                active.push_back(r);
            }
        }
        savedRegs.clear();
        std::vector<char> used(kNumRegs, 0);
        for (int r = 0; r < R; r++)
            if (regOf[r] >= 0) used[regOf[r]] = 1;
        for (int p = kFirstCallee; p < kNumRegs; p++)
            if (used[p]) savedRegs.push_back(p);
        nspill = 0;
        for (int r = 0; r < R; r++)  // every register without a physical one gets a slot (also never-live ones)
            if (regOf[r] < 0) slotOf[r] = nspill++;
        Stats& s = stats();
        for (int r = 0; r < R; r++)
            if (end[r] >= start[r]) {
                s.vregs++;
                if (regOf[r] >= 0) s.inRegs++;
                else s.spilled++;
            }
    }

    void epilogue() {
        for (int i = 0; i < nsaved(); i++) o << "  mov " << kRegs[savedRegs[i]] << ", QWORD PTR [rbp-" << 8 * (i + 1) << "]\n";
        o << "  leave\n  ret\n";
    }

    // dst = a op b for add/sub/and/or/xor/imul, computing straight into dst's register when it has one
    void binop(const Inst& in, const char* mn, bool comm) {
        Opd a = in.a, b = in.b;
        if (inReg(in.dst)) {
            std::string D = loc(in.dst);
            bool bInD = b.isReg() && loc(b.v) == D, aInD = a.isReg() && loc(a.v) == D;
            if (bInD && !aInD && comm) {
                std::swap(a, b);
                bInD = false;
            }
            if (!bInD) {
                movTo(D, a);
                if (in.op == Op::Mul && b.isImm() && fits32(b.v)) {
                    o << "  imul " << D << ", " << D << ", " << b.v << "\n";
                    return;
                }
                std::string s = src(b);
                if (s.empty()) { ld("rcx", b); s = "rcx"; }
                o << "  " << mn << " " << D << ", " << s << "\n";
                return;
            }
        }
        ld("rax", a);
        if (in.op == Op::Mul && b.isImm() && fits32(b.v)) {
            o << "  imul rax, rax, " << b.v << "\n";
        } else {
            std::string s = src(b);
            if (s.empty()) { ld("rcx", b); s = "rcx"; }
            o << "  " << mn << " rax, " << s << "\n";
        }
        st(in.dst);
    }

    void unop(const Inst& in, const char* code) {
        if (inReg(in.dst)) {
            movTo(loc(in.dst), in.a);
            o << "  " << code << " " << loc(in.dst) << "\n";
        } else {
            ld("rax", in.a);
            o << "  " << code << " rax\n";
            st(in.dst);
        }
    }

    void emitFunc(const IRFunc& fn) {
        f = &fn;
        sym = symbol(fn.name);
        stubs.clear();
        allocate();
        o << "\n  .globl " << sym << "\n  .type " << sym << ", @function\n" << sym << ":\n";
        if (debug) o << "  .loc 1 " << fn.line << " 0\n";
        o << "  push rbp\n  mov rbp, rsp\n";
        int frame = 8 * (nsaved() + nspill);
        frame = (frame + 15) & ~15;
        if (frame) o << "  sub rsp, " << frame << "\n";
        for (int i = 0; i < nsaved(); i++) o << "  mov QWORD PTR [rbp-" << 8 * (i + 1) << "], " << kRegs[savedRegs[i]] << "\n";
        {  // incoming parameters to wherever the allocator put them: stack slots first, then registers as a parallel move
            std::vector<std::pair<std::string, std::string>> mv;
            for (int p = 0; p < fn.nparams; p++) {
                if (!inReg(p)) o << "  mov " << loc(p) << ", " << kArgRegs[p] << "\n";
                else mv.push_back({loc(p), kArgRegs[p]});
            }
            parallelMove(mv);
        }

        const auto& code = fn.code;
        int lastLoc = 0;
        for (size_t i = 0; i < code.size(); i++) {
            const Inst& in = code[i];
            if (debug && in.op != Op::Label && in.line > 0 && in.line != lastLoc) {
                o << "  .loc 1 " << in.line << " 0\n";
                lastLoc = in.line;
            }
            switch (in.op) {
                case Op::Label: o << lab(in.t1) << ":\n"; break;
                case Op::Mov:
                    if (in.a.isReg() && in.a.v == in.dst) break;
                    if (inReg(in.dst)) movTo(loc(in.dst), in.a);
                    else if (in.a.isImm() && fits32(in.a.v)) o << "  mov " << loc(in.dst) << ", " << in.a.v << "\n";
                    else if (isRegOpd(in.a)) o << "  mov " << loc(in.dst) << ", " << loc(in.a.v) << "\n";
                    else { ld("rax", in.a); st(in.dst); }
                    break;
                case Op::Add: binop(in, "add", true); break;
                case Op::Sub: binop(in, "sub", false); break;
                case Op::Mul: {  // strength reduction: x*2^k -> shl, x*3/5/9 -> lea
                    Opd a = in.a, b = in.b;
                    if (a.isImm() && !b.isImm()) std::swap(a, b);
                    long long c = b.isImm() ? b.v : 0;
                    int k = (c > 1 && (c & (c - 1)) == 0) ? __builtin_ctzll((unsigned long long)c) : -1;
                    if (opt::enabled("strength") && a.isReg() && k > 0) {
                        std::string D = inReg(in.dst) ? loc(in.dst) : "rax";
                        movTo(D, a);
                        o << "  shl " << D << ", " << k << "\n";
                        if (D == "rax") st(in.dst);
                    } else if (opt::enabled("strength") && a.isReg() && (c == 3 || c == 5 || c == 9)) {
                        std::string A = isRegOpd(a) ? loc(a.v) : "rax";
                        if (A == "rax") ld("rax", a);
                        std::string D = inReg(in.dst) ? loc(in.dst) : "rax";
                        o << "  lea " << D << ", [" << A << "+" << A << "*" << (c - 1) << "]\n";
                        if (D == "rax") st(in.dst);
                    } else {
                        binop(in, "imul", true);
                    }
                    break;
                }
                case Op::And: binop(in, "and", true); break;
                case Op::Or: binop(in, "or", true); break;
                case Op::Xor: binop(in, "xor", true); break;
                case Op::Shl: case Op::Shr: {  // the hardware masks the count to 6 bits, which is exactly the language rule
                    const char* mn = in.op == Op::Shl ? "shl" : "sar";
                    if (in.b.isImm()) {
                        int k = (int)(in.b.v & 63);
                        if (inReg(in.dst)) {
                            movTo(loc(in.dst), in.a);
                            o << "  " << mn << " " << loc(in.dst) << ", " << k << "\n";
                        } else {
                            ld("rax", in.a);
                            o << "  " << mn << " rax, " << k << "\n";
                            st(in.dst);
                        }
                    } else {
                        ld("rcx", in.b);
                        ld("rax", in.a);
                        o << "  " << mn << " rax, cl\n";
                        st(in.dst);
                    }
                    break;
                }
                case Op::Neg: unop(in, "neg"); break;
                case Op::BNot: unop(in, "not"); break;
                case Op::Not:
                    if (inReg(in.dst)) {
                        movTo(loc(in.dst), in.a);
                        o << "  xor " << loc(in.dst) << ", 1\n";
                    } else {
                        ld("rax", in.a);
                        o << "  xor rax, 1\n";
                        st(in.dst);
                    }
                    break;
                case Op::Rt: {  // runtime calls take their arguments (floats as raw bits) in the integer registers
                    std::vector<std::pair<std::string, Opd>> mv;
                    for (size_t a = 0; a < in.args.size() && a < 6; a++) mv.push_back({kArgRegs[a], in.args[a]});
                    argMove(mv);
                    o << "  call " << in.name << "\n";
                    if (in.dst >= 0) st(in.dst);
                    break;
                }
                case Op::Div: case Op::Mod: {
                    long long c = in.b.isImm() ? in.b.v : 0;
                    int k = (c > 1 && c < (1LL << 62) && (c & (c - 1)) == 0) ? __builtin_ctzll((unsigned long long)c) : -1;
                    if (opt::enabled("strength") && k > 0) {
                        // signed x / 2^k rounds toward zero: add 2^k - 1 first when x is negative
                        ld("rax", in.a);
                        o << "  mov rdx, rax\n  sar rdx, 63\n  shr rdx, " << 64 - k << "\n";
                        if (in.op == Op::Div) {
                            o << "  add rax, rdx\n  sar rax, " << k << "\n";
                        } else {  // x - ((x + bias) & -2^k)
                            o << "  lea rcx, [rax+rdx]\n  and rcx, " << -c << "\n  sub rax, rcx\n";
                        }
                        st(in.dst);
                        break;
                    }
                    ld("rax", in.a);
                    ld("rcx", in.b);
                    bool bImm = in.b.isImm();
                    if (!(bImm && in.b.v != 0)) o << "  test rcx, rcx\n  je " << stub(1, in.line) << "\n";
                    if (bImm && in.b.v == -1) {
                        if (in.op == Op::Div) o << "  neg rax\n";
                        else o << "  xor eax, eax\n";
                    } else if (bImm) {
                        o << "  cqo\n  idiv rcx\n";
                        if (in.op == Op::Mod) o << "  mov rax, rdx\n";
                    } else {
                        int k = uid++;
                        o << "  cmp rcx, -1\n  jne .Ld" << k << "\n";
                        if (in.op == Op::Div) o << "  neg rax\n";
                        else o << "  xor eax, eax\n";
                        o << "  jmp .De" << k << "\n.Ld" << k << ":\n  cqo\n  idiv rcx\n";
                        if (in.op == Op::Mod) o << "  mov rax, rdx\n";
                        o << ".De" << k << ":\n";
                    }
                    st(in.dst);
                    break;
                }
                case Op::Eq: case Op::Ne: case Op::Lt: case Op::Le: case Op::Gt: case Op::Ge: {
                    bool fuse = i + 1 < code.size() && code[i + 1].op == Op::Br && code[i + 1].a.isReg() &&
                                code[i + 1].a.v == in.dst && uses[in.dst] == 1 && defs[in.dst] == 1;
                    cmpOps(in);
                    if (fuse) {
                        const Inst& br = code[i + 1];
                        if (nextIs(i + 1, br.t2)) o << "  j" << cc(in.op) << " " << lab(br.t1) << "\n";
                        else if (nextIs(i + 1, br.t1)) o << "  j" << ccInv(in.op) << " " << lab(br.t2) << "\n";
                        else o << "  j" << cc(in.op) << " " << lab(br.t1) << "\n  jmp " << lab(br.t2) << "\n";
                        i++;
                    } else if (inReg(in.dst)) {
                        o << "  set" << cc(in.op) << " al\n  movzx " << kRegs32[regOf[in.dst]] << ", al\n";
                    } else {
                        o << "  set" << cc(in.op) << " al\n  movzx eax, al\n";
                        st(in.dst);
                    }
                    break;
                }
                case Op::Jmp:
                    if (!nextIs(i, in.t1)) o << "  jmp " << lab(in.t1) << "\n";
                    break;
                case Op::Br: {
                    if (isRegOpd(in.a)) o << "  test " << loc(in.a.v) << ", " << loc(in.a.v) << "\n";
                    else if (in.a.isReg()) o << "  cmp " << loc(in.a.v) << ", 0\n";
                    else { ld("rax", in.a); o << "  test rax, rax\n"; }
                    if (nextIs(i, in.t2)) o << "  jne " << lab(in.t1) << "\n";
                    else if (nextIs(i, in.t1)) o << "  je " << lab(in.t2) << "\n";
                    else o << "  jne " << lab(in.t1) << "\n  jmp " << lab(in.t2) << "\n";
                    break;
                }
                case Op::Call: {
                    std::vector<std::pair<std::string, Opd>> mv;
                    for (size_t a = 0; a < in.args.size(); a++) mv.push_back({kArgRegs[a], in.args[a]});
                    argMove(mv);
                    o << "  call " << symbol(in.name) << "\n";
                    if (in.dst >= 0) st(in.dst);
                    break;
                }
                case Op::FnAddr:
                    if (inReg(in.dst)) o << "  lea " << loc(in.dst) << ", [rip+" << symbol(in.name) << "]\n";
                    else { o << "  lea rax, [rip+" << symbol(in.name) << "]\n"; st(in.dst); }
                    break;
                case Op::CallInd: {  // arguments in order, the closure after them; its code pointer is element 0
                    std::vector<std::pair<std::string, Opd>> mv;
                    for (size_t a = 0; a < in.args.size(); a++) mv.push_back({kArgRegs[a], in.args[a]});
                    const char* clo = kArgRegs[in.args.size()];
                    mv.push_back({clo, in.a});
                    argMove(mv);
                    if (!in.nonnull) o << "  test " << clo << ", " << clo << "\n  je " << stub(4, in.line) << "\n";
                    o << "  call QWORD PTR [" << clo << "+8]\n";
                    if (in.dst >= 0) st(in.dst);
                    break;
                }
                case Op::Ret:
                    if (in.a.k != Opd::None) ld("rax", in.a);
                    epilogue();
                    break;
                case Op::Load: case Op::Store: {
                    std::string B;
                    if (isRegOpd(in.a)) B = loc(in.a.v);
                    else { ld("rax", in.a); B = "rax"; }
                    if (!in.nonnull) o << "  test " << B << ", " << B << "\n  je " << stub(4, in.line) << "\n";
                    bool byte = in.w == 1;  // [byte] / [bool] / str elements are one byte wide
                    std::string addr;
                    if (in.b.isImm() && in.b.v >= 0 && in.b.v < (1LL << 28)) {  // constant index (struct fields, payloads)
                        if (!in.nocheck) o << "  cmp QWORD PTR [" << B << "], " << in.b.v << "\n  jbe " << stub(0, in.line) << "\n";
                        addr = "[" + B + "+" + std::to_string(byte ? in.b.v + 8 : 8 * in.b.v + 8) + "]";
                    } else {
                        std::string I;
                        if (isRegOpd(in.b)) I = loc(in.b.v);
                        else { ld("rcx", in.b); I = "rcx"; }
                        if (!in.nocheck) o << "  cmp " << I << ", QWORD PTR [" << B << "]\n  jae " << stub(0, in.line) << "\n";
                        addr = byte ? "[" + B + "+" + I + "+8]" : "[" + B + "+" + I + "*8+8]";
                    }
                    if (in.op == Op::Load) {
                        if (inReg(in.dst)) {
                            if (byte) o << "  movzx " << kRegs32[regOf[in.dst]] << ", BYTE PTR " << addr << "\n";
                            else o << "  mov " << loc(in.dst) << ", QWORD PTR " << addr << "\n";
                        } else {
                            o << (byte ? "  movzx eax, BYTE PTR " : "  mov rax, QWORD PTR ") << addr << "\n";
                            st(in.dst);
                        }
                    } else if (byte) {
                        if (in.c.isImm()) o << "  mov BYTE PTR " << addr << ", " << (in.c.v & 0xFF) << "\n";
                        else if (isRegOpd(in.c)) o << "  mov BYTE PTR " << addr << ", " << kRegs8[regOf[in.c.v]] << "\n";
                        else { ld("rdx", in.c); o << "  mov BYTE PTR " << addr << ", dl\n"; }
                    } else {
                        if (in.c.isImm() && fits32(in.c.v)) o << "  mov QWORD PTR " << addr << ", " << in.c.v << "\n";
                        else if (isRegOpd(in.c)) o << "  mov QWORD PTR " << addr << ", " << loc(in.c.v) << "\n";
                        else { ld("rdx", in.c); o << "  mov QWORD PTR " << addr << ", rdx\n"; }
                        if (in.ptr && !in.fresh && !(in.c.isImm() && in.c.v == 0)) {
                            // write barrier: mark the 512-byte card of the object as dirty (objects outside the block
                            // range, i.e. large ones, are rescanned by every minor collection instead)
                            int k = uid++;
                            o << "  mov rcx, " << B << "\n  sub rcx, QWORD PTR [rip+ember_heap_base]\n"
                              << "  cmp rcx, QWORD PTR [rip+ember_heap_span]\n  jae .Lwb" << k << "\n  shr rcx, 9\n"
                              << "  add rcx, QWORD PTR [rip+ember_cards]\n  mov BYTE PTR [rcx], 1\n.Lwb" << k << ":\n";
                        }
                    }
                    break;
                }
                case Op::Len: {
                    std::string B;
                    if (isRegOpd(in.a)) B = loc(in.a.v);
                    else { ld("rax", in.a); B = "rax"; }
                    if (!in.nonnull) o << "  test " << B << ", " << B << "\n  je " << stub(4, in.line) << "\n";
                    if (inReg(in.dst)) o << "  mov " << loc(in.dst) << ", QWORD PTR [" << B << "]\n";
                    else { o << "  mov rax, QWORD PTR [" << B << "]\n"; st(in.dst); }
                    break;
                }
                case Op::NewArr:  // ember_new_array(count, elem_width, noscan, line): zeroed [len][elems...]
                    ld("rdi", in.a);
                    o << "  mov esi, " << in.w << "\n  mov edx, " << (in.noscan ? 1 : 0) << "\n  mov ecx, " << in.line
                      << "\n  call ember_new_array\n";
                    st(in.dst);
                    break;
                case Op::FAdd: case Op::FSub: case Op::FMul: case Op::FDiv: {
                    ldx("xmm0", in.a);
                    ldx("xmm1", in.b);
                    const char* mn = in.op == Op::FAdd ? "addsd" : in.op == Op::FSub ? "subsd" : in.op == Op::FMul ? "mulsd" : "divsd";
                    o << "  " << mn << " xmm0, xmm1\n";
                    stx(in.dst);
                    break;
                }
                case Op::FEq: case Op::FNe: case Op::FLt: case Op::FLe: case Op::FGt: case Op::FGe:
                    // ucomisd sets ZF=PF=CF=1 when either side is NaN; the conditions below are all false then
                    // (except !=), which is exactly IEEE comparison semantics
                    ldx("xmm0", in.a);
                    ldx("xmm1", in.b);
                    switch (in.op) {
                        case Op::FEq: o << "  ucomisd xmm0, xmm1\n  sete al\n  setnp cl\n  and al, cl\n"; break;
                        case Op::FNe: o << "  ucomisd xmm0, xmm1\n  setne al\n  setp cl\n  or al, cl\n"; break;
                        case Op::FLt: o << "  ucomisd xmm1, xmm0\n  seta al\n"; break;
                        case Op::FLe: o << "  ucomisd xmm1, xmm0\n  setae al\n"; break;
                        case Op::FGt: o << "  ucomisd xmm0, xmm1\n  seta al\n"; break;
                        default: o << "  ucomisd xmm0, xmm1\n  setae al\n"; break;
                    }
                    o << "  movzx eax, al\n";
                    st(in.dst);
                    break;
                case Op::FNeg: ld("rax", in.a); o << "  btc rax, 63\n"; st(in.dst); break;
                case Op::IToF: ld("rax", in.a); o << "  pxor xmm0, xmm0\n  cvtsi2sd xmm0, rax\n"; stx(in.dst); break;
                case Op::FToI: ldx("xmm0", in.a); o << "  cvttsd2si rax, xmm0\n"; st(in.dst); break;
                case Op::FSqrt: ldx("xmm0", in.a); o << "  sqrtsd xmm0, xmm0\n"; stx(in.dst); break;
                case Op::PrintFloat: ld("rdi", in.a); o << "  call ember_print_float\n"; break;
                case Op::Str:
                    if (inReg(in.dst)) o << "  lea " << loc(in.dst) << ", [rip+.Lstr" << in.a.v << "]\n";
                    else { o << "  lea rax, [rip+.Lstr" << in.a.v << "]\n"; st(in.dst); }
                    break;
                case Op::PrintInt: ld("rdi", in.a); o << "  call ember_print_int\n"; break;
                case Op::PrintBool: ld("rdi", in.a); o << "  call ember_print_bool\n"; break;
                case Op::PrintStr: ld("rdi", in.a); o << "  call ember_print_str\n"; break;
                case Op::ReadInt: o << "  call ember_read_int\n"; st(in.dst); break;
            }
        }
        for (const Stub& s : stubs) {
            o << s.label << ":\n  mov edi, " << s.kind << "\n  mov esi, " << s.line << "\n  call ember_panic\n";
        }
        o << "  .size " << sym << ", .-" << sym << "\n";
    }

    static std::string esc(const std::string& s) {
        std::string r;
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { r += '\\'; r += (char)c; }
            else if (c >= 32 && c < 127) r += (char)c;
            else { char b[8]; snprintf(b, sizeof b, "\\%03o", c); r += b; }
        }
        return r;
    }

public:
    explicit CodeGen(const Module& mod, std::string file = "", bool dbg = false) : m(mod), srcFile(std::move(file)), debug(dbg) {}

    std::string emit() {
        o << "  .intel_syntax noprefix\n";
        if (debug) o << "  .file 1 \"" << srcFile << "\"\n";
        o << "  .section .rodata\n";
        for (size_t i = 0; i < m.strings.size(); i++)  // a str literal is a static [len][bytes] block
            o << "  .balign 8\n.Lstr" << i << ":\n  .quad " << m.strings[i].size() << "\n  .ascii \"" << esc(m.strings[i])
              << "\"\n  .byte 0\n";
        o << "  .text\n";
        for (const IRFunc& fn : m.funcs) emitFunc(fn);
        o << "\n  .section .note.GNU-stack,\"\",@progbits\n";
        return o.str();
    }
};
