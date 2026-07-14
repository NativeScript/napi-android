/*
 * NativeScript QuickJS / QuickJS-NG bytecode compiler shim.
 *
 * The stock `qjsc` emits a C source array, not a loadable blob, so it can't feed
 * our runtime. This tiny tool instead reads a JS file, compiles it with
 * JS_EVAL_FLAG_COMPILE_ONLY, serializes it via JS_WriteObject(JS_WRITE_OBJ_BYTECODE),
 * and writes a small NativeScript bytecode *container*:
 *
 *     [8 bytes magic][4 bytes format version, little-endian][JS_WriteObject payload]
 *
 * The runtime (napi/quickjs `js_run_bytecode_file`) checks the magic, skips the
 * 12-byte header, and hands the payload to JS_ReadObject + JS_EvalFunction. The
 * container gives robust detection (QuickJS's own serialized output has no strong
 * leading magic) and lets us version the format independently of the engine.
 *
 * One source serves both engines; the magic is chosen at build time so a QuickJS
 * blob is never mistaken for a QuickJS-NG blob (their bytecode is incompatible):
 *   -DNSBC_MAGIC='"NSBCQJS"'   QuickJS (bellard)
 *   -DNSBC_MAGIC='"NSBCNGS"'   QuickJS-NG
 * The 7-char string plus a trailing NUL byte is the 8-byte magic; it MUST match
 * the `magic` in the corresponding lib/<engine>.js adapter and the runtime.
 *
 * Build (linked against the cloned upstream engine), e.g.:
 *   cc -O2 -DNSBC_MAGIC='"NSBCQJS"' -I<quickjs> -o nsbc-quickjs \
 *      qjs-compile.c <quickjs>/libquickjs.a -lm -lpthread
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "quickjs.h"

#ifndef NSBC_MAGIC
#define NSBC_MAGIC "NSBCQJS"
#endif
#define NSBC_FORMAT_VERSION 1u

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (rd != (size_t)n) { free(buf); return NULL; }
    buf[n] = '\0';
    *out_len = (size_t)n;
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.js> <output.bc>\n", argv[0]);
        return 2;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[2];

    size_t src_len = 0;
    uint8_t *src = read_file(in_path, &src_len);
    if (!src) { fprintf(stderr, "cannot read %s\n", in_path); return 1; }

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = rt ? JS_NewContext(rt) : NULL;
    if (!ctx) { fprintf(stderr, "quickjs init failed\n"); free(src); return 1; }

    /* Compile only — do not run. The completion value of the compiled top-level
     * is the module wrapper function, exactly as when running the source. */
    JSValue obj = JS_Eval(ctx, (const char *)src, src_len, in_path,
                          JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    free(src);
    if (JS_IsException(obj)) {
        JSValue exc = JS_GetException(ctx);
        const char *msg = JS_ToCString(ctx, exc);
        fprintf(stderr, "compile error in %s: %s\n", in_path, msg ? msg : "(unknown)");
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, exc);
        return 1;
    }

    size_t bc_len = 0;
    uint8_t *bc = JS_WriteObject(ctx, &bc_len, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);
    if (!bc) { fprintf(stderr, "JS_WriteObject failed for %s\n", in_path); return 1; }

    FILE *out = fopen(out_path, "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", out_path); js_free(ctx, bc); return 1; }

    unsigned char header[12];
    memcpy(header, NSBC_MAGIC, 7);
    header[7] = 0;
    uint32_t ver = NSBC_FORMAT_VERSION;
    header[8]  = (unsigned char)(ver & 0xff);
    header[9]  = (unsigned char)((ver >> 8) & 0xff);
    header[10] = (unsigned char)((ver >> 16) & 0xff);
    header[11] = (unsigned char)((ver >> 24) & 0xff);

    int ok = (fwrite(header, 1, sizeof(header), out) == sizeof(header)) &&
             (bc_len == 0 || fwrite(bc, 1, bc_len, out) == bc_len);
    fclose(out);
    js_free(ctx, bc);
    /* Process is about to exit; skip JS_FreeContext/JS_FreeRuntime to keep the
     * shim simple and robust across engine versions. */
    if (!ok) { fprintf(stderr, "write failed for %s\n", out_path); return 1; }
    return 0;
}
