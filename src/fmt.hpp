// fmt.hpp - the source formatter behind `embc fmt file.em`.
//
// It works on tokens (with comments kept), so it never changes what a program means: only the whitespace between
// tokens is rewritten, and every token keeps its exact spelling from the source.
//   * one statement per line, blocks indented by 4 spaces, `} else {` on one line;
//   * braces that hold fields or variants (struct literals, struct and enum declarations) stay inline, as does `{ }`;
//   * one space around binary operators, after commas and colons, none inside brackets, around `..`, after unary
//     operators or inside generic angle brackets (`Vec<int>`);
//   * a line break written in the middle of a statement is kept, as a continuation line indented one level deeper per
//     open bracket;
//   * blank lines are kept (at most one in a row), comments stay where they are (trailing or on their own line).
// Formatting is idempotent: formatting the output again gives the same text.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "lexer.hpp"

class Formatter {
    const std::string& src;
    std::vector<Token> t;
    std::vector<size_t> lineOff;
    std::vector<std::string> raw;
    std::vector<char> inlineBrace, genOpen, genClose, unary;
    std::string out, line;
    std::vector<char> groups;  // open brackets: '(' '[' 'i' (inline brace) 'b' (block)
    std::vector<int> bases;    // for each open group: the indentation level of the line it was opened on
    int curLevel = 0;          // indentation level of the line being built

    size_t offOf(const Token& k) const {
        if (k.k == T::End) return src.size();
        return lineOff[k.pos.line - 1] + (size_t)k.pos.col - 1;
    }
    static bool operand(T k) {
        return k == T::Ident || k == T::Int || k == T::Float || k == T::Str || k == T::Char || k == T::RParen ||
               k == T::RBrack || k == T::True || k == T::False || k == T::Null || k == T::RBrace;
    }
    static bool typeTok(T k) {
        return k == T::Ident || k == T::Comma || k == T::LBrack || k == T::RBrack || k == T::Dot || k == T::Fn ||
               k == T::LParen || k == T::RParen || k == T::Arrow || k == T::Lt || k == T::Gt || k == T::Shr;
    }
    // a line inside an open block or bracket sits one level deeper than the line where that block or bracket opened
    int level() const { return groups.empty() ? 0 : bases.back() + 1; }
    int openBrackets() const {  // brackets (not blocks) open since the innermost block
        int n = 0;
        for (size_t k = groups.size(); k-- > 0 && groups[k] != 'b';) n++;
        return n;
    }

    // turn the token at j (`>=` or `>>=`) into `head` followed by `=`
    void splitAt(size_t j, T head, const char* headText) {
        Token eq = t[j];
        eq.k = T::Assign;
        eq.text = "=";
        eq.nlBefore = 0;
        t[j].k = head;
        t[j].text = headText;
        t.insert(t.begin() + (long)j + 1, eq);
        raw[j] = headText;
        raw.insert(raw.begin() + (long)j + 1, "=");
        for (auto* v : {&inlineBrace, &genOpen, &genClose, &unary}) v->insert(v->begin() + (long)j + 1, 0);
    }

