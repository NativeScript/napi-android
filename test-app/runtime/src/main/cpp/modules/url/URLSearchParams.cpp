#include "URLSearchParams.h"
#include <string>
#include <vector>

using namespace ada;
using namespace tns;

namespace {
    enum IterKind { ITER_KEYS = 0, ITER_VALUES = 1, ITER_ENTRIES = 2 };

    // Per-iterator state: keeps the source URLSearchParams alive via a strong
    // ref, holds a weak self-ref to the iterator object for the `next` receiver
    // brand check, and tracks the current index. Freed by the iterator's finalizer.
    struct IterState {
        uint32_t idx;
        int kind;
    };

    // Internal, non-enumerable keys the iterator carries:
    //  - ITER_SOURCE_KEY: a strong reference to the source URLSearchParams (kept
    //    alive for `next`); released by ordinary property teardown.
    //  - ITER_BRAND_KEY: a napi_external wrapping this iterator's IterState*, used
    //    by `next` for the receiver brand check.
    // Holding these as properties (not napi_refs) means the iterator's finalizer
    // never has to delete a reference — illegal during the GC sweep on every
    // engine — so it can stay a plain C++ delete (see MakeIterator).
    static constexpr const char *ITER_SOURCE_KEY = "__nsSource";
    static constexpr const char *ITER_BRAND_KEY = "__nsBrand";

    napi_value js_str(napi_env env, std::string_view s) {
        napi_value v;
        napi_create_string_utf8(env, s.data(), s.length(), &v);
        return v;
    }

    void ThrowTypeError(napi_env env, const char *msg) {
        napi_throw_type_error(env, nullptr, msg);
    }

    // WebIDL USVString coercion: ToString (invokes user toString, throws for Symbol).
    // Returns false leaving a pending exception on failure.
    bool ValueToString(napi_env env, napi_value v, std::string &out) {
        napi_value str;
        if (napi_coerce_to_string(env, v, &str) != napi_ok) {
            return false;
        }
        size_t len = 0;
        if (napi_get_value_string_utf8(env, str, nullptr, 0, &len) != napi_ok) {
            return false;
        }
        std::vector<char> buf(len + 1);
        napi_get_value_string_utf8(env, str, buf.data(), len + 1, nullptr);
        out.assign(buf.data(), len);
        return true;
    }

