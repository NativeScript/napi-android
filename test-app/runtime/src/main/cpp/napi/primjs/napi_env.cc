/**
 * Copyright (c) 2017 Node.js API collaborators. All Rights Reserved.
 *
 * Use of this source code is governed by a MIT license that can be
 * found in the LICENSE file in the root of the source tree.
 */

// Copyright 2024 The Lynx Authors. All rights reserved.
// Licensed under the Apache License Version 2.0 that can be found in the
// LICENSE file in the root directory of this source tree.

#include "napi_env.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "napi_env_quickjs.h"
#include "primjs-api.h"

struct napi_env_data__ {
  void AddCleanupHook(void (*fun)(void* arg), void* arg) {
    cleanup_hooks.insert(CleanupHook{fun, arg, cleanup_hooks.size()});
  }

  void RemoveCleanupHook(void (*fun)(void* arg), void* arg) {
    cleanup_hooks.erase(CleanupHook{fun, arg, 0});
  }

  ~napi_env_data__() { RunCleanup(); }

  struct CleanupHook {
    void (*fun)(void*);
    void* arg;
    uint64_t insertion_order_counter;

    struct Equal {
      bool operator()(const CleanupHook& lhs, const CleanupHook& rhs) const {
        return lhs.fun == rhs.fun && lhs.arg == rhs.arg;
      }
    };

    struct Hash {
      std::size_t operator()(CleanupHook const& s) const {
        std::size_t h1 =
            std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(s.fun));
        std::size_t h2 =
            std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(s.arg));
        return h1 ^ (h2 << 1);
      }
    };
  };

  void RunCleanup() {
    while (!cleanup_hooks.empty()) {
      // Copy into a vector, since we can't sort an unordered_set in-place.
      std::vector<CleanupHook> callbacks(cleanup_hooks.begin(),
                                         cleanup_hooks.end());
      // We can't erase the copied elements from `cleanup_hooks_` yet, because
      // we need to be able to check whether they were un-scheduled by another
      // hook.

      std::sort(callbacks.begin(), callbacks.end(),
                [](const CleanupHook& a, const CleanupHook& b) {
                  // Sort in descending order so that the most recently inserted
                  // callbacks are run first.
                  return a.insertion_order_counter > b.insertion_order_counter;
                });

      for (const auto& cb : callbacks) {
        if (cleanup_hooks.count(cb) == 0) {
          // This hook was removed from the `cleanup_hooks_` set during another
          // hook that was run earlier. Nothing to do here.
          continue;
        }

        cb.fun(cb.arg);
        cleanup_hooks.erase(cb);
      }
    }
  }

  std::unordered_set<CleanupHook, CleanupHook::Hash, CleanupHook::Equal>
      cleanup_hooks;
};

napi_status napi_get_version(napi_env env, uint32_t* version) {
  *version = NAPI_VERSION_EXPERIMENTAL;
  return napi_clear_last_error(env);
}

napi_status napi_add_env_cleanup_hook(napi_env env, void (*fun)(void* arg),
                                      void* arg) {
  env->state.env_data->AddCleanupHook(fun, arg);
  return napi_clear_last_error(env);
}

napi_status napi_remove_env_cleanup_hook(napi_env env, void (*fun)(void* arg),
                                         void* arg) {
  env->state.env_data->RemoveCleanupHook(fun, arg);
  return napi_clear_last_error(env);
}

// Warning: Keep in-sync with napi_status enum
static const char* error_messages[] = {
    nullptr,
    "Invalid argument",
    "An object was expected",
    "A string was expected",
    "A string or symbol was expected",
    "A function was expected",
    "A number was expected",
    "A boolean was expected",
    "An array was expected",
    "Unknown failure",
    "An exception is pending",
    "The async work item was cancelled",
    "napi_escape_handle already called on scope",
    "Invalid handle scope usage",
    "Invalid callback scope usage",
    "Thread-safe function queue is full",
    "Thread-safe function handle is closing",
    "A bigint was expected",
    "A date was expected",
    "An arraybuffer was expected",
    "A detachable arraybuffer was expected",
    "Napi deadlock",
    "napi_no_external_buffers_allowed",
    "napi_cannot_run_js",
    "napi_handle_scope_empty",
    "napi_memory_error",
    "napi_promise_exception"};

#define NAPI_ARRAYSIZE(array) (sizeof(array) / sizeof(array[0]))

napi_status napi_get_last_error_info(napi_env env,
                                     const napi_extended_error_info** result) {
  // you must update this assert to reference the last message
  // in the napi_status enum each time a new error message is added.
  // We don't have a napi_status_last as this would result in an ABI
  // change each time a message was added.
  const int last_status = napi_promise_exception;

  static_assert(NAPI_ARRAYSIZE(error_messages) == last_status + 1,
                "Count of error messages must match count of error values");
  assert(env->state.last_error.error_code <= last_status);

  // Wait until someone requests the last error information to fetch the error
  // message string
  env->state.last_error.error_message =
      error_messages[env->state.last_error.error_code];

  *result = &(env->state.last_error);
  return napi_ok;
}

napi_env napi_new_env() {
  napi_env env = new napi_env__{};
  env->state.env_data = new napi_env_data__{};
  return env;
}

void napi_free_env(napi_env env) {
  delete env->state.env_data;
  delete env;
}