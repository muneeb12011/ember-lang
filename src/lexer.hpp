// lexer.hpp - turns source text into tokens.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "diag.hpp"

enum class T {
    Int, Float, Char, Ident, Str,
    Fn, Let, Mut, If, Else, While, For, In, Return, Break, Continue, True, False, Struct, Null, New, Const, Import, Pub, As, Enum, Match,
    LParen, RParen, LBrace, RBrace, LBrack, RBrack, Comma, Semi, Colon, Arrow, FatArrow, DotDot, Dot, Assign,
    Eq, Ne, Lt, Le, Gt, Ge, Plus, Minus, Star, Slash, Percent, AndAnd, OrOr, Bang,
    Amp, Pipe, Caret, Tilde, Shl, Shr,
    PlusEq, MinusEq, StarEq, SlashEq, PercentEq, AmpEq, PipeEq, CaretEq, ShlEq, ShrEq,
    Comment,  // only with lex(..., keepComments = true), for the formatter
    End
};

struct Token {
    T k;
    std::string text;
    long long ival = 0;
    Pos pos;
    double fval = 0;
    int nlBefore = 0;  // newlines between the previous token and this one (the formatter keeps blank lines)
};

inline std::string opText(T k) {
    switch (k) {
        case T::Plus: return "+";
        case T::Minus: return "-";
        case T::Star: return "*";
        case T::Slash: return "/";
        case T::Percent: return "%";
        case T::Eq: return "==";
        case T::Ne: return "!=";
        case T::Lt: return "<";
        case T::Le: return "<=";
        case T::Gt: return ">";
        case T::Ge: return ">=";
        case T::AndAnd: return "&&";
        case T::OrOr: return "||";
        case T::Bang: return "!";
        case T::Amp: return "&";
        case T::Pipe: return "|";
        case T::Caret: return "^";
        case T::Tilde: return "~";
        case T::Shl: return "<<";
        case T::Shr: return ">>";
        default: return "?";
    }
}

inline int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

