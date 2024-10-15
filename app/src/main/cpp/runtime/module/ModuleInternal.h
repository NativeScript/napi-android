#ifndef JNI_MODULE_H_
#define JNI_MODULE_H_

#include "JEnv.h"
#include "js_native_api.h"
#include <string>
#include <map>
#include "robin_hood.h"

namespace ns {
class ModuleInternal {
    public:
        ModuleInternal();

        ~ModuleInternal();

        void Init(napi_env env, const std::string& baseDir = "");

        napi_status Load(napi_env env, const std::string& path);

        /*
         * Reuses `Load` logic and adds TryCatch exception handling to push any unhandled exceptions
         * during script's initial load through the worker scope's `onerror` handler (if implemented before the exception was thrown)
         */
        void LoadWorker(napi_env env, const std::string& path);

        /*
         * Checks if target script exists, will throw if negative
         * Used before initializing workers, to ensure a thread will not be created, when the file doesn't exist
         */
        static void CheckFileExists(napi_env env, const std::string& path, const std::string& baseDir);

        static int MODULE_PROLOGUE_LENGTH;
    private:
        enum class ModulePathKind {
            Global,
            Relative,
            Absolute
        };

        struct ModuleCacheEntry {
            ModuleCacheEntry(napi_ref _obj)
                    : obj(_obj), isData(false) {
            }

            ModuleCacheEntry(napi_ref _obj, bool _isData)
                    : obj(_obj), isData(_isData) {
            }

            bool isData;
            napi_ref obj;
        };

        static napi_value RequireCallback(napi_env env, napi_callback_info info);

        static napi_value RequireNativeCallback(napi_env env, napi_callback_info info);

        napi_value RequireCallbackImpl(napi_env env, napi_callback_info info);

        napi_value WrapModuleContent(napi_env env, const std::string& path);

        napi_value LoadImpl(napi_env env, const std::string& moduleName, const std::string& baseDir, bool& isData);

        napi_value LoadModule(napi_env env, const std::string& path, const std::string& moduleCacheKey);

        napi_value LoadData(napi_env env, const std::string& path);

        napi_value LoadScript(napi_env env, const std::string& modulePath, napi_value fullRequiredModulePath);

        napi_value GetRequireFunction(napi_env env, const std::string& dirName);

        // void SaveScriptCache(napi_env env, napi_value script, const std::string& path);

        ModulePathKind GetModulePathKind(const std::string& path);

        static jclass MODULE_CLASS;
        static jmethodID RESOLVE_PATH_METHOD_ID;
        static const char* MODULE_PROLOGUE;
        static const char* MODULE_EPILOGUE;

        napi_env m_env;
        napi_ref m_requireFunction;
        napi_ref m_requireFactoryFunction;
        robin_hood::unordered_map<std::string, napi_ref> m_requireCache;
        robin_hood::unordered_map<std::string, ModuleCacheEntry> m_loadedModules;

        class TempModule {
            public:
                TempModule(ModuleInternal* module, const std::string& modulePath, const std::string& cacheKey, napi_ref poModuleObj)
                    :m_module(module), m_dispose(true), m_modulePath(modulePath), m_cacheKey(cacheKey), m_poModuleObj(poModuleObj) {
                    m_module->m_loadedModules.emplace(m_modulePath, ModuleCacheEntry(m_poModuleObj));
                    m_module->m_loadedModules.emplace(m_cacheKey, ModuleCacheEntry(m_poModuleObj));
                }

                ~TempModule() {
                    if (m_dispose) {
                        m_module->m_loadedModules.erase(m_modulePath);
                        m_module->m_loadedModules.erase(m_cacheKey);
                    }
                }

                void SaveToCache() {
                    m_dispose = false;
                }

            private:
                bool m_dispose;
                ModuleInternal* m_module;
                std::string m_modulePath;
                std::string m_cacheKey;
                napi_ref m_poModuleObj;
        };

};
}

#endif /* JNI_MODULE_H_ */