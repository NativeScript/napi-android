#include "URL.h"
#include "URLSearchParams.h"
#include <string>

using namespace ada;
using namespace tns;

namespace {
    URL *GetInstance(napi_env env, napi_callback_info info) {
        NAPI_PREAMBLE
        napi_value jsThis;
        void *data;
        NAPI_GUARD(napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, &data)) {
            return nullptr;
        }

        URL *instance;
        NAPI_GUARD(napi_unwrap(env, jsThis, reinterpret_cast<void **>(&instance))) {
            return nullptr;
        }

        return instance;
    }

    template<typename Getter>
    napi_value GetUrlProperty(napi_env env, napi_callback_info info,
                              Getter getter) {
        NAPI_PREAMBLE
        URL *instance = GetInstance(env, info);
        if (!instance)
            return nullptr;

        auto value = (instance->GetURL()->*getter)();

        napi_value result;
        NAPI_GUARD(napi_create_string_utf8(env, value.data(), value.length(), &result)) {
            return nullptr;
        }

        return result;
    }

    template<typename Setter>
    napi_value SetUrlProperty(napi_env env, napi_callback_info info, Setter setter) {
        NAPI_CALLBACK_BEGIN(1)

        URL *instance = GetInstance(env, info);
        if (!instance)
            return nullptr;

        if (argc < 1)
            return nullptr;

        size_t str_size;
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
            return nullptr;
        }

        std::vector<char> buffer(str_size + 1);
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], buffer.data(), str_size + 1, nullptr)) {
            return nullptr;
        }

        (instance->GetURL()->*setter)(buffer.data());
        return napi_util::get_true(env);
    }
}

URL::URL(url_aggregator url) : url_(url) {}

url_aggregator *URL::GetURL() {
    return &url_;
}

napi_value URL::GetHash(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_hash);
}

napi_value URL::GetHost(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_host);
}

napi_value URL::GetHostName(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_hostname);
}

napi_value URL::GetHref(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_href);
}

napi_value URL::GetOrigin(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_origin);
}

napi_value URL::GetPassword(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_password);
}

napi_value URL::GetPathName(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_pathname);
}

napi_value URL::GetPort(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_port);
}

napi_value URL::GetProtocol(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_protocol);
}

napi_value URL::GetSearch(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_search);
}

napi_value URL::GetUserName(napi_env env, napi_callback_info info) {
    return GetUrlProperty(env, info, &url_aggregator::get_username);
}

napi_value URL::SetHash(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_hash);
}

napi_value URL::SetHost(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_host);
}

napi_value URL::SetHostName(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_hostname);
}

napi_value URL::SetHref(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_href);
}

napi_value URL::SetPassword(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_password);
}

napi_value URL::SetPathName(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_pathname);
}

napi_value URL::SetPort(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_port);
}

napi_value URL::SetProtocol(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_protocol);
}

napi_value URL::SetSearch(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_search);
}

napi_value URL::SetUserName(napi_env env, napi_callback_info info) {
    return SetUrlProperty(env, info, &url_aggregator::set_username);
}


// Add toString method
napi_value URL::ToString(napi_env env, napi_callback_info info) {
    NAPI_PREAMBLE
    URL *instance = GetInstance(env, info);
    if (!instance)
        return nullptr;

    auto value = instance->GetURL()->get_href();

    napi_value result;
    NAPI_GUARD(napi_create_string_utf8(env, value.data(), value.length(), &result)) {
        return nullptr;
    }

    return result;
}

// Constructor
napi_value URL::New(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    if (argc < 1) {
        napi_throw_type_error(env, nullptr, "URL constructor requires at least 1 argument");
        return nullptr;
    }

    size_t str_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
        return nullptr;
    }

    std::vector<char> url_buffer(str_size + 1);
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], url_buffer.data(), str_size + 1, nullptr)) {
        return nullptr;
    }

    url_aggregator url;
    std::string url_string(url_buffer.data());

    if (argc > 1) {
        // Handle base URL
        size_t base_str_size;
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], nullptr, 0, &base_str_size)) {
            return nullptr;
        }

        std::vector<char> base_buffer(base_str_size + 1);
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], base_buffer.data(), base_str_size + 1,
                                              nullptr)) {
            return nullptr;
        }

        std::string base_string(base_buffer.data());
        std::string_view base_string_view(base_string);

        if (!can_parse(url_string, &base_string_view)) {
            napi_throw_type_error(env, nullptr, "Invalid URL");
            return nullptr;
        }

        auto base_url = ada::parse<ada::url_aggregator>(base_string_view, nullptr);
        auto result = ada::parse<ada::url_aggregator>(url_string, &base_url.value());

        if (!result) {
            napi_throw_type_error(env, nullptr, "Invalid URL");
            return nullptr;
        }
        url = result.value();
    } else {
        auto result = ada::parse<ada::url_aggregator>(url_string, nullptr);
        if (!result) {
            napi_throw_type_error(env, nullptr, "Invalid URL");
            return nullptr;
        }
        url = result.value();
    }

    URL *urlImpl = new URL(url);
    napi_wrap(env, jsThis, urlImpl, URL::Destructor, urlImpl, nullptr);

    return jsThis;
}

