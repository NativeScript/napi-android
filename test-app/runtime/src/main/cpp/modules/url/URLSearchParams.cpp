#include "URLSearchParams.h"
#include <string>

using namespace ada;
using namespace tns;

namespace {
    URLSearchParams *GetInstance(napi_env env, napi_callback_info info) {
        NAPI_PREAMBLE
        napi_value jsThis;
        void *data;
        NAPI_GUARD(napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, &data)) {
            return nullptr;
        }

        URLSearchParams *instance;
        NAPI_GUARD(napi_unwrap(env, jsThis, reinterpret_cast<void **>(&instance))) {
            return nullptr;
        }

        return instance;
    }
}


URLSearchParams::URLSearchParams(url_search_params params) : params_(params) {}

url_search_params *URLSearchParams::GetURLSearchParams() {
    return &params_;
}

napi_value URLSearchParams::New(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    url_search_params params;

    if (argc > 0) {
        size_t str_size;
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
            return nullptr;
        }

        std::vector<char> buffer(str_size + 1);
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], buffer.data(), str_size + 1, nullptr)) {
            return nullptr;
        }

        params = url_search_params(std::string_view(buffer.data(), str_size));
    }


    URLSearchParams *searchParams = new URLSearchParams(params);
    napi_wrap(env, jsThis, searchParams, URLSearchParams::Destructor, searchParams, nullptr);

    return jsThis;
}

void URLSearchParams::Destructor(napi_env env, void *data, void *hint) {
#ifdef __HERMES__
    URLSearchParams *searchParams = static_cast<URLSearchParams *>(hint);
#else
    URLSearchParams *searchParams = static_cast<URLSearchParams *>(data);
#endif
    delete searchParams;
}

// Instance methods
napi_value URLSearchParams::Append(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 2) return nullptr;

    size_t key_size, value_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &key_size)) {
        return nullptr;
    }
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], nullptr, 0, &value_size)) {
        return nullptr;
    }

    std::vector<char> key_buffer(key_size + 1);
    std::vector<char> value_buffer(value_size + 1);

    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], key_buffer.data(), key_size + 1, nullptr)) {
        return nullptr;
    }
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], value_buffer.data(), value_size + 1,
                                          nullptr)) {
        return nullptr;
    }

    instance->GetURLSearchParams()->append(key_buffer.data(), value_buffer.data());
    return nullptr;
}

napi_value URLSearchParams::Has(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) return nullptr;

    size_t str_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
        return nullptr;
    }

    std::vector<char> buffer(str_size + 1);
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], buffer.data(), str_size + 1, nullptr)) {
        return nullptr;
    }

    bool has = instance->GetURLSearchParams()->has(buffer.data());

    napi_value result;
    NAPI_GUARD(napi_get_boolean(env, has, &result)) {
        return nullptr;
    }

    return result;
}

napi_value URLSearchParams::Get(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) return nullptr;

    size_t str_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
        return nullptr;
    }

    std::vector<char> buffer(str_size + 1);
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], buffer.data(), str_size + 1, nullptr)) {
        return nullptr;
    }

    auto value = instance->GetURLSearchParams()->get(buffer.data());
    if (!value.has_value()) {
        napi_value undefined;
        NAPI_GUARD(napi_get_undefined(env, &undefined)) {
            return nullptr;
        }
        return undefined;
    }

    napi_value result;
    NAPI_GUARD(
            napi_create_string_utf8(env, value.value().data(), value.value().length(), &result)) {
        return nullptr;
    }

    return result;
}

napi_value URLSearchParams::Delete(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) return nullptr;

    size_t str_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
        return nullptr;
    }

    std::vector<char> buffer(str_size + 1);
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], buffer.data(), str_size + 1, nullptr)) {
        return nullptr;
    }

    instance->GetURLSearchParams()->remove(buffer.data());
    return nullptr;
}

napi_value URLSearchParams::GetAll(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) return nullptr;

    size_t str_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
        return nullptr;
    }

    std::vector<char> buffer(str_size + 1);
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], buffer.data(), str_size + 1, nullptr)) {
        return nullptr;
    }

    auto values = instance->GetURLSearchParams()->get_all(buffer.data());

    napi_value result;
    NAPI_GUARD(napi_create_array_with_length(env, values.size(), &result)) {
        return nullptr;
    }

    for (size_t i = 0; i < values.size(); i++) {
        napi_value item;
        NAPI_GUARD(napi_create_string_utf8(env, values[i].data(), values[i].length(), &item)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_element(env, result, i, item)) {
            return nullptr;
        }
    }

    return result;
}

napi_value URLSearchParams::Set(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 2) return nullptr;

    size_t key_size, value_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &key_size)) {
        return nullptr;
    }
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], nullptr, 0, &value_size)) {
        return nullptr;
    }

    std::vector<char> key_buffer(key_size + 1);
    std::vector<char> value_buffer(value_size + 1);

    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], key_buffer.data(), key_size + 1, nullptr)) {
        return nullptr;
    }
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], value_buffer.data(), value_size + 1,
                                          nullptr)) {
        return nullptr;
    }

    instance->GetURLSearchParams()->set(key_buffer.data(), value_buffer.data());
    return nullptr;
}

napi_value URLSearchParams::GetSize(napi_env env, napi_callback_info info) {
    NAPI_PREAMBLE
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    auto size = instance->GetURLSearchParams()->size();

    napi_value result;
    NAPI_GUARD(napi_create_int32(env, static_cast<int32_t>(size), &result)) {
        return nullptr;
    }

    return result;
}

napi_value URLSearchParams::Sort(napi_env env, napi_callback_info info) {
    NAPI_PREAMBLE
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    instance->GetURLSearchParams()->sort();
    return nullptr;
}

