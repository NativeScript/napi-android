//
// napi port of URLPatternImpl (upstream nativescript/android-runtime #1830).
//

#include "URLPattern.h"
#include <string>

using namespace std;
using namespace tns;
using namespace ada;

thread_local napi_env URLPattern::s_current_env = nullptr;

namespace {
    napi_value js_str(napi_env env, std::string_view s) {
        napi_value v;
        napi_create_string_utf8(env, s.data(), s.size(), &v);
        return v;
    }

    std::string to_std_string(napi_env env, napi_value v) {
        size_t len = 0;
        napi_get_value_string_utf8(env, v, nullptr, 0, &len);
        std::string s(len, '\0');
        napi_get_value_string_utf8(env, v, s.data(), len + 1, &len);
        return s;
    }

    std::optional<std::string> get_str_prop(napi_env env, napi_value obj, const char *key) {
        napi_value v;
        if (napi_get_named_property(env, obj, key, &v) != napi_ok) {
            return std::nullopt;
        }
        if (napi_util::is_of_type(env, v, napi_string)) {
            return to_std_string(env, v);
        }
        return std::nullopt;
    }

    void clear_pending(napi_env env) {
        bool pending = false;
        napi_is_exception_pending(env, &pending);
        if (pending) {
            napi_value e;
            napi_get_and_clear_last_exception(env, &e);
        }
    }

    URLPattern *GetInstance(napi_env env, napi_callback_info info) {
        size_t argc = 0;
        napi_value jsThis;
        void *data = nullptr;
        if (napi_get_cb_info(env, info, &argc, nullptr, &jsThis, &data) != napi_ok) {
            return nullptr;
        }
        URLPattern *instance = nullptr;
        if (napi_unwrap(env, jsThis, reinterpret_cast<void **>(&instance)) != napi_ok) {
            return nullptr;
        }
        return instance;
    }

    napi_value GetPatternProperty(napi_env env, napi_callback_info info,
                                  std::string_view (url_pattern<napi_regex_provider>::*getter)() const) {
        URLPattern *instance = GetInstance(env, info);
        if (instance == nullptr) {
            return js_str(env, "");
        }
        auto value = (instance->GetPattern()->*getter)();
        return js_str(env, value);
    }
}

// ---------------------------------------------------------------------------
// napi RegExp provider
// ---------------------------------------------------------------------------

std::optional<napi_regex_provider::regex_type>
napi_regex_provider::create_instance(std::string_view pattern, bool ignore_case) {
    napi_env env = URLPattern::s_current_env;
    if (env == nullptr) {
        return std::nullopt;
    }

    napi_value global;
    napi_get_global(env, &global);

    napi_value regexpCtor;
    if (napi_get_named_property(env, global, "RegExp", &regexpCtor) != napi_ok) {
        return std::nullopt;
    }

    napi_value patternStr = js_str(env, pattern);

    // v8 impl always sets the unicode flag and optionally ignore-case.
    std::string flags = "u";
    if (ignore_case) {
        flags += "i";
    }
    napi_value flagsStr = js_str(env, flags);

    napi_value argv[2] = {patternStr, flagsStr};
    napi_value regex;
    if (napi_new_instance(env, regexpCtor, 2, argv, &regex) != napi_ok) {
        clear_pending(env);
        return std::nullopt;
    }

    napi_ref ref = napi_util::make_ref(env, regex, 1);
    return NapiRegex(env, ref);
}

std::optional<std::vector<std::optional<std::string>>>
napi_regex_provider::regex_search(std::string_view input, const regex_type &pattern) {
    napi_env env = pattern.env;
    if (env == nullptr || pattern.ref == nullptr) {
        return std::nullopt;
    }

    napi_value regex = napi_util::get_ref_value(env, pattern.ref);
    if (regex == nullptr) {
        return std::nullopt;
    }

    napi_value execFn;
    if (napi_get_named_property(env, regex, "exec", &execFn) != napi_ok) {
        return std::nullopt;
    }

    napi_value inputStr = js_str(env, input);
    napi_value argv[1] = {inputStr};
    napi_value result;
    if (napi_call_function(env, regex, execFn, 1, argv, &result) != napi_ok) {
        clear_pending(env);
        return std::nullopt;
    }

    if (napi_util::is_null_or_undefined(env, result)) {
        return std::nullopt;
    }

    std::vector<std::optional<std::string>> ret;
    bool isArray = false;
    napi_is_array(env, result, &isArray);
    if (isArray) {
        uint32_t len = 0;
        napi_get_array_length(env, result, &len);
        ret.reserve(len);
        for (uint32_t i = 0; i < len; i++) {
            napi_value item;
            if (napi_get_element(env, result, i, &item) != napi_ok) {
                return std::nullopt;
            }
            if (napi_util::is_undefined(env, item)) {
                ret.emplace_back(std::nullopt);
            } else if (napi_util::is_of_type(env, item, napi_string)) {
                ret.emplace_back(to_std_string(env, item));
            }
        }
    }

    return ret;
}

