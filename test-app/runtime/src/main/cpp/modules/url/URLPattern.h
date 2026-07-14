//
// napi port of URLPatternImpl (upstream nativescript/android-runtime #1830).
// Backed by ada's url_pattern with a Node-API RegExp provider so it works
// across every engine the runtime supports (V8, QuickJS, Hermes, JSC, PrimJS).
//

#ifndef TEST_APP_URLPATTERN_H
#define TEST_APP_URLPATTERN_H

#include "native_api_util.h"
#include "ada/ada.h"
#include <optional>
#include <string>
#include <vector>

using namespace ada;

namespace tns {

    // Move-only RAII wrapper around a napi_ref to a JS RegExp. Mirrors the
    // move-only semantics of v8::Global<v8::RegExp> used by the original impl,
    // and stores its own env so it can release the reference on destruction.
    class NapiRegex {
    public:
        napi_env env = nullptr;
        napi_ref ref = nullptr;

        NapiRegex() = default;
        NapiRegex(napi_env e, napi_ref r) : env(e), ref(r) {}

        NapiRegex(const NapiRegex &) = delete;
        NapiRegex &operator=(const NapiRegex &) = delete;

        NapiRegex(NapiRegex &&other) noexcept: env(other.env), ref(other.ref) {
            other.ref = nullptr;
            other.env = nullptr;
        }

        NapiRegex &operator=(NapiRegex &&other) noexcept {
            if (this != &other) {
                if (ref != nullptr && env != nullptr) {
                    napi_delete_reference(env, ref);
                }
                env = other.env;
                ref = other.ref;
                other.ref = nullptr;
                other.env = nullptr;
            }
            return *this;
        }

        ~NapiRegex() {
            if (ref != nullptr && env != nullptr) {
#ifdef __V8__
                node_api_post_finalizer(env, [](napi_env env, void *d, void*) {
                    napi_delete_reference(env, (napi_ref) d);
                }, ref, nullptr);
#else
                napi_delete_reference(env, ref);
#endif



            }
        }
    };

    class napi_regex_provider {
    public:
        napi_regex_provider() = default;

        using regex_type = NapiRegex;

        static std::optional<regex_type> create_instance(std::string_view pattern,
                                                         bool ignore_case);

        static std::optional<std::vector<std::optional<std::string>>> regex_search(
                std::string_view input, const regex_type &pattern);

        static bool regex_match(std::string_view input, const regex_type &pattern);
    };

    class URLPattern {
    public:
        static void Init(napi_env env);
        static void Destructor(napi_env env, void *nativeObject, void *finalize_hint);

        explicit URLPattern(url_pattern<napi_regex_provider> pattern);
        url_pattern<napi_regex_provider> *GetPattern();

        // Set at the entry of every callback that drives ada's regex provider
        // (New/Test/Exec), so create_instance can reach the current env.
        static thread_local napi_env s_current_env;

    private:
        static napi_value New(napi_env env, napi_callback_info info);

        static napi_value GetHash(napi_env env, napi_callback_info info);
        static napi_value GetHostName(napi_env env, napi_callback_info info);
        static napi_value GetPassword(napi_env env, napi_callback_info info);
        static napi_value GetPathName(napi_env env, napi_callback_info info);
        static napi_value GetPort(napi_env env, napi_callback_info info);
        static napi_value GetProtocol(napi_env env, napi_callback_info info);
        static napi_value GetSearch(napi_env env, napi_callback_info info);
        static napi_value GetUserName(napi_env env, napi_callback_info info);
        static napi_value GetHasRegexpGroups(napi_env env, napi_callback_info info);

        static napi_value Test(napi_env env, napi_callback_info info);
        static napi_value Exec(napi_env env, napi_callback_info info);

        url_pattern<napi_regex_provider> pattern_;
    };
}

#endif //TEST_APP_URLPATTERN_H