    // Brand-checked instance retrieval: throws "Illegal invocation" (TypeError) when
    // the receiver is not a wrapped URLSearchParams, matching the spec.
    URLSearchParams *GetInstance(napi_env env, napi_callback_info info) {
        napi_value jsThis;
        if (napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, nullptr) != napi_ok) {
            return nullptr;
        }
        URLSearchParams *instance = nullptr;
        if (napi_unwrap(env, jsThis, reinterpret_cast<void **>(&instance)) != napi_ok ||
            instance == nullptr) {
            ThrowTypeError(env, "Illegal invocation");
            return nullptr;
        }
        return instance;
    }

    napi_value WellKnownSymbol(napi_env env, const char *name) {
        napi_value global, symbolCtor, sym;
        napi_get_global(env, &global);
        napi_get_named_property(env, global, "Symbol", &symbolCtor);
        napi_get_named_property(env, symbolCtor, name, &sym);
        return sym;
    }

    napi_value SymbolIterator(napi_env env) {
        return WellKnownSymbol(env, "iterator");
    }

    // ES IteratorClose: best-effort call to the iterator's return() on abrupt
    // completion (so a generator's `finally` runs), preserving any pending exception.
    void IteratorClose(napi_env env, napi_value iterObj) {
        napi_value savedEx = nullptr;
        bool pending = false;
        napi_is_exception_pending(env, &pending);
        if (pending) {
            napi_get_and_clear_last_exception(env, &savedEx);
        }
        napi_value returnFn;
        if (napi_get_named_property(env, iterObj, "return", &returnFn) == napi_ok &&
            napi_util::is_of_type(env, returnFn, napi_function)) {
            napi_value res;
            napi_call_function(env, iterObj, returnFn, 0, nullptr, &res);
            bool p2 = false;
            napi_is_exception_pending(env, &p2);
            if (p2) {
                napi_value e;
                napi_get_and_clear_last_exception(env, &e);
            }
        }
        if (savedEx != nullptr) {
            napi_throw(env, savedEx);
        }
    }

    // The `next` function of a live URLSearchParams iterator.
    napi_value IteratorNext(napi_env env, napi_callback_info info) {
        void *data = nullptr;
        napi_value jsThis;
        napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, &data);
        auto *st = static_cast<IterState *>(data);

        // Brand check: `next` must be invoked on the very iterator it belongs to.
        // The receiver carries a napi_external wrapping its IterState*; a matching
        // pointer proves identity. A foreign receiver (e.g. next.call({})) has no
        // such brand and throws, per spec.
        if (st == nullptr) {
            ThrowTypeError(env, "Illegal invocation");
            return nullptr;
        }
        napi_value brandVal = nullptr;
        void *brand = nullptr;
        if (napi_get_named_property(env, jsThis, ITER_BRAND_KEY, &brandVal) != napi_ok ||
            !napi_util::is_of_type(env, brandVal, napi_external) ||
            napi_get_value_external(env, brandVal, &brand) != napi_ok || brand != st) {
            ThrowTypeError(env, "Illegal invocation");
            return nullptr;
        }

        napi_value result;
        napi_create_object(env, &result);

        napi_value srcObj;
        URLSearchParams *self = nullptr;
        if (napi_get_named_property(env, jsThis, ITER_SOURCE_KEY, &srcObj) != napi_ok ||
            napi_unwrap(env, srcObj, reinterpret_cast<void **>(&self)) != napi_ok ||
            self == nullptr) {
            napi_set_named_property(env, result, "value", napi_util::undefined(env));
            napi_set_named_property(env, result, "done", napi_util::get_true(env));
            return result;
        }

        auto *p = self->GetURLSearchParams();
        if (st->idx >= p->size()) {
            napi_set_named_property(env, result, "value", napi_util::undefined(env));
            napi_set_named_property(env, result, "done", napi_util::get_true(env));
            return result;
        }

        auto pair = (*p)[st->idx++]; // live, duplicate-key correct
        napi_value value;
        if (st->kind == ITER_KEYS) {
            value = js_str(env, pair.first);
        } else if (st->kind == ITER_VALUES) {
            value = js_str(env, pair.second);
        } else {
            napi_create_array_with_length(env, 2, &value);
            napi_set_element(env, value, 0, js_str(env, pair.first));
            napi_set_element(env, value, 1, js_str(env, pair.second));
        }
        napi_set_named_property(env, result, "value", value);
        napi_set_named_property(env, result, "done", napi_util::get_false(env));
        return result;
    }

    // iterator[Symbol.iterator]() -> this (iterators are iterable).
    napi_value IteratorSelf(napi_env env, napi_callback_info info) {
        napi_value jsThis;
        napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, nullptr);
        return jsThis;
    }

    napi_value MakeIterator(napi_env env, napi_value jsThis, int kind) {
        auto *st = new IterState{0, kind};

        napi_value iterator;
        napi_create_object(env, &iterator);

        // Attach two non-enumerable internal properties (napi_default => not
        // enumerable, invisible to JS):
        //  - the strong source URLSearchParams reference, kept alive for `next`
        //    and released by ordinary property teardown (no cycle: the iterator
        //    does not reference the source's keys);
        //  - a napi_external wrapping the IterState* for the `next` brand check.
        // The external owns no data (nullptr finalize) — st is freed by the
        // iterator's own finalizer below.
        napi_value brandVal;
        napi_create_external(env, st, nullptr, nullptr, &brandVal);
        napi_property_descriptor descs[2] = {};
        descs[0].utf8name = ITER_SOURCE_KEY;
        descs[0].value = jsThis;
        descs[0].attributes = napi_default;
        descs[1].utf8name = ITER_BRAND_KEY;
        descs[1].value = brandVal;
        descs[1].attributes = napi_default;
        napi_define_properties(env, iterator, 2, descs);

        // Free the IterState when the iterator is collected. napi_add_finalizer
        // works on a plain object on every engine (unlike napi_wrap, which this
        // runtime backs with a V8 internal-field slot a plain object lacks). The
        // callback only frees C++ memory (no napi/JS-heap calls), so it is safe to
        // run synchronously during the GC sweep everywhere — no deferral needed.
        napi_add_finalizer(env, iterator, st, [](napi_env, void *d, void *) {
            delete static_cast<IterState *>(d);
        }, nullptr, nullptr);

        napi_value nextFn;
        napi_create_function(env, "next", NAPI_AUTO_LENGTH, IteratorNext, st, &nextFn);
        napi_set_named_property(env, iterator, "next", nextFn);

        napi_value selfFn;
        napi_create_function(env, "[Symbol.iterator]", NAPI_AUTO_LENGTH, IteratorSelf, nullptr,
                             &selfFn);
        napi_set_property(env, iterator, SymbolIterator(env), selfFn);

        // Symbol.toStringTag = "URLSearchParams Iterator" for spec-correct
        // Object.prototype.toString output.
        napi_set_property(env, iterator, WellKnownSymbol(env, "toStringTag"),
                          js_str(env, "URLSearchParams Iterator"));

        return iterator;
    }

    // Drives the ES iterator protocol on `iterable`, invoking fn(itemValue) for each
    // yielded value. Returns false (with a pending exception) on protocol error or if
    // fn returns false.
    template<class F>
    bool ForEachOfIterable(napi_env env, napi_value iterable, F &&fn) {
        napi_value iterMethod;
        if (napi_get_property(env, iterable, SymbolIterator(env), &iterMethod) != napi_ok) {
            return false;
        }
        if (!napi_util::is_of_type(env, iterMethod, napi_function)) {
            ThrowTypeError(env, "value is not iterable");
            return false;
        }
        napi_value iterObj;
        if (napi_call_function(env, iterable, iterMethod, 0, nullptr, &iterObj) != napi_ok) {
            return false;
        }
        if (!napi_util::is_object(env, iterObj)) {
            ThrowTypeError(env, "iterator result is not an object");
            return false;
        }
        napi_value nextFn;
        napi_get_named_property(env, iterObj, "next", &nextFn);
        if (!napi_util::is_of_type(env, nextFn, napi_function)) {
            ThrowTypeError(env, "iterator.next is not a function");
            return false;
        }

        while (true) {
            napi_value res;
            if (napi_call_function(env, iterObj, nextFn, 0, nullptr, &res) != napi_ok) {
                return false;
            }
            if (!napi_util::is_object(env, res)) {
                ThrowTypeError(env, "iterator result is not an object");
                return false;
            }
            napi_value doneVal;
            napi_get_named_property(env, res, "done", &doneVal);
            bool done = false;
            napi_value doneCoerced;
            if (napi_coerce_to_bool(env, doneVal, &doneCoerced) == napi_ok) {
                napi_get_value_bool(env, doneCoerced, &done);
            }
            if (done) {
                break;
            }
            napi_value value;
            napi_get_named_property(env, res, "value", &value);
            if (!fn(value)) {
                // abrupt completion → close the source iterator (runs finally blocks)
                IteratorClose(env, iterObj);
                return false;
            }
        }
        return true;
    }

    // sequence<sequence<USVString>> form.
    bool BuildFromSequence(napi_env env, napi_value iterable, url_search_params &params) {
        return ForEachOfIterable(env, iterable, [&](napi_value entry) -> bool {
            if (!napi_util::is_object(env, entry)) {
                ThrowTypeError(env, "URLSearchParams init sequence entry is not iterable");
                return false;
            }
            std::vector<std::string> pair;
            bool ok = ForEachOfIterable(env, entry, [&](napi_value item) -> bool {
                std::string s;
                if (!ValueToString(env, item, s)) {
                    return false;
                }
                pair.push_back(std::move(s));
                return true;
            });
            if (!ok) {
                return false;
            }
            if (pair.size() != 2) {
                ThrowTypeError(env,
                               "URLSearchParams init sequence entry does not contain exactly two elements");
                return false;
            }
            params.append(pair[0], pair[1]);
            return true;
        });
    }

    // record<USVString, USVString> form.
    bool BuildFromRecord(napi_env env, napi_value object, url_search_params &params) {
        napi_value keys;
        if (napi_get_all_property_names(env, object, napi_key_own_only, napi_key_enumerable,
                                        napi_key_numbers_to_strings, &keys) != napi_ok) {
            return false;
        }
        uint32_t len = 0;
        napi_get_array_length(env, keys, &len);
        for (uint32_t i = 0; i < len; i++) {
            napi_value key, value;
            napi_get_element(env, keys, i, &key);
            napi_get_property(env, object, key, &value);
            std::string k, v;
            if (!ValueToString(env, key, k) || !ValueToString(env, value, v)) {
                return false; // e.g. Symbol key
            }
            params.append(k, v);
        }
        return true;
    }
}


