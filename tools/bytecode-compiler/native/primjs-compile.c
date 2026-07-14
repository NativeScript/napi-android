/*
 * NativeScript PrimJS bytecode compiler shim.
 *
 * PrimJS is QuickJS-derived but renames the public API to the LEPUS_* prefix.
 * This is the LEPUS_* twin of qjs-compile.c: read a JS file, compile it with
 * LEPUS_EVAL_FLAG_COMPILE_ONLY, serialize via LEPUS_WriteObject(LEPUS_WRITE_OBJ_BYTECODE),
 * and write the NativeScript bytecode container:
 *
 *     [8 bytes magic "NSBCPJS\0"][4 bytes format version, little-endian][payload]
 *
 * The runtime (napi/primjs `js_run_bytecode_file`) checks the magic, skips the
 * 12-byte header, and hands the payload to LEPUS_ReadObject + LEPUS_EvalFunction.
 * The magic MUST match the `magic` in lib/primjs.js and the runtime.
 *
 * Build against the cloned PrimJS engine, e.g.:
 *   cc -O2 -I<primjs>/include -o nsbc-primjs primjs-compile.c <primjs libs> -lm -lpthread
 * (PrimJS's exact include paths / link targets come from its own build; see the
 *  workflow's build-primjs step.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "quickjs.h"

#ifndef NSBC_MAGIC
#define NSBC_MAGIC "NSBCPJS"
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

    LEPUSRuntime *rt = LEPUS_NewRuntime();
    LEPUSContext *ctx = rt ? LEPUS_NewContext(rt) : NULL;
    if (!ctx) { fprintf(stderr, "primjs init failed\n"); free(src); return 1; }

    LEPUSValue obj = LEPUS_Eval(ctx, (const char *)src, src_len, in_path,
                                LEPUS_EVAL_TYPE_GLOBAL | LEPUS_EVAL_FLAG_COMPILE_ONLY);
    free(src);
    if (LEPUS_IsException(obj)) {
        LEPUSValue exc = LEPUS_GetException(ctx);
        const char *msg = LEPUS_ToCString(ctx, exc);
        fprintf(stderr, "compile error in %s: %s\n", in_path, msg ? msg : "(unknown)");
        if (msg) LEPUS_FreeCString(ctx, msg);
        LEPUS_FreeValue(ctx, exc);
        return 1;
    }

    size_t bc_len = 0;
    uint8_t *bc = LEPUS_WriteObject(ctx, &bc_len, obj, LEPUS_WRITE_OBJ_BYTECODE);
    LEPUS_FreeValue(ctx, obj);
    if (!bc) { fprintf(stderr, "LEPUS_WriteObject failed for %s\n", in_path); return 1; }

    FILE *out = fopen(out_path, "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", out_path); lepus_free(ctx, bc); return 1; }

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
    lepus_free(ctx, bc);
    if (!ok) { fprintf(stderr, "write failed for %s\n", out_path); return 1; }
    return 0;
}