inline std::vector<Token> lex(const std::string& s, int file = 0, bool keepComments = false) {
    static const std::map<std::string, T> kw = {
        {"fn", T::Fn}, {"let", T::Let}, {"mut", T::Mut}, {"if", T::If}, {"else", T::Else},
        {"while", T::While}, {"for", T::For}, {"in", T::In}, {"return", T::Return},
        {"break", T::Break}, {"continue", T::Continue}, {"true", T::True}, {"false", T::False},
        {"struct", T::Struct}, {"null", T::Null}, {"new", T::New}, {"const", T::Const}, {"import", T::Import},
        {"pub", T::Pub}, {"as", T::As}, {"enum", T::Enum}, {"match", T::Match}};
    std::vector<Token> out;
    size_t i = 0, lineStart = 0;
    int line = 1;
    auto pos = [&](size_t at) { return Pos{line, (int)(at - lineStart) + 1, file}; };
    int nl = 0;
    auto push = [&](T k, std::string text, Pos p, long long v = 0) {
        out.push_back(Token{k, std::move(text), v, p});
        out.back().nlBefore = nl;
        nl = 0;
    };

    // one escape sequence starting at s[j] == '\\'; returns the byte and advances j
    auto escape = [&](size_t& j) -> char {
        char e = j + 1 < s.size() ? s[j + 1] : '\0';
        switch (e) {
            case 'n': j += 2; return '\n';
            case 't': j += 2; return '\t';
            case 'r': j += 2; return '\r';
            case '0': j += 2; return '\0';
            case '\\': j += 2; return '\\';
            case '"': j += 2; return '"';
            case '\'': j += 2; return '\'';
            case 'x': {
                int h = j + 2 < s.size() ? hexDigit(s[j + 2]) : -1, l = j + 3 < s.size() ? hexDigit(s[j + 3]) : -1;
                if (h < 0 || l < 0) throw CompileError{pos(j), "`\\x` needs two hex digits"};
                j += 4;
                return (char)(h * 16 + l);
            }
            default: throw CompileError{pos(j), std::string("unknown escape sequence `\\") + e + "`"};
        }
    };

    while (i < s.size()) {
        char c = s[i];
        if (c == '\n') { line++; i++; lineStart = i; nl++; continue; }
        if (isspace((unsigned char)c)) { i++; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') {
            size_t b = i;
            while (i < s.size() && s[i] != '\n') i++;
            if (keepComments) {
                size_t e = i;
                while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) e--;
                push(T::Comment, s.substr(b, e - b), pos(b));
            }
            continue;
        }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            Pos start = pos(i);
            size_t b = i;
            i += 2;
            bool closed = false;
            while (i < s.size()) {
                if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/') { i += 2; closed = true; break; }
                if (s[i] == '\n') { line++; lineStart = i + 1; }
                i++;
            }
            if (!closed) throw CompileError{start, "unterminated block comment"};
            if (keepComments) push(T::Comment, s.substr(b, i - b), start);
            continue;
        }
        Pos p = pos(i);
        if (isdigit((unsigned char)c)) {
            // hex / binary integers
            if (c == '0' && i + 2 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X' || s[i + 1] == 'b' || s[i + 1] == 'B')) {
                bool hex = s[i + 1] == 'x' || s[i + 1] == 'X';
                size_t j = i + 2;
                unsigned long long v = 0;
                int digits = 0;
                bool overflow = false;
                for (; j < s.size(); j++) {
                    if (s[j] == '_') continue;
                    int d = hexDigit(s[j]);
                    if (d < 0 || (!hex && d > 1)) break;
                    if (v >> (hex ? 60 : 63)) overflow = true;
                    v = v * (hex ? 16 : 2) + (unsigned)d;
                    digits++;
                }
                if (!digits) throw CompileError{p, hex ? "expected hex digits after `0x`" : "expected binary digits after `0b`"};
                if (overflow || v > (unsigned long long)INT64_MAX) throw CompileError{p, "integer literal is too large for a 64-bit int"};
                push(T::Int, s.substr(i, j - i), p, (long long)v);
                i = j;
                continue;
            }
            size_t j = i;
            while (j < s.size() && (isdigit((unsigned char)s[j]) || s[j] == '_')) j++;
            if (j + 1 < s.size() && s[j] == '.' && isdigit((unsigned char)s[j + 1])) {  // 1.5, 2.0e-3 (but not the range `0..n`)
                j++;
                while (j < s.size() && isdigit((unsigned char)s[j])) j++;
                if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                    size_t k = j + 1;
                    if (k < s.size() && (s[k] == '+' || s[k] == '-')) k++;
                    if (k < s.size() && isdigit((unsigned char)s[k])) {
                        while (k < s.size() && isdigit((unsigned char)s[k])) k++;
                        j = k;
                    }
                }
                std::string txt;
                for (size_t q = i; q < j; q++)
                    if (s[q] != '_') txt += s[q];
                push(T::Float, txt, p);
                out.back().fval = strtod(txt.c_str(), nullptr);
                i = j;
                continue;
            }
            std::string num;
            for (size_t q = i; q < j; q++)
                if (s[q] != '_') num += s[q];
            unsigned long long v = strtoull(num.c_str(), nullptr, 10);
            if (num.size() > 19 || v > (unsigned long long)INT64_MAX) throw CompileError{p, "integer literal is too large for a 64-bit int"};
            push(T::Int, num, p, (long long)v);
            i = j;
            continue;
        }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < s.size() && (isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            std::string id = s.substr(i, j - i);
            auto it = kw.find(id);
            push(it != kw.end() ? it->second : T::Ident, id, p);
            i = j;
            continue;
        }
        if (c == '"') {
            std::string val;
            size_t j = i + 1;
            bool closed = false;
            while (j < s.size() && s[j] != '\n') {
                if (s[j] == '"') { closed = true; j++; break; }
                if (s[j] == '\\') val += escape(j);
                else val += s[j++];
            }
            if (!closed) throw CompileError{p, "unterminated string literal"};
            push(T::Str, val, p);
            i = j;
            continue;
        }
        if (c == '\'') {
            size_t j = i + 1;
            if (j >= s.size() || s[j] == '\n' || s[j] == '\'') throw CompileError{p, "empty character literal"};
            unsigned char ch;
            if (s[j] == '\\') ch = (unsigned char)escape(j);
            else if ((unsigned char)s[j] >= 0x80) throw CompileError{p, "a character literal is a single byte (ASCII); use a string for other text"};
            else ch = (unsigned char)s[j++];
            if (j >= s.size() || s[j] != '\'') throw CompileError{p, "unterminated character literal"};
            push(T::Char, std::string(1, (char)ch), p, ch);
            i = j + 1;
            continue;
        }
        auto at = [&](size_t k) { return i + k < s.size() ? s[i + k] : '\0'; };
        struct Sym { T k; int len; };
        Sym sym{T::End, 0};
        if (c == '<' && at(1) == '<' && at(2) == '=') sym = {T::ShlEq, 3};
        else if (c == '>' && at(1) == '>' && at(2) == '=') sym = {T::ShrEq, 3};
        else if (c == '-' && at(1) == '>') sym = {T::Arrow, 2};
        else if (c == '.' && at(1) == '.') sym = {T::DotDot, 2};
        else if (c == '=' && at(1) == '=') sym = {T::Eq, 2};
        else if (c == '=' && at(1) == '>') sym = {T::FatArrow, 2};
        else if (c == '!' && at(1) == '=') sym = {T::Ne, 2};
        else if (c == '<' && at(1) == '=') sym = {T::Le, 2};
        else if (c == '>' && at(1) == '=') sym = {T::Ge, 2};
        else if (c == '<' && at(1) == '<') sym = {T::Shl, 2};
        else if (c == '>' && at(1) == '>') sym = {T::Shr, 2};
        else if (c == '&' && at(1) == '&') sym = {T::AndAnd, 2};
        else if (c == '|' && at(1) == '|') sym = {T::OrOr, 2};
        else if (c == '+' && at(1) == '=') sym = {T::PlusEq, 2};
        else if (c == '-' && at(1) == '=') sym = {T::MinusEq, 2};
        else if (c == '*' && at(1) == '=') sym = {T::StarEq, 2};
        else if (c == '/' && at(1) == '=') sym = {T::SlashEq, 2};
        else if (c == '%' && at(1) == '=') sym = {T::PercentEq, 2};
        else if (c == '&' && at(1) == '=') sym = {T::AmpEq, 2};
        else if (c == '|' && at(1) == '=') sym = {T::PipeEq, 2};
        else if (c == '^' && at(1) == '=') sym = {T::CaretEq, 2};
        else {
            switch (c) {
                case '(': sym = {T::LParen, 1}; break;
                case ')': sym = {T::RParen, 1}; break;
                case '{': sym = {T::LBrace, 1}; break;
                case '}': sym = {T::RBrace, 1}; break;
                case '[': sym = {T::LBrack, 1}; break;
                case ']': sym = {T::RBrack, 1}; break;
                case ',': sym = {T::Comma, 1}; break;
                case ';': sym = {T::Semi, 1}; break;
                case ':': sym = {T::Colon, 1}; break;
                case '.': sym = {T::Dot, 1}; break;
                case '=': sym = {T::Assign, 1}; break;
                case '<': sym = {T::Lt, 1}; break;
                case '>': sym = {T::Gt, 1}; break;
                case '+': sym = {T::Plus, 1}; break;
                case '-': sym = {T::Minus, 1}; break;
                case '*': sym = {T::Star, 1}; break;
                case '/': sym = {T::Slash, 1}; break;
                case '%': sym = {T::Percent, 1}; break;
                case '!': sym = {T::Bang, 1}; break;
                case '&': sym = {T::Amp, 1}; break;
                case '|': sym = {T::Pipe, 1}; break;
                case '^': sym = {T::Caret, 1}; break;
                case '~': sym = {T::Tilde, 1}; break;
                default: break;
            }
        }
        if (sym.len == 0) throw CompileError{p, std::string("unexpected character `") + c + "`"};
        push(sym.k, s.substr(i, sym.len), p);
        i += sym.len;
    }
    push(T::End, "", pos(i));
    return out;
}