URLSearchParams::URLSearchParams(url_search_params params) : params_(params) {}

url_search_params *URLSearchParams::GetURLSearchParams() {
    return &params_;
}

napi_value URLSearchParams::New(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    url_search_params params;

    if (argc > 0 && !napi_util::is_undefined(env, argv[0])) {
        if (napi_util::is_of_type(env, argv[0], napi_string)) {
            std::string init;
            if (!ValueToString(env, argv[0], init)) {
                return nullptr;
            }
            params = url_search_params(init);
        } else if (napi_util::is_object(env, argv[0])) {
            napi_value iterMethod;
            if (napi_get_property(env, argv[0], SymbolIterator(env), &iterMethod) != napi_ok) {
                return nullptr;
            }
            if (napi_util::is_null_or_undefined(env, iterMethod)) {
                if (!BuildFromRecord(env, argv[0], params)) {
                    return nullptr;
                }
            } else if (napi_util::is_of_type(env, iterMethod, napi_function)) {
                if (!BuildFromSequence(env, argv[0], params)) {
                    return nullptr;
                }
            } else {
                ThrowTypeError(env, "URLSearchParams init Symbol.iterator is not a function");
                return nullptr;
            }
        } else {
            // number / boolean / bigint / null / symbol -> USVString
            std::string init;
            if (!ValueToString(env, argv[0], init)) {
                return nullptr;
            }
            params = url_search_params(init);
        }
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

    if (argc < 2) {
        ThrowTypeError(env, "URLSearchParams.append requires 2 arguments");
        return nullptr;
    }

    std::string key, value;
    if (!ValueToString(env, argv[0], key) || !ValueToString(env, argv[1], value)) {
        return nullptr;
    }

    instance->GetURLSearchParams()->append(key, value);
    return nullptr;
}

napi_value URLSearchParams::Has(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) return napi_util::get_false(env);

    std::string key;
    if (!ValueToString(env, argv[0], key)) {
        return nullptr;
    }

    bool has;
    if (argc > 1 && !napi_util::is_undefined(env, argv[1])) {
        std::string value;
        if (!ValueToString(env, argv[1], value)) {
            return nullptr;
        }
        has = instance->GetURLSearchParams()->has(key, value);
    } else {
        has = instance->GetURLSearchParams()->has(key);
    }

    napi_value result;
    napi_get_boolean(env, has, &result);
    return result;
}

