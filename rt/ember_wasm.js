// ember_wasm.js - the loader for programs built with `embc --target wasm`. The compiler writes an executable JS file
// that starts with `const EMBER_RT = "<base64>", EMBER_PROG = "<base64>";` (the runtime module and the program module)
// followed by this file. Run it with node: `node prog.js args...`.
//
// One linear memory is shared by both modules: the program's string literals live at 1024, the runtime's data at
// 16 MB, the heap after that. This file provides the host functions the runtime imports (output, input, files,
// arguments, environment, clock, float formatting, math).
"use strict";
const fs = require("fs");
const { Worker, isMainThread, parentPort } = require("worker_threads");

// The program runs on a worker thread with a large stack (native Ember programs get 1 GB; node's default is ~1 MB)
if (isMainThread) {
    const w = new Worker(__filename, { argv: process.argv.slice(2), resourceLimits: { stackSizeMb: 1000 } });
    w.on("message", (code) => { process.exitCode = code; });
    w.on("error", (e) => {
        fs.writeSync(2, /call stack/i.test(String(e && e.message)) ? "runtime error: stack overflow\n" : "runtime error: " + e + "\n");
        process.exitCode = 1;
    });
} else {

const memory = new WebAssembly.Memory({ initial: 512, maximum: 65536 });
const bytes = (p, n) => Buffer.from(memory.buffer, p, n);
const text = (p, n) => bytes(p, n).toString("latin1");
const put = (p, s) => {  // copy a latin1 string into memory at p
    const b = Buffer.from(s, "latin1");
    b.copy(bytes(p, b.length));
    return b.length;
};
class EmberExit {
    constructor(code) { this.code = code; }
}

// "%.6f" exactly as glibc's printf: the exact binary value, rounded half to even at the sixth decimal
const f64 = new DataView(new ArrayBuffer(8));
function fixed6(bits) {
    f64.setBigInt64(0, BigInt.asIntN(64, bits), true);
    const x = f64.getFloat64(0, true);
    const neg = (BigInt.asUintN(64, bits) >> 63n) === 1n;
    if (Number.isNaN(x)) return neg ? "-nan" : "nan";
    if (!Number.isFinite(x)) return neg ? "-inf" : "inf";
    const u = BigInt.asUintN(64, bits);
    const e = Number((u >> 52n) & 0x7ffn);
    let m = u & ((1n << 52n) - 1n);
    let exp;
    if (e === 0) exp = -1074; else { m |= 1n << 52n; exp = e - 1075; }
    let n;
    if (exp >= 0) {
        n = (m << BigInt(exp)) * 1000000n;
    } else {
        const num = m * 1000000n, den = 1n << BigInt(-exp);
        n = num / den;
        const r2 = (num % den) * 2n;
        if (r2 > den || (r2 === den && (n & 1n) === 1n)) n += 1n;
    }
    let s = n.toString();
    if (s.length < 7) s = "0".repeat(7 - s.length) + s;
    return (neg ? "-" : "") + s.slice(0, -6) + "." + s.slice(-6);
}

const ERRNO = { ENOENT: [2, "No such file or directory"], EACCES: [13, "Permission denied"], EISDIR: [21, "Is a directory"],
                ENOTDIR: [20, "Not a directory"], EEXIST: [17, "File exists"], EROFS: [30, "Read-only file system"],
                ENOSPC: [28, "No space left on device"], EINVAL: [22, "Invalid argument"] };
const errText = {};
for (const k in ERRNO) errText[ERRNO[k][0]] = ERRNO[k][1];
const errCode = (e) => (ERRNO[e.code] ? ERRNO[e.code][0] : 5);

let stdin = null, stdinPos = 0, lastFile = null;
const args = process.argv.slice(2);
const host = {
    write(fd, p, n) {
        const b = bytes(p, n);
        let off = 0;
        while (off < n) off += fs.writeSync(fd === 2 ? 2 : 1, b, off, n - off);
    },
    read(p, max) {
        if (stdin === null) {
            try { stdin = fs.readFileSync(0); } catch (e) { stdin = Buffer.alloc(0); }
        }
        const n = Math.min(max, stdin.length - stdinPos);
        if (n <= 0) return 0;
        stdin.copy(bytes(p, n), 0, stdinPos, stdinPos + n);
        stdinPos += n;
        return n;
    },
    exit(code) { throw new EmberExit(code); },
    fmt_float(bits, p) { return put(p, fixed6(bits)); },
    fmath(fn, a, b) {
        switch (fn) {
            case 0: return Math.sin(a);
            case 1: return Math.cos(a);
            case 2: return Math.tan(a);
            case 3: return Math.atan(a);
            case 4: return Math.atan2(a, b);
            case 5: return Math.exp(a);
            case 6: return Math.log(a);
            case 7: return Math.pow(a, b);
            case 8: return Math.floor(a);
            default: return Math.ceil(a);
        }
    },
    arg_count() { return args.length; },
    arg_len(i) { return Buffer.byteLength(args[i], "utf8"); },
    arg_copy(i, p) { Buffer.from(args[i], "utf8").copy(bytes(p, Buffer.byteLength(args[i], "utf8"))); },
    env_len(p, n) {
        const v = process.env[text(p, n)];
        return v === undefined ? -1 : Buffer.byteLength(v, "utf8");
    },
    env_copy(p, n, out) {
        const v = Buffer.from(process.env[text(p, n)], "utf8");
        v.copy(bytes(out, v.length));
    },
    file_read(p, n) {
        try { lastFile = fs.readFileSync(text(p, n)); } catch (e) { return -errCode(e); }
        return lastFile.length;
    },
    file_take(out) { lastFile.copy(bytes(out, lastFile.length)); lastFile = null; },
    file_write(p, n, d, dn, append) {
        try {
            if (append) fs.appendFileSync(text(p, n), Buffer.from(bytes(d, dn)));
            else fs.writeFileSync(text(p, n), Buffer.from(bytes(d, dn)));
        } catch (e) { return -errCode(e); }
        return 0;
    },
    file_exists(p, n) { return fs.existsSync(text(p, n)) ? 1 : 0; },
    strerror(code, out) { return put(out, errText[code] || "Input/output error"); },
    clock_ns() { return process.hrtime.bigint(); },
};

const rt = new WebAssembly.Instance(new WebAssembly.Module(Buffer.from(EMBER_RT, "base64")), { env: { memory }, host });
const prog = new WebAssembly.Instance(new WebAssembly.Module(Buffer.from(EMBER_PROG, "base64")), { env: { memory }, rt: rt.exports });
let code = 0;
try {
    code = Number(BigInt.asUintN(8, prog.exports.main()));
    rt.exports.ember_flush();
} catch (e) {
    if (e instanceof EmberExit) {
        code = e.code;
    } else {
        try { rt.exports.ember_flush(); } catch (_) {}
        if (e instanceof RangeError && /call stack/i.test(e.message)) fs.writeSync(2, "runtime error: stack overflow\n");
        else fs.writeSync(2, "runtime error: " + (e && e.message ? e.message : String(e)) + "\n");
        code = 1;
    }
}
parentPort.postMessage(code);
}
