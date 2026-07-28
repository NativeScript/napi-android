#include "ModuleInternal.h"
#include "File.h"
#include "JniLocalRef.h"
#include "ArgConverter.h"
#include "NativeScriptAssert.h"
#include "Constants.h"
#include "NativeScriptException.h"
#include "Util.h"
#include "CallbackHandlers.h"
#include "Runtime.h"
#include <sstream>
#include <mutex>
#include <libgen.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <ctime>
#include "GlobalHelpers.h"
#include <utime.h>



using namespace tns;
using namespace std;

ModuleInternal::ModuleInternal()
    : m_env(nullptr), m_requireFunction(nullptr), m_requireFactoryFunction(nullptr) {
}

void ModuleInternal::DeInit() {
    napi_status status;
    if (m_env != nullptr) {
        NAPI_GUARD(napi_delete_reference(m_env, this->m_requireFunction)) {}
        NAPI_GUARD(napi_delete_reference(m_env, this->m_requireFactoryFunction)) {}
    }

    for (const auto& pair: this->m_requireCache) {
        if (m_env != nullptr) {
            NAPI_GUARD(napi_delete_reference(m_env, pair.second)) {}
        }
    }
    this->m_requireCache.clear();
}

void ModuleInternal::Init(napi_env env, const std::string& baseDir) {
    napi_status status;
    JEnv jenv;

    if (MODULE_CLASS == nullptr) {
        MODULE_CLASS = jenv.FindClass("com/tns/Module");
        assert(MODULE_CLASS != nullptr);

        RESOLVE_PATH_METHOD_ID = jenv.GetStaticMethodID(MODULE_CLASS, "resolvePath", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
        assert(RESOLVE_PATH_METHOD_ID != nullptr);
    }

    m_env = env;

    const char *requireFactoryScript = R"(
    (function () {
        return function require_factory(requireInternal, dirName) {
		return function require(modulePath) {
            if(typeof global.__requireOverride !== "undefined") {
				var result = global.__requireOverride(modulePath, dirName);
				if(result) {
					return result;
				}
			}
			return requireInternal(modulePath, dirName);
		}
	}
})();
)";

    napi_value source;
    NAPI_GUARD(napi_create_string_utf8(env, requireFactoryScript, NAPI_AUTO_LENGTH, &source)) {
        return;
    }

    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return;
    }

    napi_value result;
    status = js_execute_script(env, source, "<require_factory>", &result);
    assert(status == napi_ok);

    m_requireFactoryFunction = napi_util::make_ref(m_env, result);

    napi_value requireFunction = napi_util::napi_set_function(env, global, "__nativeRequire", RequireCallback, this);
    m_requireFunction = napi_util::make_ref(m_env, requireFunction);

    napi_value globalRequire = GetRequireFunction(env, baseDir.empty() ? Constants::APP_ROOT_FOLDER_PATH : baseDir);
    status = napi_set_named_property(env, global, "require", globalRequire);
    assert(status == napi_ok);
}

napi_value ModuleInternal::GetRequireFunction(napi_env env, const std::string& dirName) {
    napi_value requireFunc;

    auto itFound = m_requireCache.find(dirName);

    if (itFound != m_requireCache.end()) {
        requireFunc = napi_util::get_ref_value(env, itFound->second);
    } else {
        napi_status status;
        napi_value requireFuncFactory = napi_util::get_ref_value(env, m_requireFactoryFunction);

        napi_value requireInternalFunc = napi_util::get_ref_value(env, m_requireFunction);

        napi_value args[2];
        args[0] = requireInternalFunc;
        NAPI_GUARD(napi_create_string_utf8(env, dirName.c_str(), NAPI_AUTO_LENGTH, &args[1])) {
            return nullptr;
        }

        napi_value thiz;
        NAPI_GUARD(napi_create_object(env, &thiz)) {
            return nullptr;
        }

        napi_value result;
        status = napi_call_function(env, thiz, requireFuncFactory, 2, args, &result);
        assert(status == napi_ok && result != nullptr);

        bool isFunction = napi_util::is_of_type(env, result, napi_function);
        assert(isFunction);
        
        requireFunc = result;

        napi_ref poFunc = napi_util::make_ref(env, requireFunc);
        m_requireCache.emplace(dirName, poFunc);
    }

    return requireFunc;
}