bool napi_regex_provider::regex_match(std::string_view input, const regex_type &pattern) {
    napi_env env = pattern.env;
    if (env == nullptr || pattern.ref == nullptr) {
        return false;
    }

    napi_value regex = napi_util::get_ref_value(env, pattern.ref);
    if (regex == nullptr) {
        return false;
    }

    napi_value execFn;
    if (napi_get_named_property(env, regex, "exec", &execFn) != napi_ok) {
        return false;
    }

    napi_value inputStr = js_str(env, input);
    napi_value argv[1] = {inputStr};
    napi_value result;
    if (napi_call_function(env, regex, execFn, 1, argv, &result) != napi_ok) {
        clear_pending(env);
        return false;
    }

    return !napi_util::is_null_or_undefined(env, result);
}

// ---------------------------------------------------------------------------
// URLPattern
// ---------------------------------------------------------------------------

URLPattern::URLPattern(url_pattern<napi_regex_provider> pattern)
        : pattern_(std::move(pattern)) {}

url_pattern<napi_regex_provider> *URLPattern::GetPattern() {
    return &this->pattern_;
}

void URLPattern::Destructor(napi_env env, void *data, void *hint) {
#ifdef __HERMES__
    URLPattern *pattern = static_cast<URLPattern *>(hint);
#else
    URLPattern *pattern = static_cast<URLPattern *>(data);
#endif
    delete pattern;
}

static std::optional<ada::url_pattern_init> ParseInput(napi_env env, napi_value input) {
    if (!napi_util::is_object(env, input)) {
        return {};
    }

    auto init = ada::url_pattern_init{};
    if (auto v = get_str_prop(env, input, "protocol")) init.protocol = *v;
    if (auto v = get_str_prop(env, input, "username")) init.username = *v;
    if (auto v = get_str_prop(env, input, "password")) init.password = *v;
    if (auto v = get_str_prop(env, input, "hostname")) init.hostname = *v;
    if (auto v = get_str_prop(env, input, "port")) init.port = *v;
    if (auto v = get_str_prop(env, input, "pathname")) init.pathname = *v;
    if (auto v = get_str_prop(env, input, "search")) init.search = *v;
    if (auto v = get_str_prop(env, input, "hash")) init.hash = *v;
    if (auto v = get_str_prop(env, input, "baseURL")) init.base_url = *v;
    return init;
}

static void SetComponent(napi_env env, napi_value object, const char *componentKey,
                         const ada::url_pattern_component_result &component) {
    napi_value ret;
    napi_create_object(env, &ret);
    napi_set_named_property(env, ret, "input", js_str(env, component.input));

    napi_value groupValue;
    napi_create_object(env, &groupValue);
    for (const auto &[key, value]: component.groups) {
        if (value) {
            napi_set_named_property(env, groupValue, key.c_str(), js_str(env, value.value()));
        } else {
            napi_set_named_property(env, groupValue, key.c_str(), napi_util::undefined(env));
        }
    }
    napi_set_named_property(env, ret, "groups", groupValue);

    napi_set_named_property(env, object, componentKey, ret);
}

static void BuildJS(napi_env env, napi_value object, const ada::url_pattern_result &result) {
    auto len = result.inputs.size();
    napi_value inputs;
    napi_create_array_with_length(env, len, &inputs);
    for (uint32_t i = 0; i < len; i++) {
        const auto &item = result.inputs[i];
        if (std::holds_alternative<std::string_view>(item)) {
            auto view = std::get<std::string_view>(item);
            napi_set_element(env, inputs, i, js_str(env, view));
        }
    }
    napi_set_named_property(env, object, "inputs", inputs);

    SetComponent(env, object, "protocol", result.protocol);
    SetComponent(env, object, "hash", result.hash);
    SetComponent(env, object, "hostname", result.hostname);
    SetComponent(env, object, "username", result.username);
    SetComponent(env, object, "password", result.password);
    SetComponent(env, object, "pathname", result.pathname);
    SetComponent(env, object, "port", result.port);
    SetComponent(env, object, "search", result.search);
}

