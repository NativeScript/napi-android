/*
 * Copyright (C) 2026 Software Mansion. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef JSBytecodeCache_h
#define JSBytecodeCache_h

#include <JavaScriptCore/JSBase.h>
#include <JavaScriptCore/JSValueRef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*!
 @function
 @abstract Serialize a top-level program to a JSC bytecode cache on disk.
 @discussion Parses and byte-compiles @p source (a classic, non-module program
  identified by @p sourceURL) and writes the resulting bytecode cache blob to
  @p outputFilePath, truncating any existing contents. The caller owns file
  publication semantics: write to a temporary path and rename it into place, and
  track staleness (e.g. via the source file's modification time) however it sees
  fit. The blob is only meaningful to JSEvaluateProgramWithBytecodeCacheFile and
  is validated there before use.
 @param ctx The execution context (its VM is used for compilation).
 @param source The program source. Must be identical to the source later passed
  to JSEvaluateProgramWithBytecodeCacheFile for the cache to be accepted.
 @param sourceURL The source URL reported in errors; may be NULL.
 @param outputFilePath A NUL-terminated UTF-8 filesystem path to write to.
 @param errorMessage On failure, if non-NULL, receives a retained JSStringRef
  describing the failure (release it with JSStringRelease).
 @result true if the cache was written, false otherwise.
 */
JS_EXPORT bool JSWriteBytecodeCacheForProgram(JSContextRef ctx, JSStringRef source, JSStringRef sourceURL, const char* outputFilePath, JSStringRef* errorMessage);

/*!
 @function
 @abstract Evaluate a top-level program, reusing a previously written bytecode
  cache when it is still valid.
 @discussion Memory-maps the cache blob at @p cacheFilePath and, if it is valid
  for this exact @p source, evaluates the program directly from the cached
  bytecode, skipping parsing and byte-compilation. If the file is missing,
  stale, or incompatible (e.g. produced by a different JSC build), nothing is
  evaluated, @p cacheRejected is set to true and NULL is returned; the caller
  should then run the source normally and refresh the cache. A genuine runtime
  error during evaluation is reported via @p exception (and NULL is returned)
  while leaving @p cacheRejected false.
 @param ctx The execution context to evaluate in.
 @param source The program source (must match the source the cache was made from).
 @param sourceURL The source URL reported in errors and stack traces; may be NULL.
 @param cacheFilePath A NUL-terminated UTF-8 path to a blob previously produced
  by JSWriteBytecodeCacheForProgram.
 @param thisObject The object to use as "this", or NULL for undefined.
 @param cacheRejected If non-NULL, set to true when the cache could not be used
  (miss/stale/incompatible) and false otherwise.
 @param exception If non-NULL, receives an exception thrown while running.
 @result The program's completion value, or NULL on a cache miss or an exception.
 */
JS_EXPORT JSValueRef JSEvaluateProgramWithBytecodeCacheFile(JSContextRef ctx, JSStringRef source, JSStringRef sourceURL, const char* cacheFilePath, JSValueRef thisObject, bool* cacheRejected, JSValueRef* exception);

#ifdef __cplusplus
}
#endif

#endif /* JSBytecodeCache_h */