napi_value ModuleInternal::RequireCallback(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN(0)
    try {
        auto thiz = static_cast<ModuleInternal*>(data);
        return thiz->RequireCallbackImpl(env, info);
    } catch (NativeScriptException& e) {
        e.ReThrowToNapi(env);
    } catch (std::exception& e) {
        stringstream ss;
        ss << "Error: c++ exception: " << e.what() << endl;
        NativeScriptException nsEx(ss.str());
        nsEx.ReThrowToNapi(env);
    } catch (...) {
        NativeScriptException nsEx(std::string("Error: c++ exception!"));
        nsEx.ReThrowToNapi(env);
    }

    return nullptr;
}

napi_value ModuleInternal::RequireCallbackImpl(napi_env env, napi_callback_info info) {
    NAPI_CALLBACK_BEGIN_VARGS_FAST(4)

    if (argc != 2) {
        throw NativeScriptException(string("require should be called with two parameters"));
    }
    if (!napi_util::is_of_type(env, argv[0], napi_string)) {
        throw NativeScriptException(string("require's first parameter should be string"));
    }
    if (!napi_util::is_of_type(env, argv[1], napi_string)) {
        throw NativeScriptException(string("require's second parameter should be string"));
    }

    string moduleName = ArgConverter::ConvertToString(env, argv[0]);
    string callingModuleDirName = ArgConverter::ConvertToString(env, argv[1]);

    auto isData = false;

    auto moduleObj = LoadImpl(env, moduleName, callingModuleDirName, isData);

    if (isData) {
        assert(!napi_util::is_null_or_undefined(env, moduleObj));
        return moduleObj;
    } else {
        napi_value exports;
        // Throw rather than return nullptr so a failed require surfaces as a JS
        // exception instead of silently evaluating to undefined.
        NAPI_GUARD(napi_get_named_property(env, moduleObj, "exports", &exports)) {
            throw NativeScriptException("Failed to read exports for module: " + moduleName);
        }
        assert(!napi_util::is_null_or_undefined(env, exports));
        return exports;
    }
}

napi_value ModuleInternal::RequireNativeCallback(napi_env env, napi_callback_info info) {
    napi_status status;
    void* data = nullptr;
    NAPI_GUARD(napi_get_cb_info(env, info, nullptr, nullptr, nullptr, &data)) {
        return nullptr;
    }
    auto cb = reinterpret_cast<napi_register_module_v *>(data);
    napi_value exports;
    NAPI_GUARD(napi_create_object(env, &exports)) {
        return nullptr;
    }
    return cb(env, exports);
}

napi_status ModuleInternal::Load(napi_env env, const std::string& path) {
    napi_status status;
    napi_value global;
    NAPI_GUARD(napi_get_global(env, &global)) {
        return status;
    }

    napi_value require;
    NAPI_GUARD(napi_get_named_property(env, global, "require", &require)) {
        return status;
    }

    napi_value args[1];
    NAPI_GUARD(napi_create_string_utf8(env, path.c_str(), path.size(), &args[0])) {
        return status;
    }

    napi_value result;
    status = napi_call_function(env, global, require, 1, args, &result);
    return status;
}

void ModuleInternal::LoadWorker(napi_env env, const string& path) {
    napi_status status;
    Load(env, path);
    bool hasPendingException;
    NAPI_GUARD(napi_is_exception_pending(env, &hasPendingException)) {
        return;
    }

    if (hasPendingException) {
        napi_value error;
        NAPI_GUARD(napi_get_and_clear_last_exception(env, &error)) {
            return;
        }
        CallbackHandlers::CallWorkerScopeOnErrorHandle(env, error);
    }
}

void ModuleInternal::CheckFileExists(napi_env env, const std::string& path, const std::string& baseDir) {
    JEnv jEnv;
    JniLocalRef jsModulename(jEnv.NewStringUTF(path.c_str()));
    JniLocalRef jsBaseDir(jEnv.NewStringUTF(baseDir.c_str()));
    jEnv.CallStaticObjectMethod(MODULE_CLASS, RESOLVE_PATH_METHOD_ID, (jstring) jsModulename, (jstring) jsBaseDir);
}