napi_value URLPattern::New(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(3)
    s_current_env = env;

    // No-args: construct an empty pattern.
    if (argc == 0 || napi_util::is_undefined(env, argv[0])) {
        auto init = ada::url_pattern_init{};
        auto pattern = ada::parse_url_pattern<napi_regex_provider>(std::move(init));
        if (!pattern) {
            napi_throw_type_error(env, nullptr, "Failed to construct URLPattern");
            return nullptr;
        }
        auto *impl = new URLPattern(std::move(*pattern));
        napi_wrap(env, jsThis, impl, URLPattern::Destructor, impl, nullptr);
        return jsThis;
    }

    std::optional<ada::url_pattern_init> init{};
    std::optional<std::string> input{};
    std::optional<std::string> base_url{};
    std::optional<ada::url_pattern_options> options{};

    if (napi_util::is_of_type(env, argv[0], napi_string)) {
        input = to_std_string(env, argv[0]);
    } else if (napi_util::is_object(env, argv[0])) {
        auto parsed = ParseInput(env, argv[0]);
        if (parsed) {
            init = std::move(*parsed);
        }
    } else {
        napi_throw_type_error(env, nullptr, "Input must be an object or a string");
        return nullptr;
    }

    // 2nd arg may be a base URL string or an options object.
    napi_value baseOrOptions = argv[1];
    if (napi_util::is_of_type(env, baseOrOptions, napi_string)) {
        base_url = to_std_string(env, baseOrOptions);
    } else if (napi_util::is_object(env, baseOrOptions)) {
        napi_value ignoreCase;
        if (napi_get_named_property(env, baseOrOptions, "ignoreCase", &ignoreCase) == napi_ok &&
            napi_util::is_of_type(env, ignoreCase, napi_boolean)) {
            options = ada::url_pattern_options{.ignore_case = napi_util::get_bool(env, ignoreCase)};
        }
    }

    // 3rd arg is the options object when a base URL was given as 2nd arg.
    napi_value opts = argv[2];
    if (napi_util::is_object(env, opts)) {
        napi_value ignoreCase;
        if (napi_get_named_property(env, opts, "ignoreCase", &ignoreCase) == napi_ok &&
            napi_util::is_of_type(env, ignoreCase, napi_boolean)) {
            options = ada::url_pattern_options{.ignore_case = napi_util::get_bool(env, ignoreCase)};
        }
    }

    std::string_view base_url_view{};
    if (base_url) {
        base_url_view = {base_url->data(), base_url->size()};
    }

    ada::url_pattern_input arg0;
    if (init.has_value()) {
        arg0 = *init;
    } else {
        arg0 = std::string_view(*input);
    }

    auto pattern = ada::parse_url_pattern<napi_regex_provider>(
            std::move(arg0),
            base_url.has_value() ? &base_url_view : nullptr,
            options.has_value() ? &options.value() : nullptr);

    if (!pattern) {
        napi_throw_type_error(env, nullptr, "Failed to construct URLPattern");
        return nullptr;
    }

    auto *impl = new URLPattern(std::move(*pattern));
    napi_wrap(env, jsThis, impl, URLPattern::Destructor, impl, nullptr);
    return jsThis;
}

napi_value URLPattern::GetHash(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_hash);
}

napi_value URLPattern::GetHostName(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_hostname);
}

napi_value URLPattern::GetPassword(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_password);
}

napi_value URLPattern::GetPathName(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_pathname);
}

napi_value URLPattern::GetPort(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_port);
}

napi_value URLPattern::GetProtocol(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_protocol);
}

napi_value URLPattern::GetSearch(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_search);
}

napi_value URLPattern::GetUserName(napi_env env, napi_callback_info info) {
    return GetPatternProperty(env, info, &url_pattern<napi_regex_provider>::get_username);
}

napi_value URLPattern::GetHasRegexpGroups(napi_env env, napi_callback_info info) {
    URLPattern *instance = GetInstance(env, info);
    napi_value result;
    bool value = instance != nullptr && instance->GetPattern()->has_regexp_groups();
    napi_get_boolean(env, value, &result);
    return result;
}

