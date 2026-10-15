// opt.hpp - IR optimizer. Every pass preserves behaviour exactly, including runtime errors:
// anything that can trap (division, array access, allocation, calls, I/O) is never removed or reordered.
#pragma once
#include <algorithm>
#include <cstdlib>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "ir.hpp"

namespace opt {

// EMBER_NO_OPT=name,name,... turns individual passes off (for measuring what each one is worth)
inline bool enabled(const char* pass) {
    static const std::string off = [] {
        const char* e = getenv("EMBER_NO_OPT");
        return std::string(",") + (e ? e : "") + ",";
    }();
    return off.find(std::string(",") + pass + ",") == std::string::npos;
}

inline long long wadd(long long a, long long b) { return (long long)((unsigned long long)a + (unsigned long long)b); }
inline long long wsub(long long a, long long b) { return (long long)((unsigned long long)a - (unsigned long long)b); }
inline long long wmul(long long a, long long b) { return (long long)((unsigned long long)a * (unsigned long long)b); }

// Shared with the interpreter so folding and execution can never disagree.
// Division by 0 is a runtime error (returns false); x / -1 wraps instead of trapping like raw idiv would.
inline bool evalBin(Op op, long long a, long long b, long long& out) {
    switch (op) {
        case Op::Add: out = wadd(a, b); return true;
        case Op::Sub: out = wsub(a, b); return true;
        case Op::Mul: out = wmul(a, b); return true;
        case Op::Div:
            if (b == 0) return false;
            out = b == -1 ? wsub(0, a) : a / b;
            return true;
        case Op::Mod:
            if (b == 0) return false;
            out = b == -1 ? 0 : a % b;
            return true;
        case Op::Eq: out = a == b; return true;
        case Op::Ne: out = a != b; return true;
        case Op::Lt: out = a < b; return true;
        case Op::Le: out = a <= b; return true;
        case Op::Gt: out = a > b; return true;
        case Op::Ge: out = a >= b; return true;
        case Op::And: out = a & b; return true;
        case Op::Or: out = a | b; return true;
        case Op::Xor: out = a ^ b; return true;
        case Op::Shl: out = (long long)((unsigned long long)a << (b & 63)); return true;
        case Op::Shr: out = a >> (b & 63); return true;
        default: return false;
    }
}

inline bool isBin(Op o) { return (o >= Op::Add && o <= Op::Ge) || (o >= Op::And && o <= Op::Shr); }

// ------------------------------------------------------------------------------------------ CFG
struct CFG {
    std::vector<int> start;                 // block b = [start[b], start[b+1])
    std::vector<std::vector<int>> succ;
    std::unordered_map<int, int> labelBlock;
    int end(int b, int n) const { return b + 1 < (int)start.size() ? start[b + 1] : n; }
};

inline CFG buildCFG(const IRFunc& f) {
    CFG g;
    int n = (int)f.code.size();
    if (n == 0) return g;
    g.start.push_back(0);
    for (int i = 0; i < n; i++) {
        if (f.code[i].op == Op::Label && i != 0 && g.start.back() != i) g.start.push_back(i);
        if (isTerm(f.code[i].op) && i + 1 < n && g.start.back() != i + 1) g.start.push_back(i + 1);
    }
    int nb = (int)g.start.size();
    g.succ.assign(nb, {});
    for (int b = 0; b < nb; b++)
        if (f.code[g.start[b]].op == Op::Label) g.labelBlock[f.code[g.start[b]].t1] = b;
    for (int b = 0; b < nb; b++) {
        const Inst& last = f.code[g.end(b, n) - 1];
        auto tgt = [&](int l) {
            auto it = g.labelBlock.find(l);
            if (it != g.labelBlock.end()) g.succ[b].push_back(it->second);
        };
        if (last.op == Op::Jmp) tgt(last.t1);
        else if (last.op == Op::Br) { tgt(last.t1); tgt(last.t2); }
        else if (last.op != Op::Ret && b + 1 < nb) g.succ[b].push_back(b + 1);
    }
    return g;
}

// ------------------------------------------------------------------------------- tail calls -> loops
inline bool tailCalls(IRFunc& f) {
    auto isTail = [&](size_t i) {
        if (i + 1 >= f.code.size()) return false;
        const Inst& c = f.code[i];
        const Inst& r = f.code[i + 1];
        if (c.op != Op::Call || c.name != f.name || r.op != Op::Ret) return false;
        if ((int)c.args.size() != f.nparams) return false;
        return f.retUnit ? c.dst == -1 : (c.dst >= 0 && r.a.isReg() && r.a.v == c.dst);
    };
    bool any = false;
    for (size_t i = 0; i < f.code.size(); i++) any |= isTail(i);
    if (!any) return false;
    int entry = f.nlabels++;
    std::vector<Inst> out;
    out.emplace_back();
    out.back().op = Op::Label;
    out.back().t1 = entry;
    for (size_t i = 0; i < f.code.size(); i++) {
        if (!isTail(i)) {
            out.push_back(f.code[i]);
            continue;
        }
        const Inst c = f.code[i];
        std::vector<int> tmp;
        for (size_t j = 0; j < c.args.size(); j++) {  // evaluate all arguments before overwriting any parameter
            int t = f.nregs++;
            tmp.push_back(t);
            Inst m;
            m.op = Op::Mov;
            m.dst = t;
            m.a = c.args[j];
            out.push_back(m);
        }
        for (size_t j = 0; j < c.args.size(); j++) {
            Inst m;
            m.op = Op::Mov;
            m.dst = (int)j;
            m.a = Opd::reg(tmp[j]);
            out.push_back(m);
        }
        Inst j;
        j.op = Op::Jmp;
        j.t1 = entry;
        out.push_back(j);
        i++;  // the Ret
    }
    f.code = std::move(out);
    return true;
}

// ------------------------------------------- constants, copies, algebra, branch folding (per basic block)
inline bool localOpt(IRFunc& f) {
    bool changed = false;
    std::unordered_map<int, long long> K;
    std::unordered_map<int, int> C;
    auto subst = [&](Opd& o) {
        if (!o.isReg()) return;
        auto it = K.find((int)o.v);
        if (it != K.end()) { o = Opd::imm(it->second); changed = true; return; }
        auto jt = C.find((int)o.v);
        if (jt != C.end()) { o = Opd::reg(jt->second); changed = true; }
    };
    auto kill = [&](int r) {
        K.erase(r);
        C.erase(r);
        for (auto it = C.begin(); it != C.end();) it = it->second == r ? C.erase(it) : std::next(it);
    };
    auto toMov = [&](Inst& in, Opd v) {
        in.op = Op::Mov;
        in.a = v;
        in.b = Opd();
        changed = true;
    };

    for (Inst& in : f.code) {
        if (in.op == Op::Label) { K.clear(); C.clear(); continue; }
        subst(in.a); subst(in.b); subst(in.c);
        for (auto& a : in.args) subst(a);

        if (isBin(in.op)) {
            long long out;
            if (in.a.isImm() && in.b.isImm() && evalBin(in.op, in.a.v, in.b.v, out)) {
                toMov(in, Opd::imm(out));
            } else {
                bool same = in.a.isReg() && in.b.isReg() && in.a.v == in.b.v;
                switch (in.op) {
                    case Op::Add:
                        if (in.b.isImm() && in.b.v == 0) toMov(in, in.a);
                        else if (in.a.isImm() && in.a.v == 0) toMov(in, in.b);
                        break;
                    case Op::Sub:
                        if (in.b.isImm() && in.b.v == 0) toMov(in, in.a);
                        else if (same) toMov(in, Opd::imm(0));
                        break;
                    case Op::Mul:
                        if ((in.b.isImm() && in.b.v == 0) || (in.a.isImm() && in.a.v == 0)) toMov(in, Opd::imm(0));
                        else if (in.b.isImm() && in.b.v == 1) toMov(in, in.a);
                        else if (in.a.isImm() && in.a.v == 1) toMov(in, in.b);
                        break;
                    case Op::Div:
                        if (in.b.isImm() && in.b.v == 1) toMov(in, in.a);
                        break;
                    case Op::Mod:
                        if (in.b.isImm() && in.b.v == 1) toMov(in, Opd::imm(0));
                        break;
                    case Op::Eq: case Op::Le: case Op::Ge:
                        if (same) toMov(in, Opd::imm(1));
                        break;
                    case Op::Ne: case Op::Lt: case Op::Gt:
                        if (same) toMov(in, Opd::imm(0));
                        break;
                    default: break;
                }
            }
        } else if (in.op == Op::Neg && in.a.isImm()) {
            toMov(in, Opd::imm(wsub(0, in.a.v)));
        } else if (in.op == Op::BNot && in.a.isImm()) {
            toMov(in, Opd::imm(~in.a.v));
        } else if (in.op == Op::Not && in.a.isImm()) {
            toMov(in, Opd::imm(in.a.v ^ 1));
        } else if (in.op == Op::Br) {
            if (in.a.isImm()) {
                in.op = Op::Jmp;
                in.t1 = in.a.v != 0 ? in.t1 : in.t2;
                in.t2 = -1;
                in.a = Opd();
                changed = true;
            } else if (in.t1 == in.t2) {
                in.op = Op::Jmp;
                in.t2 = -1;
                in.a = Opd();
                changed = true;
            }
        }

        if (in.dst >= 0) {
            kill(in.dst);
            if (in.op == Op::Mov) {
                if (in.a.isImm()) K[in.dst] = in.a.v;
                else if (in.a.isReg() && in.a.v != in.dst) C[in.dst] = (int)in.a.v;
            }
        }
        if (isTerm(in.op)) { K.clear(); C.clear(); }
    }

    size_t before = f.code.size();
    f.code.erase(std::remove_if(f.code.begin(), f.code.end(),
                                [](const Inst& i) { return i.op == Op::Mov && i.a.isReg() && i.a.v == i.dst; }),
                 f.code.end());
    return changed || f.code.size() != before;
}

// ----------------------------------------------------- unreachable code, jump threading, dead labels
inline bool cleanup(IRFunc& f) {
    bool changed = false;
    int n = (int)f.code.size();

    // thread jumps through labels that only jump onward
    std::unordered_map<int, int> fwd;
    for (int i = 0; i < n; i++) {
        if (f.code[i].op != Op::Label) continue;
        int j = i + 1;
        while (j < n && f.code[j].op == Op::Label) j++;
        if (j < n && f.code[j].op == Op::Jmp) fwd[f.code[i].t1] = f.code[j].t1;
    }
    auto resolve = [&](int l) {
        for (int d = 0; d < 16; d++) {
            auto it = fwd.find(l);
            if (it == fwd.end() || it->second == l) break;
            l = it->second;
        }
        return l;
    };
    for (Inst& in : f.code) {
        if (in.op == Op::Jmp || in.op == Op::Br) {
            int a = resolve(in.t1);
            if (a != in.t1) { in.t1 = a; changed = true; }
            if (in.op == Op::Br) {
                int b = resolve(in.t2);
                if (b != in.t2) { in.t2 = b; changed = true; }
                if (in.t1 == in.t2) { in.op = Op::Jmp; in.a = Opd(); in.t2 = -1; changed = true; }
            }
        }
    }

    // drop unreachable blocks
    CFG g = buildCFG(f);
    if (!g.start.empty()) {
        std::vector<char> seen(g.start.size(), 0);
        std::vector<int> st{0};
        seen[0] = 1;
        while (!st.empty()) {
            int b = st.back();
            st.pop_back();
            for (int s : g.succ[b])
                if (!seen[s]) { seen[s] = 1; st.push_back(s); }
        }
        std::vector<Inst> out;
        for (size_t b = 0; b < g.start.size(); b++) {
            if (!seen[b]) { changed = true; continue; }
            for (int i = g.start[b]; i < g.end((int)b, n); i++) out.push_back(f.code[i]);
        }
        f.code = std::move(out);
        n = (int)f.code.size();
    }

    // jmp to the very next label is a no-op
    std::vector<Inst> out;
    for (int i = 0; i < n; i++) {
        if (f.code[i].op == Op::Jmp) {
            bool next = false;
            for (int j = i + 1; j < n && f.code[j].op == Op::Label; j++)
                if (f.code[j].t1 == f.code[i].t1) { next = true; break; }
            if (next) { changed = true; continue; }
        }
        out.push_back(f.code[i]);
    }
    f.code = std::move(out);

    // labels nobody jumps to
    std::unordered_set<int> used;
    for (const Inst& in : f.code) {
        if (in.op == Op::Jmp) used.insert(in.t1);
        if (in.op == Op::Br) { used.insert(in.t1); used.insert(in.t2); }
    }
    size_t before = f.code.size();
    f.code.erase(std::remove_if(f.code.begin(), f.code.end(),
                                [&](const Inst& i) { return i.op == Op::Label && !used.count(i.t1); }),
                 f.code.end());
    return changed || f.code.size() != before;
}

// -------------------------------------------------------------------------- dead code (liveness)
template <class F>
inline void forUses(const Inst& in, F fn) {
    for (const Opd* o : {&in.a, &in.b, &in.c})
        if (o->isReg()) fn((int)o->v);
    for (const Opd& a : in.args)
        if (a.isReg()) fn((int)a.v);
}

inline bool isPure(const Inst& in) {
    if (in.op == Op::Len) return in.nonnull;  // len(null) traps, so it may only be dropped when provably safe
    switch (in.op) {
        case Op::Mov: case Op::Add: case Op::Sub: case Op::Mul: case Op::Eq: case Op::Ne: case Op::Lt:
        case Op::Le: case Op::Gt: case Op::Ge: case Op::Neg: case Op::Not: case Op::Str:
        case Op::And: case Op::Or: case Op::Xor: case Op::Shl: case Op::Shr: case Op::BNot:
        case Op::FAdd: case Op::FSub: case Op::FMul: case Op::FDiv: case Op::FEq: case Op::FNe: case Op::FLt:
        case Op::FLe: case Op::FGt: case Op::FGe: case Op::FNeg: case Op::IToF: case Op::FToI: case Op::FSqrt:
        case Op::FnAddr:
            return true;
        case Op::Div: case Op::Mod:  // cannot trap with a constant divisor other than 0
            return in.b.isImm() && in.b.v != 0;
        default:
            return false;
    }
}

// classic backward dataflow: which registers are live on entry to / exit from each basic block
inline void liveness(const IRFunc& f, const CFG& g, std::vector<std::vector<char>>& liveIn,
                     std::vector<std::vector<char>>& liveOut) {
    int nb = (int)g.start.size(), n = (int)f.code.size(), R = f.nregs;
    liveIn.assign(nb, std::vector<char>(R, 0));
    liveOut.assign(nb, std::vector<char>(R, 0));
    std::vector<std::vector<char>> use(nb, std::vector<char>(R, 0)), def(nb, std::vector<char>(R, 0));
    for (int b = 0; b < nb; b++)
        for (int i = g.start[b]; i < g.end(b, n); i++) {
            const Inst& in = f.code[i];
            forUses(in, [&](int r) { if (!def[b][r]) use[b][r] = 1; });
            if (in.dst >= 0) def[b][in.dst] = 1;
        }
    for (bool again = true; again;) {
        again = false;
        for (int b = nb - 1; b >= 0; b--) {
            for (int s : g.succ[b])
                for (int r = 0; r < R; r++)
                    if (liveIn[s][r] && !liveOut[b][r]) liveOut[b][r] = 1;
            for (int r = 0; r < R; r++) {
                char v = use[b][r] || (liveOut[b][r] && !def[b][r]);
                if (v && !liveIn[b][r]) { liveIn[b][r] = 1; again = true; }
            }
        }
    }
}

inline bool dce(IRFunc& f) {
    CFG g = buildCFG(f);
    int nb = (int)g.start.size(), n = (int)f.code.size();
    if (nb == 0) return false;
    std::vector<std::vector<char>> liveIn, liveOut;
    liveness(f, g, liveIn, liveOut);
    std::vector<char> dead(n, 0);
    bool changed = false;
    for (int b = 0; b < nb; b++) {
        std::vector<char> live = liveOut[b];
        for (int i = g.end(b, n) - 1; i >= g.start[b]; i--) {
            const Inst& in = f.code[i];
            if (in.dst >= 0 && !live[in.dst] && isPure(in)) {
                dead[i] = 1;
                changed = true;
                continue;
            }
            if (in.dst >= 0) live[in.dst] = 0;
            forUses(in, [&](int r) { live[r] = 1; });
        }
    }
    if (changed) {
        std::vector<Inst> out;
        for (int i = 0; i < n; i++)
            if (!dead[i]) out.push_back(std::move(f.code[i]));
        f.code = std::move(out);
    }
    return changed;
}

// `t = <op>; x = t` with t used nowhere else becomes `x = <op>` (the lowering computes into fresh temporaries and
// then copies; this removes the copy, which matters most for loop counters and accumulators)
inline bool coalesce(IRFunc& f) {
    std::vector<int> nuse(f.nregs, 0), ndef(f.nregs, 0);
    for (const Inst& in : f.code) {
        forUses(in, [&](int r) { nuse[r]++; });
        if (in.dst >= 0) ndef[in.dst]++;
    }
    bool changed = false;
    std::vector<char> drop(f.code.size(), 0);
    for (size_t k = 0; k + 1 < f.code.size(); k++) {
        Inst& a = f.code[k];
        Inst& b = f.code[k + 1];
        if (a.dst < 0 || a.dst < f.nparams || b.op != Op::Mov || !b.a.isReg() || b.a.v != a.dst || b.dst == a.dst) continue;
        int t = a.dst;
        if (nuse[t] != 1 || ndef[t] != 1) continue;
        a.dst = b.dst;
        drop[k + 1] = 1;
        k++;
        changed = true;
    }
    if (changed) {
        std::vector<Inst> out;
        for (size_t i = 0; i < f.code.size(); i++)
            if (!drop[i]) out.push_back(std::move(f.code[i]));
        f.code = std::move(out);
    }
    return changed;
}

// ---------------------------------------------------------------------- common subexpressions (per block)
// A pure computation, an array length or a load that repeats one already available in the same block is replaced by
// a copy of the earlier result. Availability ends when an operand or the holder is redefined; loads also end at
// stores (a store with a constant index only ends loads with that index or a variable one) and at calls into Ember
// code (which may store). Lengths never change, and the runtime's helpers never write to existing objects.
inline bool cse(IRFunc& f) {
    bool changed = false;
    std::unordered_map<std::string, int> avail;           // expression -> register holding its value
    std::unordered_map<int, std::vector<std::string>> mention;  // register -> expressions that read or hold it
    auto key = [](const Inst& in) {
        Opd a = in.a, b = in.b;
        bool comm = in.op == Op::Add || in.op == Op::Mul || in.op == Op::And || in.op == Op::Or || in.op == Op::Xor ||
                    in.op == Op::Eq || in.op == Op::Ne || in.op == Op::FAdd || in.op == Op::FMul;
        auto txt = [](const Opd& o) { return (o.isImm() ? "#" : o.isReg() ? "r" : "_") + std::to_string(o.v); };
        std::string ta = txt(a), tb = txt(b);
        if (comm && tb < ta) std::swap(ta, tb);
        return std::to_string((int)in.op) + ":" + ta + "," + tb + "," + std::to_string(in.w);
    };
    auto forget = [&](int r) {
        auto it = mention.find(r);
        if (it == mention.end()) return;
        for (const std::string& k : it->second) avail.erase(k);
        mention.erase(it);
    };
    auto forgetLoads = [&](const Inst* st) {
        for (auto it = avail.begin(); it != avail.end();) {
            bool isLoad = it->first.rfind(std::to_string((int)Op::Load) + ":", 0) == 0;
            bool keep = !isLoad;
            if (isLoad && st && st->b.isImm()) {  // a store to a constant index leaves loads of other constant indexes
                std::string k = it->first;
                size_t c = k.find(',');
                std::string idx = k.substr(c + 1, k.find(',', c + 1) - c - 1);
                keep = idx[0] == '#' && idx != "#" + std::to_string(st->b.v);
            }
            it = keep ? std::next(it) : avail.erase(it);
        }
    };
    for (Inst& in : f.code) {
        if (in.op == Op::Label) {
            avail.clear();
            mention.clear();
            continue;
        }
        if (in.op == Op::Store) forgetLoads(&in);
        if (in.op == Op::Call || in.op == Op::CallInd) forgetLoads(nullptr);
        bool candidate = in.dst >= 0 && (in.op == Op::Load || in.op == Op::Len ||
                                         (isPure(in) && in.op != Op::Mov && in.op != Op::FnAddr && in.op != Op::Str));
        std::string k;
        if (candidate) {
            k = key(in);
            auto it = avail.find(k);
            if (it != avail.end() && it->second != in.dst) {
                int src = it->second;
                in.op = Op::Mov;
                in.a = Opd::reg(src);
                in.b = Opd();
                in.c = Opd();
                in.nocheck = in.nonnull = false;
                changed = true;
                candidate = false;
            }
        }
        if (in.dst >= 0) forget(in.dst);
        if (candidate) {
            bool selfRef = (in.a.isReg() && in.a.v == in.dst) || (in.b.isReg() && in.b.v == in.dst);
            if (!selfRef) {
                avail[k] = in.dst;
                mention[in.dst].push_back(k);
                if (in.a.isReg()) mention[(int)in.a.v].push_back(k);
                if (in.b.isReg()) mention[(int)in.b.v].push_back(k);
            }
        }
        if (isTerm(in.op)) {
            avail.clear();
            mention.clear();
        }
    }
    return changed;
}

// -------------------------------------------------------------------------------- loops and dominators
struct Loops {
    std::vector<std::vector<int>> preds;
    std::vector<std::vector<char>> dom;  // dom[b][d]: d dominates b
    struct Loop { int header; std::vector<char> body; };
    std::vector<Loop> loops;
};

inline Loops findLoops(const CFG& g) {
    Loops L;
    int nb = (int)g.start.size();
    L.preds.assign(nb, {});
    for (int b = 0; b < nb; b++)
        for (int s : g.succ[b]) L.preds[s].push_back(b);
    L.dom.assign(nb, std::vector<char>(nb, 1));
    if (nb) {
        L.dom[0].assign(nb, 0);
        L.dom[0][0] = 1;
    }
    for (bool again = true; again;) {
        again = false;
        for (int b = 1; b < nb; b++) {
            std::vector<char> d(nb, 1);
            bool any = false;
            for (int p : L.preds[b]) {
                any = true;
                for (int k = 0; k < nb; k++) d[k] = d[k] && L.dom[p][k];
            }
            if (!any) d.assign(nb, 0);  // unreachable
            d[b] = 1;
            if (d != L.dom[b]) { L.dom[b] = d; again = true; }
        }
    }
    for (int b = 0; b < nb; b++)
        for (int h : g.succ[b]) {
            if (!L.dom[b][h]) continue;  // b -> h is a back edge: h dominates b
            Loops::Loop lp;
            lp.header = h;
            lp.body.assign(nb, 0);
            lp.body[h] = 1;
            std::vector<int> st{b};
            while (!st.empty()) {
                int x = st.back();
                st.pop_back();
                if (lp.body[x]) continue;
                lp.body[x] = 1;
                for (int p : L.preds[x]) st.push_back(p);
            }
            bool merged = false;  // several back edges to one header: one loop
            for (auto& o : L.loops)
                if (o.header == h) {
                    for (int k = 0; k < nb; k++) o.body[k] |= lp.body[k];
                    merged = true;
                }
            if (!merged) L.loops.push_back(lp);
        }
    return L;
}

// -------------------------------------------------------------------- loop-invariant code motion
// A pure, non-trapping computation inside a loop whose operands are not changed by the loop, whose result is the
// only definition of its register, is used only inside the loop and is not live into the header, is computed once in
// front of the loop instead. Speculating it is harmless: it cannot trap, and nothing outside the loop reads it.
inline bool licm(IRFunc& f) {
    bool any = false;
    for (int round = 0; round < 8; round++) {
        CFG g = buildCFG(f);
        int nb = (int)g.start.size(), n = (int)f.code.size();
        if (nb < 2) return any;
        Loops L = findLoops(g);
        std::vector<std::vector<char>> liveIn, liveOut;
        liveness(f, g, liveIn, liveOut);
        std::vector<int> blockOf(n);
        for (int b = 0; b < nb; b++)
            for (int i = g.start[b]; i < g.end(b, n); i++) blockOf[i] = b;
        bool moved = false;
        for (const auto& lp : L.loops) {
            int h = lp.header;
            // the preheader slot: the header is entered from outside only by falling through from the block before it
            if (h == 0) continue;
            int outside = 0;
            for (int p : L.preds[h]) if (!lp.body[p]) outside++;
            bool fallIn = false;
            for (int p : L.preds[h]) if (!lp.body[p] && p == h - 1) fallIn = true;
            if (outside != 1 || !fallIn) continue;
            int insertAt = g.start[h];  // before the header's label
            const Inst& last = f.code[g.end(h - 1, n) - 1];
            if (last.op == Op::Br || last.op == Op::Ret) continue;
            if (last.op == Op::Jmp) insertAt = g.end(h - 1, n) - 1;  // before `jmp header`
            std::vector<int> defsIn(f.nregs, 0), usesOut(f.nregs, 0);
            for (int i = 0; i < n; i++) {
                const Inst& in = f.code[i];
                if (lp.body[blockOf[i]]) {
                    if (in.dst >= 0) defsIn[in.dst]++;
                } else {
                    forUses(in, [&](int r) { usesOut[r]++; });
                }
            }
            std::vector<int> hoist;
            std::vector<char> hoisted(f.nregs, 0);
            for (int i = 0; i < n; i++) {
                if (!lp.body[blockOf[i]]) continue;
                const Inst& in = f.code[i];
                if (in.dst < 0 || !isPure(in) || in.op == Op::Len) continue;
                if (defsIn[in.dst] != 1 || usesOut[in.dst] || liveIn[h][in.dst] || in.dst < f.nparams) continue;
                bool inv = true;
                forUses(in, [&](int r) { if (defsIn[r] && !hoisted[r]) inv = false; });
                if (!inv) continue;
                hoist.push_back(i);
                hoisted[in.dst] = 1;
            }
            if (hoist.empty()) continue;
            std::vector<Inst> out;
            std::vector<char> take(n, 0);
            for (int i : hoist) take[i] = 1;
            for (int i = 0; i < n; i++) {
                if (i == insertAt)
                    for (int k : hoist) out.push_back(f.code[k]);
                if (!take[i]) out.push_back(f.code[i]);
            }
            f.code = std::move(out);
            moved = any = true;
            break;  // positions changed: recompute
        }
        if (!moved) break;
    }
    return any;
}

// ------------------------------------------------------------- constants across blocks
// A register (not a parameter) whose only definition is `r = mov <constant>`, and whose definition dominates all its
// uses, is that constant everywhere (localOpt only propagates within a block).
inline bool globalConst(IRFunc& f) {
    CFG g = buildCFG(f);
    int nb = (int)g.start.size(), n = (int)f.code.size();
    if (nb < 2) return false;
    Loops L = findLoops(g);
    std::vector<int> blockOf(n), ndef(f.nregs, 0), defPos(f.nregs, -1);
    for (int b = 0; b < nb; b++)
        for (int i = g.start[b]; i < g.end(b, n); i++) blockOf[i] = b;
    for (int i = 0; i < n; i++)
        if (f.code[i].dst >= 0) { ndef[f.code[i].dst]++; defPos[f.code[i].dst] = i; }
    std::vector<char> isK(f.nregs, 0);
    for (int r = f.nparams; r < f.nregs; r++)
        if (ndef[r] == 1 && f.code[defPos[r]].op == Op::Mov && f.code[defPos[r]].a.isImm()) isK[r] = 1;
    // every use must be dominated by the definition
    for (int i = 0; i < n; i++)
        forUses(f.code[i], [&](int r) {
            if (!isK[r]) return;
            int d = defPos[r];
            bool dom = blockOf[d] == blockOf[i] ? d < i : L.dom[blockOf[i]][blockOf[d]] != 0;
            if (!dom) isK[r] = 0;
        });
    bool changed = false;
    for (Inst& in : f.code) {
        auto sub = [&](Opd& o) {
            if (o.isReg() && isK[o.v]) { o = Opd::imm(f.code[defPos[o.v]].a.v); changed = true; }
        };
        sub(in.a); sub(in.b); sub(in.c);
        for (Opd& a : in.args) sub(a);
    }
    return changed;
}

// --------------------------------------------------- bounds-check elimination from induction-variable ranges
// For a loop whose header tests `iv < B` (or `iv <= B`) and exits otherwise, where iv starts at a constant >= 0 and
// is only ever increased by a positive constant inside the loop, an access a[iv] (or a[x], x a single-definition
// copy of iv) that can only be reached after that test and before any increment of iv is in range when
//   * B is `len(a)` computed from the same, unchanged a, or
//   * a's only definition allocated it with size B (for <) or B + k, k >= 1 (for <=), or a constant size above B.
// (The interpreter re-checks every access marked this way, so an unsound proof shows up in the tests.)
inline int& bceCount() {
    static int n = 0;
    return n;
}
inline bool rangeBCE(IRFunc& f) {
    CFG g = buildCFG(f);
    int nb = (int)g.start.size(), n = (int)f.code.size();
    if (nb < 2) return false;
    Loops L = findLoops(g);
    std::vector<int> blockOf(n);
    for (int b = 0; b < nb; b++)
        for (int i = g.start[b]; i < g.end(b, n); i++) blockOf[i] = b;
    std::vector<std::vector<int>> defAt(f.nregs);
    for (int i = 0; i < n; i++)
        if (f.code[i].dst >= 0) defAt[f.code[i].dst].push_back(i);
    std::unordered_map<int, int> labelPos;
    for (int i = 0; i < n; i++)
        if (f.code[i].op == Op::Label) labelPos[f.code[i].t1] = i;
    auto single = [&](long long r) -> const Inst* {
        return r >= f.nparams && defAt[r].size() == 1 ? &f.code[defAt[r][0]] : nullptr;
    };
    // the allocation size of array register a, if a has exactly one definition and it is an allocation
    auto allocSize = [&](long long a) -> const Opd* {
        const Inst* d = single(a);
        if (!d) return nullptr;
        if (d->op == Op::NewArr) return &d->a;
        if (d->op == Op::Rt && d->name == "ember_new_str_array") return &d->args[0];
        return nullptr;
    };
    bool changed = false;
    for (const auto& lp : L.loops) {
        int h = lp.header;
        const Inst& br = f.code[g.end(h, n) - 1];
        if (br.op != Op::Br || !br.a.isReg()) continue;
        auto tb = g.labelBlock.find(br.t1), fb = g.labelBlock.find(br.t2);
        if (tb == g.labelBlock.end() || fb == g.labelBlock.end() || !lp.body[tb->second] || lp.body[fb->second]) continue;
        int tBlock = tb->second;
        const Inst* cmp = nullptr;
        for (int i = g.start[h]; i < g.end(h, n); i++)
            if (f.code[i].dst == br.a.v) cmp = &f.code[i];
        if (!cmp || (cmp->op != Op::Lt && cmp->op != Op::Le) || !cmp->a.isReg()) continue;
        long long iv = cmp->a.v;
        Opd bound = cmp->b;
        bool le = cmp->op == Op::Le;
        if (iv < f.nparams) continue;
        // iv: >= 0 on entry, only increased inside the loop
        bool ok = true;
        int minDef = n;
        for (int d : defAt[iv]) {
            const Inst& x = f.code[d];
            if (lp.body[blockOf[d]]) {
                if (!(x.op == Op::Add && x.a.isReg() && x.a.v == iv && x.b.isImm() && x.b.v > 0 && x.b.v < (1 << 20))) ok = false;
                minDef = std::min(minDef, d);
            } else if (!(x.op == Op::Mov && x.a.isImm() && x.a.v >= 0)) {
                ok = false;
            }
        }
        if (!ok) continue;
        // no path from an increment back to an earlier point of the body except through the header
        int hpos = g.start[h];
        auto noIncBefore = [&](int p) {
            if (p >= minDef) return false;
            for (int q = minDef; q < n; q++) {
                if (!lp.body[blockOf[q]]) continue;
                const Inst& j = f.code[q];
                for (int t : {j.op == Op::Jmp || j.op == Op::Br ? j.t1 : -1, j.op == Op::Br ? j.t2 : -1}) {
                    if (t < 0) continue;
                    int tp = labelPos.count(t) ? labelPos[t] : n;
                    if (tp > hpos && tp <= p) return false;
                }
            }
            return L.dom[blockOf[p]][tBlock] != 0;
        };
        auto inRange = [&](long long base) {  // is iv < len(base) whenever the test held?
            if (bound.isReg()) {
                const Inst* bd = single(bound.v);
                if (!le && bd && bd->op == Op::Len && bd->a.isReg() && bd->a.v == base) {
                    // the same array: either it never changes, or the length is taken in the header each time round
                    // and the loop does not change it
                    if (base >= f.nparams && defAt[base].size() == 1) return true;
                    if (base < f.nparams && defAt[base].empty()) return true;
                    if (blockOf[defAt[bound.v][0]] != h) return false;
                    for (int d : defAt[base])
                        if (lp.body[blockOf[d]]) return false;
                    return true;
                }
            }
            const Opd* sz = allocSize(base);
            if (!sz) return false;
            if (bound.isImm() && sz->isImm()) return le ? sz->v > bound.v : sz->v >= bound.v;
            if (!bound.isReg() || defAt[bound.v].size() != 1 || lp.body[blockOf[defAt[bound.v][0]]]) return false;
            if (!le) return sz->isReg() && sz->v == bound.v;
            const Inst* sd = sz->isReg() ? single(sz->v) : nullptr;
            return sd && sd->op == Op::Add && sd->a.isReg() && sd->a.v == bound.v && sd->b.isImm() && sd->b.v >= 1;
        };
        // index registers equal to iv at a safe point: iv itself, and single-definition copies made at one
        std::vector<char> alias(f.nregs, 0);
        for (int i = 0; i < n; i++) {
            const Inst& x = f.code[i];
            if (lp.body[blockOf[i]] && x.op == Op::Mov && x.a.isReg() && x.a.v == iv && x.dst >= f.nparams &&
                defAt[x.dst].size() == 1 && noIncBefore(i))
                alias[x.dst] = 1;
        }
        for (int i = 0; i < n; i++) {
            Inst& x = f.code[i];
            if ((x.op != Op::Load && x.op != Op::Store) || x.nocheck || !x.b.isReg() || !x.a.isReg() || !lp.body[blockOf[i]]) continue;
            bool idxOk = x.b.v == iv && noIncBefore(i);
            if (!idxOk && alias[x.b.v]) {  // every value the copy ever holds was a checked value of iv; its one
                int cd = defAt[x.b.v][0];  // definition must come first on every path to the access
                idxOk = blockOf[cd] == blockOf[i] ? cd < i : L.dom[blockOf[i]][blockOf[cd]] != 0;
            }
            if (idxOk && inRange(x.a.v)) {
                x.nocheck = true;
                bceCount()++;
                changed = true;
            }
        }
    }
    return changed;
}

inline int& inlinedCount() {
    static int n = 0;
    return n;
}

// ------------------------------------------------------------------------------------------ inlining
// Small functions that make no calls ("leaf") are expanded at every call site: parameters become copies of the
// arguments, the callee's registers and labels are renumbered into fresh ranges, and each `ret` becomes a move
// into the call's destination plus a jump to the join label. Running it twice lets functions that only call
// leaves become leaves themselves.
inline bool isLeaf(const IRFunc& f) {
    for (const Inst& in : f.code)
        if (in.op == Op::Call) return false;
    return true;
}

inline int bodySize(const IRFunc& f) {
    int n = 0;
    for (const Inst& in : f.code)
        if (in.op != Op::Label) n++;
    return n;
}

inline bool callsItself(const IRFunc& f) {
    for (const Inst& in : f.code)
        if (in.op == Op::Call && in.name == f.name) return true;
    return false;
}

// Leaves up to maxSize instructions are always expanded; functions that do call others (but not themselves) are
// expanded when they are at most nonLeafSize instructions and the caller has not grown past callerBudget.
inline bool inlineCalls(Module& m, int maxSize = 30, int nonLeafSize = 40, int callerBudget = 4000) {
    std::unordered_map<std::string, const IRFunc*> small;
    // the candidates are copied, so expanding a function into another never reads a body that is being rewritten
    std::vector<IRFunc> copies;
    copies.reserve(m.funcs.size());
    bool nonLeaf = enabled("inline-nonleaf");
    for (const IRFunc& f : m.funcs) {
        if (f.name == "main") continue;
        bool leaf = isLeaf(f);
        int sz = bodySize(f);
        if ((leaf && sz <= maxSize) || (nonLeaf && !leaf && sz <= nonLeafSize && !callsItself(f))) {
            copies.push_back(f);
        }
    }
    for (const IRFunc& c : copies) small[c.name] = &c;
    bool changed = false;
    for (IRFunc& f : m.funcs) {
        std::vector<Inst> out;
        int grown = bodySize(f);
        for (const Inst& in : f.code) {
            auto it = in.op == Op::Call ? small.find(in.name) : small.end();
            if (it != small.end() && (it->second->name == f.name || (!isLeaf(*it->second) && grown > callerBudget)))
                it = small.end();
            if (it == small.end()) {
                out.push_back(in);
                continue;
            }
            grown += bodySize(*it->second);
            const IRFunc& c = *it->second;
            int rb = f.nregs, lb = f.nlabels;
            f.nregs += c.nregs;
            f.nlabels += c.nlabels + 1;
            int endL = lb + c.nlabels;
            for (int p = 0; p < c.nparams; p++) {
                Inst mv;
                mv.op = Op::Mov;
                mv.dst = rb + p;
                mv.a = in.args[p];
                mv.line = in.line;
                out.push_back(mv);
            }
            auto shift = [&](Opd& o) { if (o.isReg()) o.v += rb; };
            for (const Inst& ci : c.code) {
                Inst x = ci;
                if (x.dst >= 0) x.dst += rb;
                shift(x.a); shift(x.b); shift(x.c);
                for (Opd& a : x.args) shift(a);  // runtime calls (Op::Rt) keep their operands here
                switch (x.op) {
                    case Op::Label: case Op::Jmp: x.t1 += lb; break;
                    case Op::Br: x.t1 += lb; x.t2 += lb; break;
                    case Op::Ret: {
                        if (in.dst >= 0 && x.a.k != Opd::None) {
                            Inst mv;
                            mv.op = Op::Mov;
                            mv.dst = in.dst;
                            mv.a = x.a;
                            mv.line = x.line;
                            out.push_back(mv);
                        }
                        Inst j;
                        j.op = Op::Jmp;
                        j.t1 = endL;
                        j.line = x.line;
                        out.push_back(j);
                        continue;
                    }
                    default: break;
                }
                out.push_back(x);
            }
            Inst e;
            e.op = Op::Label;
            e.t1 = endL;
            out.push_back(e);
            inlinedCount()++;
            changed = true;
        }
        f.code = std::move(out);
    }
    return changed;
}

// drop functions that nothing reachable from main calls any more
inline void pruneUnreachable(Module& m) {
    std::unordered_set<std::string> live{"main"};
    for (bool again = true; again;) {
        again = false;
        for (const IRFunc& f : m.funcs) {
            if (!live.count(f.name)) continue;
            for (const Inst& in : f.code)  // direct calls, and functions whose address is taken (closures)
                if ((in.op == Op::Call || in.op == Op::FnAddr) && live.insert(in.name).second) again = true;
        }
    }
    m.funcs.erase(std::remove_if(m.funcs.begin(), m.funcs.end(), [&](const IRFunc& f) { return !live.count(f.name); }),
                  m.funcs.end());
}

inline void optimizeFunc(IRFunc& f) {
    tailCalls(f);
    for (int round = 0; round < 12; round++) {
        bool c = localOpt(f);
        c |= cleanup(f);
        c |= dce(f);
        if (enabled("cse")) c |= cse(f);
        if (enabled("coalesce")) c |= coalesce(f);
        if (!c && enabled("gconst")) c |= globalConst(f);
        if (!c && enabled("licm")) c |= licm(f);
        if (!c) break;
    }
    if (enabled("bce")) rangeBCE(f);
}

inline void optimize(Module& m, int level) {
    if (level <= 0) return;
    for (IRFunc& f : m.funcs) optimizeFunc(f);
    for (int pass = 0; pass < 3; pass++) {
        if (!inlineCalls(m)) break;
        for (IRFunc& f : m.funcs) optimizeFunc(f);
    }
    pruneUnreachable(m);
}

}  // namespace opt