napi_value ModuleInternal::LoadInternalModule(napi_env env, const std::string& moduleName) {
    if (moduleName == "url") {
        napi_status status;
        napi_value moduleObj;
        NAPI_GUARD(napi_create_object(env, &moduleObj)) {
            return nullptr;
        }
        napi_value url;
        napi_value exports;
        NAPI_GUARD(napi_create_object(env, &exports)) {
            return nullptr;
        }
        NAPI_GUARD(napi_get_named_property(env, napi_util::global(env), "URL", &url)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_named_property(env, exports, "URL", url)) {
            return nullptr;
        }
        NAPI_GUARD(napi_set_named_property(env, moduleObj, "exports", exports)) {
            return nullptr;
        }
        napi_util::napi_set_function(env, exports, "pathToFileURL", [](napi_env env, napi_callback_info info) -> napi_value {
            return ArgConverter::convertToJsString(env, "file://");
        });
        return moduleObj;
    }
    return nullptr;
}

napi_value ModuleInternal::LoadImpl(napi_env env, const std::string& moduleName, const std::string& baseDir, bool& isData) {
    auto pathKind = GetModulePathKind(moduleName);
    auto cachePathKey = (pathKind == ModulePathKind::Global) ? moduleName : (baseDir + "*" + moduleName);

    napi_value result;

    DEBUG_WRITE(">>LoadImpl cachePathKey=%s", cachePathKey.c_str());

    auto it = m_loadedModules.find(cachePathKey);

    /**
     * Load internal modules like url,fs etc directly if someone does
     * require('url');
     */
    napi_value moduleObj = ModuleInternal::LoadInternalModule(env, moduleName);
    if (moduleObj) return moduleObj;

    if (it == m_loadedModules.end()) {
        std::string path;

        // Search App System libs
        std::string sys_lib("system_lib://");
        if (moduleName.rfind(sys_lib, 0) == 0) {
            auto pos = moduleName.find(sys_lib);
            path = std::string(moduleName);
            path.replace(pos, sys_lib.length(), "");
        } else if (Util::EndsWith(moduleName, ".so")) {
            path = "lib" + moduleName;
        } else if (Util::EndsWith(moduleName, ".node")) {
            std::string libName = moduleName;
            Util::ReplaceAll(libName, ".node", "");
            path = "lib" + libName + ".so";
        } else {
            JEnv jenv;
            JniLocalRef jsModulename(jenv.NewStringUTF(moduleName.c_str()));
            JniLocalRef jsBaseDir(jenv.NewStringUTF(baseDir.c_str()));
            JniLocalRef jsModulePath(
                    jenv.CallStaticObjectMethod(MODULE_CLASS, RESOLVE_PATH_METHOD_ID,
                                               (jstring) jsModulename, (jstring) jsBaseDir));

            path = ArgConverter::jstringToString((jstring) jsModulePath);
        }

        auto it2 = m_loadedModules.find(path);

        if (it2 == m_loadedModules.end()) {
            if (Util::EndsWith(path, ".js") || Util::EndsWith(path, ".so")) {
                isData = false;
                result = LoadModule(env, path, cachePathKey);
            } else if (Util::EndsWith(path, ".json")) {
                isData = true;
                result = LoadData(env, path);
            } else {
                std::string errMsg = "Unsupported file extension: " + path;
                throw NativeScriptException(errMsg);
            }
        } else {
            auto& cacheEntry = it2->second;
            isData = cacheEntry.isData;
            result = napi_util::get_ref_value(env, cacheEntry.obj);
        }
    } else {
        auto& cacheEntry = it->second;
        isData = cacheEntry.isData;
        result = napi_util::get_ref_value(env, cacheEntry.obj);
    }

    return result;
}

std::string ModuleInternal::EnsureFileProtocol(const std::string& path) {
    const std::string protocol = "file://";
    if (path.compare(0, protocol.length(), protocol) != 0) {
        return protocol + path;
    }
    return path;
}

