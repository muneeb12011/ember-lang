// diag.hpp - source files, positions and compiler diagnostics with a caret under the offending column.
#pragma once
#include <iostream>
#include <string>
#include <vector>

struct Pos {
    int line = 1, col = 1, file = 0;  // file indexes sources()
};

struct CompileError {
    CompileError() = default;
    CompileError(Pos p, std::string m) : pos(p), msg(std::move(m)) {}
    Pos pos;
    std::string msg;
    Pos notePos{0, 0, -1};  // optional second location ("declared here"), printed as a note
    std::string note;
};

struct SourceFile {
    std::string path, text;
};

inline std::vector<SourceFile>& sources() {
    static std::vector<SourceFile> v;
    return v;
}

inline int addSource(const std::string& path, const std::string& text) {
    sources().push_back({path, text});
    return (int)sources().size() - 1;
}

inline void printDiag(const Pos& p, const std::string& msg, const char* kind = "error") {
    const std::string& file = p.file < (int)sources().size() ? sources()[p.file].path : std::string("<unknown>");
    static const std::string empty;
    const std::string& src = p.file < (int)sources().size() ? sources()[p.file].text : empty;
    size_t start = 0;
    for (int ln = 1; ln < p.line && start < src.size(); ln++) {
        size_t nl = src.find('\n', start);
        if (nl == std::string::npos) {
            start = src.size();
            break;
        }
        start = nl + 1;
    }
    size_t end = src.find('\n', start);
    std::string text = src.substr(start, end == std::string::npos ? std::string::npos : end - start);
    for (char& c : text)
        if (c == '\t') c = ' ';
    std::string gutter = "  " + std::to_string(p.line) + " | ";
    std::cerr << file << ":" << p.line << ":" << p.col << ": " << kind << ": " << msg << "\n";
    std::cerr << gutter << text << "\n";
    std::cerr << std::string(gutter.size() + (p.col > 0 ? p.col - 1 : 0), ' ') << "^\n";
}

inline void printError(const CompileError& e) {
    printDiag(e.pos, e.msg);
    if (!e.note.empty() && e.notePos.file >= 0) printDiag(e.notePos, e.note, "note");
}