// Static method
napi_value URL::CanParse(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    if (argc < 1) {
        napi_throw_type_error(env, nullptr, "canParse requires at least 1 argument");
        return nullptr;
    }

    size_t str_size;
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], nullptr, 0, &str_size)) {
        return nullptr;
    }

    std::vector<char> buffer(str_size + 1);
    NAPI_GUARD(napi_get_value_string_utf8(env, argv[0], buffer.data(), str_size + 1, nullptr)) {
        return nullptr;
    }

    bool result;
    if (argc > 1) {
        size_t base_str_size;
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], nullptr, 0, &base_str_size)) {
            return nullptr;
        }

        std::vector<char> base_buffer(base_str_size + 1);
        NAPI_GUARD(napi_get_value_string_utf8(env, argv[1], base_buffer.data(), base_str_size + 1,
                                              nullptr)) {
            return nullptr;
        }

        std::string_view base_string_view(base_buffer.data());
        result = can_parse(buffer.data(), &base_string_view);
    } else {
        result = can_parse(buffer.data(), nullptr);
    }

    napi_value returnValue;
    NAPI_GUARD(napi_get_boolean(env, result, &returnValue)) {
        return nullptr;
    }

    return returnValue;
}

void URL::Destructor(napi_env env, void *data, void *hint) {
#ifdef __HERMES__
    URL *url = static_cast<URL *>(hint);
#else
    URL *url = static_cast<URL *>(data);
#endif
    delete url;
}

void URL::Init(napi_env env) {
    NAPI_PREAMBLE
    napi_value ctor;
    static const int instance_prop_count = 12;
    napi_property_descriptor properties[instance_prop_count] = {
            {"hash",     nullptr, nullptr,  GetHash,     SetHash,     nullptr, napi_default, nullptr},
            {"host",     nullptr, nullptr,  GetHost,     SetHost,     nullptr, napi_default, nullptr},
            {"hostname", nullptr, nullptr,  GetHostName, SetHostName, nullptr, napi_default, nullptr},
            {"href",     nullptr, nullptr,  GetHref,     SetHref,     nullptr, napi_default, nullptr},
            {"origin",   nullptr, nullptr,  GetOrigin,   nullptr,     nullptr, napi_default, nullptr},
            {"password", nullptr, nullptr,  GetPassword, SetPassword, nullptr, napi_default, nullptr},
            {"pathname", nullptr, nullptr,  GetPathName, SetPathName, nullptr, napi_default, nullptr},
            {"port",     nullptr, nullptr,  GetPort,     SetPort,     nullptr, napi_default, nullptr},
            {"protocol", nullptr, nullptr,  GetProtocol, SetProtocol, nullptr, napi_default, nullptr},
            {"search",   nullptr, nullptr,  GetSearch,   SetSearch,   nullptr, napi_default, nullptr},
            {"username", nullptr, nullptr,  GetUserName, SetUserName, nullptr, napi_default, nullptr},
            {"toString", nullptr, ToString, nullptr,     nullptr,     nullptr, napi_default, nullptr}};

    NAPI_GUARD(napi_define_class(env, "URL", NAPI_AUTO_LENGTH, New,
                                 nullptr, instance_prop_count,
                                 properties, &ctor)) {
        return;
    }

    // Add static methods
    static const int static_prop_count = 1;
    napi_property_descriptor static_properties[static_prop_count] = {
            {"canParse", nullptr, CanParse, nullptr, nullptr, nullptr, napi_static, nullptr},
    };

    NAPI_GUARD(napi_define_properties(env, ctor,
                                      static_prop_count,
                                      static_properties)) {
        return;
    }

    // Add URL to global object
    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }

    NAPI_GUARD(napi_set_named_property(env, global, "URL", ctor)) {
        return;
    }
}