napi_value ModuleInternal::LoadModule(napi_env env, const std::string& modulePath, const std::string& moduleCacheKey) {
    napi_status status;
    napi_value result;

    // A failure setting up the module scaffold must propagate as an exception:
    // require() returning nullptr would surface to JS as `undefined` with no
    // error, unlike every other failure path in this function which throws.
    napi_value context;
    NAPI_GUARD(napi_get_global(env, &context)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    napi_value moduleObj;
    NAPI_GUARD(napi_create_object(env, &moduleObj)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    napi_value exportsObj;
    NAPI_GUARD(napi_create_object(env, &exportsObj)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    NAPI_GUARD(napi_set_named_property(env, moduleObj, "exports", exportsObj)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    napi_value fullRequiredModulePath;
    NAPI_GUARD(napi_create_string_utf8(env, modulePath.c_str(), modulePath.size(), &fullRequiredModulePath)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }
    NAPI_GUARD(napi_set_named_property(env, moduleObj, "filename", fullRequiredModulePath)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    napi_ref poModuleObj = napi_util::make_ref(env, moduleObj);
    TempModule tempModule(this, modulePath, moduleCacheKey, poModuleObj);

    napi_value moduleFunc;

    if (Util::EndsWith(modulePath, ".js")) {
        DEBUG_WRITE("%s", modulePath.c_str());

        // Fast path: if the build compiled this module to engine bytecode, run it
        // directly. This peeks the file header only — the source is never read or
        // wrapped for a bytecode module. Bytecode is the compiled form of the
        // *wrapped* module content, so it yields the same wrapper function.
        status = js_run_bytecode_file(env, EnsureFileProtocol(modulePath).c_str(), &moduleFunc);
        if (status == napi_cannot_run_js) {
            // Not bytecode — compile and run the wrapped source as usual.
            napi_value script = LoadScript(env, modulePath, fullRequiredModulePath);
            status = js_execute_script(env, script, EnsureFileProtocol(modulePath).c_str(), &moduleFunc);
        }
        if (status != napi_ok) {
            bool pendingException;
            NAPI_GUARD(napi_is_exception_pending(env, &pendingException)) {}
            napi_value error = nullptr;
            if (pendingException) {
                NAPI_GUARD(napi_get_and_clear_last_exception(env, &error)) {}
            }
            if (error) {
                throw NativeScriptException(env, error, "Error running script " + modulePath);
            } else {
                throw NativeScriptException("Error running script " + modulePath);
            }
        }
    } else if (Util::EndsWith(modulePath, ".so")) {
        auto handle = dlopen(modulePath.c_str(), RTLD_NOW);
        if (handle == nullptr) {
            auto error = dlerror();
            std::string errMsg(error);
            throw NativeScriptException(errMsg);
        }
        auto func = dlsym(handle, "napi_register_module_v1");

        if (func == nullptr) {
            std::string errMsg("Cannot find 'napi_register_module_v1' in " + modulePath);
            throw NativeScriptException(errMsg);
        }

        auto cb = reinterpret_cast<napi_register_module_v *>(func);
        napi_value exports;
        NAPI_GUARD(napi_create_object(env, &exports)) {
            throw NativeScriptException("Failed to load module: " + modulePath);
        }
        napi_value result = cb(env, exports);
        NAPI_GUARD(napi_set_named_property(env, moduleObj, "exports", result)) {
            throw NativeScriptException("Failed to load module: " + modulePath);
        }
        tempModule.SaveToCache();
        return moduleObj;
    } else {
        std::string errMsg = "Unsupported file extension: " + modulePath;
        throw NativeScriptException(errMsg);
    }

    napi_value fileName;
    NAPI_GUARD(napi_create_string_utf8(env, modulePath.c_str(), modulePath.size(), &fileName)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    char pathcopy[1024];
    strcpy(pathcopy, modulePath.c_str());
    std::string strDirName(dirname(pathcopy));

    napi_value dirName;
    NAPI_GUARD(napi_create_string_utf8(env, strDirName.c_str(), strDirName.size(), &dirName)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    napi_value require = GetRequireFunction(env, strDirName);

    napi_value requireArgs[5] = { moduleObj, exportsObj, require, fileName, dirName };

    NAPI_GUARD(napi_set_named_property(env, moduleObj, "require", require)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }
    napi_util::define_property(env, moduleObj, "id", fileName);

    napi_value thiz;
    NAPI_GUARD(napi_create_object(env, &thiz)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    napi_value globalExtends;
    NAPI_GUARD(napi_get_named_property(env, context, "__extends", &globalExtends)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }
    NAPI_GUARD(napi_set_named_property(env, thiz, "__extends", globalExtends)) {
        throw NativeScriptException("Failed to load module: " + modulePath);
    }

    napi_value callResult;
    // Keep the module-call status in its own variable: NAPI_GUARD below reassigns
    // the shared `status`, which would otherwise mask a failed module invocation.
    napi_status callStatus = napi_call_function(env, thiz, moduleFunc, 5, requireArgs, &callResult);
    bool pendingException = false;
    NAPI_GUARD(napi_is_exception_pending(env, &pendingException)) {}
     if (callStatus != napi_ok || pendingException) {
         napi_value exception;
         NAPI_GUARD(napi_get_and_clear_last_exception(env, &exception)) {}
         if (exception) {
             throw NativeScriptException(env, exception, "Error calling module function: ");
         } else {
             throw NativeScriptException("Error calling module function: " + modulePath);
         }
     }

    tempModule.SaveToCache();
    result = moduleObj;

    return result;
}

napi_value ModuleInternal::LoadScript(napi_env env, const std::string& path, napi_value fullRequiredModulePath) {
    napi_value scriptText = ModuleInternal::WrapModuleContent(env, path);
    return scriptText;
}

napi_value ModuleInternal::LoadData(napi_env env, const std::string& path) {
    napi_status status;
    std::string jsonData = Runtime::GetRuntime(m_env)->ReadFileText(path);
    napi_value json = JsonParseString(env, jsonData);

    if (!napi_util::is_of_type(env, json, napi_object)) {
        bool pendingException;
        NAPI_GUARD(napi_is_exception_pending(env, &pendingException)) {}
        if (pendingException) {
            napi_value error;
            NAPI_GUARD(napi_get_and_clear_last_exception(env, &error)) {}
            throw NativeScriptException(env, error, "JSON is not valid, file=" + path);
        } else {
            throw NativeScriptException("JSON is not valid, file=" + path);
        }
    }

    napi_ref poObj = napi_util::make_ref(env, json);
    m_loadedModules.emplace(path, ModuleCacheEntry(poObj, true /* isData */));
    return json;
}

napi_value ModuleInternal::WrapModuleContent(napi_env env, const std::string& path) {

    std::string content = Runtime::GetRuntime(m_env)->ReadFileText(path);

    // TODO: Use statically allocated buffer for better performance
    std::string result(MODULE_PROLOGUE);
    result.reserve(content.length() + 1024);
    result += content;
    result += MODULE_EPILOGUE;

    napi_status status;
    napi_value wrappedContent;
    NAPI_GUARD(napi_create_string_utf8(env, result.c_str(), result.size(), &wrappedContent)) {
        return nullptr;
    }

    return wrappedContent;
}

ModuleInternal::ModulePathKind ModuleInternal::GetModulePathKind(const std::string& path) {
    ModulePathKind kind;
    switch (path[0]) {
    case '.':
        kind = ModulePathKind::Relative;
        break;
    case '/':
        kind = ModulePathKind::Absolute;
        break;
    default:
        kind = ModulePathKind::Global;
        break;
    }
    return kind;
}

jclass ModuleInternal::MODULE_CLASS = nullptr;
jmethodID ModuleInternal::RESOLVE_PATH_METHOD_ID = nullptr;

const char* ModuleInternal::MODULE_PROLOGUE = "(function(module, exports, require, __filename, __dirname){ ";
const char* ModuleInternal::MODULE_EPILOGUE = "\n})";
int ModuleInternal::MODULE_PROLOGUE_LENGTH = std::string(ModuleInternal::MODULE_PROLOGUE).length();