napi_value URLSearchParams::Get(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) return napi_util::null(env);

    std::string key;
    if (!ValueToString(env, argv[0], key)) {
        return nullptr;
    }

    auto value = instance->GetURLSearchParams()->get(key);
    if (!value.has_value()) {
        // Per spec, a missing name returns null.
        return napi_util::null(env);
    }

    return js_str(env, value.value());
}

napi_value URLSearchParams::Delete(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) {
        ThrowTypeError(env, "URLSearchParams.delete requires 1 argument");
        return nullptr;
    }

    std::string key;
    if (!ValueToString(env, argv[0], key)) {
        return nullptr;
    }

    if (argc > 1 && !napi_util::is_undefined(env, argv[1])) {
        std::string value;
        if (!ValueToString(env, argv[1], value)) {
            return nullptr;
        }
        instance->GetURLSearchParams()->remove(key, value);
    } else {
        instance->GetURLSearchParams()->remove(key);
    }
    return nullptr;
}

napi_value URLSearchParams::GetAll(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(1)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1) {
        ThrowTypeError(env, "URLSearchParams.getAll requires 1 argument");
        return nullptr;
    }

    std::string key;
    if (!ValueToString(env, argv[0], key)) {
        return nullptr;
    }

    auto values = instance->GetURLSearchParams()->get_all(key);

    napi_value result;
    napi_create_array_with_length(env, values.size(), &result);
    for (size_t i = 0; i < values.size(); i++) {
        napi_set_element(env, result, i, js_str(env, values[i]));
    }
    return result;
}