napi_value URLPattern::Test(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)
    s_current_env = env;

    URLPattern *instance = GetInstance(env, info);
    if (instance == nullptr) {
        napi_value result;
        napi_get_boolean(env, false, &result);
        return result;
    }

    ada::url_pattern_input input;
    std::string input_base;

    if (argc == 0 || napi_util::is_undefined(env, argv[0])) {
        input = ada::url_pattern_init{};
    } else if (napi_util::is_of_type(env, argv[0], napi_string)) {
        input_base = to_std_string(env, argv[0]);
        input = std::string_view(input_base);
    } else if (napi_util::is_object(env, argv[0])) {
        auto parsed = ParseInput(env, argv[0]);
        if (parsed) {
            input = std::move(*parsed);
        }
    } else {
        napi_throw_type_error(env, nullptr, "URLPattern input needs to be a string or an object");
        return nullptr;
    }

    std::optional<std::string> baseURL{};
    if (argc > 1 && !napi_util::is_undefined(env, argv[1])) {
        if (!napi_util::is_of_type(env, argv[1], napi_string)) {
            napi_throw_type_error(env, nullptr, "baseURL must be a string");
            return nullptr;
        }
        baseURL = to_std_string(env, argv[1]);
    }

    std::optional<std::string_view> baseURL_opt =
            baseURL ? std::optional<std::string_view>(*baseURL) : std::nullopt;

    napi_value result;
    if (auto res = instance->GetPattern()->test(input, baseURL_opt ? &*baseURL_opt : nullptr)) {
        napi_get_boolean(env, res.value(), &result);
    } else {
        napi_get_null(env, &result);
    }
    return result;
}

napi_value URLPattern::Exec(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)
    s_current_env = env;

    URLPattern *instance = GetInstance(env, info);
    if (instance == nullptr) {
        return napi_util::undefined(env);
    }

    ada::url_pattern_input input;
    std::string input_base;

    if (argc == 0 || napi_util::is_undefined(env, argv[0])) {
        input = ada::url_pattern_init{};
    } else if (napi_util::is_of_type(env, argv[0], napi_string)) {
        input_base = to_std_string(env, argv[0]);
        input = std::string_view(input_base);
    } else if (napi_util::is_object(env, argv[0])) {
        auto parsed = ParseInput(env, argv[0]);
        if (parsed) {
            input = std::move(*parsed);
        }
    } else {
        napi_throw_type_error(env, nullptr, "URLPattern input needs to be a string or an object");
        return nullptr;
    }

    std::optional<std::string> baseURL{};
    if (argc > 1 && !napi_util::is_undefined(env, argv[1])) {
        if (!napi_util::is_of_type(env, argv[1], napi_string)) {
            napi_throw_type_error(env, nullptr, "baseURL must be a string");
            return nullptr;
        }
        baseURL = to_std_string(env, argv[1]);
    }

    std::optional<std::string_view> baseURL_opt =
            baseURL ? std::optional<std::string_view>(*baseURL) : std::nullopt;

    napi_value result;
    napi_get_null(env, &result);

    if (auto res = instance->GetPattern()->exec(input, baseURL_opt ? &*baseURL_opt : nullptr)) {
        auto value = res.value();
        if (value.has_value()) {
            napi_create_object(env, &result);
            BuildJS(env, result, value.value());
        }
    }
    return result;
}

void URLPattern::Init(napi_env env) {
    NAPI_PREAMBLE
    napi_value ctor;
    static const int instance_prop_count = 11;
    napi_property_descriptor properties[instance_prop_count] = {
            {"test",            nullptr, Test, nullptr,            nullptr, nullptr, napi_default, nullptr},
            {"exec",            nullptr, Exec, nullptr,            nullptr, nullptr, napi_default, nullptr},
            {"hasRegExpGroups", nullptr, nullptr, GetHasRegexpGroups, nullptr, nullptr, napi_default, nullptr},
            {"hash",            nullptr, nullptr, GetHash,         nullptr, nullptr, napi_default, nullptr},
            {"hostname",        nullptr, nullptr, GetHostName,     nullptr, nullptr, napi_default, nullptr},
            {"password",        nullptr, nullptr, GetPassword,     nullptr, nullptr, napi_default, nullptr},
            {"pathname",        nullptr, nullptr, GetPathName,     nullptr, nullptr, napi_default, nullptr},
            {"port",            nullptr, nullptr, GetPort,         nullptr, nullptr, napi_default, nullptr},
            {"protocol",        nullptr, nullptr, GetProtocol,     nullptr, nullptr, napi_default, nullptr},
            {"search",          nullptr, nullptr, GetSearch,       nullptr, nullptr, napi_default, nullptr},
            {"username",        nullptr, nullptr, GetUserName,     nullptr, nullptr, napi_default, nullptr}};

    NAPI_GUARD(napi_define_class(env, "URLPattern", NAPI_AUTO_LENGTH, New,
                                 nullptr, instance_prop_count,
                                 properties, &ctor)) {
        return;
    }

    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }

    NAPI_GUARD(napi_set_named_property(env, global, "URLPattern", ctor)) {
        return;
    }
}