    void analyse() {
        size_t n = t.size();
        inlineBrace.assign(n, 0);
        genOpen.assign(n, 0);
        genClose.assign(n, 0);
        unary.assign(n, 0);
        auto sig = [&](size_t i, int dir) -> long {  // nearest non-comment token
            long j = (long)i + dir;
            while (j >= 0 && j < (long)n && t[j].k == T::Comment) j += dir;
            return j;
        };
        for (size_t i = 0; i < n; i++) {
            T k = t[i].k;
            long p = sig(i, -1), q = sig(i, 1);
            if (k == T::LBrace) {
                long q2 = q >= 0 ? sig((size_t)q, 1) : -1;
                bool fields = q >= 0 && q2 >= 0 && t[q].k == T::Ident && t[q2].k == T::Colon;
                bool empty = q >= 0 && t[q].k == T::RBrace;
                bool enumDecl = false;  // enum Name { ... } or enum Name<T> { ... }
                long b = p;
                if (b >= 0 && t[b].k == T::Gt) {
                    int d = 0;
                    while (b >= 0) {
                        if (t[b].k == T::Gt) d++;
                        if (t[b].k == T::Lt && --d == 0) break;
                        b--;
                    }
                    b = b >= 0 ? sig((size_t)b, -1) : -1;
                }
                if (b >= 0 && t[b].k == T::Ident) {
                    long e = sig((size_t)b, -1);
                    enumDecl = e >= 0 && t[e].k == T::Enum;
                }
                inlineBrace[i] = fields || empty || enumDecl;
            }
            if (k == T::Lt && p >= 0 && t[p].k == T::Ident) {  // a generic argument list?
                int d = 0;
                size_t j = i;
                bool ok = false;
                for (; j < n; j++) {
                    T x = t[j].k;
                    if (x == T::Comment) continue;
                    if (x == T::Ge || x == T::ShrEq) {  // `Vec<int>= v` : the close is glued to `=`; split it
                        int closes = x == T::Ge ? 1 : 2;
                        if (d - closes > 0) break;
                        splitAt(j, x == T::Ge ? T::Gt : T::Shr, x == T::Ge ? ">" : ">>");
                        n = t.size();
                        x = t[j].k;
                    }
                    if (!typeTok(x)) break;
                    if (x == T::Lt) d++;
                    if (x == T::Gt) d--;
                    if (x == T::Shr) d -= 2;
                    if (d <= 0) { ok = d == 0 || (d == -1 && x == T::Shr); break; }
                }
                if (ok && j < n) {
                    long a = sig(j, 1);
                    T nx = a >= 0 ? t[a].k : T::End;
                    if (nx == T::LParen || nx == T::LBrace || nx == T::Dot || nx == T::RParen || nx == T::Comma ||
                        nx == T::Gt || nx == T::Shr || nx == T::RBrack || nx == T::Assign || nx == T::Semi || nx == T::Ident ||
                        nx == T::Arrow || nx == T::RBrace) {
                        for (size_t m = i; m <= j; m++) {
                            if (t[m].k == T::Lt) genOpen[m] = 1;
                            if (t[m].k == T::Gt || t[m].k == T::Shr) genClose[m] = 1;
                        }
                    }
                }
            }
            if (k == T::Minus || k == T::Bang || k == T::Tilde) unary[i] = p < 0 || !operand(t[p].k) || genClose[p];
        }
    }

    void flush() {
        if (line.empty()) return;
        out += line;
        out += '\n';
        line.clear();
    }
    void startLine(const Token& k, int extra) {
        if (k.nlBefore >= 2 && !out.empty() && out.compare(out.size() - 2, 2, "{\n") != 0 &&
            out.compare(out.size() - 2, 2, "\n\n") != 0)
            out += '\n';
        curLevel = std::max(0, level() + extra);
        line = std::string(4 * (size_t)curLevel, ' ');
    }
    bool space(size_t pi, size_t i) const {
        T p = t[pi].k, k = t[i].k;
        if (p == T::LParen || p == T::LBrack) return false;
        if (k == T::RParen || k == T::RBrack || k == T::Comma || k == T::Semi || k == T::Dot || k == T::Colon) return false;
        if (p == T::Dot || p == T::DotDot || k == T::DotDot) return false;
        if (unary[pi]) return false;
        if (genOpen[i] || genOpen[pi] || genClose[i]) return false;
        if (genClose[pi]) return k == T::LBrace || k == T::Assign || k == T::Ident || k == T::Arrow;
        if (k == T::LParen) {
            return !(operand(p) || p == T::Fn) || p == T::RBrace;
        }
        if (k == T::LBrack) return !(operand(p) && p != T::RBrace);
        return true;
    }

public:
    explicit Formatter(const std::string& source) : src(source) {}