napi_value URLSearchParams::Set(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 2) {
        ThrowTypeError(env, "URLSearchParams.set requires 2 arguments");
        return nullptr;
    }

    std::string key, value;
    if (!ValueToString(env, argv[0], key) || !ValueToString(env, argv[1], value)) {
        return nullptr;
    }

    instance->GetURLSearchParams()->set(key, value);
    return nullptr;
}

napi_value URLSearchParams::GetSize(napi_env env, napi_callback_info info) {
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    auto size = instance->GetURLSearchParams()->size();
    napi_value result;
    napi_create_int32(env, static_cast<int32_t>(size), &result);
    return result;
}

napi_value URLSearchParams::Sort(napi_env env, napi_callback_info info) {
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    instance->GetURLSearchParams()->sort();
    return nullptr;
}

napi_value URLSearchParams::ToString(napi_env env, napi_callback_info info) {
    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    auto value = instance->GetURLSearchParams()->to_string();
    return js_str(env, value);
}

napi_value URLSearchParams::Keys(napi_env env, napi_callback_info info) {
    napi_value jsThis;
    napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, nullptr);
    if (!GetInstance(env, info)) return nullptr;
    return MakeIterator(env, jsThis, ITER_KEYS);
}

napi_value URLSearchParams::Values(napi_env env, napi_callback_info info) {
    napi_value jsThis;
    napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, nullptr);
    if (!GetInstance(env, info)) return nullptr;
    return MakeIterator(env, jsThis, ITER_VALUES);
}

napi_value URLSearchParams::Entries(napi_env env, napi_callback_info info) {
    napi_value jsThis;
    napi_get_cb_info(env, info, nullptr, nullptr, &jsThis, nullptr);
    if (!GetInstance(env, info)) return nullptr;
    return MakeIterator(env, jsThis, ITER_ENTRIES);
}

napi_value URLSearchParams::ForEach(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(2)

    URLSearchParams *instance = GetInstance(env, info);
    if (!instance) return nullptr;

    if (argc < 1 || !napi_util::is_of_type(env, argv[0], napi_function)) {
        ThrowTypeError(env, "URLSearchParams.forEach requires a callback function");
        return nullptr;
    }

    napi_value callback = argv[0];
    napi_value thisArg = argc >= 2 ? argv[1] : nullptr;

    napi_value global;
    napi_get_global(env, &global);

    // Use get_entries() so duplicate keys (e.g. ?a=1&a=2) each yield their own value.
    auto entries = instance->GetURLSearchParams()->get_entries();
    while (entries.has_next()) {
        if (auto entry = entries.next()) {
            auto &[key, value] = entry.value();
            // Per spec, forEach callback receives (value, key, searchParams).
            napi_value args[3] = {js_str(env, value), js_str(env, key), jsThis};
            napi_value result;
            if (napi_call_function(env, thisArg ? thisArg : global, callback, 3, args, &result) !=
                napi_ok) {
                // If the callback throws, stop iteration.
                return nullptr;
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

    // Make prototype[Symbol.iterator] === entries (spec: default iterator is entries()).
    napi_value proto, entriesFn;
    if (napi_get_named_property(env, ctor, "prototype", &proto) == napi_ok &&
        napi_get_named_property(env, proto, "entries", &entriesFn) == napi_ok) {
        napi_set_property(env, proto, SymbolIterator(env), entriesFn);
    }

    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }

    NAPI_GUARD(napi_set_named_property(env, global, "URLSearchParams", ctor)) {
        return;
    }
}