napi_value URLSearchParams::ToString(napi_env env, napi_callback_info info) {
    NAPI_PREAMBLE
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    auto value = instance->GetURLSearchParams()->to_string();

    napi_value result;
    NAPI_GUARD(napi_create_string_utf8(env, value.data(), value.length(), &result)) {
        return nullptr;
    }

    return result;
}

napi_value URLSearchParams::Keys(napi_env env, napi_callback_info info) {
    NAPI_PREAMBLE
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    auto keys = instance->GetURLSearchParams()->get_keys();
    std::vector<std::string_view> key_list;

    while (keys.has_next()) {
        if (auto key = keys.next()) {
            key_list.push_back(key.value());
        }
    }

    napi_value result;
    NAPI_GUARD(napi_create_array_with_length(env, key_list.size(), &result)) {
        return nullptr;
    }

    for (size_t i = 0; i < key_list.size(); i++) {
        napi_value item;
        NAPI_GUARD(napi_create_string_utf8(env, key_list[i].data(), key_list[i].length(), &item)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_element(env, result, i, item)) {
            return nullptr;
        }
    }

    return result;
}

napi_value URLSearchParams::Values(napi_env env, napi_callback_info info) {
    NAPI_PREAMBLE
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    auto keys = instance->GetURLSearchParams()->get_keys();
    std::vector<std::string_view> value_list;

    while (keys.has_next()) {
        if (auto key = keys.next()) {
            if (auto value = instance->GetURLSearchParams()->get(key.value())) {
                value_list.push_back(value.value());
            }
        }
    }

    napi_value result;
    NAPI_GUARD(napi_create_array_with_length(env, value_list.size(), &result)) {
        return nullptr;
    }

    for (size_t i = 0; i < value_list.size(); i++) {
        napi_value item;
        NAPI_GUARD(
                napi_create_string_utf8(env, value_list[i].data(), value_list[i].length(), &item)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_element(env, result, i, item)) {
            return nullptr;
        }
    }

    return result;
}

napi_value URLSearchParams::Entries(napi_env env, napi_callback_info info) {
    NAPI_PREAMBLE
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    auto keys = instance->GetURLSearchParams()->get_keys();
    std::vector<std::pair<std::string_view, std::string_view>> entries;

    while (keys.has_next()) {
        if (auto key = keys.next()) {
            if (auto value = instance->GetURLSearchParams()->get(key.value())) {
                entries.emplace_back(key.value(), value.value());
            }
        }
    }

    napi_value result;
    NAPI_GUARD(napi_create_array_with_length(env, entries.size(), &result)) {
        return nullptr;
    }

    for (size_t i = 0; i < entries.size(); i++) {
        napi_value entry;
        NAPI_GUARD(napi_create_array_with_length(env, 2, &entry)) {
            return nullptr;
        }

        napi_value key, value;
        NAPI_GUARD(napi_create_string_utf8(env, entries[i].first.data(), entries[i].first.length(),
                                           &key)) {
            return nullptr;
        }
        NAPI_GUARD(
                napi_create_string_utf8(env, entries[i].second.data(), entries[i].second.length(),
                                        &value)) {
            return nullptr;
        }

        NAPI_GUARD(napi_set_element(env, entry, 0, key)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_element(env, entry, 1, value)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_element(env, result, i, entry)) {
            return nullptr;
        }
    }

    return result;
}

napi_value URLSearchParams::ForEach(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) return nullptr;

    napi_value callback = argv[0];
    napi_value thisArg = argc >= 2 ? argv[1] : nullptr;

    auto keys = instance->GetURLSearchParams()->get_keys();
    while (keys.has_next()) {
        if (auto key = keys.next()) {
            if (auto value = instance->GetURLSearchParams()->get(key.value())) {
                napi_value args[3];
                NAPI_GUARD(
                        napi_create_string_utf8(env, value.value().data(), value.value().length(),
                                                &args[0])) {
                    return nullptr;
                }
                NAPI_GUARD(napi_create_string_utf8(env, key.value().data(), key.value().length(),
                                                   &args[1])) {
                    return nullptr;
                }
                args[2] = jsThis;

                napi_value global;
                NAPI_GUARD(napi_get_global(env, &global)) {
                    return nullptr;
                }

                napi_value result;
                NAPI_GUARD(napi_call_function(env, thisArg ? thisArg : global, callback, 3, args,
                                              &result)) {
                    return nullptr;
                }
            }
        }
    }

    return nullptr;
}


void URLSearchParams::Init(napi_env env) {
    NAPI_PREAMBLE
    napi_value ctor;
    static const int prop_count = 13;
    napi_property_descriptor properties[prop_count] = {
            {"append",   nullptr, Append,   nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"delete",   nullptr, Delete,   nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"entries",  nullptr, Entries,  nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"forEach",  nullptr, ForEach,  nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"get",      nullptr, Get,      nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"getAll",   nullptr, GetAll,   nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"has",      nullptr, Has,      nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"keys",     nullptr, Keys,     nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"set",      nullptr, Set,      nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"size",     nullptr, nullptr,  GetSize, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"sort",     nullptr, Sort,     nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"toString", nullptr, ToString, nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr},
            {"values",   nullptr, Values,   nullptr, nullptr, nullptr, napi_default_jsproperty, nullptr}
    };

    NAPI_GUARD(napi_define_class(env, "URLSearchParams", NAPI_AUTO_LENGTH, New,
                                 nullptr, prop_count,
                                 properties, &ctor)) {
        return;
    }

    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }

    NAPI_GUARD(napi_set_named_property(env, global, "URLSearchParams", ctor)) {
        return;
    }
}