    std::string run() {
        t = lex(src, 0, true);
        lineOff.push_back(0);
        for (size_t i = 0; i < src.size(); i++)
            if (src[i] == '\n') lineOff.push_back(i + 1);
        raw.resize(t.size());
        for (size_t i = 0; i + 1 < t.size(); i++) {
            std::string r = src.substr(offOf(t[i]), offOf(t[i + 1]) - offOf(t[i]));
            while (!r.empty() && isspace((unsigned char)r.back())) r.pop_back();
            raw[i] = r;
        }
        analyse();
        long prev = -1;  // previous non-comment token
        bool prevBlockClose = false;
        for (size_t i = 0; i + 1 < t.size(); i++) {
            const Token& k = t[i];
            if (k.k == T::Comment) {
                if (!line.empty() && k.nlBefore == 0) {
                    line += (raw[i].rfind("//", 0) == 0 ? "  " : " ") + raw[i];
                    if (raw[i].rfind("//", 0) == 0) flush();
                } else {
                    flush();
                    startLine(k, 0);
                    line += raw[i];
                    if (raw[i].rfind("//", 0) == 0 || i + 1 >= t.size() || t[i + 1].nlBefore > 0) flush();
                }
                continue;
            }
            bool closeBlock = k.k == T::RBrace && !groups.empty() && groups.back() == 'b';
            bool closing = k.k == T::RParen || k.k == T::RBrack || k.k == T::RBrace;
            int openLevel = -1;  // a closing bracket that starts a line lines up with the line that opened it
            if (closeBlock) flush();
            if (closing && !groups.empty()) {
                openLevel = bases.back();
                groups.pop_back();
                bases.pop_back();
            }
            bool breakHere = !line.empty() && k.nlBefore > 0 && !(prev >= 0 && prevBlockClose);  // written mid-statement
            if (breakHere) flush();
            if (line.empty() && openLevel >= 0) {
                if (k.nlBefore >= 2 && !out.empty() && out.compare(out.size() - 2, 2, "\n\n") != 0) out += '\n';
                curLevel = openLevel;
                line = std::string(4 * (size_t)curLevel, ' ');
            } else if (line.empty()) {
                // a continuation directly inside a block (not inside a bracket) is indented one more level
                startLine(k, breakHere && (groups.empty() || groups.back() == 'b') ? 1 : 0);
            } else if (prev >= 0 && space((size_t)prev, i)) {
                line += ' ';
            }
            line += raw[i];
            prevBlockClose = false;
            switch (k.k) {
                case T::LParen: groups.push_back('('); bases.push_back(curLevel); break;
                case T::LBrack: groups.push_back('['); bases.push_back(curLevel); break;
                case T::LBrace:
                    groups.push_back(inlineBrace[i] ? 'i' : 'b');
                    bases.push_back(curLevel);
                    if (!inlineBrace[i]) flush();
                    break;
                case T::RBrace:
                    if (!closeBlock && groups.empty()) {  // end of a one-line struct / enum declaration
                        T nx = t[i + 1].k;
                        if (nx == T::Fn || nx == T::Struct || nx == T::Enum || nx == T::Pub || nx == T::Const ||
                            nx == T::Import || nx == T::End || (nx == T::Comment && t[i + 1].nlBefore > 0))
                            flush();
                    }
                    if (closeBlock) {
                        prevBlockClose = true;
                        size_t q = i + 1;
                        while (q < t.size() && t[q].k == T::Comment && t[q].nlBefore == 0) q++;
                        T nx = t[i + 1].k == T::Comment && t[i + 1].nlBefore == 0 ? T::Comment : t[q].k;
                        if (nx != T::Else && nx != T::Comma && nx != T::RParen && nx != T::Semi && nx != T::RBrack &&
                            nx != T::Comment && !(nx == T::RBrace && !groups.empty() && groups.back() == 'i'))
                            flush();
                    }
                    break;
                case T::Semi:
                    if (openBrackets() == 0 && !(t[i + 1].k == T::Comment && t[i + 1].nlBefore == 0)) flush();
                    break;
                case T::Comma:
                    if (prev >= 0 && t[prev].k == T::RBrace && openBrackets() == 0) flush();  // after a match arm
                    break;
                default: break;
            }
            prev = (long)i;
        }
        flush();
        while (!out.empty() && out.back() == '\n' && out.size() >= 2 && out[out.size() - 2] == '\n') out.pop_back();
        return out;
    }
};
